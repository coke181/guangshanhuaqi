# Software Rasterizer

基于 `C++17 + Qt 6 Widgets` 的桌面图形实验项目。  
核心不是调用 `OpenGL / Direct3D / Vulkan`，而是在 `CPU` 上自己实现软件光栅化渲染器，再用 Qt 做视口、面板、文件工作流和编辑交互。

## 项目特点

- 纯 CPU 渲染管线：顶点变换、近平面裁剪、背面剔除、三角形光栅化、深度测试
- 材质与纹理：`Lambert`、`Blinn-Phong`、基础 `PBR` 参数、纹理过滤与地址模式
- 灯光与阴影：方向光、点光源、聚光灯，带阴影和调试视图
- 抗锯齿与透明：`4x MSAA`、透明材质、基础混合流程
- 调试能力：深度、法线、UV、Overdraw、Object ID、Material ID、Triangle ID、Barycentric、Shadow 等视图
- 编辑器能力：对象/灯光选择、gizmo 拖拽、对象材质编辑、场景保存/加载、撤销/重做
- 图像工具：照片转线稿、透明线稿导出、批量线稿导出
- 性能方向：tile 化并行光栅化、调度统计、benchmark 与基础一致性测试

## 技术栈

- `C++17`
- `Qt 6 Widgets`
- `CMake`
- 单元测试：`Qt Test`

## 构建环境

- Windows
- Visual Studio 2022
- Qt 6.10+ / 6.11
- CMake 3.21+

## 快速开始

先设置 Qt 安装目录环境变量：

```powershell
$env:QTDIR="D:\QT\6.11.0\msvc2022_64"
```

配置、编译、测试：

```powershell
cmake --preset qt-msvc2022-debug
cmake --build --preset qt-msvc2022-debug
ctest --preset qt-msvc2022-debug --output-on-failure
```

运行可执行文件：

```powershell
.\build\presets\qt-msvc2022-debug\Debug\software_rasterizer.exe
```

## VS Code 调试

仓库自带 `.vscode/launch.json` 与 `.vscode/tasks.json`。  
按 `F5` 前请先确保已经设置好 `QTDIR` 环境变量，否则 Qt 运行时 DLL 无法找到。

## 目录结构

- `SoftwareRenderer.*`
  纯 C++ 软件光栅化核心。
- `RasterMath.h`
  向量、矩阵和基础数学工具。
- `RasterWidget.*`
  Qt 与渲染核心之间的桥接层，负责场景状态、输入、gizmo、渲染调度和视口叠加。
- `MainWindow.*`
  主窗口、菜单、Dock 面板、状态栏和参数编辑逻辑。
- `tests/`
  渲染器测试与 benchmark。
- `PROJECT_OVERVIEW.md`
  更详细的项目结构说明。
- `SOFTWARE_RASTERIZER_ROADMAP.md`
  后续迭代方向与项目整理说明。

## 当前状态

这是一个持续演进中的实验项目，不追求 GPU API 兼容，而是聚焦：

- 完整软件渲染主链路
- 交互式视口与编辑器工作流
- 可验证的性能优化与调试能力
- 更接近真实图形工具/小型引擎编辑器的工程组织

## License

当前仓库未附带开源许可证。  
如果你准备公开发布，建议补一个明确的 `LICENSE` 文件。
