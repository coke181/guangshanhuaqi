#ifndef SOFTWARE_RENDERER_H
#define SOFTWARE_RENDERER_H

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <array>
#include <string>
#include <thread>
#include <vector>
#include "RasterMath.h"

// VertexInput 是外部提交给渲染器的原始顶点数据。
// 这一层还处在物体空间或模型空间。
struct VertexInput {
    Vec3f position;//位置
    Vec3f color;//颜色
    Vec3f normal;//法线方向
    Vec2f uv;//纹理坐标
};

// VertexOutput 是顶点阶段的输出。
// 当前保存裁剪空间位置以及后续需要继续插值的 varying。
struct VertexOutput {
    Vec4f clipPosition;//裁剪空间坐标
    Vec3f color;//颜色
    Vec3f normal;//法线方向
    Vec2f uv;//纹理坐标
    Vec3f worldPos;//世界空间位置
    Vec3f viewDir;//指向相机的观察方向
};

// ScreenVertex 是光栅化阶段的输入。
// 这一层已经完成透视除法和视口映射，但仍保留 varying 供片元阶段插值。
struct ScreenVertex {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 1.0f;//深度
    float inverseW = 1.0f;//透视校正因子
    Vec3f color;//颜色
    Vec3f normal;//法线方向
    Vec2f uv;//纹理坐标
    Vec3f worldPos;//世界空间位置
    Vec3f viewDir;//指向相机的观察方向
};

// 顶点阶段执行时可用的上下文。
struct VertexShaderContext {//顶点着色器上下文
    Mat4f transform;//最终送去裁剪空间的变换
    Mat4f modelTransform;//局部到世界空间的变换
    Mat4f viewTransform;//世界到观察空间的变换
    Mat4f projectionTransform;//观察到裁剪空间的变换
    Vec3f cameraWorldPosition;//相机世界空间位置
};

// 片元阶段的输入，来自透视正确插值后的 varying。
struct FragmentInput {//片元输入
    float x = 0.0f;
    float y = 0.0f;
    float depth = 1.0f;//深度
    Vec3f color;//颜色
    Vec3f normal;//法线方向
    Vec2f uv;//纹理坐标
    Vec3f worldPos;//世界空间位置
    Vec3f viewDir;//指向相机的观察方向
    Vec3f tangent{1.0f, 0.0f, 0.0f};//切线方向
    Vec3f bitangent{0.0f, 1.0f, 0.0f};//副切线方向
    float textureLod = 0.0f;//当前片元建议使用的 mip 级别
    Vec3f barycentric{1.0f, 0.0f, 0.0f};//屏幕空间重心坐标
    int objectId = 0;//对象编号
    int materialId = 0;//材质编号
    int triangleId = 0;//三角形编号
    bool frontFacing = true;//是否为正面朝向
};

// 片元阶段的输出。
struct FragmentOutput {//片元输出
    Vec3f color;//颜色
    float alpha = 1.0f;//透明度
    bool discard = false;//是否丢弃
};

// 纹理过滤模式决定一个 UV 会如何落到 texel 上。
enum class TextureFilter {//纹理过滤
    Nearest,//最近邻采样
    Bilinear,//双线性插值
    Trilinear//三线性 + mip 过滤
};

// 纹理坐标越界后的地址模式。
enum class AddressMode {//寻址模式
    Wrap,//平铺
    Clamp//钳制
};

// 采样器状态把过滤和地址模式从 Texture2D 本体上拆出来。
struct SamplerState {//采样器状态
    TextureFilter filter = TextureFilter::Bilinear;
    AddressMode addressU = AddressMode::Wrap;
    AddressMode addressV = AddressMode::Wrap;
};

struct TextureMipLevel {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> texels;
};

// Texture2D 保存最小贴图数据，并提供最近点和双线性采样。
struct Texture2D {//2d纹理
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> texels;//纹素
    std::vector<TextureMipLevel> mipLevels;//mipmap 链

    bool isValid() const;
    int mipLevelCount() const;
    void rebuildMipChain();
    Vec4f sampleColor(const Vec2f &uv, const SamplerState &sampler, float lod = 0.0f) const;
    Vec3f sample(const Vec2f &uv, const SamplerState &sampler, float lod = 0.0f) const;
    Vec3f sampleNearest(const Vec2f &uv,
                        AddressMode addressU = AddressMode::Wrap,
                        AddressMode addressV = AddressMode::Wrap) const;
    Vec3f sampleBilinear(const Vec2f &uv,
                         AddressMode addressU = AddressMode::Wrap,
                         AddressMode addressV = AddressMode::Wrap) const;

    static Texture2D makeCheckerboard(int width,
                                      int height,
                                      int tileSize,
                                      std::uint32_t colorA,
                                      std::uint32_t colorB);//程序化纹理生成
};

// Camera 把观察者的位置和投影视锥参数独立出来。
// 这样渲染流程就能从 model/projection 升级到标准的 model/view/projection。
enum class CameraProjectionMode {
    Perspective,
    Orthographic
};

struct Camera {//相机
    Vec3f position;//位置
    Vec3f target;//注视目标点
    Vec3f up;//上方向向量
    float verticalFovRadians = 1.0f;//垂直视场角
    float nearPlane = 0.1f;//近裁剪面距离
    float farPlane = 100.0f;//远裁剪面距离
    CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;//投影模式
    float orthographicHeight = 4.0f;//正交投影视口高度

    // 输出视图矩阵，把世界空间变到观察空间。
    Mat4f viewMatrix() const;//视图矩阵
    // 输出当前投影模式对应的投影矩阵。
    Mat4f projectionMatrix(float aspect) const;//投影矩阵
};

enum class ShadowFilterQuality {
    Hard,
    Pcf3x3,
    Pcf5x5
};

// 一个最小方向光，供默认 Lambert 着色使用。
struct DirectionalLight {//方向光源
    Vec3f direction;//方向
    Vec3f color;//颜色
    float intensity = 1.0f;//光源强度
    float ambient = 0.15f;//环境光强度
    bool castShadow = false;//是否启用阴影
    float shadowStrength = 0.65f;//阴影压暗强度
    float shadowBias = 0.0025f;//阴影深度偏移
    int shadowMapSize = 512;//阴影图分辨率
    float shadowCoverage = 12.0f;//阴影覆盖范围
    ShadowFilterQuality shadowFilterQuality = ShadowFilterQuality::Pcf3x3;//阴影过滤质量
    bool enabled = true;//是否启用这盏灯
    std::string name;//灯光名称
};

// 点光源提供局部空间照明和距离衰减。
struct PointLight {
    Vec3f position;//世界空间位置
    Vec3f color;//颜色
    float intensity = 8.0f;//发光强度
    float ambient = 0.0f;//额外环境项
    float range = 8.0f;//影响半径
    bool castShadow = false;//是否启用阴影
    float shadowStrength = 0.65f;//阴影压暗强度
    float shadowBias = 0.01f;//阴影深度偏移
    int shadowMapSize = 256;//单面阴影图分辨率
    float shadowRange = 8.0f;//阴影视锥覆盖距离
    ShadowFilterQuality shadowFilterQuality = ShadowFilterQuality::Pcf3x3;//阴影过滤质量
    bool enabled = true;//是否启用这盏灯
    std::string name;//灯光名称
};

struct SpotLight {
    Vec3f position;//世界空间位置
    Vec3f direction{0.0f, -1.0f, 0.0f};//朝向
    Vec3f color{1.0f, 0.95f, 0.86f};//颜色
    float intensity = 10.0f;//发光强度
    float ambient = 0.0f;//额外环境项
    float range = 10.0f;//影响半径
    float innerConeDegrees = 22.5f;//内锥角
    float outerConeDegrees = 32.0f;//外锥角
    bool castShadow = false;//是否启用阴影
    float shadowStrength = 0.65f;//阴影压暗强度
    float shadowBias = 0.0025f;//阴影深度偏移
    int shadowMapSize = 512;//阴影图分辨率
    float shadowRange = 10.0f;//阴影视锥覆盖距离
    ShadowFilterQuality shadowFilterQuality = ShadowFilterQuality::Pcf3x3;//阴影过滤质量
    bool enabled = true;//是否启用这盏灯
    std::string name;//灯光名称
};

struct ShadowMap {
    int width = 0;
    int height = 0;
    Mat4f lightTransform;//世界到光源裁剪空间
    std::vector<float> depthValues;

    bool isValid() const;
    float sampleDepth(const Vec2f &uv) const;
};

struct PointShadowMap {
    static constexpr int kFaceCount = 6;

    int faceSize = 0;
    float farPlane = 1.0f;
    Vec3f lightPosition{0.0f, 0.0f, 0.0f};
    std::array<ShadowMap, kFaceCount> faces;

    bool isValid() const;
    float sampleDepth(const Vec3f &worldPosition, float bias = 0.0f) const;
};

struct Material;
struct LightingContext;

using VertexShader = std::function<VertexOutput(const VertexInput &, const VertexShaderContext &, const Material &)>;//顶点着色器
using FragmentShader = std::function<FragmentOutput(const FragmentInput &, const Material &, const LightingContext &)>;//片段着色器

// 材质类型决定默认片元着色逻辑如何解释纹理、顶点色和光照。
enum class MaterialType {
    LambertTextured,
    LambertVertexColor,
    UnlitTextured,
    UnlitVertexColor,
    BlinnPhongTextured,
    BlinnPhongVertexColor,
    PbrTextured,
    PbrVertexColor
};

// 材质表面模式决定对象按不透明还是透明流程提交。
enum class MaterialSurfaceMode {
    Opaque,
    AlphaBlend
};

// LightingContext 把场景级光照和渲染期阴影缓存收拢到一起。
// authored lights 由场景/UI 持有，shadow maps 由 render pass 在每帧填充。
struct LightingContext {
    std::vector<DirectionalLight> directionalLights;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;
    std::vector<std::shared_ptr<const ShadowMap>> directionalShadowMaps;
    std::vector<std::shared_ptr<const PointShadowMap>> pointShadowMaps;
    std::vector<std::shared_ptr<const ShadowMap>> spotShadowMaps;

    bool hasDirectionalLights() const { return !directionalLights.empty(); }
    bool hasPointLights() const { return !pointLights.empty(); }
    bool hasSpotLights() const { return !spotLights.empty(); }
    bool hasLights() const { return hasDirectionalLights() || hasPointLights() || hasSpotLights(); }
    static LightingContext makeDefault();
};

// Material 收拢当前 draw call 需要的着色状态。
// 这样渲染器本身只负责流程，不再持有“默认光照/默认纹理/默认 shader”。
struct Material {//材质
    MaterialType type = MaterialType::LambertTextured;//材质类型
    MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;//表面模式
    float opacity = 1.0f;//整体不透明度
    bool depthWriteEnable = true;//是否写深度
    Texture2D texture;//基础颜色贴图
    SamplerState sampler;//基础颜色采样状态
    Texture2D normalTexture;//法线贴图
    SamplerState normalSampler;//法线贴图采样状态
    Texture2D metallicRoughnessTexture;//金属度/粗糙度贴图
    SamplerState metallicRoughnessSampler;//金属度/粗糙度采样状态
    Vec3f specularColor{1.0f, 1.0f, 1.0f};//高光颜色
    float specularStrength = 0.35f;//高光强度
    float shininess = 32.0f;//高光指数
    float normalStrength = 1.0f;//法线贴图强度
    float metallic = 0.0f;//金属度
    float roughness = 0.55f;//粗糙度
    bool receiveShadow = true;//是否接收阴影
    VertexShader vertexShader;//自定义顶点着色器；为空时走内建快速路径
    FragmentShader fragmentShader;//自定义片段着色器；为空时走内建快速路径

    static Material makeLambertTextured();
    static Material makeLambertVertexColor();
    static Material makeUnlitTextured();
    static Material makeUnlitVertexColor();
    static Material makeBlinnPhongTextured();
    static Material makeBlinnPhongVertexColor();
    static Material makePbrTextured();
    static Material makePbrVertexColor();
};

// Mesh 把几何数据从 draw 调用参数里提出来，方便复用同一份顶点和索引。
struct Mesh {
    std::vector<VertexInput> vertices;//顶点数据
    std::vector<std::uint32_t> indices;//索引缓冲

    bool isValid() const;//是否生效
    static Mesh makeCube();//生成立法体
    // 从 OBJ 文本解析一个最小 Mesh，支持 v / vt / vn / f。
    static bool loadObjFromText(const std::string &text,
                                Mesh &mesh,
                                std::string *errorMessage = nullptr);
    // 从磁盘读取 OBJ 文件并解析成 Mesh。
    static bool loadObjFromFile(const std::string &path,
                                Mesh &mesh,
                                std::string *errorMessage = nullptr);
};

// Transform 提供对象层最常见的 TRS 描述，再统一生成 model matrix。
struct Transform {//变换
    Vec3f position{0.0f, 0.0f, 0.0f};//平移
    Vec3f rotationRadians{0.0f, 0.0f, 0.0f};//旋转
    Vec3f scale{1.0f, 1.0f, 1.0f};//缩放

    Mat4f modelMatrix() const;//矩阵模型
};

// RenderItem 把一份 mesh、材质和变换绑定成一次场景提交单元。
struct RenderItem {//渲染项
    const Mesh *mesh = nullptr;//网格
    const Material *material = nullptr;//材质
    Transform transform;//变换
};

// Scene 把一次完整渲染所需的高层输入收拢在一起。
struct Scene {//场景
    Camera camera;//相机
    std::uint32_t clearColor = 0xff101722u;//清屏颜色
    LightingContext lighting;//场景级光照上下文
    std::vector<RenderItem> items;//渲染项列表
};

// 背面剔除模式。
enum class CullMode {//背面剔除模式
    None,//不
    Back,//背面
    Front//正面
};

// 最小混合模式，先支持关闭和标准 source-alpha。
enum class BlendMode {//混合模式
    Opaque,//不透明
    Alpha//透明
};

struct BlendState {
    BlendMode mode = BlendMode::Opaque;
};

// 深度比较函数决定“新片元是否可以覆盖旧片元”。
enum class DepthFunc {//深度比较函数
    Never,//永不通过
    Less,//新深度 < 旧深度时通过（默认）
    LessEqual, // 新深度 ≤ 旧深度
    Equal,// 相等才通过
    NotEqual, // 不等才通过
    Greater,// 新深度 > 旧深度
    GreaterEqual,// 新深度 ≥ 旧深度
    Always// 总是通过
};

// 填充模式决定三角形是实体填充还是只画边线。
enum class FillMode {//填充模式
    Solid, // 实体填充
    Wireframe// 只画线框
};

// DebugView 用于把片元阶段改写成调试可视化输出。
enum class DebugView {//调试界面
    None,// 不启用调试视图
    Depth,// 用灰度显示深度
    Normal, // 用法线方向当颜色
    UV, // 用 UV 坐标当颜色
    Overdraw,// 显示像素被重复绘制的次数
    ObjectId,// 用伪随机颜色显示对象编号
    MaterialId,// 用伪随机颜色显示材质编号
    TriangleId,// 用伪随机颜色显示三角形编号
    FaceOrientation,// 用颜色显示正反面朝向
    Barycentric,// 用 RGB 显示重心坐标
    Shadow,// 用灰度显示当前片元阴影因子
    Lighting// 用颜色显示当前片元的灯光贡献
};

// RenderState 描述一次绘制应如何执行，而不是画什么。
enum class AntiAliasingMode {
    None,
    Coverage4x // 历史命名保留，实际实现为 4x MSAA
};

struct RenderState {
    bool depthTestEnable = true;// 是否开启深度测试
    bool depthWriteEnable = true;// 是否写入深度缓冲
    DepthFunc depthFunc = DepthFunc::Less;// 深度比较方式
    CullMode cullMode = CullMode::Back;// 剔除模式
    FillMode fillMode = FillMode::Solid;// 填充模式
    DebugView debugView = DebugView::None;// 调试视图
    AntiAliasingMode antiAliasing = AntiAliasingMode::None;// 抗锯齿模式
    BlendState blend;// 混合状态
};

// RenderPass 把场景数据和本次执行状态打包到一起。
struct RenderPass {//渲染通道
    Scene scene;// 场景数据
    RenderState state; // 渲染状态
    bool clearColorEnabled = true; // 是否清颜色缓冲
    bool clearDepthEnabled = true; // 是否清深度缓冲
    float clearDepthValue = 1.0f; // 深度缓冲清除值
};

// 暴露给 UI 和测试的最小帧统计。
struct RenderStats {
    int width = 0; // 帧缓冲宽度
    int height = 0; // 帧缓冲高度
    int trianglesSubmitted = 0;// 提交的三角形总数
    int trianglesCulled = 0;// 被剔除的三角形数
    int trianglesRasterized = 0;// 实际光栅化的三角形数
    int pixelsDrawn = 0; // 实际绘制的像素数
};

// 并行光栅化调度统计，供 benchmark 和一致性测试读取。
struct ParallelRasterStats {
    int tileSize = 16;
    int tileCount = 0;
    int workerThreadCount = 0;
    int taskCount = 0;
    int parallelTaskCount = 0;
    int serialTaskCount = 0;
    int parallelTileCount = 0;
    int serialTileCount = 0;
    int skippedParallelDispatchCount = 0;
    int minParallelTileCount = 0;
    int minParallelPixelCount = 0;
    int tilesPerTask = 1;
    std::int64_t tileBuildMicroseconds = 0;
    std::int64_t dispatchMicroseconds = 0;
    std::int64_t waitMicroseconds = 0;
};

// 纯 CPU 软光栅化器。
// 当前职责覆盖 framebuffer、顶点变换、裁剪、三角形光栅化和深度测试。
class SoftwareRenderer
{
public:
    struct DrawDebugInfo {
        int objectId = 0;
        int materialId = 0;
        int triangleBase = 0;
    };

    // 构造一个 1x1 的初始渲染器，避免空 buffer 状态。
    SoftwareRenderer();
    ~SoftwareRenderer();

    SoftwareRenderer(const SoftwareRenderer &) = delete;
    SoftwareRenderer &operator=(const SoftwareRenderer &) = delete;

    // 重新分配 color/depth buffer。
    void resize(int width, int height);
    // 清屏并重置深度。
    void clear(std::uint32_t color);
    // 以 Mesh 的形式提交几何数据。
    void drawMesh(const Mesh &mesh,
                  const Mat4f &transform,
                  const Material &material);
    // 以 Transform 的形式提交几何数据，供对象层直接调用。
    void drawMesh(const Mesh &mesh,
                  const Transform &transform,
                  const Material &material);
    // 提交一个已经绑定好 mesh/material/transform 的渲染项。
    void drawRenderItem(const RenderItem &item);
    // 连续提交一组渲染项，供简单场景遍历使用。
    void drawRenderItems(const std::vector<RenderItem> &items);
    // 以显式 render pass 的方式提交，把场景数据和执行状态一起传入。
    void renderPass(const RenderPass &pass);
    // 按 Scene 提交世界空间对象，由渲染器内部完成 view/projection 组合。
    void renderScene(const Scene &scene);
    // 以索引三角形的方式提交几何数据。
    void drawIndexedTriangles(const std::vector<VertexInput> &vertices,
                              const std::vector<std::uint32_t> &indices,
                              const Mat4f &transform,
                              const Material &material);
    // 设置当前相机。
    void setCamera(const Camera &camera);
    // 读取当前相机。
    const Camera &camera() const { return m_camera; }
    // 设置低层 drawMesh/drawRenderItems 路径使用的默认光照上下文。
    void setLightingContext(const LightingContext &lighting);
    // 读取低层路径使用的默认光照上下文。
    const LightingContext &lightingContext() const { return m_defaultLightingContext; }
    // 设置低层 drawMesh/drawTriangle 路径使用的默认 render state。
    void setRenderState(const RenderState &state);
    // 读取低层路径使用的默认 render state。
    const RenderState &renderState() const { return m_defaultRenderState; }
    // 开关整套并行光栅化调度。
    void setParallelRasterEnabled(bool enabled);
    // 读取并行光栅化开关。
    bool parallelRasterEnabled() const { return m_parallelConfig.enabled; }
    // 设置工作线程数；0 表示关闭线程池，负数表示自动。
    void setWorkerThreadCount(int count);
    // 读取当前请求的工作线程数；-1 表示自动。
    int requestedWorkerThreadCount() const { return m_parallelConfig.requestedWorkerCount; }
    // 读取当前工作线程数。
    int workerThreadCount() const { return static_cast<int>(m_workerThreads.size()); }
    // 设置 tile 尺寸。
    void setRasterTileSize(int size);
    // 读取当前 tile 尺寸。
    int rasterTileSize() const { return m_parallelConfig.tileSize; }
    // 设置进入并行路径的最小 tile / pixel 门槛。
    void setParallelThresholds(int minTileCount, int minPixelCount);
    // 读取进入并行路径的最小 tile 门槛。
    int minParallelTileCount() const { return m_parallelConfig.minParallelTileCount; }
    // 读取进入并行路径的最小 pixel 门槛。
    int minParallelPixelCount() const { return m_parallelConfig.minParallelPixelCount; }
    // 设置每个并行任务最多处理多少个 tile。
    void setParallelTilesPerTask(int tilesPerTask);
    // 读取每个并行任务最多处理多少个 tile。
    int parallelTilesPerTask() const { return m_parallelConfig.tilesPerTask; }
    // 设置当前剔除模式。
    void setCullMode(CullMode mode);
    // 读取当前剔除模式。
    CullMode cullMode() const { return m_defaultRenderState.cullMode; }
    // 设置当前抗锯齿模式。
    void setAntiAliasingMode(AntiAliasingMode mode);
    // 读取当前抗锯齿模式。
    AntiAliasingMode antiAliasingMode() const { return m_defaultRenderState.antiAliasing; }
    // 内置 demo：渲染一个旋转彩色立方体。
    void renderDemo(float elapsedSeconds);

    // 当前 framebuffer 宽度。
    int width() const { return m_width; }
    // 当前 framebuffer 高度。
    int height() const { return m_height; }
    // 供 Qt 直接包装成 QImage 的原始 color buffer 指针；返回前会先 resolve 脏像素。
    const std::uint32_t *colorBufferData();
    // 读取单个像素，主要给测试用。
    std::uint32_t colorAt(int x, int y);
    // 读取本帧统计信息。
    const RenderStats &stats() const { return m_stats; }
    // 读取本帧并行调度统计。
    const ParallelRasterStats &parallelStats() const { return m_parallelStats; }

private:
    struct RasterTileBounds {
        int startX = 0;
        int endX = -1;
        int startY = 0;
        int endY = -1;
    };

    struct TileTaskResult {
        int pixelsDrawn = 0;
        std::vector<std::uint32_t> dirtyPixelIndices;
    };

    struct ShadowMapCacheEntry {
        std::shared_ptr<ShadowMap> shadowMap;
        std::uint64_t signature = 0;
        bool valid = false;
    };

    struct TriangleAttribute2SoA {
        float x[3] = {};
        float y[3] = {};
    };

    struct TriangleAttribute3SoA {
        float x[3] = {};
        float y[3] = {};
        float z[3] = {};
    };

    struct TriangleRasterWork {
        ScreenVertex vertices[3];
        bool edgeTopLeft[3] = {};
        bool useMsaa = false;
        bool overdrawDebug = false;
        bool frontFacing = true;
        float edgeStepX[3] = {};
        float edgeStepY[3] = {};
        float weightedInverseW[3] = {};
        float weightedDepth[3] = {};
        TriangleAttribute3SoA weightedColor;
        TriangleAttribute3SoA weightedNormal;
        TriangleAttribute2SoA weightedUv;
        TriangleAttribute3SoA weightedWorldPos;
        TriangleAttribute3SoA weightedViewDir;
        Vec3f tangent{1.0f, 0.0f, 0.0f};
        Vec3f bitangent{0.0f, 1.0f, 0.0f};
        float textureLod = 0.0f;
        float inverseArea = 0.0f;
        Vec3f cameraWorldPosition;
        int objectId = 0;
        int materialId = 0;
        int triangleId = 0;
    };

    struct LineRasterWork {
        ScreenVertex from;
        ScreenVertex to;
        Vec2f start;
        Vec2f end;
        Vec3f tangent{1.0f, 0.0f, 0.0f};
        Vec3f bitangent{0.0f, 1.0f, 0.0f};
        float textureLod = 0.0f;
        Vec3f cameraWorldPosition;
        bool overdrawDebug = false;
        float lineRadiusSquared = 0.25f;
        bool frontFacing = true;
        int objectId = 0;
        int materialId = 0;
        int triangleId = 0;
    };

    // 清零统计并同步当前分辨率。
    void resetStats();
    // 根据当前配置重建工作线程池。
    void rebuildWorkerThreads();
    // 只清深度缓存，供 render pass 控制是否保留颜色。
    void clearDepth(float depth);
    // 清空 overdraw 计数，供调试视图按帧重算。
    void clearOverdraw();
    // 把包围盒切成固定尺寸 tile，供后续并行任务复用。
    std::vector<RasterTileBounds> buildTilesForBounds(int startX, int endX, int startY, int endY) const;
    // 合并一个 tile 任务的局部统计和脏像素列表。
    void mergeTileTaskResult(TileTaskResult &&result);
    // 判断这批 tile 是否值得走多线程，避免小任务的调度开销反而更大。
    bool shouldUseParallelRaster(std::size_t tileCount, int estimatedPixelCount) const;
    // 启动固定数量的后台工作线程，供 tile 任务复用。
    void startWorkerThreads();
    // 关闭后台工作线程。
    void stopWorkerThreads();
    // 后台工作线程主循环。
    void workerThreadMain();
    // 把一个 tile 批次任务提交给线程池，返回结果 future。
    std::future<TileTaskResult> enqueueTileTask(std::function<TileTaskResult()> task);
    // 用统一线程池执行一批 tile 任务；满足阈值时并行，否则串行。
    void executeTileTasks(const std::vector<RasterTileBounds> &tiles,
                          int estimatedPixelCount,
                          const std::function<TileTaskResult(const RasterTileBounds &)> &workFn);
    // 基于场景几何和光源参数生成阴影缓存签名；返回 0 表示当前帧不适合缓存。
    std::uint64_t buildShadowMapCacheSignature(const std::vector<const RenderItem *> &items,
                                               const DirectionalLight &light) const;
    // 清空当前保存的方向光阴影缓存。
    void invalidateShadowMapCache();
    // 为一盏方向光构建 shadow map；render pass 会按 LightingContext 里的灯逐个调用。
    std::shared_ptr<ShadowMap> buildShadowMapForItems(const std::vector<const RenderItem *> &items,
                                                      const DirectionalLight &light) const;
    // 为一盏聚光灯构建 shadow map。
    std::shared_ptr<ShadowMap> buildShadowMapForItems(const std::vector<const RenderItem *> &items,
                                                      const SpotLight &light) const;
    // 为一点光源构建立方体阴影图。
    std::shared_ptr<PointShadowMap> buildPointShadowMapForItems(const std::vector<const RenderItem *> &items,
                                                                const PointLight &light) const;
    // 把一个三角形写进 shadow map 深度缓冲。
    void rasterizeShadowTriangle(const VertexOutput &a,
                                 const VertexOutput &b,
                                 const VertexOutput &c,
                                 ShadowMap &shadowMap) const;
    // 读取当前生效的 render state；如果没有 pass 覆盖，就退回默认状态。
    const RenderState &activeRenderState() const;
    // 对单个三角形执行顶点阶段、裁剪和光栅化。
    void drawTriangle(const VertexInput &a,
                      const VertexInput &b,
                      const VertexInput &c,
                      const VertexShaderContext &context,
                      const Material &material,
                      const DrawDebugInfo &debugInfo,
                      int triangleIndex);
    // 用同一份 shader 上下文绘制整份 Mesh，避免每个三角形重复拼上下文。
    void drawMeshWithContext(const Mesh &mesh,
                             const VertexShaderContext &context,
                             const Material &material,
                             const DrawDebugInfo &debugInfo);
    // 顶点阶段：调用当前顶点 shader。
    VertexOutput runVertexShader(const VertexInput &vertex,
                                 const VertexShaderContext &context,
                                 const Material &material) const;
    // 片元阶段：调用当前片元 shader。
    FragmentOutput runFragmentShader(const FragmentInput &fragment,
                                     const Material &material) const;
    // 对单个三角形执行近平面裁剪。
    std::vector<VertexOutput> clipTriangleAgainstNearPlane(const VertexOutput &a,
                                                           const VertexOutput &b,
                                                           const VertexOutput &c) const;
    // 把一个顶点阶段输出变到屏幕空间。
    bool projectVertexOutput(const VertexOutput &vertex, ScreenVertex &screenVertex) const;
    // 根据当前剔除模式判断一个三角形是否应被丢弃。
    bool shouldCullTriangle(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c) const;
    // 在屏幕空间填充一个三角形。
    void rasterizeTriangle(const ScreenVertex &a,
                           const ScreenVertex &b,
                           const ScreenVertex &c,
                           const Material &material,
                           const DrawDebugInfo &debugInfo,
                           int triangleId);
    // 线框模式下按边绘制一个三角形。
    void rasterizeWireframeTriangle(const ScreenVertex &a,
                                    const ScreenVertex &b,
                                    const ScreenVertex &c,
                                    const Material &material,
                                    const DrawDebugInfo &debugInfo,
                                    int triangleId,
                                    bool frontFacing);
    // 绘制一条屏幕空间边，供 wireframe 路径复用。
    void rasterizeLineSegment(const ScreenVertex &from,
                              const ScreenVertex &to,
                              const Material &material,
                              const DrawDebugInfo &debugInfo,
                              int triangleId,
                              bool frontFacing);
    // 在一个 tile 内执行三角形光栅化任务；线程安全前提是不同任务写入的 tile 不重叠。
    TileTaskResult rasterizeTriangleTile(const TriangleRasterWork &work,
                                         const RasterTileBounds &tile,
                                         const Material &material);
    // 在线段的一个 tile 内执行 MSAA 光栅化任务。
    TileTaskResult rasterizeLineSegmentTile(const LineRasterWork &work,
                                            const RasterTileBounds &tile,
                                            const Material &material);
    // 对单个像素执行 1x 着色，并把结果镜像写入全部 MSAA 样本。
    void shadeAndWritePixel(int x,
                            int y,
                            const FragmentInput &fragment,
                            const Material &material);
    // 给 tile 任务使用的 1x 写路径；局部累计统计和脏像素，避免并发写全局列表。
    bool shadeAndWritePixelDeferred(int x,
                                    int y,
                                    const FragmentInput &fragment,
                                    const Material &material,
                                    TileTaskResult &result);
    // 对单个 MSAA 样本执行 depth/shader/blend/write。
    bool shadeAndWriteSample(int x,
                             int y,
                             int sampleIndex,
                             const FragmentInput &fragment,
                             const Material &material);
    // 计算一个片元在当前调试视图/材质下的最终着色结果。
    bool shadeFragment(const FragmentInput &fragment,
                       const Material &material,
                       Vec4f &shadedColor) const;
    // 把一个像素的 4 个样本 resolve 回主颜色缓冲。
    void resolvePixelColor(int x, int y);
    // 维护像素级最小深度缓存，给非 MSAA 路径复用。
    void resolvePixelDepth(int x, int y);
    // 标记一个像素的 resolve 结果已过期，等本轮绘制结束后再统一刷回主缓冲。
    void markPixelDirty(int x, int y);
    // 把所有脏像素统一 resolve 回主颜色/深度缓冲。
    void resolveDirtyPixels();
    // 像素线性下标。
    std::size_t pixelBufferIndex(int x, int y) const;
    // 样本线性下标，布局为 pixelIndex * sampleCount + sampleIndex。
    std::size_t sampleBufferIndex(int x, int y, int sampleIndex) const;
    // 按当前 depth func 决定是否通过深度测试。
    bool depthTestPasses(float incomingDepth, float storedDepth) const;
    // top-left rule 使用的边分类。
    static bool isTopLeftEdge(float ax, float ay, float bx, float by);
    // 把 overdraw 次数映射成调试颜色。
    static Vec4f overdrawDebugColor(std::uint32_t count);
    // 把任意整数编号映射成稳定伪随机调试颜色。
    static Vec4f debugIdColor(int id);
    // 默认顶点 shader。
    static VertexOutput defaultVertexShader(const VertexInput &vertex,
                                            const VertexShaderContext &context,
                                            const Material &material);
    // 默认片元 shader 使用场景级 LightingContext 和当前材质做着色。
    static FragmentOutput defaultFragmentShader(const FragmentInput &fragment,
                                                const Material &material,
                                                const LightingContext &lighting);
    // 二维边函数，既能判断点是否在三角形内，也能给出带符号面积。
    static float edgeFunction(float ax, float ay, float bx, float by, float px, float py);
    // 把 0..1 浮点颜色打包成 ARGB8。
    static std::uint32_t packColor(const Vec4f &color);

    // framebuffer 尺寸。
    int m_width;
    int m_height;
    // 当前观察相机。
    Camera m_camera;
    // 低层绘制入口使用的默认光照上下文。
    LightingContext m_defaultLightingContext;
    // pass 级别光照上下文在绘制期间临时覆盖默认光照。
    const LightingContext *m_activeLightingContext;
    // 低层绘制入口使用的默认 render state。
    RenderState m_defaultRenderState;
    // pass 级别状态在绘制期间临时覆盖默认状态。
    const RenderState *m_activeRenderState;
    // ARGB8 颜色缓冲。
    std::vector<std::uint32_t> m_colorBuffer;
    // 每像素 resolve 后的最小深度缓存，范围约定为 0..1。
    std::vector<float> m_depthBuffer;
    // 每样本颜色缓存，供 4x MSAA 做独立颜色存储。
    std::vector<std::uint32_t> m_sampleColorBuffer;
    // 每样本深度缓存，供 4x MSAA 做独立深度测试。
    std::vector<float> m_sampleDepthBuffer;
    // 每像素 overdraw 计数，仅供调试视图使用。
    std::vector<std::uint32_t> m_overdrawBuffer;
    // 脏标记避免同一像素在一帧里被重复加入 resolve 队列。
    std::vector<std::uint8_t> m_dirtyPixelFlags;
    // 本轮绘制里真正改过 sample buffer 的像素列表。
    std::vector<std::uint32_t> m_dirtyPixelIndices;
    // 方向光阴影图缓存，避免静态场景在纯相机移动时重复重建。
    std::vector<ShadowMapCacheEntry> m_directionalShadowMapCache;
    // 固定工作线程池，避免每次 draw 都创建/销毁异步任务。
    std::vector<std::thread> m_workerThreads;
    // 工作线程共享任务队列。
    std::deque<std::function<void()>> m_workerTasks;
    // 保护任务队列和停止标志。
    std::mutex m_workerMutex;
    // 工作线程等待新任务的条件变量。
    std::condition_variable m_workerCondition;
    // 线程池关闭标志。
    bool m_stopWorkers = false;
    struct ParallelRasterConfig {
        bool enabled = true;
        int requestedWorkerCount = -1;
        int tileSize = 16;
        int minParallelTileCount = 4;
        int minParallelPixelCount = 1024;
        int tilesPerTask = 1;
    };
    // 并行光栅化配置。
    ParallelRasterConfig m_parallelConfig;
    // 每帧更新的统计信息。
    RenderStats m_stats;
    // 每帧并行调度统计。
    ParallelRasterStats m_parallelStats;

    static constexpr int kMsaaSampleCount = 4;
};

#endif // SOFTWARE_RENDERER_H
