# 项目说明文档

本文档基于当前仓库源码整理，目标是回答两个问题：

1. 这个项目现在实际做到了什么。
2. 各个模块分别承担什么职责，项目是怎么跑起来的。

这份说明以代码实现为准，比 `README.md` 更详细。

## 1. 项目定位

这是一个基于 `C++17 + Qt 6 Widgets` 实现的桌面图形工具项目，核心不是调用 GPU API，而是在 CPU 上自己实现了一套软件光栅化渲染器，然后用 Qt 做窗口、面板、交互和文件工作流。

从当前代码看，它已经不是单纯“画一个三角形”的练习，而是一个包含下面两条能力线的小型图形实验平台：

- 一条是 `3D 软件渲染器 / 场景编辑器`
- 另一条是 `照片转线稿` 的图像处理工具

也就是说，这个项目同时具备：

- 可交互的 3D 视口
- 纯 CPU 光栅化渲染核心
- 模型、材质、灯光、相机、后处理、导出、性能调优
- 照片线稿生成、透明线稿导出、批量处理

## 2. 项目整体结构

当前工程的主要文件和职责如下：

- `main.cpp`
  - Qt 程序入口。
  - 负责启动 `QApplication` 并创建主窗口。

- `MainWindow.*`
  - 整个桌面应用的 UI 外壳。
  - 负责菜单栏、状态栏、Dock 面板、按钮、下拉框、参数输入、撤销重做。
  - 自身不做渲染计算，主要把用户操作转发给 `RasterWidget`。

- `RasterWidget.*`
  - Qt 和渲染核心之间的桥接层。
  - 负责视口显示、鼠标键盘交互、相机控制、gizmo、场景状态管理、导出、线稿模式、场景序列化。
  - 它是整个项目最核心的“应用层控制器”。

- `SoftwareRenderer.*`
  - 纯 C++ 的软件光栅化核心。
  - 负责顶点变换、裁剪、三角形光栅化、深度测试、材质着色、光照、阴影、MSAA、透明混合、调试视图、多线程 tile 光栅化。

- `RasterMath.h`
  - 最小数学层。
  - 提供向量、矩阵、变换、投影、`lookAt`、法线变换等基础能力。

- `tests/tst_renderer.cpp`
  - Qt Test 编写的渲染回归测试。
  - 当前有 `48` 个测试函数，覆盖渲染正确性、调试视图、材质、纹理、OBJ、MSAA、并行一致性等关键行为。

- `tests/renderer_benchmark.cpp`
  - 独立 benchmark 程序。
  - 用于比较单线程和多线程渲染性能，以及 tile 参数的影响。

## 3. 这个项目现在能做什么

从用户视角看，项目现在已经支持下面这些功能。

### 3.1 3D 视口与渲染预览

- 打开窗口后可以实时看到 CPU 渲染出来的 framebuffer。
- 视口支持常规实体显示，也支持线框和多种调试视图。
- 场景静止时不会无意义持续刷新，交互或状态变化时才触发重渲染。

#### 3.1.1 它不是怎么“显示”的，而是怎么“拼起来”的

这个项目的 3D 视口不是 `QOpenGLWidget`，也不是 GPU swapchain，而是下面这条链路：

`MainWindow`
-> `RasterWidget`
-> `SoftwareRenderer`
-> CPU 颜色缓冲
-> `QImage`
-> `QPainter`
-> Qt 窗口

也就是说，真正的图像先在 CPU 内存里算出来，再交给 Qt 显示。

#### 3.1.2 MainWindow 负责把视口放进主窗口

主窗口层做的事情很直接：

- `MainWindow::createUi()` 创建一个 `RasterWidget`
- 把它设为 `setCentralWidget(m_rasterWidget)`
- 菜单、Dock 面板、状态栏都围绕这个中心视口工作
- `frameReady` 信号会把每帧统计信息抛回主窗口，用来刷新底部状态栏

所以从结构上说：

- `MainWindow` 负责“界面壳子”
- `RasterWidget` 负责“视口本体”

#### 3.1.3 RasterWidget 是视口的总调度器

`RasterWidget` 不是单纯显示一张图，而是同时管理：

- 当前场景对象
- 当前相机
- 当前灯光
- 当前渲染状态
- 当前导出和后处理状态
- 视口输入事件
- gizmo 与叠加层绘制
- 触发渲染和缓存渲染结果

构造函数里做了几件关键的事：

- 设置 `Qt::WA_OpaquePaintEvent`
  - 表示控件会完全自己绘制，避免 Qt 先做一层背景清空
- `setAutoFillBackground(false)`
  - 减少无意义背景填充
- 连接 `QTimer::timeout -> renderFrame()`
  - 用于连续渲染场景
- 初始化默认场景
  - `resetSceneToDemoCube()`
- 把默认抗锯齿设成 `4x MSAA`
- 最后调用 `requestRender()`
  - 请求首帧渲染

#### 3.1.4 什么时候会触发重渲染

这个项目不是固定每 16ms 死循环渲染，而是“按需渲染 + 必要时连续渲染”。

核心入口是 `RasterWidget::requestRender()`：

- 先把 `m_sceneDirty` 标成 `true`
- 如果控件不可见，直接返回
- 如果是线稿模式，只做 `update()`
- 如果当前已经排队过一次刷新，就不重复排队
- 否则通过 `QTimer::singleShot(0, ...)` 合并到当前事件循环末尾执行一次 `renderFrame()`

这意味着：

- 多个 UI 控件连续改值时，不会每改一次就立刻重渲一遍
- 同一事件循环内的多次 `requestRender()` 会被合并

#### 3.1.5 为什么它静止时不空转

`RasterWidget::updateRenderLoopState()` 和 `needsContinuousRendering()` 决定是否真的开启 16ms 定时器。

只有在这些情况下才会连续渲染：

- 自由飞行按键按住
- 鼠标轨道旋转 / 平移 / free-look
- gizmo 正在拖拽
- 场景里有持续动画对象

否则：

- 定时器会停掉
- 视口只在状态变化时重绘一次

这套机制很适合 CPU 渲染器，因为 CPU 出图成本比普通 Qt 控件高得多，静止时不空转能明显减少资源浪费。

#### 3.1.6 renderFrame 做了什么

真正的一帧刷新从 `RasterWidget::renderFrame()` 进入，流程很短：

1. 如果当前是线稿模式，直接返回
2. 如果控件不可见，直接返回
3. 调用 `renderSceneNow(false)`
4. 如果这次没有必要重渲，就返回
5. 如果成功重渲，发出 `frameReady(m_renderer.stats())`
6. 调 `update()` 让 Qt 触发一次 `paintEvent`

所以 `renderFrame()` 只负责组织一次“算图 + 通知 UI + 请求绘制”，真正的渲染细节在 `renderSceneNow()` 和 `SoftwareRenderer` 里。

#### 3.1.7 renderSceneNow 如何把场景喂给渲染器

`RasterWidget::renderSceneNow(bool force)` 是视口层和渲染核心之间最关键的桥。

它大致做了下面几步：

1. 先判断当前是否需要真的重渲
   - 线稿模式下不走 3D 渲染
   - 尺寸为 0 时不渲染
   - 如果既不脏、也不在连续渲染状态、也不是 `force`，就直接跳过

2. 保证 framebuffer 尺寸和控件一致
   - 如果 `m_renderer.width()/height()` 和 widget 尺寸不同，就调用 `m_renderer.resize()`

3. 计算时间增量
   - 用 `QElapsedTimer` 算出和上一帧的 `deltaSeconds`
   - 用于自由飞行相机更新

4. 组装一个高层 `Scene`
   - 相机来自当前 `m_renderer.camera()`
   - 清屏色写入 `scene.clearColor`
   - 场景光照写入 `scene.lighting`

5. 把应用层对象转换成渲染层对象
   - 遍历 `m_sceneObjects`
   - 解析每个对象当前真正生效的材质
     - 可能来自对象实例材质
     - 也可能来自共享材质资产
   - 调用 `resolveMaterialBinding()` 得到最终 `Material`
   - 组装 `RenderItem { mesh, material, transform }`

6. 调 `m_renderer.renderScene(scene)`
   - 这一步才真正进入软件光栅化核心

7. 把输出缓存成 `m_scenePresentationImage`
   - 通过 `buildRendererPresentationImage(true)` 读取 renderer 的颜色缓冲并转换成最终可显示图像

所以 `renderSceneNow()` 本质上是在做一层“应用状态 -> 渲染输入”的编排。

#### 3.1.8 SoftwareRenderer 如何真正出图

`SoftwareRenderer::renderScene(scene)` 会把 `Scene` 包成一个 `RenderPass`，然后交给 `renderPass()`。

`renderPass()` 这一步已经包含了一帧渲染的大部分关键逻辑：

1. 清理上一轮脏像素并重置统计
2. 按需清颜色缓冲和深度缓冲
3. 写入当前相机
4. 计算 `view` 和 `projection`
5. 用 pass 内的 `RenderState` 暂时覆盖默认状态
6. 准备当前帧的 `LightingContext`
7. 为需要投影阴影的灯构建 shadow map
   - 方向光
   - 点光源
   - 聚光灯
8. 把场景对象排序
   - 不透明对象先画
   - 透明对象按相机距离 `back-to-front` 排序后再画
9. 遍历每个 `RenderItem`
   - 生成 `model/view/projection` 上下文
   - 进入网格绘制

继续往下展开，单个对象会进入：

- `drawMeshWithContext()`
- `drawTriangle()`
- `clipTriangleAgainstNearPlane()`
- `projectVertexOutput()`
- `rasterizeTriangle()` 或 `rasterizeWireframeTriangle()`
- `shadeFragment()`

也就是说，这里已经覆盖了一条完整的软件渲染管线：

- 顶点变换
- 近平面裁剪
- 屏幕投影
- 背面剔除
- 三角形遍历
- 重心插值
- 深度测试
- 材质着色
- 光照与阴影
- 透明混合
- MSAA resolve

#### 3.1.9 framebuffer 是如何变成 Qt 图像的

CPU 渲染器并不是直接画到窗口上，而是先写入内部缓冲区：

- `m_colorBuffer`
- `m_depthBuffer`
- `m_sampleColorBuffer`
- `m_sampleDepthBuffer`

`RasterWidget::buildRendererPresentationImage()` 会做这件事：

1. 调 `m_renderer.colorBufferData()`
   - 这里会先确保 `resolveDirtyPixels()` 把脏的 MSAA 样本统一 resolve 成最终像素颜色
2. 把返回的 `ARGB32` 指针包装成 `QImage`
3. 立刻 `copy()`
   - 避免直接引用 renderer 内部缓冲导致生命周期问题
4. 如果当前允许后处理，再执行：
   - exposure
   - tone mapping
   - contrast
   - saturation
   - gamma

最后得到的是一张标准 Qt `QImage`。

#### 3.1.10 paintEvent 如何把图像显示出来

显示阶段非常直接：

- `paintEvent()` 里调用 `buildViewportPresentationImage(false)`
- 然后用 `QPainter painter(this); painter.drawImage(rect(), image);`

如果当前是 3D 模式，`paintEvent()` 在底图之上还会额外叠加：

- 对象 gizmo
- 灯光 gizmo
- 相机状态和快捷键 overlay

也就是说，视口最终显示内容其实分成两层：

1. 底层是 `SoftwareRenderer` 输出的 CPU framebuffer
2. 上层是 Qt 直接画的调试/交互叠加层

这种分层方式的好处是：

- 3D 主图由统一软件渲染流程负责
- gizmo 和 UI 叠加不需要再走一遍三角形光栅化
- 交互实现会简单很多

#### 3.1.11 窗口尺寸变化时怎么处理

`resizeEvent()` 里会直接调用：

- `m_renderer.resize(std::max(1, width()), std::max(1, height()))`

这会重新分配：

- 颜色缓冲
- 深度缓冲
- MSAA 样本颜色缓冲
- MSAA 样本深度缓冲
- overdraw 缓冲
- dirty flag

然后再 `requestRender()` 出一帧新的匹配尺寸图像。

这种做法很直接，代价是 resize 时会整块重建 framebuffer，但逻辑足够清晰，适合当前项目规模。

#### 3.1.12 这一整套方案的核心特点

总结一下，当前 3D 视口的实现有几个很明显的设计特点：

- 它是 `CPU 渲染 + Qt 显示`，不是 GPU 管线
- `RasterWidget` 既是视口控件，也是场景调度器
- 渲染和显示分离
  - `SoftwareRenderer` 负责算图
  - `QPainter` 负责贴图和叠加层
- 刷新策略不是傻瓜式固定帧循环，而是“按需 + 连续交互时实时刷新”
- framebuffer 和窗口同尺寸，省掉额外缩放链路
- 导出、后处理、调试视图都复用同一套底层图像生成路径

### 3.2 相机控制

- 左键轨道旋转
- 中键平移
- 滚轮缩放
- 右键 free-look 看向
- `W/A/S/D/Q/E` 自由飞行
- `Shift` 飞行加速
- 前 / 后 / 左 / 右 / 上 / 下六个轴向视图切换
- 透视 / 正交投影切换
- 可调参数包括：
  - `FOV`
  - 正交高度
  - near / far
  - 自由飞行速度
- 支持相机预设保存与加载

#### 3.2.1 相机系统的职责边界

这个项目里的相机控制不是分散在很多地方临时拼出来的，而是有比较明确的分层：

- `MainWindow`
  - 负责相机面板、菜单命令和按钮
  - 不直接修改数学状态，只把用户输入转交给 `RasterWidget`

- `RasterWidget`
  - 负责相机交互逻辑
  - 负责把鼠标、键盘输入转换成相机变换
  - 负责保存和恢复相机参数
  - 负责把相机变化同步给 UI

- `SoftwareRenderer`
  - 只保存当前生效的 `Camera`
  - 在渲染时使用相机生成 `viewMatrix()` 和 `projectionMatrix()`

也就是说：

- 相机“怎么动”在 `RasterWidget`
- 相机“怎么参与渲染”在 `SoftwareRenderer`

#### 3.2.2 相机数据本身长什么样

渲染核心里的 `Camera` 结构包含这些核心字段：

- `position`
- `target`
- `up`
- `verticalFovRadians`
- `nearPlane`
- `farPlane`
- `projectionMode`
  - `Perspective`
  - `Orthographic`
- `orthographicHeight`

这个设计是典型的“观察点 + 注视点”相机，而不是直接存欧拉角。

好处是：

- 轨道旋转很好做
- 平移时可以同步移动 `position` 和 `target`
- free-look 时只需要重算朝向，再用固定距离更新 `target`
- 视图矩阵可以直接通过 `lookAt` 生成

#### 3.2.3 基础方向向量是怎么推出来的

`RasterWidget.cpp` 顶部定义了相机辅助函数：

- `cameraForward(camera)`
- `cameraRight(camera)`
- `cameraOrthoUp(camera)`

这三个方向不是额外存的，而是根据：

- `target - position`
- `up`
- 向量叉积

实时算出来的。

这很重要，因为后面的所有交互都依赖这组三维基向量：

- 轨道旋转围绕 `target`
- 平移沿 `right / up`
- 自由飞行沿 `forward / right / up`
- 顶部 overlay 也会显示当前朝向

#### 3.2.4 MainWindow 层是如何接线的

主窗口给相机系统做了完整的 UI 接线，主要包括：

- 投影模式下拉框
- FOV 输入框
- 正交高度输入框
- near / far 输入框
- 移动速度输入框
- 保存预设按钮
- 加载预设按钮
- 重置按钮
- 六个轴向视图按钮

这些控件最终分别连接到：

- `changeCameraProjectionMode()`
- `changeCameraVerticalFov()`
- `changeCameraOrthographicHeight()`
- `changeCameraNearPlane()`
- `changeCameraFarPlane()`
- `changeCameraMoveSpeed()`
- `resetCameraView()`
- `setCameraFrontView()` / `Back` / `Left` / `Right` / `Top` / `Bottom`

这些槽函数本身很薄，基本都只是：

- 调 `m_rasterWidget` 的对应接口
- 再同步场景预设控件状态

所以相机面板的交互逻辑没有被塞进 `MainWindow`，这一点结构上是干净的。

#### 3.2.5 鼠标轨道旋转是怎么做的

左键空白区域拖动时，`RasterWidget::mousePressEvent()` 会进入轨道旋转模式：

- `m_isOrbiting = true`
- 记录 `m_lastMousePosition`

鼠标移动时进入 `mouseMoveEvent()`，如果处于轨道模式，就会调用：

- `orbitCamera(camera, yawDeltaRadians, pitchDeltaRadians)`

`orbitCamera()` 的逻辑是：

1. 先取当前 `position - target` 得到相机相对目标点的偏移
2. 把这个偏移转换成球坐标意义下的：
   - yaw
   - pitch
   - radius
3. 把鼠标位移换成新的 yaw / pitch
4. 对 pitch 做夹紧，防止翻到极点附近数值不稳定
5. 保持半径不变，只改变观察角度
6. 用新的球坐标反算出新的 `camera.position`
7. `camera.up` 重置成世界上方向 `{0,1,0}`

这说明轨道旋转不是简单绕本地轴累乘矩阵，而是“围绕 target 的球面相机”。

这种实现的特点是：

- 目标点稳定
- 镜头距离稳定
- 很适合观察单个模型

#### 3.2.6 中键平移为什么在正交和透视下表现不同

中键拖动会调用 `panCamera(camera, deltaPixelsX, deltaPixelsY, viewportHeight)`。

关键在于它先计算 `pixelToWorld`，把“屏幕像素位移”转换成“世界空间位移”：

- 正交模式下：
  - 直接根据 `orthographicHeight / viewportHeight` 换算
- 透视模式下：
  - 根据相机到目标点距离
  - 以及 `tan(fov / 2)`
  - 估算当前屏幕像素对应的世界空间尺度

然后平移量由下面两部分组成：

- `cameraRight(camera)` 方向
- `cameraOrthoUp(camera)` 方向

最后同时移动：

- `camera.position`
- `camera.target`

所以平移不会改变观察方向，只是整个观察参考系一起横移。

#### 3.2.7 滚轮缩放在透视和正交下不是同一种操作

滚轮调用的是 `dollyCamera(camera, wheelSteps)`。

这里分了两种情况：

- 正交相机
  - 不改位置
  - 直接缩放 `orthographicHeight`
  - 本质上是改变正交视口大小

- 透视相机
  - 保持 `target` 不变
  - 改变 `position` 到 `target` 的距离
  - 本质上是相机沿视线方向前后 dolly

这也是为什么同样是滚轮，在两种投影模式下用户感知不同：

- 透视更像“推进/拉远”
- 正交更像“缩放视野”

#### 3.2.8 右键 free-look 和轨道旋转的区别

右键拖动会进入 `m_isFreeLooking` 模式，并在 `mouseMoveEvent()` 中调用：

- `freeLookCamera(camera, yawDeltaRadians, pitchDeltaRadians)`

它和 `orbitCamera()` 的最大区别是：

- 轨道旋转是绕 `target` 转，`position` 跟着绕圈
- free-look 是固定 `position`，只旋转观察方向

具体实现上：

1. 先取当前 `forward`
2. 根据 `forward` 反推出当前 yaw / pitch
3. 加上鼠标带来的增量
4. 算出新的 `nextForward`
5. 保持 `camera.position` 不变
6. 用一个固定焦距 `focusDistance` 更新 `camera.target = position + nextForward * focusDistance`

所以 free-look 更像第一人称镜头，而轨道旋转更像建模软件里的物体观察相机。

#### 3.2.9 自由飞行是怎么更新的

键盘按下时，`keyPressEvent()` 只做一件事：更新布尔状态。

例如：

- `W` -> `m_moveForward = true`
- `S` -> `m_moveBackward = true`
- `A` / `D`
- `Q` / `E`
- `Shift` -> `m_moveFast = true`

真正移动相机不是在按键事件里做，而是在每帧的：

- `updateFreeFlyCamera(deltaSeconds)`

这里会根据当前布尔状态组装三轴移动意图：

- 前后
- 左右
- 上下

再按照：

- `cameraForward(camera)`
- `cameraRight(camera)`
- `cameraOrthoUp(camera)`

合成最终位移向量。

速度公式大致是：

- 基础速度 = `m_cameraMoveSpeed`
- 如果按住 `Shift`，乘以 `3.0`
- 再乘 `deltaSeconds`

最后同时平移：

- `camera.position`
- `camera.target`

这套做法的优点是很稳定：

- 和帧率解耦
- 方向由相机朝向决定
- 不需要单独维护复杂的速度积分状态

#### 3.2.10 为什么自由飞行需要连续渲染

自由飞行不是靠单次 `requestRender()` 实现的，而是靠持续刷新。

`needsContinuousRendering()` 在这些按键状态为真时会返回 true：

- `m_moveForward`
- `m_moveBackward`
- `m_moveLeft`
- `m_moveRight`
- `m_moveUp`
- `m_moveDown`

于是 `updateRenderLoopState()` 会启动 16ms 定时器，持续调用 `renderFrame()`。

每一帧里：

- 先算 `deltaSeconds`
- 再跑 `updateFreeFlyCamera(deltaSeconds)`
- 再触发场景重渲

所以自由飞行本质上是“按键驱动的连续帧更新相机”。

#### 3.2.11 参数面板修改后发生了什么

相机参数面板最终调用的是一组显式 setter：

- `setCameraProjectionMode()`
- `setCameraVerticalFovDegrees()`
- `setCameraOrthographicHeight()`
- `setCameraNearPlane()`
- `setCameraFarPlane()`
- `setCameraMoveSpeed()`

这些 setter 的共同特点是：

- 先做输入合法性约束
- 如果值没变，直接返回
- 如果值变化：
  - 更新 `Camera` 或速度字段
  - 把场景预设标记成 `Custom`
  - 发出 `cameraChanged()`
  - 请求重渲

几个典型的约束策略如下：

- FOV 被限制在 `10° ~ 140°`
- `orthographicHeight >= 0.1`
- `nearPlane` 必须小于 `farPlane`
- `farPlane` 至少比 `nearPlane` 大 `0.01`
- `moveSpeed >= 0.05`

这说明相机面板不是“原样写值”，而是带保护的状态写入。

#### 3.2.12 透视 / 正交切换是如何接到渲染里的

投影模式的切换本身很简单：

- `Camera` 里有 `projectionMode`
- `Camera::projectionMatrix(aspect)` 会根据它输出不同投影矩阵

于是当 `RasterWidget::setCameraProjectionMode()` 改完 `camera.projectionMode` 后：

- `m_renderer.setCamera(camera)`
- 下次渲染时 `SoftwareRenderer::renderPass()` 会重新取：
  - `view = camera.viewMatrix()`
  - `projection = camera.projectionMatrix(aspect)`

换句话说，UI 层只负责改相机状态，真正生效是在下一帧重新组装 VP 矩阵的时候。

#### 3.2.13 六个轴向视图怎么实现

轴向视图按钮最终都走：

- `setCameraAxisView(CameraAxisView view)`

内部调用 `makeAxisViewCamera(sourceCamera, view)`。

它的策略不是硬编码一个固定相机，而是：

1. 先保留当前 `target`
2. 保留当前相机到目标点的距离 `distance`
3. 再把 `position` 放到目标点的：
   - 前
   - 后
   - 左
   - 右
   - 上
   - 下
4. 为顶视图和底视图单独修正 `up`

这样做的好处是：

- 只改变观察方向
- 不会突然贴近或远离模型
- 更符合 DCC / 编辑器里“切轴视图”的直觉

#### 3.2.14 重置相机为什么和场景预设有关

`resetCameraView()` 的实现不是永远回到同一个镜头，而是和当前 `ScenePreset` 关联。

逻辑是：

- 如果当前预设是 `Custom`
  - 回到 `DefaultOrbit`
- 如果当前是某个非自定义场景预设
  - 回到该预设对应的默认镜头

底层靠的是 `makeResetCamera(preset)`，不同预设对应不同：

- 位置
- 目标点
- FOV

这意味着重置相机不只是“归零”，而是“回到当前观察场景最合理的默认镜头”。

#### 3.2.15 相机预设保存了什么

相机预设最终由 `cameraPresetObject()` 生成 JSON，对外保存这些字段：

- `position`
- `target`
- `up`
- `verticalFovRadians`
- `nearPlane`
- `farPlane`
- `projectionMode`
- `orthographicHeight`
- `moveSpeed`

所以它保存的不只是姿态，还有投影和移动速度。

这比只存一个位置或 FOV 更完整，恢复后能尽量还原当时的观察状态。

#### 3.2.16 相机预设怎么恢复

加载相机预设时会走：

- `loadCameraPreset()`
- `applyCameraPresetObject()`

恢复逻辑包括：

- 读取位置、目标点、up
- 读取 FOV、near、far
- 读取投影模式
- 读取正交高度
- 读取移动速度
- 再做一次合法性约束

恢复成功后：

- 相机会立即写回 `m_renderer`
- 场景预设标记为 `Custom`
- 发出 `cameraChanged()`
- 请求重渲

也就是说，相机预设的恢复是即时生效的，不需要重新打开场景。

#### 3.2.17 cameraChanged 信号的作用

相机系统里一个很重要的桥接信号是：

- `cameraChanged()`

它主要用来通知外层 UI：

- 同步场景预设下拉框
- 同步相机参数面板
- 更新按钮和输入框状态

这让相机状态和 UI 显示能保持一致。

例如用户直接在视口里拖鼠标转相机时：

- 并没有经过右侧面板
- 但面板中的 FOV、投影模式、near / far、按钮可用性仍然能及时反映当前状态

#### 3.2.18 这套相机系统的核心特点

总结一下，这个项目的相机控制有几个很明显的实现特征：

- 用 `position + target + up` 表达相机，而不是强耦合欧拉角
- 同时支持两类交互风格
  - 围绕目标观察的轨道相机
  - 固定位置转头的 free-look / 自由飞行
- 透视和正交模式共用同一套交互外壳，但在平移和缩放换算上做了区分
- 鼠标驱动的交互主要按事件触发重渲
- 键盘驱动的自由飞行通过连续帧更新实现
- 相机参数、相机预设、场景预设和 UI 同步是打通的

### 3.3 场景与对象编辑

- 场景中可以有多个对象
- 支持加载 `OBJ` 模型
- 支持追加示例立方体
- 支持对象选择、重命名、删除
- 支持对象 `平移 / 旋转 / 缩放`
- 既可以通过数值面板编辑，也可以直接在视口里通过 gizmo 拖拽
- gizmo 支持：
  - 世界坐标 / 本地坐标切换
  - 平移吸附
  - 旋转吸附
  - 缩放吸附
  - `Ctrl` 细调
- 已实现撤销 / 重做
- 对连续拖拽和连续输入做了历史合并，避免把一次操作拆成很多条历史记录

#### 3.3.1 场景对象是怎么存的

场景对象不是只存一个模型路径，而是直接存成一组完整对象记录：

- `mesh`
- `transform`
- `materialInstance`
- 是否引用共享材质
- 共享材质 id
- 源文件路径
- 源 OBJ 文本
- 显示名称
- 是否是示例立方体

也就是说，当前场景里每个对象都是一个“可独立编辑的实体”，不是简单的模型引用列表。

#### 3.3.2 对象列表怎么工作

`RasterWidget` 内部维护 `m_sceneObjects`，对象列表的基本操作包括：

- 新增示例立方体
- 删除当前选中对象
- 重命名
- 切换当前选中对象
- 读取 / 修改当前对象变换

这些接口会在修改后统一：

- 把场景预设标成 `Custom`
- 发出 `sceneContentChanged()`
- 需要时调用 `requestRender()`

主窗口中的对象下拉框、名称编辑框、变换输入框都通过这个信号链同步。

#### 3.3.3 OBJ 模型是怎么进场景的

项目支持把 `OBJ` 作为场景对象载入。

加载后会做几件事：

- 读取模型文本并解析成 `Mesh`
- 根据网格包围盒自动算一个初始 `Transform`
- 把对象放到视野里合适的位置
- 生成显示名称

如果对象没有单独的源文本，也可以从磁盘路径再读回来。

这意味着场景保存时不只是存“我载入了哪个文件”，也尽量保留了对象恢复所需的完整信息。

#### 3.3.4 示例立方体是怎么工作的

项目启动后默认会有一个示例立方体场景。

它的作用不是“占位”，而是提供一个开箱即用的观察对象，方便直接测试：

- 相机
- 材质
- 灯光
- gizmo
- 阴影
- 调试视图

你也可以随时：

- `addDemoCubeObject()` 继续追加立方体
- `useDemoCube()` 把场景恢复成内置立方体

#### 3.3.5 对象选择是怎么做的

对象选择分两层：

- 左侧对象列表里的选择
- 视口里 gizmo / 物体拾取后的选择

对象下标保存在 `m_selectedSceneObjectIndex`。

`setSelectedSceneObjectIndex()` 会：

- 夹紧下标范围
- 更新当前选中对象
- 发出 `sceneContentChanged()`
- 触发一次 UI 刷新

这样主窗口里的对象名、变换参数、材质面板都会跟着同步。

#### 3.3.6 对象重命名怎么工作

`renameSelectedSceneObject()` 只改当前选中对象的 `displayName`。

它会：

- 去掉空白
- 如果名字没变化就直接返回
- 否则更新对象名并发出 `sceneContentChanged()`

主窗口会把对象列表里的显示文本同步成新的名称。

#### 3.3.7 对象变换是怎么编辑的

对象变换统一使用 `Transform`：

- `position`
- `rotationRadians`
- `scale`

面板里显示的是角度，但内部保存的是弧度，所以 UI 和内核之间会做一次转换。

`setSelectedSceneObjectTransform()` 会把新变换写回当前对象，并触发重渲。

这也是 gizmo 拖拽、数值输入和保存场景都共享的同一份变换数据。

#### 3.3.8 gizmo 为什么要支持世界空间和本地空间

`gizmoSpaceMode` 决定当前操作轴是：

- 世界坐标轴
- 对象本地坐标轴

也就是说，同样是拖 X 轴：

- 世界模式下沿全局 X 走
- 本地模式下沿对象当前朝向后的 X 轴走

这对于旋转过的对象尤其重要，因为本地轴和世界轴不再一致。

#### 3.3.9 吸附是怎么做的

项目提供了三种吸附参数：

- 平移吸附步长
- 旋转吸附角度
- 缩放吸附步长

这些参数由面板设置后写入 `RasterWidget`，拖拽时会在最终值上做 `snapToStep()`。

同时还有一个 `Ctrl` 细调开关：

- `Ctrl` 会降低拖拽灵敏度
- `Shift` 会启用吸附

这让编辑器同时支持：

- 精调
- 粗调
- 吸附对齐

#### 3.3.10 视口里的 gizmo 是怎么画出来的

`drawSceneObjectGizmos(QPainter &painter)` 会在最终 framebuffer 之上叠加对象操纵器。

它会为每个可见对象绘制：

- 中心圆点
- 平移轴线
- 缩放方块
- 旋转环
- 文字标签

当前选中的对象会高亮，属性面板当前聚焦的轴也会同步高亮。

这就实现了“面板输入”和“视口拖拽”之间的双向可视反馈。

#### 3.3.11 gizmo 命中是怎么判断的

视口里不是靠碰撞体，而是靠屏幕空间拾取。

`pickViewportHandle()` 的思路是：

1. 先把对象位置投影到屏幕上
2. 再把轴向端点、旋转环、灯光方向球等关键点投到屏幕上
3. 根据鼠标和这些屏幕点 / 线段的距离判断是否命中

对象 gizmo 主要支持：

- 平移
- 旋转
- 缩放

光源 gizmo 则会根据类型显示不同的可拖拽部件。

#### 3.3.12 拖拽状态是怎么开始和结束的

当鼠标命中某个 gizmo 时，会调用：

- `beginViewportHandleDrag()`

这里会记录：

- 拖拽类型
- 当前轴向
- 当前索引
- 鼠标起点
- 起始世界位置
- 起始旋转
- 起始缩放
- 起始视线深度

拖拽结束时会走：

- `endViewportHandleDrag()`

然后发出 `gizmoInteractionChanged(false, ...)`，让外层 UI 结束当前历史事务。

#### 3.3.13 平移 / 旋转 / 缩放在拖拽时怎么计算

`updateViewportHandleDrag()` 根据拖拽类型分别处理：

- 平移
  - 把鼠标位移转换成世界空间偏移
  - 可按轴向拖动，也可自由平面拖动

- 旋转
  - 用鼠标绕屏幕中心的角度差计算旋转量
  - 再按 X / Y / Z 轴写回对象旋转

- 缩放
  - 根据鼠标沿轴方向的投影位移推导缩放因子
  - 再按轴写回对象缩放

灯光拖拽也走同一套机制，只是对象类型不同：

- 点光源拖位置
- 聚光灯拖位置和方向
- 方向光拖方向球面

#### 3.3.14 视口叠加层和属性面板为什么能互相高亮

当属性面板的某个字段获得焦点，或者 gizmo 开始拖动时，项目会通过：

- `gizmoInteractionChanged(...)`
- `setInspectorGizmoHighlight(...)`

把当前操作的 handle 类型、操作类型、轴向同步给 UI。

结果是：

- 面板里正在编辑的轴会高亮
- 视口里对应的 gizmo 也会高亮

这让“数值编辑”和“视口拖拽”看起来像同一套系统，而不是两套互不相关的入口。

#### 3.3.15 对象 gizmo、灯光 gizmo 和相机 overlay 是怎么联动的

这几层其实是共享同一份交互状态，不是各画各的。

`RasterWidget` 负责维护：

- 当前选中的对象
- 当前选中的灯光
- 当前正在拖拽的 handle
- 当前相机状态
- 当前键盘按键状态
- 当前 inspector 高亮状态

联动顺序大致是：

1. 鼠标点中对象或灯光 gizmo
   - `pickViewportHandle()`
   - `setSelectedSceneObjectIndex()` 或 `setSelectedLightSelection()`
   - `sceneContentChanged()` 通知外层面板同步

2. 开始拖拽 gizmo
   - `beginViewportHandleDrag()`
   - 记录拖拽类型、轴向、起点和初始变换
   - 发出 `gizmoInteractionChanged(true, ...)`
   - `MainWindow` 进入历史事务并同步高亮样式

3. 拖拽过程中实时改数据
   - `updateViewportHandleDrag()`
   - 直接写回对象变换或灯光参数
   - 再触发 `sceneContentChanged()` 和 `requestRender()`

4. 属性面板获得焦点
   - `MainWindow::updateInspectorHighlightFromFocus()`
   - 调 `setInspectorGizmoHighlight()`
   - 视口里的对应 gizmo 轴同步高亮

5. 相机或快捷键状态变化
   - 轨道、平移、free-look、WASD、滚轮都会更新 `m_renderer.camera()`
   - 同时刷新连续渲染状态
   - `drawCameraOverlay()` 直接读当前相机和按键状态，画出左下角相机状态和右下角快捷键提示

所以这几个 overlay 的本质是：

- 对象 gizmo 和灯光 gizmo 反映当前选中与拖拽状态
- 相机状态 overlay 反映当前 `Camera`
- 快捷键 overlay 反映当前可用操作说明

它们都在 `paintEvent()` 里叠到最终 framebuffer 上，因此会和 3D 主图一起刷新，但不需要重新走一遍软件光栅化。

#### 3.3.16 场景预设是怎么参与对象编辑的

项目里的 `ScenePreset` 不是只管相机，它还会顺带把场景内容切到一组可观察的状态：

- 默认环绕
- 纹理观察
- 光照观察
- 线框检查
- UV 检查
- 过绘制检查

`applyScenePreset()` 会统一调整：

- 相机
- 渲染状态
- 灯光
- 对象材质
- demo 纹理预设

所以当你切换场景预设时，不只是换镜头，而是换一整套观察上下文。

#### 3.3.17 场景为什么一改对象就变成 Custom

只要用户手动改了对象、变换、gizmo 参数或载入了新内容，项目通常都会把 `m_scenePreset` 改成 `Custom`。

这是刻意设计的，因为：

- 预设代表“某个固定观察状态”
- 用户一旦手动改动，就不再严格属于原预设

这样 UI 上的场景预设下拉框就不会继续显示成“默认状态”，避免误导。

#### 3.3.18 撤销 / 重做是怎么做的

这个项目的撤销重做不是“记录单个操作对象”，而是直接做场景快照。

`MainWindow` 会通过：

- `currentSceneHistorySnapshot()`

把整个场景状态序列化成紧凑 JSON。

历史事务的流程大致是：

1. `beginHistoryTransaction()`
   - 记下修改前快照
2. 进行对象 / 场景修改
3. `commitHistoryTransaction()`
   - 如果修改后快照不同，就压入 undo 栈
   - 清空 redo 栈

对连续输入则会走：

- `beginMergedHistoryTransaction()`

它会把短时间内的多次小改动合并成一条历史记录。

#### 3.3.19 运行时恢复历史时怎么避免递归记录

`restoreHistorySnapshot()` 会在恢复时把 `m_restoringHistory` 设为 true。

这样做是为了避免：

- 恢复历史时又触发新的历史记录
- 导致 undo / redo 互相污染

恢复成功后会：

- 重新同步所有 UI
- 更新当前快照
- 刷新高亮状态

#### 3.3.20 场景保存和编辑状态是如何统一的

场景保存不是另存一个简版快照，而是直接把当前场景状态完整写出：

- 对象
- 变换
- 材质
- 灯光
- 相机
- gizmo
- 渲染状态
- 并行参数
- 后处理
- 线稿配置

所以“编辑器里看到的状态”与“保存到磁盘里的状态”是同一套数据，不存在两个不一致的状态系统。

#### 3.3.21 这套对象编辑系统的核心特点

总结一下，这部分的实现重点是：

- 场景对象是完整实体，而不是单一 mesh 引用
- 视口里的选择、拖拽、属性面板编辑都围绕同一份对象状态
- gizmo 通过屏幕空间拾取和投影换算实现，不依赖复杂碰撞系统
- 变换编辑支持世界 / 本地轴、吸附和细调
- 场景预设、手动编辑和撤销快照是打通的
- 撤销 / 重做直接基于完整场景 JSON 快照，简单但可靠

### 3.4 材质与纹理

这一章已经不是“随便给个颜色和贴图”那么简单了，而是把材质状态、纹理资源和采样策略拆成了一套完整链路。

#### 3.4.1 材质状态是怎么组织的

真正提交给渲染器的是 `Material`，但界面里编辑的通常是 `MaterialBinding`。前者收拢一次 draw call 需要的着色状态，后者负责把“材质类型 + 贴图引用 + 采样参数 + demo 预设”拼成可编辑的数据。

- `Material` 里保存了：
  - `MaterialType`
  - `MaterialSurfaceMode`
  - `opacity`
  - `depthWriteEnable`
  - 基础颜色 / 法线 / metallic-roughness 三套纹理和采样器
  - `specularColor`、`specularStrength`、`shininess`
  - `normalStrength`
  - `metallic`、`roughness`
  - `receiveShadow`
- `MaterialBinding` 额外保存：
  - `texturePreset`
  - `textureAssetId`
  - `normalTextureAssetId`
  - `metallicRoughnessTextureAssetId`

对象本身不会直接把资产塞给 renderer，而是先通过 `resolveMaterialBinding()` 解析成真正的 `Material`，再交给渲染器。

#### 3.4.2 材质类型怎么切换

材质类型不是单一实现，而是 8 个组合：

- `LambertTextured`
- `LambertVertexColor`
- `UnlitTextured`
- `UnlitVertexColor`
- `BlinnPhongTextured`
- `BlinnPhongVertexColor`
- `PbrTextured`
- `PbrVertexColor`

`applyMaterialType()` 的关键点是“重建类型，但不丢参数”：它会先用工厂函数创建一个默认材质，再把已有的纹理、采样器和数值参数拷回去，所以切类型不会把用户刚调过的贴图、粗糙度或高光参数洗掉。

#### 3.4.3 纹理采样链路是怎么走的

`Texture2D` 本身不只是一个 texel 数组，它还能重建 mip 链并按采样器状态取样。

- `rebuildMipChain()` 会从 base level 逐级向下做 2x2 平均，生成完整 mip chain。
- `sampleColor()` 会根据 `TextureFilter` 选择：
  - `Nearest`
  - `Bilinear`
  - `Trilinear`
- `AddressMode` 决定 UV 越界后是 `Wrap` 还是 `Clamp`。
- 渲染时 `fragment.textureLod` 会先由屏幕空间 UV 变化估算出来，再传给基础颜色、法线和 metallic-roughness 三路采样。

这意味着它不是简单“贴图取色”，而是已经有了一个最小可用的纹理过滤链。

#### 3.4.4 法线图和 metallic-roughness 是怎么参与着色的

默认片元着色器里，基础颜色、法线和金属粗糙度不是分开乱算的，而是先拼成 `SurfaceShadingData` 再进入光照。

- 基础颜色纹理会调制 `fragment.color`
- 法线贴图会在切线空间解码后，用 `normalStrength` 混回几何法线
- metallic-roughness 贴图会分别影响金属度和粗糙度
- `receiveShadow` 会决定这个材质是否吃阴影

所以 Lambert、Blinn-Phong 和 PBR 不是三套互相孤立的代码，而是共享同一条贴图和法线输入链路，只在光照模型上分叉。

#### 3.4.5 这个层和 UI 是怎么接上的

`MainWindow` 里的材质面板并不是直接改 renderer，而是改当前选中对象或材质资产，再由 `sceneContentChanged()` 和 `requestRender()` 推回视口。

- 载入外部纹理会自动注册成纹理资产
- 绑定纹理时会把材质类型切到对应的 textured 变体
- `opacity < 1` 时会自动把表面模式切到 `AlphaBlend`
- `depthWriteEnable` 只在透明材质下保留成可调项

这套设计的好处是 UI 改的是“编辑态”，渲染器拿到的是“可直接执行态”。

### 3.5 材质资产与纹理资产

这里已经不是“对象自带一份材质”了，而是有了一个轻量的场景资源系统。

#### 3.5.1 资产在内存里长什么样

- `TextureAssetEntry` 保存：
  - `id`
  - `displayName`
  - `sourcePath`
  - `relativePath`
  - `Texture2D texture`
- `MaterialAssetEntry` 保存：
  - `id`
  - `displayName`
  - `sourcePath`
  - `MaterialBinding binding`

对象的 `SceneObjectEntry` 里同时保留了 `materialInstance` 和 `materialAssetId`，因此既能走共享资产，也能退回独立实例。

#### 3.5.2 共享资产和独立实例怎么切换

当前选中对象的材质并不总是直接编辑对象本身。

- 如果对象引用共享资产，`selectedEditableMaterialBinding()` 会返回资产绑定
- 如果对象没有引用资产，就返回对象自己的 `materialInstance`
- `makeSelectedObjectUseMaterialInstance()` 会把共享资产“解引用”回独立材质
- `duplicateSelectedMaterialAsAsset()` 会把当前可编辑材质复制成新资产，再绑定回对象

这样既能做共享复用，也能随时把某个对象拆出来单独改。

#### 3.5.3 资产列表的增删改是怎么保持一致的

项目里专门补了几套“不会把引用弄断”的操作：

- 材质资产可复制、重命名、删除、清理未使用项
- 纹理资产可复制、重命名、删除、清理未使用项
- 删除纹理资产时，会自动清掉所有材质绑定里对应的纹理槽
- 删除材质资产时，所有引用它的对象会先固化成独立材质，再移除资产

这种做法比“直接删掉然后让外面自己崩”稳得多，也更适合做编辑器。

#### 3.5.4 文件级材质资产是怎么导入导出的

`saveSelectedMaterialAssetToFile()` 会把当前材质连同引用到的纹理一起写到一个独立文件里，并把纹理复制到同目录资源结构中。

- 材质文件里会写 `materialAsset`
- 纹理会写进 `textureAssets`
- 资源目录默认按 `resources/textures` 组织
- `loadMaterialAssetFromFile()` 既能读新格式，也兼容旧的 `binding` 结构
- 导入时会把纹理资产重编号，再把材质里的 texture id 全部重映射

这就把“场景内资产”和“单材质文件”打通了。

### 3.6 灯光与阴影

灯光这部分已经从“只有一盏主光”升级成了完整的场景级光照上下文。

#### 3.6.1 光照上下文是怎么组织的

`LightingContext` 同时保存三类灯：

- `DirectionalLight`
- `PointLight`
- `SpotLight`

默认状态由 `LightingContext::makeDefault()` 提供，至少会带一盏主方向光，避免场景里没有光时全黑。

#### 3.6.2 三类灯光分别怎么编辑

`RasterWidget` 和 `MainWindow` 都把灯光当成可选择、可拖拽、可重命名的场景对象来处理。

- 方向光支持方向、环境项、强度、启用、阴影参数
- 点光源支持位置、颜色、强度、范围、阴影参数
- 聚光灯支持位置、方向、颜色、强度、范围、内外锥角、阴影参数
- 灯光可以新增、复制、删除
- 灯光类型可以互转，保留尽可能多的公共字段

视口里也不是纯表单编辑，点光和聚光还能直接在 3D 视口里拖拽操控。

#### 3.6.3 阴影是怎么做出来的

阴影不是后期贴图，而是每帧单独生成的 shadow map。

- 方向光和聚光灯使用二维 shadow map
- 点光源使用六向立方体 shadow map
- `castShadow` 决定某盏灯是否参与阴影
- `shadowStrength`、`shadowBias`、`shadowMapSize`、`shadowRange` / `shadowCoverage`、`shadowFilterQuality` 都可调

渲染时，`LightingContext` 会把阴影缓存一起带进来，片元阶段再按灯型去采样。

#### 3.6.4 阴影采样和缓存怎么联动

方向光有一层缓存签名，静态场景下相机动了但几何没变时，可以复用旧 shadow map，避免重复重建。

- `buildShadowMapCacheSignature()` 会把灯参数和场景几何一起哈希
- 只要材质挂了自定义 vertex shader，这层缓存就会失效
- 点光和聚光每帧按需重建，不走同样的缓存分支
- `receiveShadow` 会控制材质是否真正接收阴影

所以阴影部分不是“演示级开关”，而是已经进入渲染主链路了。

### 3.7 调试视图

调试视图不是单纯换个颜色，而是把片元阶段改写成一组可视化检查模式。

#### 3.7.1 现在有哪些调试视图

`DebugView` 已经覆盖了渲染管线里最常查问题的几类观察方式：

- `None`
- `Depth`
- `Normal`
- `UV`
- `Overdraw`
- `ObjectId`
- `MaterialId`
- `TriangleId`
- `FaceOrientation`
- `Barycentric`
- `Shadow`
- `Lighting`

这些模式基本能把“几何、插值、遮挡、阴影、着色”都拆开看。

#### 3.7.2 调试视图是在哪里生效的

调试模式不是 UI 层随便画颜色，而是在 `SoftwareRenderer::defaultFragmentShaderImpl()` 里直接分支处理。

- `ObjectId`、`MaterialId`、`TriangleId` 会把 id 映射成可区分颜色
- `FaceOrientation` 用前后面两种颜色区分背面
- `Barycentric` 直接显示重心坐标
- `Shadow` 会输出当前主阴影因子
- `Lighting` 会输出 Lambert / Blinn-Phong / PBR 的最终光照强度

这就保证调试输出和真实渲染共享同一套输入，不会出现“调试图和实际结果不一致”。

#### 3.7.3 批量导出调试图是怎么做的

`exportDebugViews()` 会把所有调试模式按固定顺序轮一遍，逐个渲成 PNG。

- 文件名由 `debugViewFileName()` 统一命名
- 导出前会临时改 `RenderState.debugView`
- 每张图都走一次完整 `renderSceneNow(true)`
- 导出完会把原始 render state 恢复回来并刷新视口

这对排查光照、深度、UV、过绘制问题很实用。

### 3.8 后处理与导出

视口输出不是渲完就结束，还会再经过一层很轻量的 CPU 后处理。

#### 3.8.1 后处理是怎么叠上去的

`buildRendererPresentationImage()` 会先把 renderer 的 color buffer 拷成 `QImage`，然后按开关决定要不要走 `applyPostProcessToImage()`。

`applyPostProcessToImage()` 的处理顺序是：

- exposure
- tone mapping
- contrast / saturation
- clamp
- gamma

它是一个纯 CPU 的逐像素 pass，目标很直接，就是让最终显示更适合屏幕。

#### 3.8.2 tone mapping 和参数控制了什么

目前可选的 tone mapping 有：

- `None`
- `Reinhard`
- `AcesApprox`

配套参数包括：

- `exposure`
- `gamma`
- `contrast`
- `saturation`

另外，如果当前是调试视图，`shouldApplyPostProcessToCurrentView()` 会直接跳过后处理，避免把调试结果再二次加工。

#### 3.8.3 截图和环绕序列是怎么导出的

- `saveViewportScreenshot()` 会导出当前视口图像
- `exportOrbitSequence()` 会在保持原相机的前提下，按帧绕目标旋转相机并逐帧保存
- 导出结束后会恢复原相机、发 `cameraChanged()`，再请求一次重新渲染

这套导出走的是同一条视口生成链路，所以导出的图和窗口里看到的结果是一致的。

### 3.9 并行光栅化与性能调优

这部分是这套软件光栅器里很重要的一层：它不是只把三角形画出来，而是把 CPU 吞吐也当成一等公民来处理。

#### 3.9.1 tile-based 光栅化是怎么切分的

渲染时会先把当前包围盒拆成固定尺寸 tile，再按 tile 去执行任务。

- `buildTilesForBounds()` 负责切 tile
- 每个 tile 记录自己的像素范围
- 三角形和线段的光栅化路径都可以复用同一套 tile 调度

这样做的好处是局部性更好，也方便把互不重叠的 tile 分发到不同线程。

#### 3.9.2 线程池和任务分发是怎么工作的

`SoftwareRenderer` 维护的是固定工作线程池，不是每帧临时起线程。

- `startWorkerThreads()` 会按请求数启动 worker
- 默认 `-1` 表示自动，通常会预留 1 个逻辑核给主线程
- `executeTileTasks()` 会根据 tile 数和估计像素数判断是否值得并行
- `tilesPerTask` 决定每个并行任务最多吃多少个 tile

如果场景太小，系统会自动退回串行，避免调度开销比收益还大。

#### 3.9.3 性能统计为什么能直接看出来

渲染器每帧都会更新 `ParallelRasterStats`，UI 里直接显示：

- 请求线程数
- 实际线程数
- tile 数
- task 数
- 并行批次 / 串行批次
- 跳过并行分发次数
- tile 构建、任务分发、等待完成的耗时

这让性能调参不再只是“感觉更快了”，而是可以直接看到调度代价。

### 3.10 线稿图像处理

除了 3D 视口，这个项目还内置了一条独立的线稿处理工作流。

#### 3.10.1 线稿模式是怎么进入的

`loadPhotoLineArt()` 会读取一张图片，把它作为线稿源图，并切进 `m_isLineArtMode`。

- 读取成功后会立刻触发 `regenerateLineArt()`
- 如果重算失败，会回退并退出线稿模式
- 线稿模式下，3D 视口编辑和导出会被禁用

也就是说，线稿不是“附带显示一张图”，而是另一套明确的工作模式。

#### 3.10.2 线稿图是怎么生成的

`makeLineArtImages()` 的处理链路比较完整：

- 先把输入图转成 ARGB32
- 按 `processScale` 缩放到处理分辨率
- 转灰度并做 3x3 blur
- 用 Sobel 风格的梯度算边缘强度
- `Thin` 模式会再做一次非极大值抑制
- 用阈值曲线和 `lineStrength` 算最终线条强度
- 输出两张图：
  - 实心白底线稿图
  - 透明背景线稿图

其中 `keepGrayBase`、`thresholdScale`、`transparentStrokeWidth`、`edgeMode` 都会直接影响最终效果。

#### 3.10.3 对比、保存和批量导出是怎么做的

线稿视图支持原图和结果图并排对比，分割线还能拖动。

- `lineArtComparePreview` 控制是否启用对比视图
- `m_lineArtCompareSplit` 记录分割位置
- `saveLineArtImage()` 和 `saveTransparentLineArtImage()` 分别保存两种结果
- `batchExportLineArt()` 会对一批图片按同一套参数批量生成线稿
- `saveLineArtConfig()` / `loadLineArtConfig()` 会把线稿参数和源图路径一起保存下来

这让线稿功能从“单次处理工具”变成了可复用的工作流。

## 4. 渲染器核心做了什么

如果从技术实现角度总结，`SoftwareRenderer` 已经是一条完整的 CPU 软光栅管线。它做的不是“画一个三角形”这么简单，而是把一帧图像拆成了数据准备、顶点变换、裁剪、光栅化、片元着色、深度/混合、MSAA resolve 和并行 tile 调度几层。

### 4.1 渲染入口

一帧真正开始的地方不是三角形，而是 `RasterWidget` 和 `SoftwareRenderer` 的联动。

- UI 或视口交互先把场景标记成 dirty
- `renderSceneNow()` 组装 `Scene`
- `SoftwareRenderer::renderScene(scene)` 再把 `Scene` 包成 `RenderPass`
- `renderPass()` 负责这一帧的整体执行

这一步通常会先做：

- 清理上一帧遗留的脏像素和统计信息
- 把场景相机、灯光、渲染状态挂到当前 pass
- 先构建阴影，再绘制实体几何
- 最后恢复 pass 前的临时状态

也就是说，这个项目的渲染不是“直接画”，而是先把一帧的上下文准备好，再按管线顺序执行。

更具体一点，调用顺序是：

`RasterWidget::renderSceneNow()` -> `SoftwareRenderer::renderScene()` -> `SoftwareRenderer::renderPass()` -> `drawMeshWithContext()` -> `drawTriangle()` -> `rasterizeTriangle()` -> `rasterizeTriangleTile()` -> `shadeAndWritePixelDeferred()` -> `resolvePixelColor()`

### 4.2 数据抽象

渲染核心先把概念拆开，再把这些概念串起来：

- `VertexInput`
- `VertexOutput`
- `ScreenVertex`
- `FragmentInput`
- `FragmentOutput`
- `Material`
- `Mesh`
- `Transform`
- `RenderItem`
- `Scene`
- `RenderState`
- `RenderPass`

这些结构分别对应“输入顶点、顶点着色输出、屏幕空间顶点、片元输入/输出、材质、网格、物体变换、场景、绘制状态和一次完整渲染提交”。这意味着渲染器不是把所有逻辑塞进一个函数，而是按图形管线分层组织。

更重要的是，`RenderPass` 让一帧渲染的高层输入和执行状态绑定在一起：场景数据、相机、灯光、渲染状态都从这里进入，`renderPass()` 再统一处理清理、阴影、排序和最终绘制。

从职责上看：

- `Scene` 是“这一帧有什么”
- `RenderPass` 是“这一帧怎么画”
- `RenderState` 是“这一帧按什么规则画”
- `LightingContext` 是“这一帧有哪些灯和阴影缓存”

### 4.3 顶点阶段

顶点阶段做的是把模型空间的数据变成后续 rasterizer 能吃的空间。

源码里的主链路是：

- `drawMesh()` / `drawRenderItems()` 组装 `VertexShaderContext`
- `runVertexShader()` 调用内建或自定义顶点着色器
- `defaultVertexShaderImpl()` 把顶点从模型空间变到世界空间和裁剪空间

这一层本质上就是标准的 `M * V * P` 流程：

- `modelTransform` 把模型放到世界里
- `viewMatrix()` 把世界转到相机空间
- `projectionMatrix()` 把相机空间投到裁剪空间

同时它还会把后续片元阶段要用的信息一起带出去：

- 世界坐标
- 法线
- UV
- 顶点色
- 观察方向
- 切线 / 副切线
- `textureLod`

所以顶点阶段不只是“改坐标”，而是在为插值和着色准备完整上下文。

源码里的关键点是 `VertexShaderContext`：

- `modelTransform` 负责模型到世界
- `viewTransform` 负责世界到相机
- `projectionTransform` 负责相机到裁剪空间
- `cameraWorldPosition` 负责算观察方向和高光项

默认顶点着色器会把这些数据包装成 `VertexOutput`，后面裁剪、投影和插值都围绕它展开。

### 4.4 三角形处理

几何阶段负责把顶点阶段的结果变成真正能被光栅化的图元。

它的处理顺序大致是：

1. 先走索引三角形提交，`Mesh::indices` 每 3 个索引组成一个三角形。
1. 再调用 `runVertexShader()` 得到三个 `VertexOutput`。
1. 用 `clipTriangleAgainstNearPlane()` 做近平面裁剪，避免相机前方被切开的三角形直接炸掉。
1. 裁剪后的多边形再拆成三角扇，重新回到三角形列表。
1. 在屏幕空间用有符号面积判断背面，并按 `CullMode` 决定是否丢弃。

这里的关键原理有两个：

- 近平面裁剪避免 near plane 之后出现负 w 或不可见三角形
- top-left rule 保证共享边不会被两个三角形重复点亮，减少裂缝和抖动

插值也不是简单线性插值，而是透视校正插值。也就是说，深度、UV、法线、世界坐标这些属性会按 `1/w` 参与修正，避免近大远小被错误拉伸。

这一段对应的实际函数链是：

- `drawTriangle()`
- `runVertexShader()`
- `clipTriangleAgainstNearPlane()`
- `projectVertexOutput()`
- `shouldCullTriangle()`
- `rasterizeTriangle()`

`clipTriangleAgainstNearPlane()` 负责裁剪，`projectVertexOutput()` 负责把裁剪空间点变成屏幕空间点，`shouldCullTriangle()` 负责在屏幕空间判断绕序。

### 4.5 光栅化与像素阶段

真正出图是在 rasterizer 里完成的。

这一层主要干五件事：

1. 把三角形包围盒切成 tile
1. 逐 tile 扫描像素
1. 逐像素或逐样本做边函数测试
1. 做深度测试和颜色混合
1. 把命中的像素记进脏列表，留给 resolve 和最终输出

源码里有两个关键分支：

- `FillMode::Solid` 走普通三角形填充
- `FillMode::Wireframe` 走线段光栅化

深度阶段也很直接：

- `depthTestPasses()` 根据 `DepthFunc` 决定比较规则
- `depthWriteEnable` 决定是否写入深度缓冲
- 透明材质会强制走 alpha 混合路径

透明对象并不是随便画，`buildOrderedRenderItems()` 会先把它们按相机距离排成 `back-to-front`，再进入混合，避免前面的半透明把后面的视觉关系打乱。

片元阶段最终由 `shadeFragment()` 统一入口控制：

- 普通材质走 `runFragmentShader()`
- 调试模式直接改写输出颜色
- 输出再交给深度/混合/样本写回

像素阶段的核心链路可以再拆一层：

- `rasterizeTriangle()` 先算包围盒和 tile
- `executeTileTasks()` 决定串行还是并行
- `rasterizeTriangleTile()` 做像素级边函数测试
- 命中后调用 `shadeAndWritePixelDeferred()`
- 如果启用 4x MSAA，再按样本写入缓冲
- 帧末 `resolvePixelColor()` 把样本合成最终像素

如果是线框模式，则 `rasterizeWireframeTriangle()` -> `rasterizeLineSegment()` -> `rasterizeLineSegmentTile()` 走的是另一条线段路径。

### 4.6 光照与阴影

默认片元着色器 `defaultFragmentShaderImpl()` 已经把一套常见实时光照模型串起来了。

它的输入先经过：

- 基础颜色纹理采样
- 法线贴图采样
- metallic/roughness 采样
- `buildSurfaceShadingData()`
- `applyNormalMap()`

然后再进入 `evaluateLightingContributions()` 做灯光累加。

当前默认着色路径支持：

- Lambert 漫反射
- Blinn-Phong 高光
- PBR 材质响应
- 顶点色与纹理色两套模式
- 法线贴图参与光照
- metallic/roughness 参与着色
- 多方向光累加
- 多点光累加
- 多聚光灯累加

它们的实现关系是：

- Lambert 主要输出 diffuse + ambient
- Blinn-Phong 在 Lambert 基础上叠加 specular
- PBR 会用 metallic / roughness 计算更接近物理的反射响应
- Unlit 则直接返回基础颜色，不走光照累加

阴影不是“把像素直接涂黑”，而是先算出一个 `shadowFactor`，再把它乘到灯光贡献上：

- `shadowFactor = 1` 表示完全受光
- `shadowFactor = 0` 表示完全被遮挡
- 中间值表示部分遮挡，通常来自 PCF 过滤后的结果
- 最终颜色仍然保留环境光和材质基础响应，所以阴影一般是“变暗”，不是纯黑

#### 4.6.1 阴影图是怎么生成的

在 `renderPass()` 里，阴影构建发生在真正绘制之前：

1. 先收集参与阴影绘制的 `shadowItems`
1. 方向光按缓存签名决定复用还是重建
1. 点光和聚光灯按需重建
1. 阴影结果挂回 `LightingContext`
1. 片元阶段再用这些 shadow map 做采样

这一步生成的不是颜色图，而是深度图：

- 方向光和聚光灯使用二维 shadow map
- 点光源使用六向 cubemap shadow map
- 阴影图里只记录“从灯的视角看，最近的表面有多远”
- `castShadow` 决定这盏灯要不要投影
- `shadowMapSize`、`shadowCoverage` / `shadowRange` 决定阴影图分辨率和覆盖范围

#### 4.6.2 片元阶段怎么判断“在不在阴影里”

真正着色时，渲染器会把当前片元的世界坐标投到灯光空间：

- `computeDirectionalShadowFactor()`
- `computePointShadowFactor()`
- `computeSpotShadowFactor()`

它们都会做同一件事：

1. 把 `worldPos` 映射到灯光视角下的采样坐标
2. 取出当前片元在灯光空间里的深度
3. 用 `shadowBias` 做一点深度偏移，减少 shadow acne
4. 在 shadow map 里取对应位置的存储深度
5. 用深度比较判断是否被遮挡
6. 用 `shadowFilterQuality` 做 PCF 平滑，减少硬边

其中 `computeShadowVisibility()` 返回的就是最终可见度：

- 比较通过越多，值越接近 `1`
- 被遮挡越多，值越接近 `0`
- `shadowStrength` 决定压暗有多重

#### 4.6.3 阴影怎么影响最终颜色

`evaluateLightingContributions()` 会把 `shadowFactor` 乘到每盏灯的漫反射和高光上：

- `diffuse *= shadowFactor`
- `specular *= shadowFactor`
- PBR 路径里同样会把辐照度按 `shadowFactor` 衰减

所以阴影的本质不是“改色”，而是“减光”。

如果材质不接收阴影，就不会吃到这个衰减：

- `receiveShadow = false` 时，片元阶段直接当作没有阴影
- `castShadow = false` 时，这盏灯不参与投影

这样就能区分：

- 这盏灯有没有投影能力
- 这个材质要不要被阴影影响
- 这块表面到底被遮住了多少

### 4.7 抗锯齿与 resolve

这里做的是真正的 `4x MSAA`，不是单纯靠 alpha 伪装。

- 每像素 4 个样本
- 每样本独立颜色缓冲
- 每样本独立深度缓冲
- 帧末做 resolve
- 线框路径也支持样本级抗锯齿

它的核心思路是：几何边缘不再只判断“这个像素中心点有没有被覆盖”，而是对一个像素里的多个采样点分别判断。这样轮廓边缘会更平滑，尤其是斜线和远处细小物体。

`resolvePixelColor()` 最后会把 4 个样本平均成一个像素颜色，这一步就是 MSAA 的收束阶段。

也就是说，一条完整像素链实际上是：

`edge test` -> `sample write` -> `depth write` -> `sample resolve` -> `pixel output`

### 4.8 并行执行模型

多线程部分不是直接给每个三角形开线程，而是 tile 级并行。

它的理由很简单：

- 三角形粒度太小，线程调度成本会爆掉
- tile 粒度能保证局部性
- 不同 tile 之间通常不会写同一批像素，天然适合并行

具体实现是：

- `buildTilesForBounds()` 把三角形包围盒切块
- `executeTileTasks()` 决定走串行还是并行
- `startWorkerThreads()` 提前拉起固定 worker
- `enqueueTileTask()` 把任务投给线程池
- `mergeTileTaskResult()` 合并局部结果和脏像素

并行是否启用不是固定死的，而是看：

- tile 数够不够多
- 估计像素数够不够大
- worker 数量是否至少为 2
- `tilesPerTask` 是否适合当前场景

所以这套设计的重点不是“能多线程”，而是“在值得并行的时候才并行”。

这套设计对于 CPU 渲染来说是比较合理的。

## 5. Qt 应用层做了什么

`MainWindow` 和 `RasterWidget` 负责把渲染核心变成一个可用工具。

### 5.1 MainWindow

主窗口现在已经不是简单“放一个 widget”，而是组织了多个 Dock 面板：

- 对象面板
- 材质面板
- 灯光面板
- 线稿面板
- 相机面板
- 后处理 / 导出面板
- 性能 / 并行面板

它还负责：

- 菜单命令
- 状态栏统计
- UI 与场景状态同步
- 历史记录事务控制
- 焦点与 gizmo 联动高亮

### 5.2 RasterWidget

`RasterWidget` 是整个应用层真正的中枢，承担了大量“非纯渲染”的工作：

- 视口绘制
- 输入事件处理
- 相机交互
- gizmo 拾取与拖拽
- 场景对象列表维护
- 灯光列表维护
- 材质/纹理资产维护
- 场景保存与读取
- 截图、序列、调试图导出
- 线稿模式切换与线稿再生成
- 后处理图像变换

可以理解为：

- `SoftwareRenderer` 负责“怎么渲”
- `RasterWidget` 负责“渲什么、何时渲、怎么和用户交互”

## 6. 场景保存与数据持久化

项目当前已经实现了比较完整的 JSON 场景存档。

保存内容包括：

- 场景版本号
- 场景预设
- 当前选中对象
- 资源目录名
- 纹理资产列表
- 材质资产列表
- 对象列表
  - 类型
  - 名称
  - 路径
  - 内嵌源文本
  - 变换
  - 材质模式
  - 材质绑定
- 相机参数
- 灯光参数
- 线稿配置
- gizmo 设置
- 渲染状态
- 并行光栅化配置
- 后处理配置

同时它还做了两件很实用的事：

- 保存场景时会导出资源到场景目录
- 读取场景时会按场景路径解析相对资源

此外还保留了部分旧格式兼容逻辑，说明作者考虑过版本迁移问题。

## 7. 自动化测试和 benchmark

这个项目的工程化程度比一般练手项目高，原因主要在两点。

### 7.1 回归测试

`tests/tst_renderer.cpp` 当前包含 `48` 个测试函数，覆盖点包括：

- 基础三角形出图
- 深度测试与不同 `DepthFunc`
- 背面剔除
- 近平面裁剪
- 线框模式
- 调试视图输出
- 自定义 shader 接口
- 纹理过滤与地址模式
- Trilinear + mip
- 4x MSAA 行为
- 法线贴图
- Lambert / Blinn-Phong / PBR
- 点光源与多灯累加
- 透明混合
- OBJ 解析
- 并行与串行输出一致性
- 并行调度统计

### 7.2 Benchmark

`renderer_benchmark` 是独立可执行程序，支持：

- 分辨率参数
- 帧数与预热帧
- 场景网格规模
- 线程数
- tile size
- 并行阈值
- tiles per task
- 单线程 / 多线程对比
- sweep 最优参数搜索

它会输出：

- 平均帧时间
- FPS
- color hash
- tile / task / worker 信息
- 构建 / 分发 / 等待耗时

这对于证明并行化改造是否真的有效很有价值。

## 8. 构建方式

项目当前使用 `CMake` 构建，依赖：

- `C++17`
- `Qt6::Core`
- `Qt6::Widgets`
- `Qt6::Test`

仓库里已经提供 `CMakePresets.json`，当前预设以本地环境 `Qt 6.11.0 + MSVC2022` 为主。

常用命令：

```bash
cmake --preset qt-msvc2022-debug
cmake --build --preset qt-msvc2022-debug
ctest --preset qt-msvc2022-debug --output-on-failure
```

生成物包括：

- `software_rasterizer`：主程序
- `tst_renderer`：测试程序
- `renderer_benchmark`：性能测试程序

## 9. 这个项目的价值可以怎么概括

如果从项目总结的角度看，这个仓库已经完成了几件比较有代表性的事情：

- 用纯 `C++` 自己实现了一条完整度较高的 CPU 软件光栅化管线
- 用 `Qt Widgets` 把渲染核心包装成了可交互的桌面工具
- 把“渲染功能”和“编辑器工作流”结合起来，而不是只做一个离线 demo
- 做了调试视图、导出、场景存档、资源管理、撤销重做、线稿处理等工具化功能
- 做了 4x MSAA、阴影、多灯光、tile 多线程和 benchmark，说明项目不只关注“能跑”，也关注正确性和性能
- 补了较完整的自动化测试，工程可信度较高

## 10. 一句话总结

这个项目当前可以概括为：

> 一个基于 `C++17 + Qt 6 Widgets` 的桌面图形实验平台，核心是自研 CPU 软件光栅化渲染器，外围包含场景编辑、材质与纹理资产、OBJ 加载、多灯光与阴影、4x MSAA、调试视图、后处理、导出、线稿图像处理，以及测试和 benchmark 工具链。
