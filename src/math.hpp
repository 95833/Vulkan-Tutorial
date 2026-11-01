#pragma once
#include <array>
#include <cstddef>

namespace lab {
// GLSL std140 mat4 = 4 个 vec4 列，每列16字节；本结构共64字节。
struct alignas(16) CameraUniform { std::array<float, 16> view_projection{}; };
static_assert(sizeof(CameraUniform) == 64, "std140 mat4 must occupy 64 bytes");
static_assert(alignof(CameraUniform) == 16, "uniform block alignment");
static_assert(offsetof(CameraUniform, view_projection) == 0, "matrix starts at byte 0");

inline CameraUniform make_camera_matrix(float shift) {
    // 最小正交/平移实验：列主序，column-vector，M * vec4(position, 1)。
    // m[12] 是第4列第1行；此处 w=1，正 shift 将场景向画面右边平移。
    // 这不是完整 lookAt + perspective 相机，也不是相机世界坐标。
    CameraUniform result;
    result.view_projection = {1,0,0,0, 0,1,0,0, 0,0,1,0, shift,0,0,1};
    return result;
}
} // namespace lab
