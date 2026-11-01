# 本版工程的真实验证记录

日期：2026-10-19 UTC。主工程由本次 `lab/src`、`lab/shaders` 和 `CMakeLists.txt` 构建。这里的证据没有引用旧版只做 clear/triangle 的历史运行。

## 环境与构建

系统为 Ubuntu 24.04.3 LTS x86-64。使用 GCC 13.3.0、CMake 3.28.3、Vulkan headers/loader 1.3.275；glslangValidator 15.1.0 将全部 8 个 shader 编译到 Vulkan 1.3 / SPIR-V 1.6。最终执行过完整 `cmake --build build --clean-first --parallel 4`，退出 0，stderr 0 字节，无编译警告。详见 `build.stdout.txt`、`build.stderr.txt`。

实际设备为 Mesa 25.2.8-0ubuntu0.24.04.4 的 Lavapipe/llvmpipe，LLVM 20.1.2，CPU 软件实现，设备报告 Vulkan 1.4.318。验证层为 VK_LAYER_KHRONOS_validation 1.3.275，程序通过 VkValidationFeaturesEXT 显式启用 synchronization validation。`tool_versions.txt` 保留工具真实版本输出，`dependency_packages.json` 记录从 Ubuntu 官方服务器下载解压的包版本、URL 与 SHA-256。

编译工具、headers、validation layer、Mesa 与依赖只解压到 `/tmp/vulkan-selfstudy-deps/root`，没有安装或更改系统包。执行主程序时使用下列当前会话环境：

```bash
source /tmp/vulkan-selfstudy-deps/env.sh
export MESA_SHADER_CACHE_DISABLE=true
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DVulkan_INCLUDE_DIR=/tmp/vulkan-selfstudy-deps/root/usr/include \
  -DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
  -DLAB_GLSLANG=/tmp/vulkan-selfstudy-deps/root/usr/bin/glslangValidator
cmake --build build --parallel 4
python3 scripts/run_all.py --exe build/vulkan_lessons --out evidence
```

`env.sh` 的关键值为：

```text
PATH 前缀 /tmp/vulkan-selfstudy-deps/root/usr/bin
LD_LIBRARY_PATH 前缀 /tmp/vulkan-selfstudy-deps/root/usr/lib/x86_64-linux-gnu
VK_DRIVER_FILES=/tmp/vulkan-selfstudy-deps/root/usr/share/vulkan/icd.d/lvp_icd.json
VK_LAYER_PATH=/tmp/vulkan-selfstudy-deps/root/usr/share/vulkan/explicit_layer.d
```

这些绝对路径仅记录本次验证；读者正常安装开发文件后使用 README 的普通 CMake 命令。发行包不包含临时 SDK、驱动或 build 目录。

## 主线 21 个 Vulkan 案例

`summary.json` 的 status 为 PASS。每个案例的退出码均为 0，stderr 均为 0 字节，stdout 明确包含同步验证已启用及 `Validation summary: errors=0; warnings=0`。

| 组别 | 实际运行 | 图像证据与检查 |
|---|---|---|
| 查询与基础 | probe、clear、triangle、vertex | probe真实查询；clear故意指定不存在的shader目录仍成功；triangle/vertex RGB相同 |
| 矩阵 | camera shift 0、0.35 | 质心向右移动89.5px，理论89.6px，保持纵向位置 |
| 纹理 | texture | 64×64红白棋盘真实上传后采样；410×410覆盖区域 |
| 深度 | depth、depth --no-depth | 中心分别为(255,0,0)与(0,0,255) |
| mipmap | --lod 0、1、2、3、4、5、6 | 每次均生成7级；mip0等于texture，mip6为(235,145,140) |
| 数组层 | --layer 0、1、2 | 红白棋盘、绿横条、蓝黄竖条；layer0等于texture |
| 多帧 | 9帧 --inflight 1、2 | 首中末RGB逐字节相同，质心X依次140.5、255.5、370.5 |

CPU 参考额外运行一次，像素内容与 Vulkan clear 相同。它不计入 21 个 Vulkan 案例。

背景为 RGB(6,9,14)。triangle 与 vertex 各有 68658 个像素不同于背景。camera 的栅格覆盖随亚像素边界可能相差一列；检查采用理论位移附近的质心差，避免把离散像素数相同误当作矩阵正确的必要条件。

主线同名 `*.stdout.txt`、`*.stderr.txt` 是每次运行独立保存的实际文本。PNG 由 `scripts/ppm_tools.py` 的标准库 PNG writer 从对应 PPM 原始 RGB 生成，没有第二次渲染或颜色变换。

## 教材透视扩展：另外两次隔离验证

教材第8章的 `make_perspective_demo_matrix()` 在 `/tmp/vulkan-perspective-check` 的独立工程副本中编译。主线 `src/math.hpp`、`run_camera` 的基线调用没有改变。测试把完整函数放入 lab 命名空间、加入 `<cmath>`，仅将 `run_camera` 中的矩阵调用换为该函数。函数原文另保存在 `perspective_extension.cpp.txt`，读者可直接使用 `extras/perspective_demo.hpp`。

两次均运行 camera、--shift 0、--validation；第二次只把 distance=2 改成4。两次退出0、stderr0、errors0、warnings0。`perspective_summary.json` 保存命令和像素统计。

| 情况 | 前景边界（含端点） | 宽高 | 前景像素 | 质心 |
|---|---|---:|---:|---|
| 基线camera0 | x/y=115..396 | 282×282 | 79524 | (255.5,255.5) |
| distance2 | x/y=134..377 | 244×244 | 59536 | (255.5,255.5) |
| distance4 | x/y=195..316 | 122×122 | 14884 | (255.5,255.5) |

244/282≈0.865，与理论√3/2≈0.866的差来自光栅化取样；122/244=0.5。投影中的负Y项也正确翻转上下颜色排列：基线左上近红、左下近黄，透视实验左上近黄、左下近红。这两次跟做练习不被虚构成主程序的另一个 CLI lesson，也不计入 `run_all.py` 的21案例报告。

## 未覆盖的范围

没有在 Windows、物理 GPU、macOS/MoltenVK 或 GLFW/swapchain 环境运行。当前软件实现的 host-visible 内存为 coherent，因此非 coherent flush/invalidate 分支未得到实际设备覆盖。没有故意制造并运行非法 Vulkan 用法，所以不存在本次真实错误 VUID 日志；教材的故障推演应按推演标识。

双槽程序支持两份资源各自安全复用，并实际保存了正确帧数据；这不证明两份设备工作一定同时执行，也不是性能基准。窗口章节为设计拓展，不属于本工程的已运行目标。
