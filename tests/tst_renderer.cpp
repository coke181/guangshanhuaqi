#include <QtTest>
#include <QTemporaryDir>
#include <cstring>
#include <fstream>
#include "SoftwareRenderer.h"

namespace {

Texture2D makeSolidTexture(std::uint32_t color)//生成纯色纹理
{
    Texture2D texture;
    texture.width = 1;
    texture.height = 1;
    texture.texels = {color};
    texture.rebuildMipChain();
    return texture;
}

Texture2D makeSolidNormalTexture(const Vec3f &normal)//生成纯色法线纹理
{
    const Vec3f encoded = clamp01({normal.x * 0.5f + 0.5f, normal.y * 0.5f + 0.5f, normal.z * 0.5f + 0.5f});
    return makeSolidTexture(0xff000000u
                            | (static_cast<std::uint32_t>(encoded.x * 255.0f + 0.5f) << 16)
                            | (static_cast<std::uint32_t>(encoded.y * 255.0f + 0.5f) << 8)
                            | static_cast<std::uint32_t>(encoded.z * 255.0f + 0.5f));
}

DirectionalLight makeHeadOnWhiteLight(float ambient = 0.0f)//生成正面白色光源
{
    return {normalize(Vec3f{0.0f, 0.0f, -1.0f}), {1.0f, 1.0f, 1.0f}, 1.0f, ambient};
}

PointLight makePointLight(const Vec3f &position,
                          const Vec3f &color,
                          float intensity = 8.0f,
                          float range = 8.0f,
                          float ambient = 0.0f)//生成点光源
{
    PointLight light;
    light.position = position;
    light.color = color;
    light.intensity = intensity;
    light.range = range;
    light.ambient = ambient;
    return light;
}

Material makeLambertMaterial(const Texture2D &texture,
                             TextureFilter filter = TextureFilter::Bilinear,
                             AddressMode addressU = AddressMode::Wrap,
                             AddressMode addressV = AddressMode::Wrap,
                             float ambient = 0.0f)
{
    Q_UNUSED(ambient);
    Material material = Material::makeLambertTextured();
    material.texture = texture;
    material.sampler = {filter, addressU, addressV};
    return material;
}

Material makeBlinnPhongMaterial(const Texture2D &texture, float ambient = 0.0f)
{
    Q_UNUSED(ambient);
    Material material = Material::makeBlinnPhongTextured();
    material.texture = texture;
    material.sampler = {TextureFilter::Bilinear, AddressMode::Wrap, AddressMode::Wrap};
    return material;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.02f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

int redChannel(std::uint32_t color)
{
    return static_cast<int>((color >> 16) & 0xffu);
}

int greenChannel(std::uint32_t color)
{
    return static_cast<int>((color >> 8) & 0xffu);
}

int blueChannel(std::uint32_t color)
{
    return static_cast<int>(color & 0xffu);
}

Mesh makeTriangleMesh(std::uint32_t i0 = 0u, std::uint32_t i1 = 1u, std::uint32_t i2 = 2u)
{
    Mesh mesh;
    mesh.vertices = {
        {{-0.45f, -0.45f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.45f, -0.45f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.45f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    mesh.indices = {i0, i1, i2};
    return mesh;
}

Scene makeStressScene(int gridWidth = 6, int gridHeight = 6)
{
    static const Mesh cubeMesh = Mesh::makeCube();
    static const std::vector<Material> materials = []() {
        std::vector<Material> result;

        Material redLambert = Material::makeLambertTextured();
        redLambert.texture = makeSolidTexture(0xffff7a7au);
        result.push_back(redLambert);

        Material greenBlinn = Material::makeBlinnPhongTextured();
        greenBlinn.texture = makeSolidTexture(0xff7affb0u);
        greenBlinn.specularStrength = 0.55f;
        greenBlinn.shininess = 56.0f;
        result.push_back(greenBlinn);

        Material bluePbr = Material::makePbrTextured();
        bluePbr.texture = makeSolidTexture(0xff7ab6ffu);
        bluePbr.metallic = 0.2f;
        bluePbr.roughness = 0.45f;
        result.push_back(bluePbr);

        return result;
    }();

    Scene scene;
    scene.camera = {{0.0f, 3.2f, 10.5f},
                    {0.0f, 0.0f, -6.0f},
                    {0.0f, 1.0f, 0.0f},
                    55.0f * 3.14159265358979323846f / 180.0f,
                    0.1f,
                    80.0f};
    scene.clearColor = 0xff0f1622u;
    scene.lighting = LightingContext::makeDefault();
    scene.lighting.pointLights.clear();
    scene.lighting.directionalLights = {
        makeHeadOnWhiteLight(0.12f),
        {normalize(Vec3f{-0.35f, -0.8f, -0.25f}), {0.65f, 0.72f, 1.0f}, 0.65f, 0.0f}
    };

    scene.items.reserve(static_cast<std::size_t>(gridWidth * gridHeight));
    for (int y = 0; y < gridHeight; ++y) {
        for (int x = 0; x < gridWidth; ++x) {
            RenderItem item;
            item.mesh = &cubeMesh;
            item.material = &materials[static_cast<std::size_t>((x + y) % static_cast<int>(materials.size()))];
            item.transform.position = {
                (static_cast<float>(x) - static_cast<float>(gridWidth - 1) * 0.5f) * 1.3f,
                std::sin(static_cast<float>(x + y) * 0.45f) * 0.2f,
                -3.0f - static_cast<float>(y) * 1.35f
            };
            item.transform.rotationRadians = {
                0.22f * static_cast<float>(y),
                0.31f * static_cast<float>(x),
                0.11f * static_cast<float>(x + y)
            };
            item.transform.scale = {0.55f, 0.55f, 0.55f};
            scene.items.push_back(item);
        }
    }

    return scene;
}

bool colorBuffersEqual(SoftwareRenderer &lhs, SoftwareRenderer &rhs)
{
    if (lhs.width() != rhs.width() || lhs.height() != rhs.height())
        return false;

    const std::size_t byteCount = static_cast<std::size_t>(lhs.width())
        * static_cast<std::size_t>(lhs.height())
        * sizeof(std::uint32_t);
    return std::memcmp(lhs.colorBufferData(), rhs.colorBufferData(), byteCount) == 0;
}

} // namespace

// 这些测试只覆盖当前最关键的回归点：
// 1. 三角形确实能写入像素
// 2. 深度测试会让更近的三角形覆盖更远的三角形
// 3. 背面剔除会按绕序丢弃背向相机的三角形
// 4. 近平面裁剪会保留跨近平面的可见部分
// 5. 自定义 vertex/fragment shader 会被真正调用
// 6. Texture2D 的过滤/地址模式切换和材质化 shader 已生效
class RendererTests : public QObject
{
    Q_OBJECT

private slots:
    // 验证最基本的光栅化链路是通的。
    void triangleWritesPixels();
    // 验证 Depth 调试视图会把深度映射成灰度。
    void debugViewDepthMapsDepthToGrayscale();
    // 验证 Normal 调试视图会把法线映射到 0..1 颜色空间。
    void debugViewNormalMapsNormalToRgb();
    // 验证 UV 调试视图会直接显示 uv 坐标。
    void debugViewUvMapsUvToColor();
    // 验证 Overdraw 调试视图会把重复覆盖映射成热力颜色。
    void debugViewOverdrawAccumulatesFragmentCount();
    // 验证对象 ID 调试视图会区分不同对象。
    void debugViewObjectIdSeparatesObjects();
    // 验证材质 ID 调试视图会区分不同材质。
    void debugViewMaterialIdSeparatesMaterials();
    // 验证三角形 ID 调试视图会区分不同三角形。
    void debugViewTriangleIdSeparatesTriangles();
    // 验证正反面朝向调试视图会区分前后面。
    void debugViewFaceOrientationSeparatesFrontAndBack();
    // 验证重心坐标调试视图输出 RGB 重心权重。
    void debugViewBarycentricMapsWeightsToRgb();
    // 验证 top-left rule 会避免 shared edge 被双重覆盖。
    void rasterizationUsesTopLeftRuleForSharedEdges();
    // 验证 LessEqual 会允许等深片元覆盖已有像素。
    void renderPassDepthFuncLessEqualAllowsEqualDepthOverwrite();
    // 验证 Greater 会结合 clearDepthValue 通过更大的深度值。
    void renderPassDepthFuncGreaterUsesCustomClearDepth();
    // 验证 Always 会忽略已有深度，按提交顺序覆盖。
    void renderPassDepthFuncAlwaysIgnoresStoredDepth();
    // 验证 wireframe 只绘制边线，不填充三角形内部。
    void renderPassWireframeDrawsEdgesWithoutFillingInterior();
    // 验证 render pass 的 cull state 会覆盖 renderer 默认状态。
    void renderPassStateOverridesDefaultCullMode();
    // 验证 render pass 可以跳过颜色清屏，并关闭深度写入。
    void renderPassCanSkipColorClearAndDisableDepthWrite();
    // 验证最小 source-alpha blending 会把片元颜色混到背景上。
    void renderPassAlphaBlendCompositesFragmentOverBackground();
    // 验证 Scene 会接管 clearColor、camera 和 world-space item 提交。
    void sceneRendersWorldSpaceItemsWithCamera();
    // 验证 Transform 会稳定生成 TRS 变换，并且 drawMesh 可直接消费它。
    void transformDrivesMeshPlacementAndScale();
    // 验证 RenderItem 可以复用同一份 mesh，并给不同实例绑定不同材质和变换。
    void renderItemsBindMeshMaterialAndTransformPerInstance();
    // 验证 z-buffer 的覆盖顺序正确。
    void nearerTriangleWinsDepthTest();
    // 验证正面保留、背面剔除。
    void backFaceCullingRejectsReversedWinding();
    // 验证跨近平面的三角形会被裁成可见的部分继续绘制。
    void nearPlaneClippingPreservesVisiblePortion();
    // 验证可替换 shader 接口生效。
    void customShadersOverrideDefaultPipelineBehavior();
    // 验证默认顶点阶段会把 worldPos / viewDir 送到片元阶段。
    void defaultVertexShaderInterpolatesWorldPosAndViewDir();
    // 验证最近点和双线性采样可以切换。
    void textureFilterSwitchChangesSamplingResult();
    // 验证三线性过滤会根据 lod 采样更低 mip。
    void textureTrilinearFilterUsesMipLevels();
    // 验证 wrap 和 clamp 地址模式会得到不同 texel。
    void textureAddressModeSwitchChangesSamplingResult();
    // 验证 4x 覆盖抗锯齿会在三角形边缘生成部分覆盖颜色。
    void antiAliasingCoverage4xProducesIntermediateEdgePixels();
    // 验证 4x 抗锯齿会做真正的每样本 resolve，而不是 coverage alpha 近似。
    void antiAliasingCoverage4xResolvesPerSampleOpaqueColors();
    // 验证 wireframe 线段在 4x MSAA 下也会产生部分覆盖像素。
    void wireframeAntiAliasingCoverage4xProducesIntermediateEdgePixels();
    // 验证默认片元 shader 已经做 Lambert * Texture。
    void defaultLambertTextureMaterialUsesTextureSample();
    // 验证法线贴图会改变默认光照结果。
    void normalMapChangesLambertLighting();
    // 验证 LightingContext 会累计多盏方向光结果。
    void lightingContextAccumulatesDirectionalLights();
    // 验证 LightingContext 也支持点光源，并会把多盏点光结果累加。
    void lightingContextAccumulatesPointLights();
    // 验证点光源会按距离衰减。
    void pointLightAttenuationRespondsToDistance();
    // 验证 Blinn-Phong 材质会在漫反射之外额外叠加高光。
    void blinnPhongMaterialAddsSpecularHighlight();
    // 验证 PBR 材质会响应 metallic / roughness 参数变化。
    void pbrMaterialRespondsToMetallicAndRoughness();
    // 验证默认贴图材质会读取纹理 alpha 并参与透明混合。
    void texturedMaterialUsesTextureAlphaForTransparency();
    // 验证不同对象可以绑定不同材质类型并得到不同结果。
    void materialTypeChangesFragmentBehaviorPerObject();
    // 验证透明对象会按距离排序，并默认关闭深度写入。
    void transparentObjectsSortBackToFrontPerMaterial();
    // 验证 OBJ 文本能解析出带 UV/法线的三角化 Mesh。
    void objLoaderTriangulatesFacesAndKeepsAttributes();
    // 验证 OBJ 缺法线时会自动生成面法线。
    void objLoaderGeneratesFaceNormalsWhenMissing();
    // 验证可以从磁盘读取 OBJ 文件。
    void objLoaderReadsMeshFromFile();
    // 验证并行光栅化和串行路径输出完全一致。
    void parallelRasterMatchesSerialOutput();
    // 验证 tile 尺寸配置会真正进入调度统计。
    void parallelRasterTileSizeIsConfigurable();
    // 验证调度统计和小任务阈值会反映到并行/串行分发结果。
    void parallelRasterStatsReportDispatchBehavior();
};

void RendererTests::triangleWritesPixels()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    const std::vector<VertexInput> vertices = {
        {{-0.8f, -0.8f, 0.3f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.8f, -0.8f, 0.3f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.8f, 0.3f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    // 只要中心点被写到非背景色，就说明三角形已成功栅格化。
    QCOMPARE(renderer.stats().trianglesSubmitted, 1);
    QCOMPARE(renderer.stats().trianglesRasterized, 1);
    QVERIFY(renderer.stats().pixelsDrawn > 0);
    QVERIFY(renderer.colorAt(32, 32) != 0xff000000u);
}

void RendererTests::debugViewDepthMapsDepthToGrayscale()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::Depth;

    renderer.renderPass(pass);

    const std::uint32_t color = renderer.colorAt(32, 32);
    QCOMPARE(redChannel(color), greenChannel(color));
    QCOMPARE(greenChannel(color), blueChannel(color));
    QVERIFY(redChannel(color) > 0);
}

void RendererTests::debugViewNormalMapsNormalToRgb()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::Normal;

    renderer.renderPass(pass);

    const std::uint32_t color = renderer.colorAt(32, 32);
    QVERIFY(std::abs(redChannel(color) - 128) <= 1);
    QVERIFY(std::abs(greenChannel(color) - 128) <= 1);
    QCOMPARE(blueChannel(color), 255);
}

void RendererTests::debugViewUvMapsUvToColor()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::UV;

    renderer.renderPass(pass);

    const std::uint32_t color = renderer.colorAt(32, 32);
    QVERIFY(redChannel(color) > 0);
    QVERIFY(greenChannel(color) > 0);
    QCOMPARE(blueChannel(color), 0);
}

void RendererTests::debugViewOverdrawAccumulatesFragmentCount()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::Overdraw;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xffffff00u);
}

void RendererTests::debugViewObjectIdSeparatesObjects()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{-0.45f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &material, {{0.45f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::ObjectId;

    renderer.renderPass(pass);

    QVERIFY(renderer.colorAt(18, 32) != renderer.colorAt(46, 32));
}

void RendererTests::debugViewMaterialIdSeparatesMaterials()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material leftMaterial = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    const Material rightMaterial = makeLambertMaterial(makeSolidTexture(0xff00ff00u));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &leftMaterial, {{-0.45f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &rightMaterial, {{0.45f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::MaterialId;

    renderer.renderPass(pass);

    QVERIFY(renderer.colorAt(18, 32) != renderer.colorAt(46, 32));
}

void RendererTests::debugViewTriangleIdSeparatesTriangles()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    Mesh mesh;
    mesh.vertices = {
        {{-0.95f, -0.9f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{-0.05f, -0.9f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f, 0.2f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
        {{0.05f, -0.9f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.95f, -0.9f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.2f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    mesh.indices = {0, 1, 2, 3, 4, 5};
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -1.6f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::TriangleId;

    renderer.renderPass(pass);

    QVERIFY(renderer.colorAt(14, 36) != renderer.colorAt(50, 36));
}

void RendererTests::debugViewFaceOrientationSeparatesFrontAndBack()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh frontMesh = makeTriangleMesh(0, 1, 2);
    const Mesh backMesh = makeTriangleMesh(0, 2, 1);
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&frontMesh, &material, {{-0.45f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&backMesh, &material, {{0.45f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::FaceOrientation;

    renderer.renderPass(pass);

    const std::uint32_t frontColor = renderer.colorAt(18, 32);
    const std::uint32_t backColor = renderer.colorAt(46, 32);
    QVERIFY(greenChannel(frontColor) > redChannel(frontColor));
    QVERIFY(redChannel(backColor) > greenChannel(backColor));
}

void RendererTests::debugViewBarycentricMapsWeightsToRgb()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.debugView = DebugView::Barycentric;

    renderer.renderPass(pass);

    const std::uint32_t color = renderer.colorAt(32, 32);
    QVERIFY(redChannel(color) > 0);
    QVERIFY(greenChannel(color) > 0);
    QVERIFY(blueChannel(color) > 0);
}

void RendererTests::rasterizationUsesTopLeftRuleForSharedEdges()
{
    SoftwareRenderer renderer;
    renderer.resize(5, 5);
    renderer.clear(0xff000000u);

    RenderState state;
    state.depthTestEnable = false;
    state.depthWriteEnable = false;
    state.cullMode = CullMode::None;
    renderer.setRenderState(state);

    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    const std::vector<VertexInput> vertices = {
        {{-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {
        0, 3, 2,
        0, 2, 1
    };

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    QCOMPARE(renderer.stats().pixelsDrawn, 16);
}

void RendererTests::renderPassDepthFuncLessEqualAllowsEqualDepthOverwrite()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material greenMaterial = makeLambertMaterial(makeSolidTexture(0xff00ff00u));
    const Material redMaterial = makeLambertMaterial(makeSolidTexture(0xffff0000u));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &greenMaterial, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &redMaterial, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.depthFunc = DepthFunc::LessEqual;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xffff0000u);
}

void RendererTests::renderPassDepthFuncGreaterUsesCustomClearDepth()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffff00u));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.depthFunc = DepthFunc::Greater;
    pass.clearDepthValue = 0.0f;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xffffff00u);
}

void RendererTests::renderPassDepthFuncAlwaysIgnoresStoredDepth()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material greenMaterial = makeLambertMaterial(makeSolidTexture(0xff00ff00u));
    const Material redMaterial = makeLambertMaterial(makeSolidTexture(0xffff0000u));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &greenMaterial, {{0.0f, 0.0f, -1.4f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &redMaterial, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.depthFunc = DepthFunc::Always;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xffff0000u);
}

void RendererTests::renderPassWireframeDrawsEdgesWithoutFillingInterior()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.fillMode = FillMode::Wireframe;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xff000000u);
    QVERIFY(renderer.colorAt(32, 20) == 0xffffffffu || renderer.colorAt(31, 20) == 0xffffffffu || renderer.colorAt(33, 20) == 0xffffffffu);
}

void RendererTests::renderPassStateOverridesDefaultCullMode()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.setCullMode(CullMode::Back);

    const Mesh mesh = makeTriangleMesh(0u, 2u, 1u);
    const Material material = makeLambertMaterial(makeSolidTexture(0xffff00ffu));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff000000u;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xffff00ffu);
}

void RendererTests::renderPassCanSkipColorClearAndDisableDepthWrite()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff0000ffu);
    renderer.setCullMode(CullMode::Back);

    const Mesh mesh = makeTriangleMesh();
    const Material greenMaterial = makeLambertMaterial(makeSolidTexture(0xff00ff00u));
    const Material redMaterial = makeLambertMaterial(makeSolidTexture(0xffff0000u));

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff101010u;
    pass.scene.items = {
        {&mesh, &greenMaterial, {{0.0f, 0.0f, -1.4f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &redMaterial, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.depthWriteEnable = false;
    pass.clearColorEnabled = false;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(0, 0), 0xff0000ffu);
    QCOMPARE(renderer.colorAt(32, 32), 0xffff0000u);
}

void RendererTests::renderPassAlphaBlendCompositesFragmentOverBackground()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    material.fragmentShader = [](const FragmentInput &fragment, const Material &material, const LightingContext &lighting) {
        Q_UNUSED(fragment);
        Q_UNUSED(material);
        Q_UNUSED(lighting);
        return FragmentOutput{{1.0f, 0.0f, 0.0f}, 0.25f, false};
    };

    RenderPass pass;
    pass.scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    pass.scene.clearColor = 0xff0000ffu;
    pass.scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    pass.state.cullMode = CullMode::None;
    pass.state.blend.mode = BlendMode::Alpha;

    renderer.renderPass(pass);

    QCOMPARE(renderer.colorAt(32, 32), 0xff4000bfu);
}

void RendererTests::sceneRendersWorldSpaceItemsWithCamera()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.setCullMode(CullMode::None);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffff00u));

    Scene scene;
    scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    scene.clearColor = 0xff112233u;
    scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.renderScene(scene);

    QCOMPARE(renderer.colorAt(0, 0), 0xff112233u);
    QCOMPARE(renderer.colorAt(32, 32), 0xffffff00u);
}

void RendererTests::transformDrivesMeshPlacementAndScale()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    Mesh mesh = makeTriangleMesh();
    mesh.vertices[0].position = {-0.25f, -0.25f, 0.3f};
    mesh.vertices[1].position = {0.25f, -0.25f, 0.3f};
    mesh.vertices[2].position = {0.0f, 0.25f, 0.3f};

    Transform transform;
    transform.position = {0.45f, -0.1f, 0.0f};
    transform.scale = {1.8f, 1.8f, 1.0f};

    const Material material = makeLambertMaterial(makeSolidTexture(0xff00ffffu));
    renderer.drawMesh(mesh, transform, material);

    QVERIFY(renderer.colorAt(40, 34) == 0xff00ffffu);
    QVERIFY(renderer.colorAt(32, 32) == 0xff000000u);
}

void RendererTests::renderItemsBindMeshMaterialAndTransformPerInstance()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    Mesh mesh = makeTriangleMesh();
    mesh.vertices[0].position = {-0.35f, -0.35f, 0.3f};
    mesh.vertices[1].position = {0.35f, -0.35f, 0.3f};
    mesh.vertices[2].position = {0.0f, 0.35f, 0.3f};

    const Material redMaterial = makeLambertMaterial(makeSolidTexture(0xffff0000u));
    const Material greenMaterial = makeLambertMaterial(makeSolidTexture(0xff00ff00u));

    const std::vector<RenderItem> items = {
        {&mesh, &redMaterial, {{-0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.15f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &greenMaterial, {{0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, -0.15f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.drawRenderItems(items);

    QCOMPARE(renderer.stats().trianglesSubmitted, 2);
    QCOMPARE(renderer.stats().trianglesRasterized, 2);
    QCOMPARE(renderer.colorAt(18, 32), 0xffff0000u);
    QCOMPARE(renderer.colorAt(46, 32), 0xff00ff00u);
}

void RendererTests::nearerTriangleWinsDepthTest()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    const std::vector<VertexInput> farTriangle = {
        {{-0.9f, -0.9f, 0.8f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.9f, -0.9f, 0.8f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.9f, 0.8f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<VertexInput> nearTriangle = {
        {{-0.6f, -0.6f, 0.1f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.6f, -0.6f, 0.1f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.6f, 0.1f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(farTriangle, indices, Mat4f::identity(), material);
    renderer.drawIndexedTriangles(nearTriangle, indices, Mat4f::identity(), material);

    // 中心像素应当是近处绿色三角形，而不是远处红色三角形。
    QCOMPARE(renderer.colorAt(32, 32), 0xff00ff00u);
}

void RendererTests::backFaceCullingRejectsReversedWinding()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::Back);
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    const std::vector<VertexInput> vertices = {
        {{-0.8f, -0.8f, 0.3f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.8f, -0.8f, 0.3f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.8f, 0.3f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> frontFacing = {0, 1, 2};
    const std::vector<std::uint32_t> backFacing = {0, 2, 1};

    renderer.drawIndexedTriangles(vertices, frontFacing, Mat4f::identity(), material);
    renderer.drawIndexedTriangles(vertices, backFacing, Mat4f::identity(), material);

    QCOMPARE(renderer.stats().trianglesSubmitted, 2);
    QCOMPARE(renderer.stats().trianglesCulled, 1);
    QCOMPARE(renderer.stats().trianglesRasterized, 1);
    QCOMPARE(renderer.colorAt(32, 32), 0xffff0000u);
}

void RendererTests::nearPlaneClippingPreservesVisiblePortion()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    // 第三个点位于近平面外侧，裁剪后应形成一个四边形，再拆成两个三角形。
    const std::vector<VertexInput> vertices = {
        {{-0.8f, -0.8f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.8f, -0.8f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.8f, -1.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    QCOMPARE(renderer.stats().trianglesSubmitted, 1);
    QCOMPARE(renderer.stats().trianglesCulled, 0);
    QCOMPARE(renderer.stats().trianglesRasterized, 2);
    QVERIFY(renderer.stats().pixelsDrawn > 0);
    QCOMPARE(renderer.colorAt(32, 45), 0xffffff00u);
}

void RendererTests::customShadersOverrideDefaultPipelineBehavior()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    int vertexShaderCalls = 0;
    int fragmentShaderCalls = 0;
    Material material = Material::makeLambertTextured();
    material.vertexShader = [&vertexShaderCalls](const VertexInput &vertex,
                                                 const VertexShaderContext &context,
                                                 const Material &material) {
        ++vertexShaderCalls;
        Q_UNUSED(material);
        return VertexOutput{
            context.transform * Vec4f{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f},
            {0.2f, 0.4f, 1.0f},
            vertex.normal,
            vertex.uv,
            vertex.position,
            {0.0f, 0.0f, 1.0f}
        };
    };
    material.fragmentShader = [&fragmentShaderCalls](const FragmentInput &fragment, const Material &material, const LightingContext &lighting) {
        ++fragmentShaderCalls;
        Q_UNUSED(fragment);
        Q_UNUSED(material);
        Q_UNUSED(lighting);
        return FragmentOutput{{1.0f, 0.0f, 1.0f}, 1.0f, false};
    };

    const std::vector<VertexInput> vertices = {
        {{-0.8f, -0.8f, 0.3f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.8f, -0.8f, 0.3f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.8f, 0.3f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    QCOMPARE(vertexShaderCalls, 3);
    QVERIFY(fragmentShaderCalls > 0);
    QCOMPARE(renderer.colorAt(32, 32), 0xffff00ffu);
}

void RendererTests::defaultVertexShaderInterpolatesWorldPosAndViewDir()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    material.fragmentShader = [](const FragmentInput &fragment, const Material &material, const LightingContext &lighting) {
        Q_UNUSED(material);
        Q_UNUSED(lighting);
        const float worldDepth = std::clamp((-fragment.worldPos.z - 1.0f) * 0.5f, 0.0f, 1.0f);
        const float facing = std::clamp(fragment.viewDir.z, 0.0f, 1.0f);
        return FragmentOutput{{worldDepth, facing, 0.0f}, 1.0f, false};
    };

    Scene scene;
    scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    scene.clearColor = 0xff000000u;
    scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.renderScene(scene);

    const std::uint32_t color = renderer.colorAt(32, 32);
    QVERIFY(std::abs(redChannel(color) - 128) <= 20);
    QVERIFY(greenChannel(color) >= 245);
    QCOMPARE(blueChannel(color), 0);
}

void RendererTests::textureFilterSwitchChangesSamplingResult()
{
    const Texture2D texture = Texture2D::makeCheckerboard(2, 2, 1, 0xffff0000u, 0xff00ff00u);
    const Vec3f nearest = texture.sample({0.5f, 0.5f}, {TextureFilter::Nearest, AddressMode::Wrap, AddressMode::Wrap});
    const Vec3f bilinear = texture.sample({0.5f, 0.5f}, {TextureFilter::Bilinear, AddressMode::Wrap, AddressMode::Wrap});

    QVERIFY(nearlyEqual(nearest.x, 1.0f));
    QVERIFY(nearlyEqual(nearest.y, 0.0f));
    QVERIFY(nearlyEqual(bilinear.x, 0.5f));
    QVERIFY(nearlyEqual(bilinear.y, 0.5f));
    QVERIFY(nearlyEqual(bilinear.z, 0.0f));
}

void RendererTests::textureTrilinearFilterUsesMipLevels()
{
    Texture2D texture;
    texture.width = 4;
    texture.height = 4;
    texture.texels = {
        0xffff0000u, 0xff00ff00u, 0xffff0000u, 0xff00ff00u,
        0xff00ff00u, 0xffff0000u, 0xff00ff00u, 0xffff0000u,
        0xffff0000u, 0xff00ff00u, 0xffff0000u, 0xff00ff00u,
        0xff00ff00u, 0xffff0000u, 0xff00ff00u, 0xffff0000u
    };
    texture.rebuildMipChain();

    const Vec4f baseLevel = texture.sampleColor({0.125f, 0.125f}, {TextureFilter::Trilinear, AddressMode::Clamp, AddressMode::Clamp}, 0.0f);
    const Vec4f mipLevel = texture.sampleColor({0.125f, 0.125f}, {TextureFilter::Trilinear, AddressMode::Clamp, AddressMode::Clamp}, 2.0f);

    QVERIFY(baseLevel.x > 0.9f);
    QVERIFY(baseLevel.y < 0.1f);
    QVERIFY(std::abs(mipLevel.x - 0.5f) < 0.1f);
    QVERIFY(std::abs(mipLevel.y - 0.5f) < 0.1f);
}

void RendererTests::textureAddressModeSwitchChangesSamplingResult()
{
    Texture2D texture;
    texture.width = 2;
    texture.height = 1;
    texture.texels = {0xffff0000u, 0xff00ff00u};

    const Vec3f wrapped = texture.sample({-0.2f, 0.0f}, {TextureFilter::Nearest, AddressMode::Wrap, AddressMode::Clamp});
    const Vec3f clamped = texture.sample({-0.2f, 0.0f}, {TextureFilter::Nearest, AddressMode::Clamp, AddressMode::Clamp});

    QVERIFY(nearlyEqual(wrapped.x, 0.0f));
    QVERIFY(nearlyEqual(wrapped.y, 1.0f));
    QVERIFY(nearlyEqual(clamped.x, 1.0f));
    QVERIFY(nearlyEqual(clamped.y, 0.0f));
}

void RendererTests::antiAliasingCoverage4xProducesIntermediateEdgePixels()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    RenderState state;
    state.depthTestEnable = false;
    state.depthWriteEnable = false;
    state.cullMode = CullMode::None;
    state.antiAliasing = AntiAliasingMode::Coverage4x;
    renderer.setRenderState(state);
    renderer.clear(0xff000000u);

    Material material = Material::makeUnlitTextured();
    material.texture = makeSolidTexture(0xffff0000u);

    const std::vector<VertexInput> vertices = {
        {{-0.92f, -0.83f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.81f, -0.71f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.18f, 0.88f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    bool foundIntermediateRed = false;
    for (int y = 0; y < renderer.height() && !foundIntermediateRed; ++y) {
        for (int x = 0; x < renderer.width(); ++x) {
            const int red = redChannel(renderer.colorAt(x, y));
            if (red > 0 && red < 255) {
                foundIntermediateRed = true;
                break;
            }
        }
    }

    QVERIFY(foundIntermediateRed);
}

void RendererTests::antiAliasingCoverage4xResolvesPerSampleOpaqueColors()
{
    SoftwareRenderer renderer;
    renderer.resize(2, 2);

    RenderState state;
    state.depthTestEnable = false;
    state.depthWriteEnable = false;
    state.cullMode = CullMode::None;
    state.antiAliasing = AntiAliasingMode::Coverage4x;
    renderer.setRenderState(state);
    renderer.clear(0xff000000u);

    Material greenMaterial = Material::makeUnlitTextured();
    greenMaterial.texture = makeSolidTexture(0xff00ff00u);

    Material redMaterial = Material::makeUnlitTextured();
    redMaterial.texture = makeSolidTexture(0xffff0000u);

    const std::vector<VertexInput> leftHalfTriangle = {
        {{-2.0f, -2.0f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.0f, 2.0f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, -2.0f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };

    const std::vector<VertexInput> rightHalfTriangle = {
        {{0.0f, -2.0f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{2.0f, -2.0f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 2.0f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(rightHalfTriangle, indices, Mat4f::identity(), greenMaterial);
    renderer.drawIndexedTriangles(leftHalfTriangle, indices, Mat4f::identity(), redMaterial);

    QCOMPARE(renderer.colorAt(0, 0), 0xff808000u);
}

void RendererTests::wireframeAntiAliasingCoverage4xProducesIntermediateEdgePixels()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    RenderState state;
    state.depthTestEnable = false;
    state.depthWriteEnable = false;
    state.cullMode = CullMode::None;
    state.fillMode = FillMode::Wireframe;
    state.antiAliasing = AntiAliasingMode::Coverage4x;
    renderer.setRenderState(state);
    renderer.clear(0xff000000u);

    Material material = Material::makeUnlitTextured();
    material.texture = makeSolidTexture(0xffff0000u);

    const std::vector<VertexInput> vertices = {
        {{-0.92f, -0.83f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.81f, -0.71f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.18f, 0.88f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    bool foundIntermediateRed = false;
    for (int y = 0; y < renderer.height() && !foundIntermediateRed; ++y) {
        for (int x = 0; x < renderer.width(); ++x) {
            const int red = redChannel(renderer.colorAt(x, y));
            if (red > 0 && red < 255) {
                foundIntermediateRed = true;
                break;
            }
        }
    }

    QVERIFY(foundIntermediateRed);
}

void RendererTests::defaultLambertTextureMaterialUsesTextureSample()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);
    const Material material = makeLambertMaterial(makeSolidTexture(0xff00ff00u));

    const std::vector<VertexInput> vertices = {
        {{-0.7f, -0.7f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.7f, -0.7f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.7f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}}
    };
    const std::vector<std::uint32_t> indices = {0, 1, 2};

    renderer.drawIndexedTriangles(vertices, indices, Mat4f::identity(), material);

    QCOMPARE(renderer.colorAt(32, 32), 0xff00ff00u);
}

void RendererTests::normalMapChangesLambertLighting()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    Mesh leftMesh = makeTriangleMesh();
    Mesh rightMesh = makeTriangleMesh();
    for (VertexInput &vertex : leftMesh.vertices)
        vertex.color = {1.0f, 1.0f, 1.0f};
    for (VertexInput &vertex : rightMesh.vertices)
        vertex.color = {1.0f, 1.0f, 1.0f};

    Material baseMaterial = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    Material normalMappedMaterial = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    normalMappedMaterial.normalTexture = makeSolidNormalTexture({1.0f, 0.0f, 0.0f});
    normalMappedMaterial.normalStrength = 1.0f;

    const std::vector<RenderItem> items = {
        {&leftMesh, &baseMaterial, {{-0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&rightMesh, &normalMappedMaterial, {{0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.drawRenderItems(items);

    QVERIFY(redChannel(renderer.colorAt(18, 32)) > 240);
    QVERIFY(redChannel(renderer.colorAt(46, 32)) < 32);
}

void RendererTests::lightingContextAccumulatesDirectionalLights()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    Scene scene;
    scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    scene.clearColor = 0xff000000u;
    scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    scene.lighting.directionalLights = {
        {normalize(Vec3f{0.0f, 0.0f, -1.0f}), {1.0f, 1.0f, 1.0f}, 0.5f, 0.0f},
        {normalize(Vec3f{0.0f, 0.0f, -1.0f}), {1.0f, 1.0f, 1.0f}, 0.5f, 0.0f}
    };

    renderer.renderScene(scene);
    QCOMPARE(renderer.colorAt(32, 32), 0xffffffffu);
}

void RendererTests::lightingContextAccumulatesPointLights()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    Scene scene;
    scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    scene.clearColor = 0xff000000u;
    scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    scene.lighting.directionalLights.clear();
    scene.lighting.pointLights = {
        makePointLight({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 8.0f, 6.0f, 0.0f),
        makePointLight({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 8.0f, 6.0f, 0.0f)
    };

    renderer.renderScene(scene);

    const std::uint32_t color = renderer.colorAt(32, 32);
    QVERIFY(redChannel(color) > 0);
    QVERIFY(greenChannel(color) > 0);
    QCOMPARE(blueChannel(color), 0);
}

void RendererTests::pointLightAttenuationRespondsToDistance()
{
    const Mesh mesh = makeTriangleMesh();
    const Material material = makeLambertMaterial(makeSolidTexture(0xffffffffu));

    Scene nearScene;
    nearScene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    nearScene.clearColor = 0xff000000u;
    nearScene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };
    nearScene.lighting.directionalLights.clear();
    nearScene.lighting.pointLights = {
        makePointLight({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 8.0f, 6.0f, 0.0f)
    };

    Scene farScene = nearScene;
    farScene.lighting.pointLights = {
        makePointLight({0.0f, 0.0f, 3.0f}, {1.0f, 1.0f, 1.0f}, 8.0f, 6.0f, 0.0f)
    };

    SoftwareRenderer nearRenderer;
    nearRenderer.resize(64, 64);
    nearRenderer.renderScene(nearScene);

    SoftwareRenderer farRenderer;
    farRenderer.resize(64, 64);
    farRenderer.renderScene(farScene);

    QVERIFY(redChannel(nearRenderer.colorAt(32, 32)) > redChannel(farRenderer.colorAt(32, 32)));
}

void RendererTests::blinnPhongMaterialAddsSpecularHighlight()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    const Mesh leftMesh = makeTriangleMesh();
    const Mesh rightMesh = makeTriangleMesh();

    Material lambertMaterial = makeLambertMaterial(makeSolidTexture(0xff000000u));
    Material blinnPhongMaterial = makeBlinnPhongMaterial(makeSolidTexture(0xff000000u));
    blinnPhongMaterial.specularColor = {1.0f, 0.0f, 0.0f};
    blinnPhongMaterial.specularStrength = 1.0f;
    blinnPhongMaterial.shininess = 32.0f;

    const std::vector<RenderItem> items = {
        {&leftMesh, &lambertMaterial, {{-0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&rightMesh, &blinnPhongMaterial, {{0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.drawRenderItems(items);

    QCOMPARE(renderer.colorAt(18, 32), 0xff000000u);
    QVERIFY(redChannel(renderer.colorAt(46, 32)) > 0);
    QCOMPARE(greenChannel(renderer.colorAt(46, 32)), 0);
    QCOMPARE(blueChannel(renderer.colorAt(46, 32)), 0);
}

void RendererTests::pbrMaterialRespondsToMetallicAndRoughness()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    LightingContext lighting;
    lighting.directionalLights = {
        {normalize(Vec3f{-0.2f, 0.0f, -1.0f}), {1.0f, 1.0f, 1.0f}, 1.6f, 0.0f}
    };
    renderer.setLightingContext(lighting);

    const Mesh leftMesh = makeTriangleMesh();
    const Mesh rightMesh = makeTriangleMesh();

    Material glossyMaterial = Material::makePbrTextured();
    glossyMaterial.texture = makeSolidTexture(0xff808080u);
    glossyMaterial.metallic = 1.0f;
    glossyMaterial.roughness = 0.08f;

    Material roughMaterial = glossyMaterial;
    roughMaterial.roughness = 1.0f;

    const std::vector<RenderItem> items = {
        {&leftMesh, &glossyMaterial, {{-0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&rightMesh, &roughMaterial, {{0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.drawRenderItems(items);

    QVERIFY(std::abs(redChannel(renderer.colorAt(18, 32)) - redChannel(renderer.colorAt(46, 32))) >= 5);
}

void RendererTests::texturedMaterialUsesTextureAlphaForTransparency()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);

    const Mesh mesh = makeTriangleMesh();
    Material material = makeLambertMaterial(makeSolidTexture(0x80ff0000u));
    material.surfaceMode = MaterialSurfaceMode::AlphaBlend;

    Scene scene;
    scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    scene.clearColor = 0xff0000ffu;
    scene.items = {
        {&mesh, &material, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.renderScene(scene);

    QCOMPARE(renderer.colorAt(32, 32), 0xff80007fu);
}

void RendererTests::materialTypeChangesFragmentBehaviorPerObject()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.clear(0xff000000u);
    renderer.setCullMode(CullMode::None);

    Mesh leftMesh = makeTriangleMesh();
    for (VertexInput &vertex : leftMesh.vertices)
        vertex.color = {0.0f, 0.0f, 1.0f};

    Mesh rightMesh = makeTriangleMesh();
    for (VertexInput &vertex : rightMesh.vertices)
        vertex.color = {1.0f, 1.0f, 1.0f};

    Material leftMaterial = Material::makeUnlitVertexColor();
    Material rightMaterial = Material::makeUnlitTextured();
    rightMaterial.texture = makeSolidTexture(0xffff0000u);

    const std::vector<RenderItem> items = {
        {&leftMesh, &leftMaterial, {{-0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&rightMesh, &rightMaterial, {{0.45f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.drawRenderItems(items);

    QCOMPARE(renderer.colorAt(18, 32), 0xff0000ffu);
    QCOMPARE(renderer.colorAt(46, 32), 0xffff0000u);
}

void RendererTests::transparentObjectsSortBackToFrontPerMaterial()
{
    SoftwareRenderer renderer;
    renderer.resize(64, 64);
    renderer.setCullMode(CullMode::None);

    const Mesh mesh = makeTriangleMesh();

    Material nearMaterial = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    nearMaterial.surfaceMode = MaterialSurfaceMode::AlphaBlend;
    nearMaterial.opacity = 0.5f;
    nearMaterial.fragmentShader = [](const FragmentInput &fragment, const Material &material, const LightingContext &lighting) {
        Q_UNUSED(fragment);
        Q_UNUSED(material);
        Q_UNUSED(lighting);
        return FragmentOutput{{0.0f, 1.0f, 0.0f}, 1.0f, false};
    };

    Material farMaterial = makeLambertMaterial(makeSolidTexture(0xffffffffu));
    farMaterial.surfaceMode = MaterialSurfaceMode::AlphaBlend;
    farMaterial.opacity = 0.5f;
    farMaterial.fragmentShader = [](const FragmentInput &fragment, const Material &material, const LightingContext &lighting) {
        Q_UNUSED(fragment);
        Q_UNUSED(material);
        Q_UNUSED(lighting);
        return FragmentOutput{{1.0f, 0.0f, 0.0f}, 1.0f, false};
    };

    Scene scene;
    scene.camera = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f * 3.14159265358979323846f / 180.0f, 0.1f, 20.0f};
    scene.clearColor = 0xff000000u;
    scene.items = {
        {&mesh, &nearMaterial, {{0.0f, 0.0f, -1.6f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}},
        {&mesh, &farMaterial, {{0.0f, 0.0f, -2.2f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}
    };

    renderer.renderScene(scene);

    QCOMPARE(renderer.colorAt(32, 32), 0xff408000u);
}

void RendererTests::objLoaderTriangulatesFacesAndKeepsAttributes()
{
    const std::string obj = R"(# quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1 4/4/1
)";

    Mesh mesh;
    std::string errorMessage;
    QVERIFY2(Mesh::loadObjFromText(obj, mesh, &errorMessage), errorMessage.c_str());

    QCOMPARE(mesh.vertices.size(), std::size_t(6));
    QCOMPARE(mesh.indices.size(), std::size_t(6));
    QCOMPARE(mesh.indices[0], 0u);
    QCOMPARE(mesh.indices[5], 5u);
    QCOMPARE(mesh.vertices[0].position.x, 0.0f);
    QCOMPARE(mesh.vertices[1].uv.x, 1.0f);
    QCOMPARE(mesh.vertices[2].uv.y, 1.0f);
    QCOMPARE(mesh.vertices[3].position.x, 0.0f);
    QCOMPARE(mesh.vertices[5].uv.y, 1.0f);
    QVERIFY(nearlyEqual(mesh.vertices[0].normal.z, 1.0f));
    QVERIFY(nearlyEqual(mesh.vertices[4].normal.z, 1.0f));
}

void RendererTests::objLoaderGeneratesFaceNormalsWhenMissing()
{
    const std::string obj = R"(
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)";

    Mesh mesh;
    std::string errorMessage;
    QVERIFY2(Mesh::loadObjFromText(obj, mesh, &errorMessage), errorMessage.c_str());

    QCOMPARE(mesh.vertices.size(), std::size_t(3));
    QVERIFY(nearlyEqual(mesh.vertices[0].normal.x, 0.0f));
    QVERIFY(nearlyEqual(mesh.vertices[1].normal.y, 0.0f));
    QVERIFY(nearlyEqual(mesh.vertices[2].normal.z, 1.0f));
    QVERIFY(nearlyEqual(mesh.vertices[0].uv.x, 0.0f));
    QVERIFY(nearlyEqual(mesh.vertices[0].uv.y, 0.0f));
}

void RendererTests::objLoaderReadsMeshFromFile()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString filePath = temporaryDir.filePath(QStringLiteral("triangle.obj"));
    std::ofstream file(filePath.toStdString(), std::ios::out | std::ios::trunc);
    QVERIFY(file.is_open());
    file << "v -0.5 0 0\n";
    file << "v 0.5 0 0\n";
    file << "v 0 1 0\n";
    file << "vt 0 0\n";
    file << "vt 1 0\n";
    file << "vt 0.5 1\n";
    file << "f 1/1 2/2 3/3\n";
    file.close();

    Mesh mesh;
    std::string errorMessage;
    QVERIFY2(Mesh::loadObjFromFile(filePath.toStdString(), mesh, &errorMessage), errorMessage.c_str());

    QCOMPARE(mesh.vertices.size(), std::size_t(3));
    QCOMPARE(mesh.indices.size(), std::size_t(3));
    QVERIFY(nearlyEqual(mesh.vertices[0].position.x, -0.5f));
    QVERIFY(nearlyEqual(mesh.vertices[1].uv.x, 1.0f));
    QVERIFY(nearlyEqual(mesh.vertices[2].normal.z, 1.0f));
}

void RendererTests::parallelRasterMatchesSerialOutput()
{
    const Scene scene = makeStressScene();

    SoftwareRenderer serialRenderer;
    serialRenderer.resize(640, 480);
    serialRenderer.setParallelRasterEnabled(false);
    serialRenderer.renderScene(scene);

    SoftwareRenderer parallelRenderer;
    parallelRenderer.resize(640, 480);
    parallelRenderer.setParallelRasterEnabled(true);
    parallelRenderer.setWorkerThreadCount(4);
    parallelRenderer.setParallelThresholds(1, 1);
    parallelRenderer.setParallelTilesPerTask(2);
    parallelRenderer.renderScene(scene);

    QVERIFY(colorBuffersEqual(serialRenderer, parallelRenderer));
    QCOMPARE(parallelRenderer.stats().trianglesSubmitted, serialRenderer.stats().trianglesSubmitted);
    QCOMPARE(parallelRenderer.stats().trianglesRasterized, serialRenderer.stats().trianglesRasterized);
    QCOMPARE(parallelRenderer.stats().pixelsDrawn, serialRenderer.stats().pixelsDrawn);
    QVERIFY(parallelRenderer.parallelStats().parallelTaskCount > 0);
    QVERIFY(parallelRenderer.parallelStats().taskCount > 0);
}

void RendererTests::parallelRasterTileSizeIsConfigurable()
{
    const Scene scene = makeStressScene();

    SoftwareRenderer rendererA;
    rendererA.resize(640, 480);
    rendererA.setParallelRasterEnabled(true);
    rendererA.setWorkerThreadCount(4);
    rendererA.setRasterTileSize(8);
    rendererA.setParallelThresholds(1, 1);
    rendererA.setParallelTilesPerTask(2);
    rendererA.renderScene(scene);

    SoftwareRenderer rendererB;
    rendererB.resize(640, 480);
    rendererB.setParallelRasterEnabled(true);
    rendererB.setWorkerThreadCount(4);
    rendererB.setRasterTileSize(32);
    rendererB.setParallelThresholds(1, 1);
    rendererB.setParallelTilesPerTask(4);
    rendererB.renderScene(scene);

    QCOMPARE(rendererA.stats().trianglesSubmitted, rendererB.stats().trianglesSubmitted);
    QCOMPARE(rendererA.stats().trianglesRasterized, rendererB.stats().trianglesRasterized);
    QVERIFY(rendererA.stats().pixelsDrawn > 0);
    QVERIFY(rendererB.stats().pixelsDrawn > 0);
    QVERIFY(rendererA.parallelStats().tileCount > 0);
    QVERIFY(rendererB.parallelStats().tileCount > 0);
    QVERIFY(rendererA.parallelStats().tileSize != rendererB.parallelStats().tileSize);
    QVERIFY(rendererA.parallelStats().parallelTaskCount > 0);
    QVERIFY(rendererB.parallelStats().parallelTaskCount > 0);
}

void RendererTests::parallelRasterStatsReportDispatchBehavior()
{
    const Scene scene = makeStressScene(1, 1);

    SoftwareRenderer serialFallbackRenderer;
    serialFallbackRenderer.resize(320, 240);
    serialFallbackRenderer.setParallelRasterEnabled(true);
    serialFallbackRenderer.setWorkerThreadCount(4);
    serialFallbackRenderer.setParallelThresholds(10000, 1000000);
    serialFallbackRenderer.renderScene(scene);

    QVERIFY(serialFallbackRenderer.parallelStats().serialTaskCount > 0);
    QVERIFY(serialFallbackRenderer.parallelStats().skippedParallelDispatchCount > 0);

    SoftwareRenderer parallelRenderer;
    parallelRenderer.resize(640, 480);
    parallelRenderer.setParallelRasterEnabled(true);
    parallelRenderer.setWorkerThreadCount(4);
    parallelRenderer.setParallelThresholds(1, 1);
    parallelRenderer.setParallelTilesPerTask(1);
    parallelRenderer.renderScene(makeStressScene());

    QVERIFY(parallelRenderer.parallelStats().parallelTaskCount > 0);
    QVERIFY(parallelRenderer.parallelStats().taskCount >= 1);
    QVERIFY(parallelRenderer.parallelStats().parallelTileCount > 0);
}

QTEST_APPLESS_MAIN(RendererTests)

#include "tst_renderer.moc"
