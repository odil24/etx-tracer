#pragma once

#include <etx/core/pimpl.hxx>
#include <etx/render/host/scene_data.hxx>
#include <etx/render/host/scene_loader_utils.hxx>
#include <etx/render/shared/math.hxx>

#include <string>
#include <map>

namespace etx {

struct SceneLoaderContext;
struct Scene;
struct IORDatabase;
struct TaskScheduler;

struct SceneObjLoader {
  SceneObjLoader();
  ~SceneObjLoader();

  uint32_t load_from_file(const char* file_name, const char* mtl_file, const std::map<std::string, std::string>& object_to_material, SceneData& data, SceneLoaderContext& context,
    Scene& scene, const IORDatabase& database, TaskScheduler& scheduler);

 private:
  ETX_DECLARE_PIMPL(SceneObjLoader, 1024);
};

}  // namespace etx
