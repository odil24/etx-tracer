#pragma once

#include <etx/render/shared/math.hxx>
#include <etx/render/shared/scene.hxx>

#include <vector>

namespace etx {

enum : uint32_t {
  SceneLoadFailed = 0u,
  SceneLoadSucceeded = 1u << 0u,
  SceneLoadCameraInfo = 1u << 1u,
};

inline float2 make_float2(const float values[]) {
  return {values[0], values[1]};
}

inline float3 make_float3(const float values[]) {
  return {values[0], values[1], values[2]};
}

inline bool validate_triangle(Triangle& tri, const std::vector<float3>& vertices) {
  if (tri.i[0] >= vertices.size() || tri.i[1] >= vertices.size() || tri.i[2] >= vertices.size()) {
    return false;
  }
  tri.geo_n = cross(vertices[tri.i[1]] - vertices[tri.i[0]], vertices[tri.i[2]] - vertices[tri.i[0]]);
  float l = length(tri.geo_n);
  if (l == 0.0f) {
    return false;
  }
  tri.geo_n /= l;
  return true;
}

}  // namespace etx
