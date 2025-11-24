#include <etx/render/host/scene_obj_loader.hxx>

#include <etx/core/core.hxx>
#include <etx/core/log.hxx>
#include <etx/render/host/scene_data.hxx>
#include <etx/render/shared/scene.hxx>
#include <etx/render/shared/ior_database.hxx>
#include <etx/render/host/tasks.hxx>

namespace etx {

struct SceneObjLoaderImpl {};

ETX_IMPLEMENT_PIMPL(SceneObjLoader);

SceneObjLoader::SceneObjLoader() {
  ETX_PIMPL_INIT(SceneObjLoader);
}

SceneObjLoader::~SceneObjLoader() {
  ETX_PIMPL_CLEANUP(SceneObjLoader);
}

uint32_t SceneObjLoader::load_from_file(const char* file_name, const char* mtl_file, const std::map<std::string, std::string>& object_to_material, SceneData& data,
  SceneLoaderContext& context, Scene& scene, const IORDatabase& database, TaskScheduler& scheduler) {
  log::warning("SceneObjLoader::load_from_file() - Dummy implementation, not yet moved from SceneRepresentationImpl");
  return SceneLoadFailed;
}

}  // namespace etx
