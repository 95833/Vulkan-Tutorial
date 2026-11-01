# Vulkan 自学实验工程：每次新增一个可观察的能力

本工程配合《Vulkan 自学教程》使用，也可以按这份 README 独立运行。你将从读取设备能力开始，依次得到清屏、三角形、顶点缓冲、矩阵 UBO、棋盘纹理、mipmap、纹理数组、深度遮挡和多帧图像。

主程序采用 **C++17、Vulkan 1.3 C API、dynamic rendering、synchronization2、一条 graphics queue、512×512 离屏目标**。每次运行保存真实 Vulkan 渲染结果，再由标准库脚本转为 PNG。实验使用 Vulkan 驱动；满足能力要求的 CPU 软件实现也可以执行它。图形驱动报告的版本、开发用 headers 版本和 loader 版本分别查询、分别记录。

**建议从 `--lesson probe` 开始，然后按下表顺序推进。** 各课入口都在 `src/lessons.cpp`，没有必须先理解的引擎类或场景框架。先找到 `run_课程名()`，看这一课实际创建和录制了什么，再跳进被调用的函数。[1][2]

## 1. 先知道运行后应得到什么

| 实验 | 新增能力 | 结果 / 对照 | 主要入口 |
|---|---|---|---|
| `probe` | Instance、设备/feature/队列/格式/内存查询 | 终端报告真实能力；不写图像 | `run_probe`、`create_context` |
| `clear` | Image、内存、view、命令、提交、同步与回读 | 全图 RGB `(6,9,14)` | `run_clear` |
| `triangle` | Shader、空 vertex input、graphics pipeline、draw | 上红、右下绿、左下蓝的三角形 | `run_triangle` |
| `vertex` | 真正的 vertex buffer、binding、attribute | 与上一课相同的三角形，数据来源改变 | `run_vertex` |
| `camera` | 64 字节 `std140 mat4` UBO、descriptor set | 彩色矩形；`--shift 0.35` 向画面右移 | `run_camera`、`make_camera_matrix` |
| `texture` | RGBA 上传、staging、sampler、combined image sampler | 中央红白棋盘矩形 | `run_texture`、`upload_texture` |
| `mips` | 分配并实际生成 7 个 mip level、显式 LOD | `--lod 0` 棋盘；`--lod 6` 淡红色 | `run_mips`、`upload_texture` |
| `layers` | 三层 2D array、array view、`sampler2DArray` | 第 0 层红白棋盘；第 1 层绿横条；第 2 层蓝黄竖条 | `run_layers` |
| `depth` | 深度附件、深度写入与比较 | 中央重叠区为近处红色；`--no-depth` 后变为后画的蓝色 | `run_depth` |
| `frames` | 每帧 UBO 更新、重新录制、安全复用；单槽/双槽 | 保存第 0、4、8 帧，矩形依次向右移动 | `run_frames`、`FrameSlot` |

`extras/cpu_reference.cpp` 是一个额外的纯 C++ 像素参考程序。它直接写出同样大小、同样背景色的 PPM，不调用 Vulkan。你可以先通过它认识文件中的像素，再理解为什么设备渲染需要另外的提交与等待。

## 2. 准备环境

### 必需条件

| 条件 | 最低要求与检查方法 |
|---|---|
| C++ 编译器 | 支持 C++17，例如本次验证的 GCC 13.3；Windows 路径见下文 |
| CMake | 3.21 或更新；`cmake --version` |
| Vulkan 开发文件 | 1.3 或更新的 headers 与 loader 链接库 |
| Vulkan 实现 | 设备报告 1.3+，查询且启用 `dynamicRendering`、`synchronization2`；`probe` 显示它们 |
| GLSL 编译器 | `glslc` 或 `glslangValidator`，能以 `vulkan1.3` 为目标；构建时自动编译全部 shader |
| Python | 3.8+；检查与 PNG 转换只使用标准库 |
| 验证层 | 使用 `--validation` 时必须有 `VK_LAYER_KHRONOS_validation` 与对应 validation features；缺失直接报错 |

SDK 可从 [LunarG 官方下载页面](https://vulkan.lunarg.com/sdk/home)取得。安装 SDK 不会把旧设备的硬件能力升级为 Vulkan 1.3。程序先查询支持情况，再把实际需要的两个 feature 写入设备创建结构；它不会把查询到的整个 feature 结构直接当作启用清单。[2]

Linux 的开发环境常涉及 `build-essential`、`cmake`、`libvulkan-dev`、`glslang-tools`、`vulkan-validationlayers`；使用 Mesa 软件实现还需要与系统匹配的 `mesa-vulkan-drivers`。请按你的发行版准备相应包，或加载官方 SDK 的环境设置。本次验证仅在 `/tmp` 下载解压 Ubuntu 官方包，没有安装或更改系统包；这组临时绝对路径不是读者必须复刻的工程结构。

### Linux：构建与第一次运行

进入本 README 所在的 `lab` 目录：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/vulkan_lessons --lesson probe --validation
./build/vulkan_lessons --lesson clear --output out/clear.ppm --validation
python3 scripts/ppm_tools.py out/clear.ppm --expect clear --png out/clear.png
```

最后一条命令先检查尺寸与纯背景，再把本次读回的原始 RGB 字节装入 PNG。PNG 转换没有再次运行 Vulkan，也没有改变颜色或重新绘制。

需要手动指定 shader 编译器时：

```bash
cmake -S . -B build -DLAB_GLSLANG=/absolute/path/to/glslangValidator
cmake --build build --parallel
```

### Windows：Visual Studio 2022 / PowerShell

准备 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载，以及 Vulkan SDK。打开 Developer PowerShell for VS 2022，进入 `lab`：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
.\build\vulkan_lessons.exe --lesson probe --validation
.\build\vulkan_lessons.exe --lesson clear --output out/clear.ppm --validation
python .\scripts\ppm_tools.py out/clear.ppm --expect clear --png out/clear.png
Invoke-Item out/clear.png
```

工程把 Visual Studio 各个配置的可执行文件统一放在 `build`，所以这里不需要另加 `Debug` 子目录；构建时由 CMake 查找 Vulkan 开发文件与工具。[3]若 Python 命令是 `py -3`，用它替换 `python`。如果 CMake 没找到 SDK 内的编译器，可指定：

```powershell
cmake -S . -B build "-DLAB_GLSLC=$env:VULKAN_SDK/Bin/glslc.exe"
```

**Windows 构建步骤是提供给读者的使用路径，本次交付没有在 Windows 或物理 GPU 上实际执行验证。** 本次真实环境及范围在第 8 节和 `evidence/verification.md` 中单独记录。

### 可选：先独立编译 CPU 参考

Linux / 类 Unix 的独立命令不需要任何 Vulkan 文件：

```bash
c++ -std=c++17 extras/cpu_reference.cpp -o cpu_reference
./cpu_reference out/cpu.ppm
python3 scripts/ppm_tools.py out/cpu.ppm --expect clear --png out/cpu.png
```

完成完整 CMake 配置后，同一参考也会作为 `build/cpu_reference`（Windows 为 `.exe`）生成。CMake 的完整配置本身仍会检查 Vulkan 与 shader 工具；要完全不依赖这些工具，请使用上面的直接 C++ 编译命令。

## 3. 按课运行：命令、观察与一次修改

下面使用 Linux 的可执行文件写法。Windows 将 `./build/vulkan_lessons` 换成 `.\build\vulkan_lessons.exe`，将 `python3` 换成 `python`。输出目录由程序自动创建。没有传入 `--output` 时，默认是 `out/课程名.ppm`。

### A. 查询设备：先证明环境具备能力

```bash
./build/vulkan_lessons --lesson probe --validation
```

先读 `Loader API` 与 `header version`，再读每个 `Device` 的 API、driver、queue family 和 memory type。`dynamicRendering=1` 表示查询结果支持；其他课程还会打印 `Enabled: dynamicRendering=1, synchronization2=1`，表示创建设备时实际启用了它们。`probe` 仅创建 Instance 并枚举，不会创建图形设备、读取 SPIR-V 或生成图像。

**你应能回答：** 编译成功为何仍可能无法运行？因为编译器和 headers 只能检查 CPU 端程序，实际驱动和设备能力还必须在运行时确认。

### B. 清屏：先让设备写出一个确定结果

```bash
./build/vulkan_lessons --lesson clear --output out/clear.ppm --validation
python3 scripts/ppm_tools.py out/clear.ppm --expect clear --png out/clear.png
```

打开 `src/lessons.cpp:run_clear`。它只做：创建目标、开始命令、开始 rendering、结束并复制、提交并等待、保存。清屏由 `begin_rendering()` 中颜色附件的 `loadOp=CLEAR` 与 `clearValue` 完成。

在 `src/render.cpp:begin_rendering` 修改三个 clear 浮点值，重新构建并运行，背景应随之改变。原版的 `--expect clear` 检查固定 `(6,9,14)`；改完颜色后原检查不再适用，直接转 PNG 或自己修改期望值。

### C. 三角形：让 draw 产生可覆盖的几何形状

```bash
./build/vulkan_lessons --lesson triangle --output out/triangle.ppm --validation
python3 scripts/ppm_tools.py out/triangle.ppm --expect triangle --png out/triangle.png
```

从 `run_triangle()` 找到 `vkCmdBindPipeline` 和 `vkCmdDraw(...,3,1,0,0)`。打开 `shaders/triangle.vert`，三次顶点调用按 `gl_VertexIndex` 选择位置与颜色。viewport 使用正高度，负 NDC y 映射到图像上方。修改顶点位置后重新构建，观察轮廓变化；修改 `color.frag` 为固定颜色，观察颜色变化。

### D. 顶点缓冲：保持图像相同，只替换数据来源

```bash
./build/vulkan_lessons --lesson vertex --output out/vertex.ppm --validation
python3 scripts/ppm_tools.py out/vertex.ppm --expect vertex --png out/vertex.png
```

这次三角形来自 CPU 的 `triangle_vertices`、真实 `VkBuffer` 和 `vkCmdBindVertexBuffers`。`Vertex` 在 `src/render.hpp` 中固定为 32 字节：

| 字段 | offset | shader location | Vulkan 格式 |
|---|---:|---:|---|
| `position[3]` | 0 | 0 | `R32G32B32_SFLOAT` |
| `color[3]` | 12 | 1 | `R32G32B32_SFLOAT` |
| `uv[2]` | 24 | 2 | `R32G32_SFLOAT` |

binding 0 的 stride 是 `sizeof(Vertex)`。彩色 pipeline 只声明位置、颜色两个实际使用的 attribute；纹理 pipeline 只声明位置、UV。`run_all.py` 会比较 `triangle` 与 `vertex` 的 RGB，原始场景应完全一致。

### E. 矩阵 UBO：用可观察的平移验证数据接线

```bash
./build/vulkan_lessons --lesson camera --shift 0 --output out/camera0.ppm --validation
./build/vulkan_lessons --lesson camera --shift 0.35 --output out/camera_shift.ppm --validation
python3 scripts/ppm_tools.py out/camera_shift.ppm --expect camera --png out/camera_shift.png
```

这一课画的是边界为 `±0.55` 的彩色矩形。`src/math.hpp` 中 `CameraUniform` 使用 `alignas(16)`，仅含 16 个 `float` 的列主序矩阵，大小 64 字节。shader 明确声明 `layout(set=0,binding=0,std140)` 的 `mat4 viewProjection`，并使用 `M * vec4(position,1)`。

`make_camera_matrix(shift)` 是一个最小正交/平移基线：矩阵对角线为 1，`m[12]=shift`。此处 `w=1`，正值将**场景**向画面右移。它不是完整 `lookAt + perspective` 相机，`--shift` 也不是相机世界坐标。理论像素位移为 `shift × 512 / 2`；`0.35` 对应 89.6 像素，光栅化后的覆盖质心允许出现小量量化差异。[4]

本例 vertex buffer 和 UBO 选用可映射内存，先写入、必要时 flush，再提交设备工作。host coherent 只省去显式缓存处理；每槽数据仍必须在该槽前一帧完成后才能覆盖。

### E 的跟做扩展：真正的透视投影

完整可复制函数保存在 `extras/perspective_demo.hpp`，名为 `make_perspective_demo_matrix()`。它保持同一个 64 字节 UBO 接口，组合 P×V，采用 60° 竖直视角、近面 0.1、远面 10、默认距离 2，并在投影矩阵中处理 Y 方向。

要跟做，在 `src/lessons.cpp` 顶部加入 `#include "../extras/perspective_demo.hpp"`，只把 `run_camera` 中的 `make_camera_matrix(options.shift)` 换成 `make_perspective_demo_matrix()`。重新构建后仍运行 `--lesson camera --shift 0`。距离 2 时图像宽高应约为基线的 0.866 倍；在该 header 中把 distance 改为 4，再运行时宽高减半。函数本身不接收 shift，所以练习时保持 `--shift 0`，完成后恢复原调用。

这段教材跟做练习已在独立副本中完成本次编译和 Vulkan/同步验证：距离 2 的前景为 244×244，距离 4 为 122×122，质心均为 `(255.5,255.5)`。本扩展采用的 Y 约定使颜色上下排列相对基线翻转。证据是 `evidence/perspective2.png`、`perspective4.png` 与 `perspective_summary.json`。它不是新增的 CLI lesson；主工程保持原先平移基线，`run_all.py` 的 21 案例仍按基线验证。

### F. 纹理：像素上传与 descriptor 必须各自完成

```bash
./build/vulkan_lessons --lesson texture --output out/texture.ppm --validation
python3 scripts/ppm_tools.py out/texture.ppm --expect texture --png out/texture.png
```

`make_texture_pixels()` 在 CPU 生成 64×64、RGBA8 的红白棋盘，每个格子 8×8 像素。`upload_texture()` 创建 staging buffer，写入像素，把图像转换为 transfer destination，执行 `vkCmdCopyBufferToImage`，再转换为 shader read 布局并等待提交完成。

`create_bindings()` 的 binding 0 仍然是矩阵 UBO，binding 1 新增 combined image sampler。sampler 使用 nearest min/mag 与 clamp-to-edge。descriptor 中写入 `SHADER_READ_ONLY_OPTIMAL` 只是在描述使用时的布局；真正的 layout transition 在命令缓冲中完成。先上传成功再绑定资源是两项不同的工作。[1][5]

### G. mipmap：查看真正生成的低分辨率图像

```bash
./build/vulkan_lessons --lesson mips --lod 0 --output out/mips0.ppm --validation
./build/vulkan_lessons --lesson mips --lod 6 --output out/mips6.ppm --validation
python3 scripts/ppm_tools.py out/mips6.ppm --expect mips6 --png out/mips6.png
```

源图仍是同一份棋盘，但 Image 创建了 7 级：64、32、16、8、4、2、1。程序先检查 linear blit 所需格式能力，再用 `vkCmdBlitImage` 一次次生成后续级别。输出日志的 `Generated mip N` 表示本次确实录制了对应 blit。

初始 layout transition 的目标阶段使用 `ALL_TRANSFER`，覆盖 base level 的 COPY 和后续级别的 BLIT。每轮将上一级从 `TRANSFER_DST` 转成 `TRANSFER_SRC`，生成下一级后将不再参与 blit 的上一级转到 shader read；最后单独转换末级。view 覆盖全部 7 级，sampler 的 `maxLod=6`。[1][6]

`--lod` 通过 8 字节 push constant 结构传入 fragment shader，`textureLod()` 显式选择级别。sampler 的 mipmap mode 为 nearest，因此非整数 LOD 按最近一级选择，不做级间插值。

本图的格子与 2 倍缩小网格对齐，所以 LOD 0～3 仍能显示同样的红白格；LOD 4～6 已平均成淡红色。**每一级尺寸不同，并不要求每一级放大显示后都看起来不同。** 最终 1×1 级应接近 RGB `(235,145,140)`；这不是给每级手写不同颜色的替代实现。

### H. 数组层：选择内容，而非选择分辨率

```bash
./build/vulkan_lessons --lesson layers --layer 0 --output out/layer0.ppm --validation
./build/vulkan_lessons --lesson layers --layer 1 --output out/layer1.ppm --validation
./build/vulkan_lessons --lesson layers --layer 2 --output out/layer2.ppm --validation
python3 scripts/ppm_tools.py out/layer2.ppm --expect layer2 --png out/layer2.png
```

这次创建 `arrayLayers=3,mipLevels=1` 的 Image，并生成 `VK_IMAGE_VIEW_TYPE_2D_ARRAY`。三层数据按层连续写入 staging buffer；同一 copy region 的 `layerCount=3` 上传三层。shader 使用 `sampler2DArray`，UV 后增加一个 layer 坐标。

layer 0 是红白棋盘，layer 1 是绿/深绿横条，layer 2 是蓝/黄竖条。更换 `--layer` 只改变采样层，所有层的尺寸相同。与 mip 的“同一内容、不同分辨率”分别观察。[5]

### I. 深度：用绘制顺序相反的证据验证遮挡

```bash
./build/vulkan_lessons --lesson depth --output out/depth.ppm --validation
./build/vulkan_lessons --lesson depth --no-depth --output out/depth_off.ppm --validation
python3 scripts/ppm_tools.py out/depth.ppm --expect depth --png out/depth.png
python3 scripts/ppm_tools.py out/depth_off.ppm --expect depth-off --png out/depth_off.png
```

`run_depth()` 先画 z=0.2 的红色近三角形，再画 z=0.7 的蓝色远三角形。深度清为 1，compare op 使用 `LESS`，通过测试时写入深度。图像中心同时落在两者内，所以开启深度时为红色；关闭深度后，后画的蓝色覆盖它。

代码选择支持 depth attachment 的 D32_SFLOAT，或回退到 D16_UNORM。布局使用 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`，aspect 使用 DEPTH；不额外要求 separateDepthStencilLayouts feature。附件 store op 为 DONT_CARE，因为这一课在渲染结束后不再读取深度数据。

### J. 多帧：先正确复用，再允许两槽在途

```bash
./build/vulkan_lessons --lesson frames --frames 9 --inflight 1 --output out/frames1.ppm --validation
./build/vulkan_lessons --lesson frames --frames 9 --inflight 2 --output out/frames2.ppm --validation
```

默认 9 帧的场景平移从 −0.45 到 +0.45；`--shift` 可加一个固定偏移。输出包括：

| `--output out/frames2.ppm` 对应文件 | 内容 |
|---|---|
| `out/frames2_000.ppm` | 第一帧，场景靠左 |
| `out/frames2_004.ppm` | 中间帧，场景居中 |
| `out/frames2_008.ppm` | 最后一帧，场景靠右 |
| `out/frames2.ppm` | 同一最后帧，方便统一处理 |

其他帧数保存 `0`、`N/2`（整数除法）、`N−1`；重复帧号只保存一次。N=1 时仅有一帧，平移为 `--shift`。

`FrameSlot` 包含独立的 color/readback、UBO、command buffer、fence 和待保存帧号；descriptor set 按同样的 slot 索引分配。静态 vertex buffer 与 pipeline 可以由两槽共用。每次复用一个槽时：

1. 等待**该槽**的上次 fence。
2. 如上次帧号是首/中/末，读取该槽已经完成的结果。
3. 写入该槽 UBO，必要时 flush。
4. reset 并重新录制该槽 command buffer，保留真实图像布局状态。
5. 在确定要提交时 reset fence，调用 `vkQueueSubmit2`。
6. 直接继续下一个槽；循环结束后 drain 所有未完成槽。

这一顺序确保不会重置仍 pending 的 command buffer，或覆盖 GPU 仍在读取的 UBO。`--inflight 2` 允许 CPU 继续准备另一个槽，最多挂起两份提交；它不承诺同一 graphics queue 上的 GPU 工作一定并行，也不把软件实现中的计时当作硬件性能结论。[1][7]

## 4. 源文件地图：需要找哪一步

| 文件 | 可搜索的函数或结构 | 负责的具体操作 |
|---|---|---|
| `src/main.cpp` | `main`、`print_help` | 参数解析、课程选择、报告与退出码 |
| `src/context.hpp/.cpp` | `Context`、`create_context` | Instance、debug messenger、设备、queue、command pool、单次 fence |
| `src/context.cpp` | `begin_commands`、`submit_and_wait` | reset/begin；end/submit/fence wait |
| `src/resources.hpp/.cpp` | `Buffer`、`Image` | 所有权清单与销毁；不自动猜测同步 |
| `src/resources.cpp` | `create_buffer`、`write_buffer` | memoryTypeBits 与属性筛选、独立分配、绑定、map/flush |
| `src/resources.cpp` | `create_image`、`transition_image` | Image/memory/view；明确传入范围和阶段的 barrier |
| `src/resources.cpp` | `make_texture_pixels`、`upload_texture` | CPU 像素、staging copy、逐级 blit、上传等待 |
| `src/render.hpp/.cpp` | `Vertex`、`RenderTarget` | 顶点内存布局与离屏目标 |
| `src/render.cpp` | `create_bindings` | descriptor layout/pool/set、sampler 和写入 |
| `src/render.cpp` | `create_graphics_pipeline` | SPIR-V、pipeline layout、vertex input、raster/depth/blend、dynamic rendering 格式 |
| `src/render.cpp` | `begin_rendering` | 布局转换、附件清屏、viewport/scissor |
| `src/render.cpp` | `end_rendering_and_copy` | 颜色写→copy读、图像到buffer、copy写→host读 |
| `src/render.cpp` | `readback_ppm` | 等待之后 map/invalidate、RGBA去alpha、写P6 |
| `src/math.hpp` | `CameraUniform`、`make_camera_matrix` | 明确 `std140` 布局的最小矩阵实验 |
| `src/lessons.hpp/.cpp` | `run_*`、`FrameSlot` | 每课按顺序展开的操作、多帧slot复用 |
| `extras/perspective_demo.hpp` | `make_perspective_demo_matrix` | 第8章透视跟做练习，主工程默认不调用 |
| `shaders/` | 8 个 `.vert/.frag` | 每课实际 shader，构建时生成 SPIR-V |
| `scripts/ppm_tools.py` | `read_ppm`、`assert_scene`、`write_png` | P6解析、像素检查、PNG转换 |
| `scripts/run_all.py` | `main` | 顺序运行全部对照，保存真实日志和总结 |

## 5. 三个必须分开的保证

**命令已录制，不等于设备已执行。** `vkCmd...` 将工作记录到 command buffer；`vkQueueSubmit2` 才提交。`begin_commands` 的前置条件是该 command buffer 的前一份提交已完成，不能靠“录完后再等”补救提前 reset 的错误。

**fence 完成，不等于可以忽略数据访问关系。** 颜色附件写与后续图像 copy 之间有 barrier，copy 对 readback buffer 的写入与 host read 之间也有 barrier。stage 指相关工作的执行阶段，access 指实际访问类型；颜色附件写属于 COLOR_ATTACHMENT_OUTPUT，深度写属于 EARLY/LATE_FRAGMENT_TESTS。[1]

**coherent，不等于可以同时随意访问。** Buffer 每份独占 allocation、offset=0 绑定，并用 `VkMemoryDedicatedAllocateInfo` 明确指定资源。CPU 映射整块 allocation；非 coherent 写入使用 offset=0、`VK_WHOLE_SIZE` flush，回读在 fence 完成后使用同样范围 invalidate。范围延伸到 allocation 末尾，避免偏移/尾部对 nonCoherentAtomSize 的错误处理；它仍不允许 CPU 与 GPU 无同步地覆盖同一数据。[8][9]

`CompletionGuard` 放在本轮资源创建之后、首次提交之前。正常返回或异常退出时，它先等待整次离屏工作，再让局部资源析构。这是本实验的收尾保护，不在每帧调用，也不是窗口 present 资源回收的通用证明。

## 6. 一条命令跑完全部里程碑

构建完成后：

```bash
python3 scripts/run_all.py --exe build/vulkan_lessons --out out/all
```

Windows：

```powershell
python .\scripts\run_all.py --exe .\build\vulkan_lessons.exe --out out/all
```

默认给每个 Vulkan 命令加 `--validation`，运行 **21 个案例**：probe、clear、triangle、vertex、两种 camera 平移、texture、depth 开/关、7 个 LOD、3 个 array layer、1/2 槽 frames。若同目录存在 CPU 参考可执行文件，还检查它与 clear 完全一致。

除了单图检查，脚本验证四组因果对照：vertex 与 triangle 相同；camera 位移接近数学预测；texture 与 mip 0 / layer 0 相同但与最终 mip 不同；双槽与单槽的相同帧逐字节相同且位置依次右移。每次保存独立 `.stdout.txt`、`.stderr.txt`、PPM、PNG 和 `summary.json`。失败会返回非零，记录已经完成的案例。

如果当前确实没有验证层，可用 `--no-validation` 跑像素实验，报告会标明没有请求验证。正式验证应准备验证层后使用默认命令。修改过原场景后，固定像素检查可能失败；应先确认修改是否有意，再调整对应期望。

## 7. 常见故障：先看最早的证据

| 现象 | 先定位什么 |
|---|---|
| CMake 找不到 Vulkan | SDK 环境、headers 和 loader 链接库；可显式给 `Vulkan_INCLUDE_DIR` / `Vulkan_LIBRARY` |
| 找不到 GLSL 编译器 | `glslc` / `glslangValidator` 的真实路径；用 `LAB_GLSLC` / `LAB_GLSLANG` 指定 |
| 运行时找不到设备 | Vulkan 实现/驱动是否可用；`probe` 或 `vulkaninfo --summary` 能否枚举 |
| no graphics device supports… | 设备 API 与两个 1.3 feature；不能用安装新 headers 绕过运行时能力 |
| `--validation requires...` | 安装/配置对应 layer 与 extension；程序不会静默假装已验证 |
| 找不到 SPIR-V | 是否完成构建；移动可执行文件后用 `--shader-dir` 指向构建生成的 shaders 目录 |
| 有 VUID 或同步警告 | 读取 `.stderr.txt` 的第一条完整消息，追踪对象、stage/access、layout 与生命周期 |
| 图片全是背景 | 先跑 clear，确认输出路径；再检查 draw、pipeline、viewport/scissor、顶点位置与shader |
| camera 向错误方向移动 | 查看 `m[12]` 与列主序；本实验正值移动场景向右，不是相机世界位置 |
| mip 最终仍是棋盘 | mip实际blit、view的levelCount、sampler maxLod、shader textureLod、push constant数值 |
| 改 layer 仍是同一内容 | arrayLayers/viewType/range、copy层数、sampler2DArray第三坐标 |
| 多帧偶尔错位或验证报pending | 是否先等本槽fence再写UBO/reset命令；是否错误地共用可写目标 |
| PPM 无法双击打开 | 用脚本转 PNG；PPM 是中间交换格式，不影响渲染是否完成 |

`--help` 提供完整参数。`--lod` 要求 0～6，`--layer` 要求 0～2，`--frames` 要求 1～10000，`--inflight` 只能是 1 或 2；无效参数明确报错并返回非零。`--no-depth` 只适用于 depth。

## 8. 本次真实验证与边界

验证日期为 **2026-10-19（UTC）**；全部证据由本版源码重新构建并运行产生。`evidence/summary.json`、对应 stdout/stderr、PNG/PPM、`tool_versions.txt` 与 `source_sha256.json` 保留结果及来源。旧版只清屏/三角形的历史记录没有被当成本次验证。

| 项目 | 本次记录 |
|---|---|
| 系统 | Ubuntu 24.04.3 LTS，x86-64 |
| C++ / 构建 | GCC 13.3.0，C++17；CMake 3.28.3；最终构建无编译警告 |
| Headers / loader | Vulkan 1.3.275 |
| Shader 编译 | glslangValidator 15.1.0，Vulkan 1.3 / SPIR-V 1.6，8 个 shader |
| 实际执行实现 | Mesa 25.2.8-0ubuntu0.24.04.4，llvmpipe / Lavapipe，LLVM 20.1.2，CPU 软件实现，设备 API 1.4.318 |
| 验证层 | Khronos 1.3.275，代码显式开启同步验证 |
| 21 个 Vulkan 案例 | 全部退出 0；每个 stderr 均为 0 字节；每个报告 errors=0、warnings=0 |
| triangle / vertex | RGB 逐字节一致；各 68658 个像素不同于背景 |
| camera 平移 | `--shift 0.35` 的前景质心实际右移 89.5 像素 |
| mip / layer 对照 | texture=mip0=layer0；最终 mip RGB `(235,145,140)` |
| depth 对照 | 中心像素开深度为红，关闭为蓝 |
| frames 对照 | 单槽与双槽的 0/4/8 帧 RGB 一致；X质心 140.5→255.5→370.5 |
| CPU 对照 | CPU参考与Vulkan clear 的RGB一致 |
| 额外透视练习 | 独立副本2次运行通过；distance2/4宽高244/122，均居中 |

本次未在 Windows、物理 GPU、非 coherent 内存实现、macOS/MoltenVK 或窗口呈现环境执行。软件驱动给本例的 host-visible allocation 提供 coherent 内存，因此非 coherent flush/invalidate 分支虽然明确实现，尚无该类设备的运行覆盖。zero validation messages 与像素检查支持本次环境中的结果，不能证明所有实现、所有修改都没有问题。

## 9. 窗口章节的边界

本工程提供上述全部离屏课程，没有提供已完成或已验证的 GLFW/swapchain target。教材中的窗口章节是下一步设计拓展。转成窗口程序时，要新增 Surface、present 能力查询、swapchain、acquire/present、resize 与 WSI 资源回收规则；普通 graphics submission fence 或 queue/device idle 不应被不加条件地解释成 present 等待 semaphore 已可销毁。

若采用 swapchain maintenance1 的 present fences 路线，必须查询并启用相应扩展和 feature，按规范等待 fence 后回收相应呈现资源。该 fence 的保证也不应描述为“像素已经显示在屏幕上”。这些能力不属于本次离屏实验的实测结论。[10]

## 官方参考

1. [Khronos：Synchronization Examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
2. [VkPhysicalDeviceVulkan13Features](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html)
3. [CMake：FindVulkan](https://cmake.org/cmake/help/latest/module/FindVulkan.html)
4. [Khronos：Shader Memory Layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)
5. [VkImageViewCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)
6. [vkCmdBlitImage](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html)
7. [vkResetCommandBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkResetCommandBuffer.html)
8. [VkMemoryDedicatedAllocateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryDedicatedAllocateInfo.html)
9. [VkMappedMemoryRange](https://docs.vulkan.org/refpages/latest/refpages/source/VkMappedMemoryRange.html)
10. [VkSwapchainPresentFenceInfoKHR](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)
