#ifndef RASTERWIDGET_H
#define RASTERWIDGET_H

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include "SoftwareRenderer.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QFocusEvent;
class QShowEvent;
class QHideEvent;
class QPainter;

// RenderPreset 收拢常用状态组合，便于 UI 一键切换。
// 改变它会同时切换深度测试、剔除、填充模式和调试视图等渲染状态。
enum class RenderPreset {
    Custom,        // 自定义状态；表示当前渲染状态已被手动修改，不再严格对应某个预设。
    Shaded,        // 常规着色预设；切换后使用实体填充、背面剔除和正常光照显示模型。
    Wireframe,     // 线框预设；切换后只显示三角形边线，便于检查拓扑和三角形划分。
    DepthDebug,    // 深度调试预设；切换后把深度缓冲可视化，便于观察前后遮挡关系。
    NormalDebug,   // 法线调试预设；切换后把法线方向编码成颜色，便于检查法线是否正确。
    UvDebug,       // UV 调试预设；切换后把 UV 坐标编码成颜色，便于检查纹理展开是否正常。
    OverdrawDebug  // 过度绘制调试预设；切换后突出重复绘制区域，便于观察像素是否被多次覆盖。
};

// DemoTexturePreset 提供几种内置纹理，方便直接观察采样和光照差异。
// 改变它会替换当前模型使用的演示纹理，但不会修改几何体和渲染流程本身。
enum class DemoTexturePreset {
    WarmChecker,   // 暖色棋盘纹理；切换后便于观察纹理重复、过滤和受光面的颜色变化。
    MonoChecker,   // 黑白棋盘纹理；切换后对比更强，便于检查采样精度和锯齿情况。
    White,         // 纯白纹理；切换后基本只看光照结果，弱化纹理本身对观察的干扰。
    Gradient       // 渐变纹理；切换后便于观察 UV 连续性、插值是否平滑以及采样方向。
};

// ScenePreset 把相机、材质和渲染状态组合成一键切换的观察场景。
// 改变它会同时调整相机、材质参数和部分渲染状态，用于快速进入特定观察目的。
enum class ScenePreset {
    Custom,            // 自定义场景；表示当前相机、材质或渲染设置被手动改动，不再对应固定场景预设。
    DefaultOrbit,      // 默认观察场景；切换后恢复常规相机和基础着色，适合整体浏览模型。
    TextureStudy,      // 纹理观察场景；切换后强调纹理与采样效果，便于检查过滤、寻址和贴图表现。
    LightingStudy,     // 光照观察场景；切换后强调光照参数和明暗变化，便于观察法线与光照计算结果。
    WireframeInspect,  // 线框检查场景；切换后进入线框显示，便于检查网格结构和三角形分布。
    UvInspect,         // UV 检查场景；切换后进入 UV 调试视图，便于检查纹理坐标展开是否合理。
    OverdrawInspect    // 过度绘制检查场景；切换后进入 overdraw 调试，便于观察重复光栅化区域。
};

// CameraAxisView 提供前后左右上下六个轴向观察方向。
enum class CameraAxisView {
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom
};

// GizmoSpaceMode 决定对象 gizmo 使用世界坐标轴还是对象本地坐标轴。
enum class GizmoSpaceMode {
    World,
    Local
};

// LineArtThresholdCurvePreset 决定边缘强度如何映射到最终线条。
enum class LineArtThresholdCurvePreset {
    Balanced,
    Soft,
    Strong
};

// LineArtEdgeMode 决定提取轮廓时更偏向标准、细线还是细节。
enum class LineArtEdgeMode {
    Standard,
    Thin,
    Detail
};

// Tone Mapping 决定高亮颜色如何压回显示设备可见范围。
enum class ToneMappingMode {
    None,
    Reinhard,
    AcesApprox
};

// RasterWidget 是 Qt 和纯 C++ 渲染器之间的唯一桥接层。
class RasterWidget : public QWidget
{
    Q_OBJECT

public:
    enum class SelectedLightKind {
        None,
        Directional,
        Point,
        Spot
    };

    enum class ViewportHandleKind {
        None,
        SceneObject,
        DirectionalLight,
        PointLight,
        SpotLight
    };

    enum class ViewportAxis {
        None,
        X,
        Y,
        Z
    };

    enum class ViewportHandleOperation {
        None,
        Translate,
        Rotate,
        Scale,
        Direction
    };

    // 创建控件并启动按帧刷新的定时器。
    explicit RasterWidget(QWidget *parent = nullptr);
    // 应用一组场景预设。
    void applyScenePreset(ScenePreset preset);
    // 读取当前场景预设。
    ScenePreset scenePreset() const;
    // 应用一组常用渲染状态预设。
    void applyRenderPreset(RenderPreset preset);
    // 切换当前调试视图。
    void setDebugView(DebugView view);
    // 读取当前调试视图。
    DebugView debugView() const;
    // 切换当前填充模式。
    void setFillMode(FillMode mode);
    // 读取当前填充模式。
    FillMode fillMode() const;
    // 切换当前剔除模式。
    void setCullMode(CullMode mode);
    // 读取当前剔除模式。
    CullMode cullMode() const;
    // 切换当前深度比较模式。
    void setDepthFunc(DepthFunc func);
    // 读取当前深度比较模式。
    DepthFunc depthFunc() const;
    // 切换当前抗锯齿模式。
    void setAntiAliasingMode(AntiAliasingMode mode);
    // 读取当前抗锯齿模式。
    AntiAliasingMode antiAliasingMode() const;
    // 开关并行光栅化。
    void setParallelRasterEnabled(bool enabled);
    // 读取并行光栅化开关。
    bool parallelRasterEnabled() const;
    // 设置工作线程请求数；-1 表示自动，0 表示关闭线程池。
    void setParallelWorkerThreadCount(int count);
    // 读取工作线程请求数。
    int parallelWorkerThreadCount() const;
    // 读取当前实际工作线程数。
    int parallelActiveWorkerThreadCount() const;
    // 设置 tile 尺寸。
    void setParallelTileSize(int size);
    // 读取 tile 尺寸。
    int parallelTileSize() const;
    // 设置进入并行路径的最小 tile 数。
    void setParallelMinTileCount(int count);
    // 读取进入并行路径的最小 tile 数。
    int parallelMinTileCount() const;
    // 设置进入并行路径的最小像素数。
    void setParallelMinPixelCount(int count);
    // 读取进入并行路径的最小像素数。
    int parallelMinPixelCount() const;
    // 设置每个并行任务的最大 tile 数。
    void setParallelTilesPerTask(int count);
    // 读取每个并行任务的最大 tile 数。
    int parallelTilesPerTask() const;
    // 读取当前帧的并行调度统计。
    ParallelRasterStats parallelRasterStats() const;
    // 设置 demo 材质的采样过滤模式。
    void setTextureFilter(TextureFilter filter);
    // 读取 demo 材质的采样过滤模式。
    TextureFilter textureFilter() const;
    // 设置当前对象的材质类型。
    void setMaterialType(MaterialType type);
    // 读取当前对象的材质类型。
    MaterialType materialType() const;
    // 设置当前对象的材质表面模式。
    void setMaterialSurfaceMode(MaterialSurfaceMode mode);
    // 读取当前对象的材质表面模式。
    MaterialSurfaceMode materialSurfaceMode() const;
    // 设置当前对象的材质不透明度。
    void setMaterialOpacity(float opacity);
    // 读取当前对象的材质不透明度。
    float materialOpacity() const;
    // 设置当前对象是否写入深度。
    void setMaterialDepthWriteEnable(bool enable);
    // 读取当前对象是否写入深度。
    bool materialDepthWriteEnable() const;
    // 为当前选中的对象从磁盘加载一张外部纹理。
    bool loadTextureForSelectedObject(const QString &path, QString *errorMessage = nullptr);
    // 清除当前选中对象的外部纹理来源，恢复使用内置预设纹理。
    void clearSelectedObjectExternalTexture();
    // 读取当前选中对象的外部纹理路径；为空表示当前仍使用内置预设纹理。
    QString selectedObjectTexturePath() const;
    // 读取当前选中对象颜色纹理资产在资产列表中的下标。
    int selectedObjectTextureAssetIndex() const;
    // 为当前选中的对象从磁盘加载一张法线贴图。
    bool loadNormalTextureForSelectedObject(const QString &path, QString *errorMessage = nullptr);
    // 清除当前选中对象的外部法线贴图。
    void clearSelectedObjectExternalNormalTexture();
    // 读取当前选中对象的法线贴图路径。
    QString selectedObjectNormalTexturePath() const;
    // 读取当前选中对象法线纹理资产在资产列表中的下标。
    int selectedObjectNormalTextureAssetIndex() const;
    // 为当前选中的对象从磁盘加载一张 metallic/roughness 贴图。
    bool loadMetallicRoughnessTextureForSelectedObject(const QString &path, QString *errorMessage = nullptr);
    // 清除当前选中对象的 metallic/roughness 贴图。
    void clearSelectedObjectExternalMetallicRoughnessTexture();
    // 读取当前选中对象的 metallic/roughness 贴图路径。
    QString selectedObjectMetallicRoughnessTexturePath() const;
    // 读取当前选中对象 metallic/roughness 纹理资产在资产列表中的下标。
    int selectedObjectMetallicRoughnessTextureAssetIndex() const;
    // 切换当前 demo 纹理预设。
    void setDemoTexturePreset(DemoTexturePreset preset);
    // 读取当前 demo 纹理预设。
    DemoTexturePreset demoTexturePreset() const;
    // 设置 demo 材质 U 方向地址模式。
    void setAddressModeU(AddressMode mode);
    // 读取 demo 材质 U 方向地址模式。
    AddressMode addressModeU() const;
    // 设置 demo 材质 V 方向地址模式。
    void setAddressModeV(AddressMode mode);
    // 读取 demo 材质 V 方向地址模式。
    AddressMode addressModeV() const;
    // 设置法线贴图过滤模式。
    void setNormalTextureFilter(TextureFilter filter);
    // 读取法线贴图过滤模式。
    TextureFilter normalTextureFilter() const;
    // 设置法线贴图 U 方向地址模式。
    void setNormalAddressModeU(AddressMode mode);
    // 读取法线贴图 U 方向地址模式。
    AddressMode normalAddressModeU() const;
    // 设置法线贴图 V 方向地址模式。
    void setNormalAddressModeV(AddressMode mode);
    // 读取法线贴图 V 方向地址模式。
    AddressMode normalAddressModeV() const;
    // 设置 metallic/roughness 贴图过滤模式。
    void setMetallicRoughnessTextureFilter(TextureFilter filter);
    // 读取 metallic/roughness 贴图过滤模式。
    TextureFilter metallicRoughnessTextureFilter() const;
    // 设置 metallic/roughness 贴图 U 方向地址模式。
    void setMetallicRoughnessAddressModeU(AddressMode mode);
    // 读取 metallic/roughness 贴图 U 方向地址模式。
    AddressMode metallicRoughnessAddressModeU() const;
    // 设置 metallic/roughness 贴图 V 方向地址模式。
    void setMetallicRoughnessAddressModeV(AddressMode mode);
    // 读取 metallic/roughness 贴图 V 方向地址模式。
    AddressMode metallicRoughnessAddressModeV() const;
    // 设置 demo 光方向。
    void setLightDirection(const Vec3f &direction);
    // 读取 demo 光方向。
    Vec3f lightDirection() const;
    // 设置 demo 光环境光强。
    void setLightAmbient(float ambient);
    // 读取 demo 光环境光强。
    float lightAmbient() const;
    // 设置 demo 光漫反射强度。
    void setLightIntensity(float intensity);
    // 读取 demo 光漫反射强度。
    float lightIntensity() const;
    // 读取当前场景里的方向光数量。
    int directionalLightCount() const;
    // 读取一个方向光的显示名称。
    QString directionalLightName(int index) const;
    // 重命名一个方向光。
    void setDirectionalLightName(int index, const QString &name);
    // 读取一个方向光的参数；越界时返回默认方向光。
    DirectionalLight directionalLight(int index) const;
    // 向场景追加一个方向光。
    void addDirectionalLight();
    // 复制一个方向光。
    void duplicateDirectionalLight(int index);
    // 删除一个方向光。
    void removeDirectionalLight(int index);
    // 把一个方向光转换成点光源。
    void convertDirectionalLightToPoint(int index);
    // 设置方向光是否启用。
    void setDirectionalLightEnabled(int index, bool enabled);
    // 设置方向光方向。
    void setDirectionalLightDirection(int index, const Vec3f &direction);
    // 设置方向光颜色。
    void setDirectionalLightColor(int index, const Vec3f &color);
    // 设置方向光环境光强度。
    void setDirectionalLightAmbient(int index, float ambient);
    // 设置方向光强度。
    void setDirectionalLightIntensity(int index, float intensity);
    // 设置方向光阴影开关。
    void setDirectionalLightShadowCastEnable(int index, bool enable);
    // 设置方向光阴影强度。
    void setDirectionalLightShadowStrength(int index, float strength);
    // 设置方向光阴影偏移。
    void setDirectionalLightShadowBias(int index, float bias);
    // 设置方向光阴影分辨率。
    void setDirectionalLightShadowMapSize(int index, int size);
    // 设置方向光阴影覆盖范围。
    void setDirectionalLightShadowCoverage(int index, float coverage);
    // 设置方向光阴影质量。
    void setDirectionalLightShadowFilterQuality(int index, ShadowFilterQuality quality);
    // 读取当前场景里的点光源数量。
    int pointLightCount() const;
    // 读取一个点光源的显示名称。
    QString pointLightName(int index) const;
    // 重命名一个点光源。
    void setPointLightName(int index, const QString &name);
    // 读取一个点光源的当前参数；越界时返回默认点光。
    PointLight pointLight(int index) const;
    // 往当前场景追加一个点光源。
    void addPointLight();
    // 复制一个点光源。
    void duplicatePointLight(int index);
    // 删除指定下标的点光源。
    void removePointLight(int index);
    // 把一个点光源转换成方向光。
    void convertPointLightToDirectional(int index);
    // 设置点光源是否启用。
    void setPointLightEnabled(int index, bool enabled);
    // 设置指定点光源的位置。
    void setPointLightPosition(int index, const Vec3f &position);
    // 设置指定点光源的环境光强度。
    void setPointLightAmbient(int index, float ambient);
    // 设置指定点光源的颜色。
    void setPointLightColor(int index, const Vec3f &color);
    // 设置指定点光源的强度。
    void setPointLightIntensity(int index, float intensity);
    // 设置指定点光源的影响范围。
    void setPointLightRange(int index, float range);
    // 设置点光阴影开关。
    void setPointLightShadowCastEnable(int index, bool enable);
    // 设置点光阴影强度。
    void setPointLightShadowStrength(int index, float strength);
    // 设置点光阴影偏移。
    void setPointLightShadowBias(int index, float bias);
    // 设置点光阴影分辨率。
    void setPointLightShadowMapSize(int index, int size);
    // 设置点光阴影覆盖范围。
    void setPointLightShadowRange(int index, float range);
    // 设置点光阴影质量。
    void setPointLightShadowFilterQuality(int index, ShadowFilterQuality quality);
    // 读取当前场景里的聚光灯数量。
    int spotLightCount() const;
    // 读取一个聚光灯名称。
    QString spotLightName(int index) const;
    // 重命名一个聚光灯。
    void setSpotLightName(int index, const QString &name);
    // 读取一个聚光灯参数。
    SpotLight spotLight(int index) const;
    // 追加聚光灯。
    void addSpotLight();
    // 复制聚光灯。
    void duplicateSpotLight(int index);
    // 删除聚光灯。
    void removeSpotLight(int index);
    // 灯光类型互转。
    void convertDirectionalLightToSpot(int index);
    void convertPointLightToSpot(int index);
    void convertSpotLightToDirectional(int index);
    void convertSpotLightToPoint(int index);
    // 设置聚光灯是否启用。
    void setSpotLightEnabled(int index, bool enabled);
    // 设置聚光灯位置。
    void setSpotLightPosition(int index, const Vec3f &position);
    // 设置聚光灯方向。
    void setSpotLightDirection(int index, const Vec3f &direction);
    // 设置聚光灯环境光。
    void setSpotLightAmbient(int index, float ambient);
    // 设置聚光灯颜色。
    void setSpotLightColor(int index, const Vec3f &color);
    // 设置聚光灯强度。
    void setSpotLightIntensity(int index, float intensity);
    // 设置聚光灯范围。
    void setSpotLightRange(int index, float range);
    // 设置聚光灯内锥角。
    void setSpotLightInnerConeDegrees(int index, float degrees);
    // 设置聚光灯外锥角。
    void setSpotLightOuterConeDegrees(int index, float degrees);
    // 设置聚光灯阴影开关。
    void setSpotLightShadowCastEnable(int index, bool enable);
    // 设置聚光灯阴影强度。
    void setSpotLightShadowStrength(int index, float strength);
    // 设置聚光灯阴影偏移。
    void setSpotLightShadowBias(int index, float bias);
    // 设置聚光灯阴影分辨率。
    void setSpotLightShadowMapSize(int index, int size);
    // 设置聚光灯阴影覆盖范围。
    void setSpotLightShadowRange(int index, float range);
    // 设置聚光灯阴影质量。
    void setSpotLightShadowFilterQuality(int index, ShadowFilterQuality quality);
    // 设置当前对象材质的高光颜色。
    void setSpecularColor(const Vec3f &color);
    // 读取当前对象材质的高光颜色。
    Vec3f specularColor() const;
    // 设置当前对象材质的高光强度。
    void setSpecularStrength(float strength);
    // 读取当前对象材质的高光强度。
    float specularStrength() const;
    // 设置当前对象材质的高光指数。
    void setShininess(float shininess);
    // 读取当前对象材质的高光指数。
    float shininess() const;
    // 设置当前对象法线贴图强度。
    void setNormalStrength(float strength);
    // 读取当前对象法线贴图强度。
    float normalStrength() const;
    // 设置当前对象材质金属度。
    void setMetallic(float metallic);
    // 读取当前对象材质金属度。
    float metallic() const;
    // 设置当前对象材质粗糙度。
    void setRoughness(float roughness);
    // 读取当前对象材质粗糙度。
    float roughness() const;
    // 设置当前对象是否开启方向光阴影。
    void setShadowCastEnable(bool enable);
    // 读取当前对象是否开启方向光阴影。
    bool shadowCastEnable() const;
    // 设置当前对象阴影强度。
    void setShadowStrength(float strength);
    // 读取当前对象阴影强度。
    float shadowStrength() const;
    // 设置当前对象阴影偏移。
    void setShadowBias(float bias);
    // 读取当前对象阴影偏移。
    float shadowBias() const;
    // 从磁盘加载一个 OBJ 模型，并切换当前场景显示内容。
    bool loadObjModel(const QString &path, QString *errorMessage = nullptr);
    // 从磁盘读取一张照片并生成线稿图显示。
    bool loadPhotoLineArt(const QString &path, QString *errorMessage = nullptr);
    // 把当前线稿图保存到磁盘；如果当前不是线稿模式则返回 false。
    bool saveLineArtImage(const QString &path, QString *errorMessage = nullptr) const;
    // 把当前透明背景线稿保存到磁盘；如果当前不是线稿模式则返回 false。
    bool saveTransparentLineArtImage(const QString &path, QString *errorMessage = nullptr) const;
    // 把当前视口内容保存成截图；3D 模式下包含 gizmo 和相机覆盖层。
    bool saveViewportScreenshot(const QString &path, QString *errorMessage = nullptr);
    // 让相机绕当前观察目标输出一组环绕序列图。
    bool exportOrbitSequence(const QString &outputDirectory,
                             int frameCount,
                             float orbitDegrees,
                             QString *errorMessage = nullptr);
    // 把当前场景支持的调试视图批量导出到目录。
    bool exportDebugViews(const QString &outputDirectory, QString *errorMessage = nullptr);
    // 当前是否正在显示照片线稿。
    bool isLineArtMode() const;
    // 开关最终显示阶段的后处理。
    void setPostProcessEnabled(bool enabled);
    // 读取后处理开关。
    bool postProcessEnabled() const;
    // 设置 tone mapping 模式。
    void setToneMappingMode(ToneMappingMode mode);
    // 读取 tone mapping 模式。
    ToneMappingMode toneMappingMode() const;
    // 设置曝光。
    void setPostExposure(float exposure);
    // 读取曝光。
    float postExposure() const;
    // 设置显示 gamma。
    void setPostGamma(float gamma);
    // 读取显示 gamma。
    float postGamma() const;
    // 设置对比度。
    void setPostContrast(float contrast);
    // 读取对比度。
    float postContrast() const;
    // 设置饱和度。
    void setPostSaturation(float saturation);
    // 读取饱和度。
    float postSaturation() const;
    // 设置线稿阈值倍率；越大越少线条。
    void setLineArtThresholdScale(float scale);
    // 读取线稿阈值倍率。
    float lineArtThresholdScale() const;
    // 设置线稿线条强度；越大线条越黑。
    void setLineArtLineStrength(float strength);
    // 读取线稿线条强度。
    float lineArtLineStrength() const;
    // 设置线稿预处理缩放比例；越小越平滑、越快。
    void setLineArtProcessScale(float scale);
    // 读取线稿预处理缩放比例。
    float lineArtProcessScale() const;
    // 设置是否保留灰度底图。
    void setLineArtKeepGrayBase(bool keepGrayBase);
    // 读取是否保留灰度底图。
    bool lineArtKeepGrayBase() const;
    // 设置是否启用原图/线稿实时对比预览。
    void setLineArtComparePreview(bool comparePreview);
    // 读取是否启用原图/线稿实时对比预览。
    bool lineArtComparePreview() const;
    // 设置线稿阈值曲线预设。
    void setLineArtThresholdCurvePreset(LineArtThresholdCurvePreset preset);
    // 读取线稿阈值曲线预设。
    LineArtThresholdCurvePreset lineArtThresholdCurvePreset() const;
    // 设置线稿边缘提取模式。
    void setLineArtEdgeMode(LineArtEdgeMode mode);
    // 读取线稿边缘提取模式。
    LineArtEdgeMode lineArtEdgeMode() const;
    // 设置透明线稿描边粗细。
    void setLineArtTransparentStrokeWidth(float width);
    // 读取透明线稿描边粗细。
    float lineArtTransparentStrokeWidth() const;
    // 导出当前线稿参数为独立配置。
    QJsonObject saveLineArtConfig() const;
    // 从独立配置恢复线稿参数。
    bool loadLineArtConfig(const QJsonObject &config, QString *errorMessage = nullptr);
    // 按当前线稿参数批量导出多张图片。
    bool batchExportLineArt(const QStringList &inputPaths,
                            const QString &outputDirectory,
                            QString *errorMessage = nullptr) const;
    // 往当前场景追加一个内置立方体对象。
    void addDemoCubeObject();
    // 恢复到内置演示立方体。
    void useDemoCube();
    // 删除当前选中的对象。
    void removeSelectedSceneObject();
    // 当前是否正在显示外部加载的模型。
    bool hasLoadedModel() const;
    // 当前是否有任何对象可供编辑。
    bool hasSceneObjects() const;
    // 当前外部模型的完整路径；未加载时为空。
    QString loadedModelPath() const;
    // 当前显示模型的名字；未加载时返回示例立方体名称。
    QString loadedModelName() const;
    // 当前场景中的对象数量。
    int sceneObjectCount() const;
    // 读取指定对象的显示名称。
    QString sceneObjectName(int index) const;
    // 重命名当前选中的对象。
    void renameSelectedSceneObject(const QString &name);
    // 读取当前选中的对象索引。
    int selectedSceneObjectIndex() const;
    // 切换当前选中的对象。
    void setSelectedSceneObjectIndex(int index);
    // 读取当前选中对象的变换。
    Transform selectedSceneObjectTransform() const;
    // 设置当前选中对象的变换。
    void setSelectedSceneObjectTransform(const Transform &transform);
    // 切换对象 gizmo 使用世界坐标还是本地坐标。
    void setGizmoSpaceMode(GizmoSpaceMode mode);
    // 读取当前 gizmo 坐标模式。
    GizmoSpaceMode gizmoSpaceMode() const;
    // 设置平移吸附步长。
    void setGizmoTranslationSnapStep(float step);
    // 读取平移吸附步长。
    float gizmoTranslationSnapStep() const;
    // 设置旋转吸附步长（角度）。
    void setGizmoRotationSnapDegrees(float degrees);
    // 读取旋转吸附步长（角度）。
    float gizmoRotationSnapDegrees() const;
    // 设置缩放吸附步长。
    void setGizmoScaleSnapStep(float step);
    // 读取缩放吸附步长。
    float gizmoScaleSnapStep() const;
    // 设置当前由属性面板驱动的 gizmo 高亮。
    void setInspectorGizmoHighlight(int handleKind, int operation, int axis);
    // 切换相机投影模式。
    void setCameraProjectionMode(CameraProjectionMode mode);
    // 读取相机投影模式。
    CameraProjectionMode cameraProjectionMode() const;
    // 设置相机垂直视场角（角度）。
    void setCameraVerticalFovDegrees(float degrees);
    // 读取相机垂直视场角（角度）。
    float cameraVerticalFovDegrees() const;
    // 设置正交投影视口高度。
    void setCameraOrthographicHeight(float height);
    // 读取正交投影视口高度。
    float cameraOrthographicHeight() const;
    // 设置近裁剪面。
    void setCameraNearPlane(float nearPlane);
    // 读取近裁剪面。
    float cameraNearPlane() const;
    // 设置远裁剪面。
    void setCameraFarPlane(float farPlane);
    // 读取远裁剪面。
    float cameraFarPlane() const;
    // 设置自由飞行移动速度。
    void setCameraMoveSpeed(float speed);
    // 读取自由飞行移动速度。
    float cameraMoveSpeed() const;
    // 把相机重置到默认观察位置。
    void resetCameraView();
    // 切到指定轴向观察方向。
    void setCameraAxisView(CameraAxisView view);
    // 同步当前在 UI 里选中的灯光，供视口高亮和 gizmo 使用。
    void setSelectedLightSelection(SelectedLightKind kind, int index);
    // 导出当前相机预设，供独立保存。
    QJsonObject saveCameraPreset() const;
    // 从独立相机预设恢复当前相机。
    bool loadCameraPreset(const QJsonObject &preset, QString *errorMessage = nullptr);
    // 把当前场景保存到磁盘，并把资源路径整理到场景目录。
    bool saveSceneToFile(const QString &path, QString *errorMessage = nullptr);
    // 从场景文件加载当前场景，并按场景目录解析相对资源路径。
    bool loadSceneFromFile(const QString &path, QString *errorMessage = nullptr);
    // 导出当前场景状态，供保存到文件。
    QJsonObject saveSceneState() const;
    // 从场景状态对象恢复当前场景。
    bool loadSceneState(const QJsonObject &state, QString *errorMessage = nullptr);
    // 当前选中对象是否正在引用共享材质资产。
    bool selectedObjectUsesMaterialAsset() const;
    // 读取当前对象材质显示名称。
    QString selectedObjectMaterialDisplayName() const;
    // 当前材质资产数量。
    int materialAssetCount() const;
    // 读取一个材质资产名称。
    QString materialAssetName(int index) const;
    // 读取一个共享材质被多少个对象引用。
    int materialAssetUsageCount(int index) const;
    // 当前纹理资产数量。
    int textureAssetCount() const;
    // 读取一个纹理资产名称。
    QString textureAssetName(int index) const;
    // 读取一个纹理资产被场景对象实际使用的次数。
    int textureAssetUsageCount(int index) const;
    // 当前对象引用的材质资产下标；若不是共享材质则返回 -1。
    int selectedObjectMaterialAssetIndex() const;
    // 让当前对象改为使用独立材质实例。
    void makeSelectedObjectUseMaterialInstance();
    // 用当前对象材质复制出一个新的共享材质资产并绑定回对象。
    void duplicateSelectedMaterialAsAsset();
    // 复制一个已有共享材质资产。
    bool duplicateMaterialAsset(int index);
    // 让当前对象引用一个已有材质资产。
    void assignSelectedObjectMaterialAsset(int index);
    // 让所有对象统一引用一个共享材质资产，返回实际变更对象数。
    int assignMaterialAssetToAllObjects(int index);
    // 把当前对象的颜色纹理切到一个已有纹理资产。
    bool assignSelectedObjectTextureAsset(int index);
    // 把当前对象的法线贴图切到一个已有纹理资产。
    bool assignSelectedObjectNormalTextureAsset(int index);
    // 把当前对象的金属粗糙贴图切到一个已有纹理资产。
    bool assignSelectedObjectMetallicRoughnessTextureAsset(int index);
    // 把一个纹理资产应用到所有对象的颜色纹理槽，返回变更对象数。
    int assignTextureAssetToAllObjectsColor(int index);
    // 复制一个已有纹理资产。
    bool duplicateTextureAsset(int index);
    // 重命名一个纹理资产。
    bool renameTextureAsset(int index, const QString &name);
    // 删除一个纹理资产，并清空所有引用它的纹理槽。
    bool removeTextureAsset(int index);
    // 删除当前场景里没有对象实际使用的纹理资产，返回删除数量。
    int removeUnusedTextureAssets();
    // 重命名一个共享材质资产。
    bool renameMaterialAsset(int index, const QString &name);
    // 删除一个共享材质资产；仍在引用它的对象会自动固化为独立材质。
    bool removeMaterialAsset(int index);
    // 删除当前场景里没有对象引用的共享材质资产，返回删除数量。
    int removeUnusedMaterialAssets();
    // 把当前对象材质另存为材质资产文件。
    bool saveSelectedMaterialAssetToFile(const QString &path, QString *errorMessage = nullptr) const;
    // 从文件加载一个材质资产，并绑定到当前对象。
    bool loadMaterialAssetFromFile(const QString &path, QString *errorMessage = nullptr);
    // 把 demo 材质整体恢复到默认状态。
    void resetDemoMaterial();

signals:
    // 每帧渲染结束后把统计信息抛给外层 UI。
    void frameReady(const RenderStats &stats);
    // 当前场景模型切换后通知外层 UI 刷新文字。
    void sceneContentChanged();
    // 当前相机状态改变后通知外层 UI 同步预设显示。
    void cameraChanged();
    // 视口里选中了一个灯光后通知外层 UI 同步灯光面板选中项；第一个参数为 SelectedLightKind 的整数值。
    void lightPicked(int lightKind, int index);
    // gizmo 交互状态改变后通知外层 UI 同步高亮和历史分组。
    void gizmoInteractionChanged(bool active, int handleKind, int operation, int axis);

protected:
    // 将当前 color buffer 贴到窗口上。
    void paintEvent(QPaintEvent *event) override;
    // 窗口大小变化时重建 framebuffer。
    void resizeEvent(QResizeEvent *event) override;
    // 鼠标按下时开始 gizmo 选择/拖拽，或进入相机交互。
    void mousePressEvent(QMouseEvent *event) override;
    // 鼠标拖动时更新 gizmo 或相机。
    void mouseMoveEvent(QMouseEvent *event) override;
    // 鼠标松开时结束当前 gizmo 或相机交互。
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 滚轮沿观察方向推进或拉远相机。
    void wheelEvent(QWheelEvent *event) override;
    // 键盘按下时记录自由飞行状态。
    void keyPressEvent(QKeyEvent *event) override;
    // 键盘松开时清理自由飞行状态。
    void keyReleaseEvent(QKeyEvent *event) override;
    // 失焦时清空自由飞行按键，避免持续漂移。
    void focusOutEvent(QFocusEvent *event) override;
    // 显示控件时按当前状态决定是否恢复连续刷新。
    void showEvent(QShowEvent *event) override;
    // 隐藏控件时暂停连续刷新，避免后台空转。
    void hideEvent(QHideEvent *event) override;

private slots:
    // 触发一帧 CPU 渲染。
    void renderFrame();

public:
    struct ViewportHandleHit {
        ViewportHandleKind kind = ViewportHandleKind::None;
        int index = -1;
        ViewportAxis axis = ViewportAxis::None;
        ViewportHandleOperation operation = ViewportHandleOperation::None;
        QPointF screenPosition;
        float viewDepth = 0.0f;
    };

private:
    struct PostProcessSettings {
        bool enabled = false;
        ToneMappingMode toneMapping = ToneMappingMode::Reinhard;
        float exposure = 1.0f;
        float gamma = 2.2f;
        float contrast = 1.0f;
        float saturation = 1.0f;
    };

    struct TextureAssetEntry {
        int id = 0;
        QString displayName;
        QString sourcePath;
        QString relativePath;
        Texture2D texture;
    };

    struct MaterialBinding {
        Material material;
        DemoTexturePreset texturePreset = DemoTexturePreset::WarmChecker;
        int textureAssetId = -1;
        int normalTextureAssetId = -1;
        int metallicRoughnessTextureAssetId = -1;
    };

    struct MaterialAssetEntry {
        int id = 0;
        QString displayName;
        QString sourcePath;
        MaterialBinding binding;
    };

    struct SceneObjectEntry {
        Mesh mesh;
        Transform transform;
        MaterialBinding materialInstance;
        bool useMaterialAsset = false;
        int materialAssetId = -1;
        QString sourcePath;
        QString sourceText;
        QString displayName;
        bool isDemoCube = false;
    };

    // 读取当前选中对象正在编辑的材质绑定；若对象引用共享资产则返回资产绑定。
    MaterialBinding *selectedEditableMaterialBinding();
    // 读取当前选中对象正在编辑的材质绑定；若对象引用共享资产则返回资产绑定。
    const MaterialBinding *selectedEditableMaterialBinding() const;
    // 把一份材质绑定解析成真正可提交给 renderer 的材质。
    Material resolveMaterialBinding(const MaterialBinding &binding) const;
    // 读取指定 id 的纹理资产。
    TextureAssetEntry *textureAssetById(int id);
    // 读取指定 id 的纹理资产。
    const TextureAssetEntry *textureAssetById(int id) const;
    // 读取指定 id 的材质资产。
    MaterialAssetEntry *materialAssetById(int id);
    // 读取指定 id 的材质资产。
    const MaterialAssetEntry *materialAssetById(int id) const;
    // 从图片文件注册一个纹理资产并返回 id。
    bool registerTextureAssetFromFile(const QString &path,
                                      const QString &suggestedName,
                                      int &assetId,
                                      QString *errorMessage = nullptr);
    // 用当前对象材质创建一个新的共享材质资产。
    int createMaterialAssetFromBinding(const MaterialBinding &binding,
                                       const QString &suggestedName,
                                       const QString &sourcePath = QString());
    // 根据场景路径把绝对路径转换为相对资源路径。
    QString relativizePathForScene(const QString &absolutePath, const QString &sceneFilePath) const;
    // 根据场景路径把相对资源路径还原成绝对路径。
    QString resolveSceneRelativePath(const QString &storedPath, const QString &sceneFilePath) const;
    // 把纹理资产序列化为 JSON。
    QJsonObject textureAssetObject(const TextureAssetEntry &asset, const QString &sceneFilePath) const;
    // 从 JSON 恢复一个纹理资产。
    bool loadTextureAssetObject(const QJsonObject &object,
                                const QString &sceneFilePath,
                                TextureAssetEntry &asset,
                                QString *errorMessage = nullptr);
    // 把材质绑定序列化为 JSON。
    QJsonObject materialBindingObject(const MaterialBinding &binding) const;
    // 从 JSON 恢复一个材质绑定。
    bool loadMaterialBindingObject(const QJsonObject &object,
                                   MaterialBinding &binding,
                                   QString *errorMessage = nullptr);
    // 把材质资产序列化为 JSON。
    QJsonObject materialAssetObject(const MaterialAssetEntry &asset, const QString &sceneFilePath) const;
    // 从 JSON 恢复一个材质资产。
    bool loadMaterialAssetObject(const QJsonObject &object,
                                 const QString &sceneFilePath,
                                 MaterialAssetEntry &asset,
                                 QString *errorMessage = nullptr);
    // 复制当前场景引用到的贴图资源到场景目录资源文件夹。
    bool exportSceneResources(const QString &sceneFilePath, QString *errorMessage = nullptr);
    // 读取当前选中的对象；如果没有对象则返回空指针。
    SceneObjectEntry *selectedSceneObject();
    // 读取当前选中的对象；如果没有对象则返回空指针。
    const SceneObjectEntry *selectedSceneObject() const;
    // 用当前参数从原始照片重新生成线稿。
    bool regenerateLineArt(QString *errorMessage = nullptr);
    // 读取当前相机参数为 JSON 对象。
    QJsonObject cameraPresetObject() const;
    // 应用一份相机参数配置。
    bool applyCameraPresetObject(const QJsonObject &preset, QString *errorMessage = nullptr);
    // 读取当前线稿参数为 JSON 对象。
    QJsonObject lineArtConfigObject(bool includeRuntimeState) const;
    // 应用一份线稿参数配置。
    bool applyLineArtConfigObject(const QJsonObject &config,
                                  bool allowLoadSourceImage,
                                  QString *errorMessage = nullptr);
    // 把当前场景同步渲染到 renderer；force=true 时忽略脏标记直接重渲染。
    bool renderSceneNow(bool force);
    // 从 renderer 当前 framebuffer 生成最终显示图像。
    QImage buildRendererPresentationImage(bool includePostProcess);
    // 生成当前视口对外导出的完整图像。
    QImage buildViewportPresentationImage(bool includeOverlays);
    // 对一张图像执行 tone mapping / gamma / contrast / saturation。
    QImage applyPostProcessToImage(const QImage &source) const;
    // 判断当前调试视图是否应该应用后处理。
    bool shouldApplyPostProcessToCurrentView() const;
    // 退出线稿显示模式，恢复由 3D 场景驱动的绘制路径。
    void exitLineArtMode();
    // 把场景重置为单个内置立方体。
    void resetSceneToDemoCube();
    // 清空所有自由飞行按键状态。
    void clearCameraMoveState();
    // 按当前按键状态推进自由飞行相机。
    bool updateFreeFlyCamera(float deltaSeconds);
    // 在最终 framebuffer 之上叠加对象 gizmo 和选中高亮。
    void drawSceneObjectGizmos(QPainter &painter) const;
    // 在最终 framebuffer 之上叠加灯光 gizmo 和选中高亮。
    void drawLightGizmos(QPainter &painter) const;
    // 在视口内显示当前相机状态和快捷键提示。
    void drawCameraOverlay(QPainter &painter) const;
    // 读取当前 gizmo 某个轴在世界空间中的方向。
    Vec3f gizmoAxisDirection(const Transform *transform, ViewportAxis axis) const;
    // 把一个世界坐标点投影到视口并返回屏幕位置与视线深度。
    bool projectViewportHandle(const Vec3f &worldPosition, QPointF &screenPosition, float &viewDepth) const;
    // 根据鼠标位置寻找当前命中的对象/灯光 gizmo。
    ViewportHandleHit pickViewportHandle(const QPoint &mousePosition) const;
    // 根据命中的 gizmo 初始化拖拽状态。
    bool beginViewportHandleDrag(const ViewportHandleHit &hit, const QPoint &mousePosition);
    // 用当前鼠标位置更新 gizmo 拖拽结果。
    bool updateViewportHandleDrag(const QPoint &mousePosition);
    // 结束当前 gizmo 拖拽。
    void endViewportHandleDrag();
    // 请求一帧渲染；会自动合并同一事件循环内的重复请求。
    void requestRender();
    // 按当前是否存在动画/持续输入来启停渲染定时器。
    void updateRenderLoopState();
    // 当前是否需要连续渲染。
    bool needsContinuousRendering() const;
    // 当前场景里是否存在持续动画对象。
    bool hasAnimatedSceneObjects() const;
    // 定时器重新启动前同步时间基线，避免首帧 delta 跳变。
    void syncFrameClock();
    // 真正执行光栅化的纯 C++ 核心。
    SoftwareRenderer m_renderer;
    // 当前场景里的所有对象。
    std::vector<SceneObjectEntry> m_sceneObjects;
    // 当前场景里的纹理资产库。
    std::vector<TextureAssetEntry> m_textureAssets;
    // 当前场景里的材质资产库。
    std::vector<MaterialAssetEntry> m_materialAssets;
    // 下一个纹理资产 id。
    int m_nextTextureAssetId = 1;
    // 下一个材质资产 id。
    int m_nextMaterialAssetId = 1;
    // 当前场景文件路径。
    QString m_sceneFilePath;
    // 当前场景资源目录名称。
    QString m_sceneResourceDirectoryName = QStringLiteral("resources");
    // 当前场景级光照设置。
    LightingContext m_lightingContext;
    // 当前选中的对象索引。
    int m_selectedSceneObjectIndex;
    // gizmo 当前使用世界坐标还是本地坐标。
    GizmoSpaceMode m_gizmoSpaceMode;
    // 平移吸附步长。
    float m_gizmoTranslationSnapStep;
    // 旋转吸附步长（弧度）。
    float m_gizmoRotationSnapRadians;
    // 缩放吸附步长。
    float m_gizmoScaleSnapStep;
    // Ctrl 细调倍率。
    float m_gizmoFineTuneFactor;
    // 当前场景预设来源，供 UI 同步显示。
    ScenePreset m_scenePreset;
    // 当前在灯光面板里选中的灯光类型。
    SelectedLightKind m_selectedLightKind;
    // 当前在灯光面板里选中的同类型灯光下标。
    int m_selectedLightIndex;
    // 当前是否正在直接显示照片线稿，而不是 3D 场景。
    bool m_isLineArtMode;
    // 当前 3D 视口最终显示图缓存。
    QImage m_scenePresentationImage;
    // 最近一次生成的线稿图。
    QImage m_lineArtImage;
    // 最近一次生成的透明背景线稿图。
    QImage m_lineArtTransparentImage;
    // 最近一次导入的原始照片。
    QImage m_lineArtSourceImage;
    // 线稿图对应的源照片路径。
    QString m_lineArtSourcePath;
    // 线稿阈值倍率。
    float m_lineArtThresholdScale;
    // 线稿线条强度。
    float m_lineArtLineStrength;
    // 线稿处理缩放比例。
    float m_lineArtProcessScale;
    // 是否保留灰度底图。
    bool m_lineArtKeepGrayBase;
    // 是否启用原图/线稿对比预览。
    bool m_lineArtComparePreview;
    // 线稿阈值曲线预设。
    LineArtThresholdCurvePreset m_lineArtThresholdCurvePreset;
    // 线稿边缘提取模式。
    LineArtEdgeMode m_lineArtEdgeMode;
    // 透明背景线稿描边粗细。
    float m_lineArtTransparentStrokeWidth;
    // 分割线式对比预览的位置，0 表示最左，1 表示最右。
    float m_lineArtCompareSplit;
    // 当前是否正在拖动线稿分割线。
    bool m_isDraggingLineArtSplit;
    // 当前后处理配置。
    PostProcessSettings m_postProcessSettings;
    // 约 60 FPS 的简单刷新节拍器。
    QTimer m_timer;
    // 记录运行时长，用来给 demo 模型做旋转动画。
    QElapsedTimer m_elapsed;
    // 是否已经排队了一个延迟渲染请求，避免重复 single-shot。
    bool m_renderQueued;
    // 当前 3D 场景是否需要重新光栅化。
    bool m_sceneDirty;
    // 当前是否处于鼠标轨道旋转。
    bool m_isOrbiting;
    // 当前是否处于鼠标平移。
    bool m_isPanning;
    // 当前是否处于右键看向式 free-look。
    bool m_isFreeLooking;
    // 当前是否正在拖拽对象/灯光 gizmo。
    bool m_isDraggingViewportHandle;
    // 当前正在拖拽的 gizmo 类型。
    ViewportHandleKind m_dragHandleKind;
    // 当前正在拖拽的 gizmo 索引。
    int m_dragHandleIndex;
    // 当前正在拖拽的 gizmo 轴；None 表示自由平面拖拽。
    ViewportAxis m_dragHandleAxis;
    // 当前正在拖拽的 gizmo 操作类型。
    ViewportHandleOperation m_dragHandleOperation;
    // gizmo 拖拽起始鼠标位置。
    QPoint m_dragStartMousePosition;
    // gizmo 拖拽起始世界位置。
    Vec3f m_dragStartWorldPosition;
    // gizmo 拖拽起始视线深度。
    float m_dragStartViewDepth;
    // 轴向拖拽时 gizmo 的世界长度。
    float m_dragAxisLengthWorld;
    // gizmo 拖拽起始方向光方向。
    Vec3f m_dragStartDirection;
    // gizmo 拖拽起始对象旋转。
    Vec3f m_dragStartRotationRadians;
    // gizmo 拖拽起始对象缩放。
    Vec3f m_dragStartScale;
    // 当前由属性面板驱动的 gizmo 高亮类型。
    ViewportHandleKind m_inspectorHighlightKind;
    // 当前由属性面板驱动的 gizmo 高亮操作。
    ViewportHandleOperation m_inspectorHighlightOperation;
    // 当前由属性面板驱动的 gizmo 高亮轴。
    ViewportAxis m_inspectorHighlightAxis;
    // 最近一次相机交互的鼠标位置。
    QPoint m_lastMousePosition;
    // 上一帧的运行时刻，用来计算自由飞行位移。
    qint64 m_lastFrameElapsedMs;
    // 自由飞行基础速度。
    float m_cameraMoveSpeed;
    // WASD 自由飞行按键状态。
    bool m_moveForward;
    bool m_moveBackward;
    bool m_moveLeft;
    bool m_moveRight;
    bool m_moveUp;
    bool m_moveDown;
    bool m_moveFast;
    bool m_gizmoFineTune;
};

#endif // RASTERWIDGET_H
