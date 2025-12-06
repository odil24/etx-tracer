#include <etx/core/environment.hxx>
#include <etx/core/json.hxx>

#include <etx/render/shared/base.hxx>
#include <etx/render/shared/math.hxx>
#include <etx/render/shared/scene.hxx>
#include <etx/render/shared/spectrum.hxx>
#include <etx/render/host/scene_tungsten_loader.hxx>
#include <etx/render/host/scene_obj_loader.hxx>
#include <etx/render/host/scene_gltf_loader.hxx>
#include <etx/render/host/scene_loader_utils.hxx>
#include <etx/render/shared/ior_database.hxx>

#include <array>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace etx {

namespace {

float3 json_to_float3(const nlohmann::json& arr, const float3& fallback) {
  if (arr.is_array() && arr.size() >= 3) {
    return {float(arr[0].get<double>()), float(arr[1].get<double>()), float(arr[2].get<double>())};
  }
  return fallback;
}

std::string resolve_path(const std::string& base_dir, const std::string& path) {
  if (path.empty())
    return {};
  std::filesystem::path p(path);
  if (p.is_absolute())
    return p.lexically_normal().string();
  return (std::filesystem::path(base_dir) / p).lexically_normal().string();
}

const char* get_ext(const std::string& path) {
  auto pos = path.find_last_of('.');
  if (pos == std::string::npos)
    return "";
  return path.c_str() + pos;
}

float3 rotate_yxz_deg(const float3& v, const float3& rot_deg) {
  const float3 r = rot_deg * (kPi / 180.0f);
  const float c0 = cosf(r.x);
  const float s0 = sinf(r.x);
  const float c1 = cosf(r.y);
  const float s1 = sinf(r.y);
  const float c2 = cosf(r.z);
  const float s2 = sinf(r.z);

  float3 out;
  out.x = (c1 * c2 - s1 * s0 * s2) * v.x + (-c1 * s2 - s1 * s0 * c2) * v.y + (-s1 * c0) * v.z;
  out.y = (c0 * s2) * v.x + (c0 * c2) * v.y + (-s0) * v.z;
  out.z = (s1 * c2 + c1 * s0 * s2) * v.x + (-s1 * s2 + c1 * s0 * c2) * v.y + (c1 * c0) * v.z;
  return out;
}

uint32_t resolve_tungsten_texture(const nlohmann::json& v, SceneLoaderContext& context, const char* base_dir) {
  if (v.is_string() == false)
    return kInvalidIndex;
  std::string p = resolve_path(base_dir, v.get<std::string>());
  if (p.empty())
    return kInvalidIndex;
  return context.add_image(p.c_str(), Image::RepeatU | Image::RepeatV, {}, {1.0f, 1.0f});
}

nlohmann::json tungsten_albedo_json(const nlohmann::json& src) {
  return src.contains("albedo") ? src["albedo"] : nlohmann::json();
}

void tungsten_set_albedo(Material& mtl, const nlohmann::json& val, SceneData& data, SceneLoaderContext& context, const char* base_dir) {
  if (val.is_string()) {
    uint32_t tex_idx = resolve_tungsten_texture(val, context, base_dir);
    if (tex_idx != kInvalidIndex) {
      mtl.scattering.image_index = tex_idx;
      mtl.reflectance.image_index = tex_idx;
      mtl.scattering.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
      mtl.reflectance.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
      return;
    }
  }

  if (val.is_array() && val.size() >= 3) {
    float3 c = {};
    c.x = val[0].is_number() ? float(val[0].get<double>()) : 1.0f;
    c.y = val[1].is_number() ? float(val[1].get<double>()) : 1.0f;
    c.z = val[2].is_number() ? float(val[2].get<double>()) : 1.0f;
    uint32_t sp_s = data.add_spectrum(SpectralDistribution::rgb_reflectance(c));
    uint32_t sp_r = data.add_spectrum(SpectralDistribution::rgb_reflectance(c));
    mtl.scattering.spectrum_index = sp_s;
    mtl.reflectance.spectrum_index = sp_r;
    return;
  }

  if (val.is_number()) {
    float s = float(val.get<double>());
    uint32_t sp_s = data.add_spectrum(SpectralDistribution::rgb_reflectance({s, s, s}));
    uint32_t sp_r = data.add_spectrum(SpectralDistribution::rgb_reflectance({s, s, s}));
    mtl.scattering.spectrum_index = sp_s;
    mtl.reflectance.spectrum_index = sp_r;
    return;
  }

  mtl.scattering.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
  mtl.reflectance.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
}

void tungsten_set_transmission(Material& mtl, const nlohmann::json& val, SceneData& data, SceneLoaderContext& context, const char* base_dir) {
  if (val.is_string()) {
    uint32_t tex_idx = resolve_tungsten_texture(val, context, base_dir);
    if (tex_idx != kInvalidIndex) {
      mtl.transmission.image_index = tex_idx;
      mtl.transmission.channel = 0u;
      mtl.transmission.value = {1.0f, 1.0f, 1.0f, 1.0f};
      return;
    }
  }

  if (val.is_array() && val.size() >= 3) {
    float3 c = {};
    c.x = val[0].is_number() ? float(val[0].get<double>()) : 1.0f;
    c.y = val[1].is_number() ? float(val[1].get<double>()) : 1.0f;
    c.z = val[2].is_number() ? float(val[2].get<double>()) : 1.0f;
    mtl.transmission.image_index = kInvalidIndex;
    mtl.transmission.value = {c.x, c.y, c.z, 1.0f};
    return;
  }

  if (val.is_number()) {
    float s = float(val.get<double>());
    mtl.transmission.image_index = kInvalidIndex;
    mtl.transmission.value = {s, s, s, 1.0f};
    return;
  }

  mtl.transmission.image_index = kInvalidIndex;
  mtl.transmission.value = {1.0f, 1.0f, 1.0f, 1.0f};
}

struct TungstenConductorIOR {
  const char* name;
  float3 eta;
  float3 k;
};

static const TungstenConductorIOR kTungstenConductors[] = {
  {"a-C", {2.9440999183f, 2.2271502925f, 1.9681668794f}, {0.8874329109f, 0.7993216383f, 0.8152862927f}},
  {"Ag", {0.1552646489f, 0.1167232965f, 0.1383806959f}, {4.8283433224f, 3.1222459278f, 2.1469504455f}},
  {"Al", {1.6574599595f, 0.8803689579f, 0.5212287346f}, {9.2238691996f, 6.2695232477f, 4.8370012281f}},
  {"AlAs", {3.6051023902f, 3.2329365777f, 2.2175611545f}, {0.0006670247f, -0.00049994f, 0.0074261204f}},
  {"AlSb", {-0.0485225705f, 4.1427547893f, 4.6697691348f}, {-0.0363741915f, 0.0937665154f, 1.3007390124f}},
  {"Au", {0.1431189557f, 0.3749570432f, 1.4424785571f}, {3.9831604247f, 2.3857207478f, 1.6032152899f}},
  {"Be", {4.1850592788f, 3.1850604423f, 2.7840913457f}, {3.8354398268f, 3.0101260162f, 2.8690088743f}},
  {"Cr", {4.3696828663f, 2.9167024892f, 1.6547005413f}, {5.2064337956f, 4.2313645277f, 3.7549467933f}},
  {"CsI", {2.1449030413f, 1.7023164587f, 1.6624194173f}, {0.0f, 0.0f, 0.0f}},
  {"Cu", {0.2004376970f, 0.9240334304f, 1.1022119527f}, {3.9129485033f, 2.4528477015f, 2.1421879552f}},
  {"Cu2O", {3.5492833755f, 2.9520622449f, 2.7369202137f}, {0.1132179294f, 0.1946659670f, 0.6001681264f}},
  {"CuO", {3.2453822204f, 2.4496293965f, 2.1974114493f}, {0.5202739621f, 0.5707372756f, 0.7172250613f}},
  {"d-C", {2.7112524747f, 2.3185812849f, 2.2288565009f}, {0.0f, 0.0f, 0.0f}},
  {"Hg", {2.3989314904f, 1.4400254917f, 0.9095512090f}, {6.3276269444f, 4.3719414152f, 3.4217899270f}},
  {"HgTe", {4.7795267752f, 3.2309984581f, 2.6600252401f}, {1.6319827058f, 1.5808189339f, 1.7295753852f}},
  {"Ir", {3.0864098394f, 2.0821938440f, 1.6178866805f}, {5.5921510077f, 4.0671757150f, 3.2672611269f}},
  {"K", {0.0640493070f, 0.0464100621f, 0.0381842017f}, {2.1042155920f, 1.3489364357f, 0.9132113889f}},
  {"Li", {0.2657871942f, 0.1956102432f, 0.2209198538f}, {3.5401743407f, 2.3111306542f, 1.6685930000f}},
  {"MgO", {2.0895885542f, 1.6507224525f, 1.5948759692f}, {0.0f, -0.0f, 0.0f}},
  {"Mo", {4.4837010280f, 3.5254578255f, 2.7760769438f}, {4.1111307988f, 3.4208716252f, 3.1506031404f}},
  {"Na", {0.0602665320f, 0.0561412435f, 0.0619909494f}, {3.1792906496f, 2.1124800781f, 1.5790940266f}},
  {"Nb", {3.4201353595f, 2.7901921379f, 2.3955856658f}, {3.4413817900f, 2.7376437930f, 2.5799132708f}},
  {"Ni", {2.3672753521f, 1.6633583302f, 1.4670554172f}, {4.4988329911f, 3.0501643957f, 2.3454274399f}},
  {"Rh", {2.5857954933f, 1.8601866068f, 1.5544279524f}, {6.7822927110f, 4.7029501026f, 3.9760892461f}},
  {"Se-e", {5.7242724833f, 4.1653992967f, 4.0816099264f}, {0.8713747439f, 1.1052845009f, 1.5647788766f}},
  {"Se", {4.0592611085f, 2.8426947380f, 2.8207582835f}, {0.7543791750f, 0.6385150558f, 0.5215872029f}},
  {"SiC", {3.1723450205f, 2.5259677964f, 2.4793623897f}, {0.0000007284f, -0.0000006859f, 0.0000100150f}},
  {"SnTe", {4.5251865890f, 1.9811525984f, 1.2816819226f}, {0.0f, 0.0f, 0.0f}},
  {"Ta", {2.0625846607f, 2.3930915569f, 2.6280684948f}, {2.4080467973f, 1.7413705864f, 1.9470377016f}},
  {"Te-e", {7.5090397678f, 4.2964603080f, 2.3698732430f}, {5.5842076830f, 4.9476231084f, 3.9975145063f}},
  {"Te", {7.3908396088f, 4.4821028985f, 2.6370708478f}, {3.2561412892f, 3.5273908133f, 3.2921683116f}},
  {"ThF4", {1.8307187117f, 1.4422274283f, 1.3876488528f}, {0.0f, 0.0f, 0.0f}},
  {"TiC", {3.7004673762f, 2.8374356509f, 2.5823030278f}, {3.2656905818f, 2.3515586388f, 2.1727857800f}},
  {"TiN", {1.6484691607f, 1.1504482522f, 1.3797795097f}, {3.3684596226f, 1.9434888540f, 1.1020123347f}},
  {"TiO2-e", {3.1065574823f, 2.5131551146f, 2.5823844157f}, {0.0000289537f, -0.0000251484f, 0.0001775555f}},
  {"TiO2", {3.4566203131f, 2.8017076558f, 2.9051485020f}, {0.0001026662f, -0.0000897534f, 0.0006356902f}},
  {"VC", {3.6575665991f, 2.7527298065f, 2.5326814570f}, {3.0683516659f, 2.1986687713f, 1.9631816252f}},
  {"VN", {2.8656011588f, 2.1191817791f, 1.9400767149f}, {3.0323264950f, 2.0561075580f, 1.6162930914f}},
  {"V", {4.2775126218f, 3.5131538236f, 2.7611257461f}, {3.4911844504f, 2.8893580874f, 3.1116965117f}},
  {"W", {4.3707029924f, 3.3002972445f, 2.9982666528f}, {3.5006778591f, 2.6048652781f, 2.2731930614f}},
};

bool find_tungsten_conductor(const std::string& name, TungstenConductorIOR& out) {
  if (name.empty())
    return false;
  std::string key = name;
  for (char& c : key)
    c = char(::tolower(static_cast<unsigned char>(c)));
  for (const auto& item : kTungstenConductors) {
    std::string item_key = item.name;
    for (char& c : item_key)
      c = char(::tolower(static_cast<unsigned char>(c)));
    if (item_key == key) {
      out = item;
      return true;
    }
  }
  return false;
}

void set_conductor_ior(Material& mtl, const std::string& material_name, SceneData& data, const IORDatabase& database) {
  const IORDefinition* def = database.find_by_name(material_name.c_str(), SpectralDistribution::Class::Conductor);
  if (def != nullptr) {
    mtl.int_ior.cls = def->cls;
    mtl.int_ior.eta_index = data.add_spectrum(def->title.c_str(), def->eta);
    mtl.int_ior.k_index = data.add_spectrum(def->title.c_str(), def->k);
    return;
  }

  TungstenConductorIOR t = {};
  if (find_tungsten_conductor(material_name, t)) {
    float3 eta_rgb = max(float3{}, spectrum::rgb_to_xyz(t.eta));
    float3 k_rgb = max(float3{}, spectrum::rgb_to_xyz(t.k));
    SpectralDistribution eta_spd = SpectralDistribution::rgb_reflectance(eta_rgb);
    SpectralDistribution k_spd = SpectralDistribution::rgb_reflectance(k_rgb);
    mtl.int_ior.cls = SpectralDistribution::Class::Conductor;
    mtl.int_ior.eta_index = data.add_spectrum(eta_spd);
    mtl.int_ior.k_index = data.add_spectrum(k_spd);
    return;
  }

  // Fallback: keep defaults already set by caller.
}

void set_dielectric_ior(Material& mtl, const std::string& bsdf_name, const nlohmann::json& b, SceneData& data, const IORDatabase& database) {
  auto try_name = [&](const std::string& n) -> bool {
    if (n.empty())
      return false;
    const IORDefinition* def = database.find_by_name(n.c_str(), SpectralDistribution::Class::Dielectric);
    if (def == nullptr)
      return false;
    mtl.int_ior.cls = def->cls;
    mtl.int_ior.eta_index = data.add_spectrum(def->title.c_str(), def->eta);
    mtl.int_ior.k_index = data.add_spectrum(def->title.c_str(), def->k);
    return true;
  };

  if (b.contains("material") && b["material"].is_string()) {
    if (try_name(b["material"].get<std::string>()))
      return;
  }

  if (b.contains("ior") && b["ior"].is_string()) {
    if (try_name(b["ior"].get<std::string>()))
      return;
  }

  float ior = b.value("ior", 1.5f);
  mtl.int_ior.cls = SpectralDistribution::Class::Dielectric;
  mtl.int_ior.eta_index = data.add_spectrum(SpectralDistribution::constant(ior));
  mtl.int_ior.k_index = data.add_spectrum(SpectralDistribution::constant(0.0f));
}

uint32_t add_tungsten_material(const std::string& name, const nlohmann::json& b, SceneData& data, SceneLoaderContext& context, const char* base_dir, const IORDatabase& database,
  bool force_two_sided) {
  uint32_t mat_idx = data.add_material(name.c_str());
  auto& mtl = data.materials[mat_idx];
  std::string type = b.value("type", "lambert");
  mtl.ext_ior.cls = SpectralDistribution::Class::Dielectric;
  mtl.ext_ior.eta_index = data.add_spectrum(SpectralDistribution::constant(1.0f));
  mtl.ext_ior.k_index = data.add_spectrum(SpectralDistribution::constant(0.0f));
  mtl.int_ior.cls = SpectralDistribution::Class::Dielectric;
  mtl.int_ior.eta_index = data.add_spectrum(SpectralDistribution::constant(1.5f));
  mtl.int_ior.k_index = data.add_spectrum(SpectralDistribution::constant(0.0f));

  if (type == "lambert" || type == "oren_nayar") {
    mtl.cls = Material::Class::Diffuse;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
    float rough = b.value("roughness", 0.0f);
    mtl.roughness.value = {rough, rough};
  } else if (type == "rough_plastic") {
    mtl.cls = Material::Class::Plastic;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
    float rough = b.value("roughness", 0.2f);
    mtl.roughness.value = {rough, rough};
    mtl.metalness.value = {0.0f, 0.0f};
    mtl.reflectance.image_index = kInvalidIndex;
    mtl.reflectance.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
    set_dielectric_ior(mtl, name, b, data, database);
  } else if (type == "mirror") {
    mtl.cls = Material::Class::Mirror;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
    mtl.roughness.value = {0.0f, 0.0f};
    mtl.metalness.value = {1.0f, 1.0f};
  } else if (type == "thinsheet") {
    mtl.cls = Material::Class::Thinfilm;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
    set_dielectric_ior(mtl, name, b, data, database);
  } else if (type == "rough_conductor") {
    mtl.cls = Material::Class::Conductor;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
    float rough = b.value("roughness", 0.1f);
    mtl.roughness.value = {rough, rough};
    mtl.metalness.value = {1.0f, 1.0f};
    std::string mat_name = b.value("material", "");
    if (mat_name.empty() == false)
      set_conductor_ior(mtl, mat_name, data, database);
  } else if (type == "conductor") {
    mtl.cls = Material::Class::Conductor;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
    mtl.roughness.value = {0.0f, 0.0f};
    mtl.metalness.value = {1.0f, 1.0f};
    std::string mat_name = b.value("material", "");
    if (mat_name.empty() == false)
      set_conductor_ior(mtl, mat_name, data, database);
  } else if (type == "dielectric") {
    mtl.cls = Material::Class::Dielectric;
    set_dielectric_ior(mtl, name, b, data, database);
    mtl.transmission.value = {1.0f, 1.0f, 1.0f, 1.0f};
    mtl.roughness.value = {b.value("roughness", 0.0f), b.value("roughness", 0.0f)};
    mtl.reflectance.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
    mtl.reflectance.image_index = kInvalidIndex;
    mtl.scattering.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
    mtl.scattering.image_index = kInvalidIndex;
    tungsten_set_transmission(mtl, tungsten_albedo_json(b), data, context, base_dir);
  } else if (type == "transparency") {
    mtl.cls = Material::Class::Boundary;
    mtl.transmission.value = {1.0f, 1.0f, 1.0f, 1.0f};
    mtl.reflectance.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
    mtl.reflectance.image_index = kInvalidIndex;
    mtl.scattering.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));
    mtl.scattering.image_index = kInvalidIndex;
    tungsten_set_transmission(mtl, tungsten_albedo_json(b), data, context, base_dir);
    if (b.contains("alpha")) {
      uint32_t alpha_tex = resolve_tungsten_texture(b["alpha"], context, base_dir);
      if (alpha_tex != kInvalidIndex) {
        mtl.transmission.image_index = alpha_tex;
        mtl.transmission.channel = 0u;
      }
    }
  } else {
    mtl.cls = Material::Class::Diffuse;
    tungsten_set_albedo(mtl, tungsten_albedo_json(b), data, context, base_dir);
  }

  if (force_two_sided)
    mtl.two_sided = 1u;

  return mat_idx;
}

float triangle_area(const Triangle& t, const std::vector<float3>& pos) {
  const float3& p0 = pos[t.i[0]];
  const float3& p1 = pos[t.i[1]];
  const float3& p2 = pos[t.i[2]];
  return 0.5f * length(cross(p1 - p0, p2 - p0));
}

float triangles_area(const SceneData& data, uint32_t tri_start, uint32_t tri_end) {
  float area = 0.0f;
  uint32_t end = tri_end;
  uint32_t count = static_cast<uint32_t>(data.triangles.size());
  if (end > count)
    end = count;
  for (uint32_t i = tri_start; i < end; ++i)
    area += triangle_area(data.triangles[i], data.vertices.pos);
  return area;
}

void set_emission_from_json(Material& mtl, const nlohmann::json& v, SceneData& data, SceneLoaderContext& context, const char* base_dir, float scale = 1.0f) {
  float3 scaled_white = {scale, scale, scale};
  if (v.is_string()) {
    uint32_t tex_idx = resolve_tungsten_texture(v, context, base_dir);
    if (tex_idx != kInvalidIndex) {
      mtl.emission.image_index = tex_idx;
      mtl.emission.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance(scaled_white));
      return;
    }
  }
  if (v.is_array() && v.size() >= 3) {
    float3 c = {float(v[0].get<double>()), float(v[1].get<double>()), float(v[2].get<double>())};
    c *= scale;
    mtl.emission.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance(c));
    mtl.emission.image_index = kInvalidIndex;
    return;
  }
  if (v.is_number()) {
    float s = float(v.get<double>()) * scale;
    mtl.emission.spectrum_index = data.add_spectrum(SpectralDistribution::rgb_reflectance({s, s, s}));
    mtl.emission.image_index = kInvalidIndex;
    return;
  }
}

bool add_builtin_quad(const float3& translate, const float3& scale, const float3& rotation_deg, uint32_t material_index, SceneData& data) {
  const uint32_t vertex_offset = static_cast<uint32_t>(data.vertices.pos.size());
  float3 edge0 = {scale.x, 0.0f, 0.0f};
  float3 edge1 = {0.0f, 0.0f, scale.z};
  edge0 = rotate_yxz_deg(edge0, rotation_deg);
  edge1 = rotate_yxz_deg(edge1, rotation_deg);

  float3 nrm = cross(edge1, edge0);
  float len = length(nrm);
  if (len > 0.0f)
    nrm /= len;
  else
    nrm = {0.0f, 1.0f, 0.0f};

  float3 tan = normalize(edge0);
  float3 btn = normalize(cross(nrm, tan));

  float3 base = translate - 0.5f * edge0 - 0.5f * edge1;

  std::array<float3, 4> final_pos = {
    base,
    base + edge0,
    base + edge0 + edge1,
    base + edge1,
  };
  std::array<float2, 4> uv = {float2{0.0f, 1.0f}, float2{1.0f, 1.0f}, float2{1.0f, 0.0f}, float2{0.0f, 0.0f}};

  float3 bbox_min = {kMaxFloat, kMaxFloat, kMaxFloat};
  float3 bbox_max = {-kMaxFloat, -kMaxFloat, -kMaxFloat};

  for (size_t i = 0; i < final_pos.size(); ++i) {
    const float3& p = final_pos[i];
    data.vertices.pos.emplace_back(p);
    data.vertices.nrm.emplace_back(nrm);
    data.vertices.tan.emplace_back(tan);
    data.vertices.btn.emplace_back(btn);
    data.vertices.tex.emplace_back(uv[i]);
    bbox_min = min(bbox_min, p);
    bbox_max = max(bbox_max, p);
  }

  Triangle t0{};
  t0.i[0] = vertex_offset + 0;
  t0.i[1] = vertex_offset + 2;
  t0.i[2] = vertex_offset + 1;
  t0.material_index = material_index;

  Triangle t1{};
  t1.i[0] = vertex_offset + 0;
  t1.i[1] = vertex_offset + 3;
  t1.i[2] = vertex_offset + 2;
  t1.material_index = material_index;

  if (!validate_triangle(t0, data.vertices.pos) || !validate_triangle(t1, data.vertices.pos)) {
    data.vertices.pos.resize(vertex_offset);
    data.vertices.nrm.resize(vertex_offset);
    data.vertices.tan.resize(vertex_offset);
    data.vertices.btn.resize(vertex_offset);
    data.vertices.tex.resize(vertex_offset);
    return false;
  }

  const uint32_t tri_start = static_cast<uint32_t>(data.triangles.size());
  data.triangles.emplace_back(t0);
  data.triangles.emplace_back(t1);
  data.triangle_to_emitter.emplace_back(kInvalidIndex);
  data.triangle_to_emitter.emplace_back(kInvalidIndex);

  data.add_mesh("quad", tri_start, 2, bbox_min, bbox_max);
  return true;
}

bool add_builtin_cube(const float3& translate, const float3& scale, const float3& rotation_deg, uint32_t material_index, SceneData& data) {
  const float3 h = 0.5f * scale;
  const uint32_t vertex_offset = static_cast<uint32_t>(data.vertices.pos.size());

  struct VDef {
    float3 p;
    float3 n;
    float2 uv;
  };

  const float3 px = {1.0f, 0.0f, 0.0f};
  const float3 nx = {-1.0f, 0.0f, 0.0f};
  const float3 py = {0.0f, 1.0f, 0.0f};
  const float3 ny = {0.0f, -1.0f, 0.0f};
  const float3 pz = {0.0f, 0.0f, 1.0f};
  const float3 nz = {0.0f, 0.0f, -1.0f};

  std::vector<VDef> verts = {
    // +X
    {{h.x, -h.y, -h.z}, px, {0.0f, 1.0f}},
    {{h.x, h.y, -h.z}, px, {0.0f, 0.0f}},
    {{h.x, h.y, h.z}, px, {1.0f, 0.0f}},
    {{h.x, -h.y, h.z}, px, {1.0f, 1.0f}},
    // -X
    {{-h.x, -h.y, h.z}, nx, {0.0f, 1.0f}},
    {{-h.x, h.y, h.z}, nx, {0.0f, 0.0f}},
    {{-h.x, h.y, -h.z}, nx, {1.0f, 0.0f}},
    {{-h.x, -h.y, -h.z}, nx, {1.0f, 1.0f}},
    // +Y
    {{-h.x, h.y, -h.z}, py, {0.0f, 1.0f}},
    {{-h.x, h.y, h.z}, py, {0.0f, 0.0f}},
    {{h.x, h.y, h.z}, py, {1.0f, 0.0f}},
    {{h.x, h.y, -h.z}, py, {1.0f, 1.0f}},
    // -Y
    {{-h.x, -h.y, h.z}, ny, {0.0f, 1.0f}},
    {{-h.x, -h.y, -h.z}, ny, {0.0f, 0.0f}},
    {{h.x, -h.y, -h.z}, ny, {1.0f, 0.0f}},
    {{h.x, -h.y, h.z}, ny, {1.0f, 1.0f}},
    // +Z
    {{h.x, -h.y, h.z}, pz, {0.0f, 1.0f}},
    {{h.x, h.y, h.z}, pz, {0.0f, 0.0f}},
    {{-h.x, h.y, h.z}, pz, {1.0f, 0.0f}},
    {{-h.x, -h.y, h.z}, pz, {1.0f, 1.0f}},
    // -Z
    {{-h.x, -h.y, -h.z}, nz, {0.0f, 1.0f}},
    {{-h.x, h.y, -h.z}, nz, {0.0f, 0.0f}},
    {{h.x, h.y, -h.z}, nz, {1.0f, 0.0f}},
    {{h.x, -h.y, -h.z}, nz, {1.0f, 1.0f}},
  };

  float3 bbox_min = {kMaxFloat, kMaxFloat, kMaxFloat};
  float3 bbox_max = {-kMaxFloat, -kMaxFloat, -kMaxFloat};

  for (const auto& v : verts) {
    float3 p = rotate_yxz_deg(v.p, rotation_deg);
    p += translate;
    float3 n = rotate_yxz_deg(v.n, rotation_deg);
    float ln = length(n);
    if (ln > 0.0f)
      n /= ln;
    data.vertices.pos.emplace_back(p);
    data.vertices.nrm.emplace_back(n);
    data.vertices.tan.emplace_back(float3{0.0f, 0.0f, 0.0f});
    data.vertices.btn.emplace_back(float3{0.0f, 0.0f, 0.0f});
    data.vertices.tex.emplace_back(v.uv);
    bbox_min = min(bbox_min, p);
    bbox_max = max(bbox_max, p);
  }

  static const uint32_t idx[] = {
    0, 1, 2, 0, 2, 3,        // +X
    4, 5, 6, 4, 6, 7,        // -X
    8, 9, 10, 8, 10, 11,     // +Y
    12, 13, 14, 12, 14, 15,  // -Y
    16, 17, 18, 16, 18, 19,  // +Z
    20, 21, 22, 20, 22, 23   // -Z
  };

  uint32_t tri_start = static_cast<uint32_t>(data.triangles.size());
  for (size_t i = 0; i < sizeof(idx) / sizeof(idx[0]); i += 3) {
    Triangle& tri = data.triangles.emplace_back();
    tri.i[0] = vertex_offset + idx[i + 0];
    tri.i[1] = vertex_offset + idx[i + 1];
    tri.i[2] = vertex_offset + idx[i + 2];
    tri.material_index = material_index;
    if (validate_triangle(tri, data.vertices.pos) == false) {
      data.triangles.pop_back();
      continue;
    }
    data.triangle_to_emitter.emplace_back(kInvalidIndex);
  }

  uint32_t tri_end = static_cast<uint32_t>(data.triangles.size());
  if (tri_end == tri_start)
    return false;

  data.add_mesh("cube", tri_start, tri_end - tri_start, bbox_min, bbox_max);
  return true;
}

bool load_tungsten_camera(const nlohmann::json& js, SceneData& data, Camera& active_camera) {
  if (js.contains("camera") == false || js["camera"].is_object() == false)
    return false;

  const auto& cam = js["camera"];
  float3 pos = active_camera.position;
  float3 target = pos + active_camera.direction;
  float3 up = kWorldUp;
  float fov = get_camera_fov(active_camera);
  uint2 film = active_camera.film_size;

  if (cam.contains("transform") && cam["transform"].is_object()) {
    const auto& tr = cam["transform"];
    if (tr.contains("position"))
      pos = json_to_float3(tr["position"], pos);
    if (tr.contains("target"))
      target = json_to_float3(tr["target"], target);
    if (tr.contains("look_at"))
      target = json_to_float3(tr["look_at"], target);
    if (tr.contains("up"))
      up = json_to_float3(tr["up"], up);
  }

  if (cam.contains("position"))
    pos = json_to_float3(cam["position"], pos);
  if (cam.contains("target"))
    target = json_to_float3(cam["target"], target);
  if (cam.contains("look_at"))
    target = json_to_float3(cam["look_at"], target);
  if (cam.contains("up"))
    up = json_to_float3(cam["up"], up);
  if (cam.contains("fov") && cam["fov"].is_number())
    fov = float(cam["fov"].get<double>());
  if (cam.contains("resolution")) {
    const auto& res = cam["resolution"];
    if (res.is_array() && res.size() >= 2) {
      film.x = static_cast<uint32_t>(res[0].get<int64_t>());
      film.y = static_cast<uint32_t>(res[1].get<int64_t>());
    } else if (res.is_number_integer()) {
      uint32_t r = static_cast<uint32_t>(std::max<int64_t>(1, res.get<int64_t>()));
      film = {r, r};
    }
  }

  if (up.x == 0.0f && up.y == 0.0f && up.z == 0.0f) {
    up = kWorldUp;
  }

  if ((film.x == 0u) || (film.y == 0u)) {
    film = {1280u, 720u};
  }

  auto& entry = data.cameras.emplace_back();
  entry.id = "tungsten_camera";
  entry.active = data.cameras.size() == 1;
  build_camera(entry.cam, pos, normalize(target - pos), normalize(up), film, fov);
  active_camera = entry.cam;
  return true;
}

void recompute_vertex_normals(SceneData& data, uint32_t vertex_start, uint32_t vertex_end, uint32_t tri_start, uint32_t tri_end) {
  if (vertex_end <= vertex_start)
    return;
  std::vector<float3> accum(vertex_end - vertex_start, float3{});
  for (uint32_t t = tri_start; t < tri_end; ++t) {
    const Triangle& tri = data.triangles[t];
    const float3& p0 = data.vertices.pos[tri.i[0]];
    const float3& p1 = data.vertices.pos[tri.i[1]];
    const float3& p2 = data.vertices.pos[tri.i[2]];
    float3 n = cross(p1 - p0, p2 - p0);
    if (length(n) <= kEpsilon)
      continue;
    accum[tri.i[0] - vertex_start] += n;
    accum[tri.i[1] - vertex_start] += n;
    accum[tri.i[2] - vertex_start] += n;
  }
  for (uint32_t i = vertex_start; i < vertex_end; ++i) {
    float3 n = accum[i - vertex_start];
    float ln = length(n);
    if (ln > kEpsilon)
      n /= ln;
    else
      n = float3{0.0f, 1.0f, 0.0f};
    data.vertices.nrm[i] = n;
  }
}

bool load_wo3_mesh(const std::string& resolved, const float3& translate, const float3& scale, uint32_t material_index, SceneData& data, bool recompute_normals) {
  std::ifstream fin(resolved, std::ios::binary);
  if (fin.good() == false) {
    log::warning("Failed to open Tungsten mesh %s", resolved.c_str());
    return false;
  }

  struct Wo3Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
  };
  struct Wo3Triangle {
    uint32_t v0, v1, v2;
    int32_t material;
  };

  uint64_t vert_count = 0;
  if (!fin.read(reinterpret_cast<char*>(&vert_count), sizeof(uint64_t))) {
    log::warning("Failed to read vertex count from %s", resolved.c_str());
    return false;
  }

  std::vector<Wo3Vertex> vbuf(vert_count);
  if (!fin.read(reinterpret_cast<char*>(vbuf.data()), vbuf.size() * sizeof(Wo3Vertex))) {
    log::warning("Failed to read vertices from %s", resolved.c_str());
    return false;
  }

  uint64_t tri_count = 0;
  if (!fin.read(reinterpret_cast<char*>(&tri_count), sizeof(uint64_t))) {
    log::warning("Failed to read triangle count from %s", resolved.c_str());
    return false;
  }

  std::vector<Wo3Triangle> tbuf(tri_count);
  if (!fin.read(reinterpret_cast<char*>(tbuf.data()), tbuf.size() * sizeof(Wo3Triangle))) {
    log::warning("Failed to read triangles from %s", resolved.c_str());
    return false;
  }

  uint32_t triangle_start = static_cast<uint32_t>(data.triangles.size());
  float3 bbox_min = {kMaxFloat, kMaxFloat, kMaxFloat};
  float3 bbox_max = {-kMaxFloat, -kMaxFloat, -kMaxFloat};

  uint32_t vertex_offset = static_cast<uint32_t>(data.vertices.pos.size());

  data.vertices.pos.reserve(vertex_offset + static_cast<uint32_t>(vbuf.size()));
  data.vertices.nrm.reserve(vertex_offset + static_cast<uint32_t>(vbuf.size()));
  data.vertices.tan.reserve(vertex_offset + static_cast<uint32_t>(vbuf.size()));
  data.vertices.btn.reserve(vertex_offset + static_cast<uint32_t>(vbuf.size()));
  data.vertices.tex.reserve(vertex_offset + static_cast<uint32_t>(vbuf.size()));

  for (const auto& v : vbuf) {
    float3 pos = {v.px * scale.x, v.py * scale.y, v.pz * scale.z};
    pos += translate;
    float3 nrm = {v.nx, v.ny, v.nz};
    float2 uv = {v.u, 1.0f - v.v};
    data.vertices.pos.emplace_back(pos);
    data.vertices.nrm.emplace_back(nrm);
    data.vertices.tan.emplace_back(float3{0.0f, 0.0f, 0.0f});
    data.vertices.btn.emplace_back(float3{0.0f, 0.0f, 0.0f});
    data.vertices.tex.emplace_back(uv);
    bbox_min = min(bbox_min, pos);
    bbox_max = max(bbox_max, pos);
  }

  data.triangles.reserve(data.triangles.size() + tbuf.size());
  data.triangle_to_emitter.reserve(data.triangle_to_emitter.size() + tbuf.size());

  for (const auto& tri_in : tbuf) {
    Triangle& tri = data.triangles.emplace_back();
    tri.i[0] = vertex_offset + tri_in.v0;
    tri.i[1] = vertex_offset + tri_in.v1;
    tri.i[2] = vertex_offset + tri_in.v2;
    tri.material_index = material_index;
    if (validate_triangle(tri, data.vertices.pos) == false) {
      data.triangles.pop_back();
      continue;
    }
    data.triangle_to_emitter.emplace_back(kInvalidIndex);
  }

  uint32_t triangle_end = static_cast<uint32_t>(data.triangles.size());
  uint32_t triangle_count = triangle_end - triangle_start;
  if (triangle_count == 0)
    return false;

  if (recompute_normals) {
    uint32_t vertex_end = static_cast<uint32_t>(data.vertices.pos.size());
    recompute_vertex_normals(data, vertex_offset, vertex_end, triangle_start, triangle_end);
  }

  std::string mesh_name = std::filesystem::path(resolved).stem().string();
  data.add_mesh(mesh_name.c_str(), triangle_start, triangle_count, bbox_min, bbox_max);
  return true;
}

uint32_t load_tungsten_primitives(const nlohmann::json& js, const char* base_dir, const std::unordered_map<std::string, uint32_t>& bsdf_to_mat, SceneData& data,
  SceneLoaderContext& context, Scene& scene, const IORDatabase& database, TaskScheduler& scheduler, Camera& active_camera, bool force_two_sided) {
  uint32_t load_result = SceneLoadFailed;

  if (js.contains("primitives") == false || js["primitives"].is_array() == false)
    return load_result;

  for (const auto& prim : js["primitives"]) {
    if (prim.is_object() == false)
      continue;
    std::string type = prim.value("type", "");
    if (type == "infinite_sphere") {
      std::string emission = prim.value("emission", "");
      bool sample = prim.value("sample", true);
      if (emission.empty() == false && sample) {
        std::string img_path = resolve_path(base_dir, emission);
        uint32_t img_idx = context.add_image(img_path.c_str(), Image::BuildSamplingTable | Image::RepeatU, {}, {1.0f, 1.0f});
        uint32_t sp_white = data.add_spectrum(SpectralDistribution::rgb_reflectance({1.0f, 1.0f, 1.0f}));

        auto& profile = data.emitter_profiles.emplace_back(EmitterProfile::Class::Environment);
        profile.emission.spectrum_index = sp_white;
        profile.emission.image_index = img_idx;
        profile.medium_index = kInvalidIndex;

        auto& inst = data.emitter_instances.emplace_back(EmitterProfile::Class::Environment);
        inst.profile = static_cast<uint32_t>(data.emitter_profiles.size() - 1);
        inst.triangle_index = kInvalidIndex;
        inst.additional_weight = 0.0f;

        load_result |= SceneLoadSucceeded;
      } else {
        log::warning("Skipping Tungsten infinite_sphere without emission or sample=false");
      }
      continue;
    }

    uint32_t material_index = scene.missing_material;
    if (prim.contains("bsdf")) {
      const auto& bsdf_node = prim["bsdf"];
      if (bsdf_node.is_string()) {
        std::string bsdf_name = bsdf_node.get<std::string>();
        auto it = bsdf_to_mat.find(bsdf_name);
        if (it != bsdf_to_mat.end()) {
          material_index = it->second;
        }
      } else if (bsdf_node.is_object()) {
        std::string mat_name = std::string("__prim_bsdf_") + std::to_string(data.materials.size());
        material_index = add_tungsten_material(mat_name, bsdf_node, data, context, base_dir, database, force_two_sided);
      }
    }

    if (material_index == scene.missing_material) {
      std::string mat_name = std::string("__prim_bsdf_") + std::to_string(data.materials.size());
      material_index = add_tungsten_material(mat_name, nlohmann::json::object(), data, context, base_dir, database, force_two_sided);
    }

    bool two_sided = prim.value("two_sided", prim.value("twoSided", false));
    if (prim.contains("bsdf") && prim["bsdf"].is_object()) {
      const auto& bsdf_node = prim["bsdf"];
      two_sided = two_sided || bsdf_node.value("two_sided", bsdf_node.value("twoSided", false));
    }
    if (force_two_sided)
      two_sided = true;

    bool has_emission = prim.contains("emission");
    bool has_power = prim.contains("power");
    float emission_scale = 1.0f;
    if (prim.contains("scale") && prim["scale"].is_number())
      emission_scale = float(prim["scale"].get<double>());

    auto& mtl = data.materials[material_index];
    if (two_sided)
      mtl.two_sided = 1u;

    float3 translate = {};
    float3 scale = {1.0f, 1.0f, 1.0f};
    float3 rotation = {};
    if (prim.contains("transform") && prim["transform"].is_object()) {
      const auto& tr = prim["transform"];
      if (tr.contains("position")) {
        translate = json_to_float3(tr["position"], {});
      }
      if (tr.contains("scale")) {
        const auto& sc = tr["scale"];
        if (sc.is_array() && sc.size() >= 3) {
          scale = {float(sc[0].get<double>()), float(sc[1].get<double>()), float(sc[2].get<double>())};
        } else if (sc.is_number()) {
          float v = float(sc.get<double>());
          scale = {v, v, v};
        }
      }
      if (tr.contains("rotation")) {
        rotation = json_to_float3(tr["rotation"], {});
      }
    }

    uint32_t tri_start = static_cast<uint32_t>(data.triangles.size());
    bool prim_loaded = false;

    if (type == "quad") {
      prim_loaded = add_builtin_quad(translate, scale, rotation, material_index, data);
    } else if (type == "cube") {
      prim_loaded = add_builtin_cube(translate, scale, rotation, material_index, data);
    } else if (type != "mesh") {
      log::warning("Unsupported Tungsten primitive type: %s", type.c_str());
    }

    if (type == "mesh") {
      std::string fname = prim.value("filename", "");
      if (fname.empty() && prim.contains("file"))
        fname = prim["file"].get<std::string>();
      std::string resolved = resolve_path(base_dir, fname);
      if (resolved.empty()) {
        log::warning("Tungsten mesh has no filename, skipping");
      } else {
        const char* ext = get_ext(resolved);
        if (_stricmp(ext, ".obj") == 0) {
          uint32_t flags = load_from_obj_file(resolved.c_str(), "", data, context, scene, database, scheduler);
          prim_loaded = (flags & SceneLoadSucceeded) != 0u;
          load_result |= flags;
        } else if (_stricmp(ext, ".gltf") == 0) {
          uint32_t flags = load_from_gltf_file(resolved.c_str(), false, data, context, scene, scheduler, active_camera);
          prim_loaded = (flags & SceneLoadSucceeded) != 0u;
          load_result |= flags;
        } else if (_stricmp(ext, ".glb") == 0) {
          uint32_t flags = load_from_gltf_file(resolved.c_str(), true, data, context, scene, scheduler, active_camera);
          prim_loaded = (flags & SceneLoadSucceeded) != 0u;
          load_result |= flags;
        } else if (_stricmp(ext, ".wo3") == 0) {
          bool recompute_normals = prim.value("recompute_normals", false);
          prim_loaded = load_wo3_mesh(resolved, translate, scale, material_index, data, recompute_normals);
          if (prim_loaded)
            load_result |= SceneLoadSucceeded;
        } else {
          log::warning("Unsupported Tungsten mesh format: %s", resolved.c_str());
        }
      }
    }

    if (prim_loaded) {
      load_result |= SceneLoadSucceeded;
      uint32_t tri_end = static_cast<uint32_t>(data.triangles.size());
      float area = triangles_area(data, tri_start, tri_end);
      if (has_power) {
        if (area <= 0.0f) {
          log::warning("Tungsten emitter has zero area, skipping power");
        } else {
          float power_scale = emission_scale / (kPi * area);
          if (two_sided)
            power_scale *= 0.5f;
          set_emission_from_json(mtl, prim["power"], data, context, base_dir, power_scale);
        }
      } else if (has_emission) {
        set_emission_from_json(mtl, prim["emission"], data, context, base_dir, emission_scale);
      }
    }
  }

  return load_result;
}

}  // namespace

uint32_t load_from_tungsten_file(const char* file_name, SceneData& data, SceneLoaderContext& context, Scene& scene, const IORDatabase& database, TaskScheduler& scheduler,
  Camera& active_camera) {
  if ((file_name == nullptr) || (file_name[0] == 0))
    return SceneLoadFailed;

  std::ifstream in(file_name, std::ios::binary);
  if (in.is_open() == false) {
    log::error("Failed to open Tungsten scene: %s", file_name);
    return SceneLoadFailed;
  }

  nlohmann::json js;
  try {
    in >> js;
  } catch (const std::exception& e) {
    log::error("Failed to parse Tungsten scene %s: %s", file_name, e.what());
    return SceneLoadFailed;
  }

  constexpr uint32_t buffer_size = 2048;
  char base_dir[buffer_size] = {};
  get_base_directory(file_name, base_dir, sizeof(base_dir));

  bool force_two_sided = js.value("enable_two_sided_shading", false);
  if (js.contains("renderer") && js["renderer"].is_object()) {
    force_two_sided = force_two_sided || js["renderer"].value("enable_two_sided_shading", false);
  }

  // Map Tungsten bsdf names to material indices (create placeholders)
  std::unordered_map<std::string, uint32_t> bsdf_to_mat;
  if (js.contains("bsdfs") && js["bsdfs"].is_array()) {
    for (const auto& b : js["bsdfs"]) {
      if (b.is_object() == false)
        continue;
      std::string name = b.value("name", "");
      if (name.empty())
        continue;
      if (bsdf_to_mat.count(name) > 0)
        continue;
      uint32_t mat_idx = add_tungsten_material(name, b, data, context, base_dir, database, force_two_sided);
      bsdf_to_mat[name] = mat_idx;
    }
  }

  bool camera_loaded = load_tungsten_camera(js, data, active_camera);

  uint32_t load_result = load_tungsten_primitives(js, base_dir, bsdf_to_mat, data, context, scene, database, scheduler, active_camera, force_two_sided);

  if ((load_result & SceneLoadSucceeded) == 0u) {
    return camera_loaded ? SceneLoadCameraInfo : SceneLoadFailed;
  }

  if (camera_loaded) {
    load_result |= SceneLoadCameraInfo;
  }

  return load_result;
}

}  // namespace etx
