#include <etx/render/host/scene_gltf_loader.hxx>

#include <etx/core/core.hxx>
#include <etx/core/log.hxx>
#include <etx/render/host/scene_data.hxx>
#include <etx/render/shared/scene.hxx>
#include <etx/render/shared/ior_database.hxx>
#include <etx/render/host/tasks.hxx>

namespace etx {

struct SceneGltfLoaderImpl {};

ETX_IMPLEMENT_PIMPL(SceneGltfLoader);

SceneGltfLoader::SceneGltfLoader() {
  ETX_PIMPL_INIT(SceneGltfLoader);
}

SceneGltfLoader::~SceneGltfLoader() {
  ETX_PIMPL_CLEANUP(SceneGltfLoader);
}

uint32_t SceneGltfLoader::load_from_file(const char* file_name, bool binary, SceneData& data, SceneLoaderContext& context, Scene& scene, const IORDatabase& database,
  TaskScheduler& scheduler) {
  log::warning("SceneGltfLoader::load_from_file() - Dummy implementation, not yet moved from SceneRepresentationImpl");
  return SceneLoadFailed;
}

}  // namespace etx
