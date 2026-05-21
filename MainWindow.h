#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "RasterWidget.h"
#include <QByteArray>
#include <QMainWindow>
#include <QVector>

class QAction;
class QEvent;
class QLabel;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QObject;
class QPushButton;
class QSpinBox;
class QTimer;

struct RenderStats;
struct ParallelRasterStats;

// 主窗口保持极简，只承载显示控件和底部状态信息。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 创建窗口并装配中心渲染控件。
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    // 监听属性控件焦点变化，用于和 gizmo 做双向高亮。
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // 接收每帧统计信息并同步到状态栏。
    void updateStatus(const RenderStats &stats);
    // 切换并行光栅化开关。
    void changeParallelRasterEnabled(bool checked);
    // 调整工作线程请求数。
    void changeParallelWorkerThreadCount(int value);
    // 调整 tile 尺寸。
    void changeParallelTileSize(int value);
    // 调整并行最小 tile 数门槛。
    void changeParallelMinTileCount(int value);
    // 调整并行最小像素门槛。
    void changeParallelMinPixelCount(int value);
    // 调整每个任务处理的 tile 数。
    void changeParallelTilesPerTask(int value);
    // 处理调试视图切换。
    void changeDebugView(int index);
    // 处理填充模式切换。
    void changeFillMode(int index);
    // 处理剔除模式切换。
    void changeCullMode(int index);
    // 处理深度比较模式切换。
    void changeDepthFunc(int index);
    // 处理抗锯齿模式切换。
    void changeAntiAliasing(int index);
    // 开关后处理。
    void changePostProcessEnabled(bool checked);
    // 切换 tone mapping 模式。
    void changeToneMappingMode(int index);
    // 调整曝光。
    void changePostExposure(double value);
    // 调整显示 gamma。
    void changePostGamma(double value);
    // 调整对比度。
    void changePostContrast(double value);
    // 调整饱和度。
    void changePostSaturation(double value);
    // 处理场景预设切换。
    void changeScenePreset(int index);
    // 处理渲染预设切换。
    void changeRenderPreset(int index);
    // 处理采样过滤切换。
    void changeTextureFilter(int index);
    // 处理材质类型切换。
    void changeMaterialType(int index);
    // 处理材质表面模式切换。
    void changeMaterialSurfaceMode(int index);
    // 切换当前对象材质是否引用共享材质资产。
    void changeMaterialUsageMode(int index);
    // 切换当前对象引用的共享材质资产。
    void changeSelectedMaterialAsset(int index);
    // 切换场景材质列表当前选中项。
    void changeSceneMaterialAsset(int index);
    // 切换场景纹理列表当前选中项。
    void changeSceneTextureAsset(int index);
    // 切换当前对象颜色纹理资产。
    void changeSelectedObjectTextureAsset(int index);
    // 切换当前对象法线纹理资产。
    void changeSelectedObjectNormalTextureAsset(int index);
    // 切换当前对象金属粗糙纹理资产。
    void changeSelectedObjectMetallicRoughnessTextureAsset(int index);
    // 把当前对象材质复制成共享材质资产。
    void duplicateSelectedMaterialAsAsset();
    // 仅把当前对象从共享材质复制为独立实例后继续编辑。
    void makeSelectedMaterialInstanceForEditing();
    // 复制场景材质列表当前选中的共享材质。
    void duplicateSceneMaterialAsset();
    // 复制场景纹理列表当前选中的纹理资产。
    void duplicateSceneTextureAsset();
    // 重命名当前选中的共享材质资产。
    void renameSelectedMaterialAsset();
    // 重命名当前选中的纹理资产。
    void renameSelectedTextureAsset();
    // 删除当前选中的共享材质资产。
    void removeSelectedMaterialAsset();
    // 删除当前选中的纹理资产。
    void removeSelectedTextureAsset();
    // 清理当前场景里没有对象引用的共享材质资产。
    void cleanupUnusedMaterialAssets();
    // 清理当前场景里没有对象使用的纹理资产。
    void cleanupUnusedTextureAssets();
    // 把场景材质列表当前选中的共享材质绑定到当前对象。
    void assignSceneMaterialAssetToSelectedObject();
    // 把场景材质列表当前选中的共享材质绑定到所有对象。
    void assignSceneMaterialAssetToAllObjects();
    // 把场景纹理绑定到当前对象颜色槽。
    void assignSceneTextureAssetToSelectedColor();
    // 把场景纹理绑定到当前对象法线槽。
    void assignSceneTextureAssetToSelectedNormal();
    // 把场景纹理绑定到当前对象金属粗糙槽。
    void assignSceneTextureAssetToSelectedMetallicRoughness();
    // 把场景纹理批量绑定到全部对象颜色槽。
    void assignSceneTextureAssetToAllObjectsColor();
    // 从文件加载一个材质资产并绑定给当前对象。
    void loadSelectedMaterialAsset();
    // 把当前对象材质另存为材质资产文件。
    void saveSelectedMaterialAsset();
    // 处理材质不透明度切换。
    void changeMaterialOpacity(double value);
    // 处理材质深度写入切换。
    void changeMaterialDepthWrite(bool checked);
    // 为当前选中的对象加载一张外部纹理。
    void loadSelectedObjectTexture();
    // 为当前选中的对象加载一张外部法线贴图。
    void loadSelectedObjectNormalTexture();
    // 为当前选中的对象加载一张外部 metallic/roughness 贴图。
    void loadSelectedObjectMetallicRoughnessTexture();
    // 处理 demo 纹理预设切换。
    void changeTexturePreset(int index);
    // 处理 U 地址模式切换。
    void changeAddressModeU(int index);
    // 处理 V 地址模式切换。
    void changeAddressModeV(int index);
    // 处理法线过滤模式切换。
    void changeNormalTextureFilter(int index);
    // 处理法线 U 地址模式切换。
    void changeNormalAddressModeU(int index);
    // 处理法线 V 地址模式切换。
    void changeNormalAddressModeV(int index);
    // 处理金属粗糙过滤模式切换。
    void changeMetallicRoughnessTextureFilter(int index);
    // 处理金属粗糙 U 地址模式切换。
    void changeMetallicRoughnessAddressModeU(int index);
    // 处理金属粗糙 V 地址模式切换。
    void changeMetallicRoughnessAddressModeV(int index);
    // 处理光方向编辑。
    void changeLightDirection();
    // 处理环境光强编辑。
    void changeLightAmbient(double value);
    // 处理漫反射强度编辑。
    void changeLightIntensity(double value);
    // 切换当前选中的灯光。
    void changeSelectedLight(int index);
    // 切换当前灯光类型。
    void changeSelectedLightType(int index);
    // 切换当前灯光启用状态。
    void changeLightEnabled(bool checked);
    // 向场景添加一个方向光。
    void addDirectionalLight();
    // 重命名当前选中的灯光。
    void renameSelectedLight();
    // 切换当前选中的点光源。
    void changeSelectedPointLight(int index);
    // 向场景添加一个点光源。
    void addPointLight();
    // 向场景添加一个聚光灯。
    void addSpotLight();
    // 复制当前选中的灯光。
    void duplicateSelectedLight();
    // 删除当前选中的灯光。
    void removeSelectedLight();
    // 删除当前选中的点光源。
    void removeSelectedPointLight();
    // 处理点光源位置编辑。
    void changePointLightPosition();
    // 处理点光源颜色编辑。
    void changePointLightColor();
    // 处理点光源强度编辑。
    void changePointLightIntensity(double value);
    // 处理点光源范围编辑。
    void changePointLightRange(double value);
    // 处理阴影分辨率编辑。
    void changeShadowMapSize(int value);
    // 处理阴影覆盖范围编辑。
    void changeShadowCoverage(double value);
    // 处理阴影质量编辑。
    void changeShadowFilterQuality(int index);
    // 处理聚光灯内锥角编辑。
    void changeSpotLightInnerCone(double value);
    // 处理聚光灯外锥角编辑。
    void changeSpotLightOuterCone(double value);
    // 处理高光颜色编辑。
    void changeSpecularColor();
    // 处理高光强度编辑。
    void changeSpecularStrength(double value);
    // 处理高光指数编辑。
    void changeShininess(double value);
    // 处理法线贴图强度编辑。
    void changeNormalStrength(double value);
    // 处理金属度编辑。
    void changeMetallic(double value);
    // 处理粗糙度编辑。
    void changeRoughness(double value);
    // 处理阴影开关编辑。
    void changeShadowCast(bool checked);
    // 处理阴影强度编辑。
    void changeShadowStrength(double value);
    // 处理阴影偏移编辑。
    void changeShadowBias(double value);
    // 从磁盘加载 OBJ 模型。
    void loadObjModel();
    // 从磁盘读取照片并生成线稿图。
    void loadPhotoLineArt();
    // 把当前线稿图保存到磁盘。
    void saveLineArt();
    // 把当前透明背景线稿保存到磁盘。
    void saveTransparentLineArt();
    // 把当前视口保存到磁盘。
    void saveViewportScreenshot();
    // 导出当前相机环绕序列。
    void exportViewportSequence();
    // 批量导出调试视图。
    void exportDebugViews();
    // 调整线稿阈值倍率。
    void changeLineArtThreshold(double value);
    // 调整线稿线条强度。
    void changeLineArtStrength(double value);
    // 调整线稿处理缩放比例。
    void changeLineArtProcessScale(int value);
    // 切换是否保留灰度底图。
    void changeLineArtGrayBase(bool checked);
    // 切换是否启用对比预览。
    void changeLineArtComparePreview(bool checked);
    // 切换线稿阈值曲线预设。
    void changeLineArtThresholdCurvePreset(int index);
    // 切换线稿边缘提取模式。
    void changeLineArtEdgeMode(int index);
    // 调整透明线稿描边粗细。
    void changeLineArtTransparentStrokeWidth(double value);
    // 批量导出线稿。
    void batchExportLineArt();
    // 保存线稿独立配置。
    void saveLineArtConfig();
    // 载入线稿独立配置。
    void loadLineArtConfig();
    // 把当前场景保存到文件。
    void saveScene();
    // 从文件恢复一个场景。
    void loadScene();
    // 往场景追加一个示例立方体对象。
    void addDemoCubeObject();
    // 重命名当前选中的对象。
    void renameSelectedObject();
    // 切换当前选中的对象。
    void changeSelectedObject(int index);
    // 编辑当前对象的变换。
    void changeSelectedObjectTransform();
    // 切换 gizmo 坐标模式。
    void changeGizmoSpaceMode(int index);
    // 调整 gizmo 平移吸附步长。
    void changeGizmoTranslationSnapStep(double value);
    // 调整 gizmo 旋转吸附步长。
    void changeGizmoRotationSnapDegrees(double value);
    // 调整 gizmo 缩放吸附步长。
    void changeGizmoScaleSnapStep(double value);
    // 删除当前选中的对象。
    void removeSelectedObject();
    // 恢复到内置立方体。
    void useDemoCube();
    // 保存当前相机为独立预设。
    void saveCameraPreset();
    // 加载独立相机预设。
    void loadCameraPreset();
    // 切换相机投影模式。
    void changeCameraProjectionMode(int index);
    // 调整相机垂直视场角。
    void changeCameraVerticalFov(double value);
    // 调整相机正交高度。
    void changeCameraOrthographicHeight(double value);
    // 调整相机近裁剪面。
    void changeCameraNearPlane(double value);
    // 调整相机远裁剪面。
    void changeCameraFarPlane(double value);
    // 调整相机自由飞行速度。
    void changeCameraMoveSpeed(double value);
    // 重置相机观察。
    void resetCameraView();
    // 切到前视图。
    void setCameraFrontView();
    // 切到后视图。
    void setCameraBackView();
    // 切到左视图。
    void setCameraLeftView();
    // 切到右视图。
    void setCameraRightView();
    // 切到俯视图。
    void setCameraTopView();
    // 切到底视图。
    void setCameraBottomView();
    // 撤销最近一次场景编辑。
    void undoSceneEdit();
    // 重做最近一次被撤销的场景编辑。
    void redoSceneEdit();
    // 把 demo 材质恢复到默认状态。
    void resetDemoMaterial();

private:
    // 中心区域的 framebuffer 展示控件。
    RasterWidget *m_rasterWidget;
    // 显示当前渲染分辨率。
    QLabel *m_resolutionLabel;
    // 显示当前帧的三角形和像素统计。
    QLabel *m_statsLabel;
    // 显示当前并行调度摘要。
    QLabel *m_parallelStatsLabel;
    // 显示当前正在观察的模型。
    QLabel *m_modelLabel;
    // 调试视图切换框。
    QComboBox *m_debugViewCombo;
    // 场景预设切换框。
    QComboBox *m_scenePresetCombo;
    // 对象选择框。
    QComboBox *m_objectCombo;
    // 当前对象名称编辑框。
    QLineEdit *m_objectNameEdit;
    // 对象面板 Dock。
    QDockWidget *m_objectDock;
    // 材质面板 Dock。
    QDockWidget *m_materialDock;
    // 灯光面板 Dock。
    QDockWidget *m_lightingDock;
    // 线稿面板 Dock。
    QDockWidget *m_lineArtDock;
    // 相机面板 Dock。
    QDockWidget *m_cameraDock;
    // 后处理 / 导出面板 Dock。
    QDockWidget *m_postProcessDock;
    // 性能 / 并行面板 Dock。
    QDockWidget *m_performanceDock;
    // 预设切换框。
    QComboBox *m_presetCombo;
    // 填充模式切换框。
    QComboBox *m_fillModeCombo;
    // 剔除模式切换框。
    QComboBox *m_cullModeCombo;
    // 深度比较模式切换框。
    QComboBox *m_depthFuncCombo;
    // 抗锯齿模式切换框。
    QComboBox *m_antiAliasingCombo;
    // 纹理过滤模式切换框。
    QComboBox *m_textureFilterCombo;
    // 材质类型切换框。
    QComboBox *m_materialTypeCombo;
    // 材质表面模式切换框。
    QComboBox *m_materialSurfaceModeCombo;
    // 当前对象纹理来源说明。
    QLabel *m_textureSourceLabel;
    // 当前对象材质来源说明。
    QLabel *m_materialSourceLabel;
    // 当前对象正在编辑共享材质时的提示容器。
    QWidget *m_sharedMaterialWarningPanel;
    // 当前对象正在编辑共享材质时的醒目提示。
    QLabel *m_sharedMaterialWarningLabel;
    // 当前对象法线贴图来源说明。
    QLabel *m_normalTextureSourceLabel;
    // 当前对象 metallic/roughness 贴图来源说明。
    QLabel *m_metallicRoughnessTextureSourceLabel;
    // metallic/roughness 通道约定说明。
    QLabel *m_metallicRoughnessChannelLabel;
    // demo 纹理预设切换框。
    QComboBox *m_texturePresetCombo;
    // 材质使用模式切换框。
    QComboBox *m_materialUsageModeCombo;
    // 共享材质资产选择框。
    QComboBox *m_materialAssetCombo;
    // 场景级共享材质列表。
    QComboBox *m_sceneMaterialAssetCombo;
    // 场景级共享材质引用计数。
    QLabel *m_sceneMaterialAssetUsageLabel;
    // 场景级纹理资产列表。
    QComboBox *m_sceneTextureAssetCombo;
    // 场景级纹理资产使用计数。
    QLabel *m_sceneTextureAssetUsageLabel;
    // 当前对象颜色纹理资产选择框。
    QComboBox *m_objectTextureAssetCombo;
    // 当前对象法线纹理资产选择框。
    QComboBox *m_objectNormalTextureAssetCombo;
    // 当前对象金属粗糙纹理资产选择框。
    QComboBox *m_objectMetallicRoughnessTextureAssetCombo;
    // U 地址模式切换框。
    QComboBox *m_addressModeUCombo;
    // V 地址模式切换框。
    QComboBox *m_addressModeVCombo;
    // 法线过滤模式切换框。
    QComboBox *m_normalTextureFilterCombo;
    // 法线 U 地址模式切换框。
    QComboBox *m_normalAddressModeUCombo;
    // 法线 V 地址模式切换框。
    QComboBox *m_normalAddressModeVCombo;
    // 金属粗糙过滤模式切换框。
    QComboBox *m_metallicRoughnessTextureFilterCombo;
    // 金属粗糙 U 地址模式切换框。
    QComboBox *m_metallicRoughnessAddressModeUCombo;
    // 金属粗糙 V 地址模式切换框。
    QComboBox *m_metallicRoughnessAddressModeVCombo;
    // 光方向 X 分量。
    QDoubleSpinBox *m_lightDirectionXSpin;
    // 光方向 Y 分量。
    QDoubleSpinBox *m_lightDirectionYSpin;
    // 光方向 Z 分量。
    QDoubleSpinBox *m_lightDirectionZSpin;
    // 灯光列表选择框。
    QComboBox *m_lightListCombo;
    // 当前灯光类型切换框。
    QComboBox *m_lightTypeCombo;
    // 当前灯光启用开关。
    QCheckBox *m_lightEnabledCheck;
    // 当前灯光名称编辑框。
    QLineEdit *m_lightNameEdit;
    // 环境光强。
    QDoubleSpinBox *m_lightAmbientSpin;
    // 漫反射强度。
    QDoubleSpinBox *m_lightIntensitySpin;
    // 添加方向光按钮。
    QPushButton *m_addDirectionalLightButton;
    // 点光源选择框。
    QComboBox *m_pointLightCombo;
    // 添加点光源按钮。
    QPushButton *m_addPointLightButton;
    // 添加聚光灯按钮。
    QPushButton *m_addSpotLightButton;
    // 复制灯光按钮。
    QPushButton *m_duplicateLightButton;
    // 删除灯光按钮。
    QPushButton *m_removeLightButton;
    // 点光源位置 X。
    QDoubleSpinBox *m_pointLightPositionXSpin;
    // 点光源位置 Y。
    QDoubleSpinBox *m_pointLightPositionYSpin;
    // 点光源位置 Z。
    QDoubleSpinBox *m_pointLightPositionZSpin;
    // 点光源颜色 R。
    QDoubleSpinBox *m_pointLightColorRSpin;
    // 点光源颜色 G。
    QDoubleSpinBox *m_pointLightColorGSpin;
    // 点光源颜色 B。
    QDoubleSpinBox *m_pointLightColorBSpin;
    // 点光源强度。
    QDoubleSpinBox *m_pointLightIntensitySpin;
    // 点光源范围。
    QDoubleSpinBox *m_pointLightRangeSpin;
    // 当前选中的灯光列表下标。
    int m_selectedLightIndex;
    // 高光颜色 R 分量。
    QDoubleSpinBox *m_specularColorRSpin;
    // 高光颜色 G 分量。
    QDoubleSpinBox *m_specularColorGSpin;
    // 高光颜色 B 分量。
    QDoubleSpinBox *m_specularColorBSpin;
    // 高光强度。
    QDoubleSpinBox *m_specularStrengthSpin;
    // 高光指数。
    QDoubleSpinBox *m_shininessSpin;
    // 法线强度。
    QDoubleSpinBox *m_normalStrengthSpin;
    // 金属度。
    QDoubleSpinBox *m_metallicSpin;
    // 粗糙度。
    QDoubleSpinBox *m_roughnessSpin;
    // 阴影强度。
    QDoubleSpinBox *m_shadowStrengthSpin;
    // 阴影偏移。
    QDoubleSpinBox *m_shadowBiasSpin;
    // 阴影分辨率。
    QSpinBox *m_shadowMapSizeSpin;
    // 阴影覆盖范围。
    QDoubleSpinBox *m_shadowCoverageSpin;
    // 阴影质量。
    QComboBox *m_shadowFilterQualityCombo;
    // 聚光灯内锥角。
    QDoubleSpinBox *m_spotInnerConeSpin;
    // 聚光灯外锥角。
    QDoubleSpinBox *m_spotOuterConeSpin;
    // 材质不透明度。
    QDoubleSpinBox *m_materialOpacitySpin;
    // 材质深度写入开关。
    QCheckBox *m_materialDepthWriteCheck;
    // 阴影开关。
    QCheckBox *m_shadowCastCheck;
    // 加载外部纹理按钮。
    QPushButton *m_loadTextureButton;
    // 加载外部法线贴图按钮。
    QPushButton *m_loadNormalTextureButton;
    // 加载外部 metallic/roughness 贴图按钮。
    QPushButton *m_loadMetallicRoughnessTextureButton;
    // 相机重置按钮。
    QPushButton *m_resetCameraButton;
    // 相机投影模式切换框。
    QComboBox *m_cameraProjectionCombo;
    // 相机垂直视场角。
    QDoubleSpinBox *m_cameraFovSpin;
    // 相机正交高度。
    QDoubleSpinBox *m_cameraOrthoHeightSpin;
    // 相机近裁剪面。
    QDoubleSpinBox *m_cameraNearSpin;
    // 相机远裁剪面。
    QDoubleSpinBox *m_cameraFarSpin;
    // 相机自由飞行速度。
    QDoubleSpinBox *m_cameraMoveSpeedSpin;
    // 保存相机预设按钮。
    QPushButton *m_saveCameraPresetButton;
    // 加载相机预设按钮。
    QPushButton *m_loadCameraPresetButton;
    // 相机前视图按钮。
    QPushButton *m_cameraFrontButton;
    // 相机后视图按钮。
    QPushButton *m_cameraBackButton;
    // 相机左视图按钮。
    QPushButton *m_cameraLeftButton;
    // 相机右视图按钮。
    QPushButton *m_cameraRightButton;
    // 相机俯视图按钮。
    QPushButton *m_cameraTopButton;
    // 相机底视图按钮。
    QPushButton *m_cameraBottomButton;
    // 位置 X 分量。
    QDoubleSpinBox *m_positionXSpin;
    // 位置 Y 分量。
    QDoubleSpinBox *m_positionYSpin;
    // 位置 Z 分量。
    QDoubleSpinBox *m_positionZSpin;
    // 旋转 X 分量（角度）。
    QDoubleSpinBox *m_rotationXSpin;
    // 旋转 Y 分量（角度）。
    QDoubleSpinBox *m_rotationYSpin;
    // 旋转 Z 分量（角度）。
    QDoubleSpinBox *m_rotationZSpin;
    // 缩放 X 分量。
    QDoubleSpinBox *m_scaleXSpin;
    // 缩放 Y 分量。
    QDoubleSpinBox *m_scaleYSpin;
    // 缩放 Z 分量。
    QDoubleSpinBox *m_scaleZSpin;
    // gizmo 坐标模式切换框。
    QComboBox *m_gizmoSpaceCombo;
    // gizmo 平移吸附步长。
    QDoubleSpinBox *m_gizmoTranslationSnapSpin;
    // gizmo 旋转吸附步长。
    QDoubleSpinBox *m_gizmoRotationSnapSpin;
    // gizmo 缩放吸附步长。
    QDoubleSpinBox *m_gizmoScaleSnapSpin;
    // 添加立方体按钮。
    QPushButton *m_addCubeButton;
    // 删除当前对象按钮。
    QPushButton *m_removeObjectButton;
    // 重置材质按钮。
    QPushButton *m_resetMaterialButton;
    // 复制为共享材质按钮。
    QPushButton *m_duplicateMaterialAssetButton;
    // 当前对象转为独立材质实例后编辑按钮。
    QPushButton *m_makeMaterialInstanceButton;
    // 复制场景共享材质按钮。
    QPushButton *m_duplicateSceneMaterialAssetButton;
    // 把场景共享材质应用到当前对象按钮。
    QPushButton *m_assignSceneMaterialAssetToSelectedButton;
    // 把场景共享材质应用到所有对象按钮。
    QPushButton *m_assignSceneMaterialAssetToAllButton;
    // 复制场景纹理按钮。
    QPushButton *m_duplicateSceneTextureAssetButton;
    // 把场景纹理应用到当前对象颜色槽按钮。
    QPushButton *m_assignSceneTextureToSelectedColorButton;
    // 把场景纹理应用到当前对象法线槽按钮。
    QPushButton *m_assignSceneTextureToSelectedNormalButton;
    // 把场景纹理应用到当前对象金属粗糙槽按钮。
    QPushButton *m_assignSceneTextureToSelectedMetallicRoughnessButton;
    // 把场景纹理应用到全部对象颜色槽按钮。
    QPushButton *m_assignSceneTextureToAllObjectsColorButton;
    // 清除当前对象颜色纹理按钮。
    QPushButton *m_clearObjectTextureAssetButton;
    // 清除当前对象法线纹理按钮。
    QPushButton *m_clearObjectNormalTextureAssetButton;
    // 清除当前对象金属粗糙纹理按钮。
    QPushButton *m_clearObjectMetallicRoughnessTextureAssetButton;
    // 重命名共享材质按钮。
    QPushButton *m_renameMaterialAssetButton;
    // 重命名纹理资产按钮。
    QPushButton *m_renameTextureAssetButton;
    // 删除共享材质按钮。
    QPushButton *m_removeMaterialAssetButton;
    // 删除纹理资产按钮。
    QPushButton *m_removeTextureAssetButton;
    // 清理未使用共享材质按钮。
    QPushButton *m_cleanupMaterialAssetsButton;
    // 清理未使用纹理资产按钮。
    QPushButton *m_cleanupTextureAssetsButton;
    // 加载材质资产按钮。
    QPushButton *m_loadMaterialAssetButton;
    // 保存材质资产按钮。
    QPushButton *m_saveMaterialAssetButton;
    // 线稿阈值倍率。
    QDoubleSpinBox *m_lineArtThresholdSpin;
    // 线稿线条强度。
    QDoubleSpinBox *m_lineArtStrengthSpin;
    // 线稿处理缩放百分比。
    QSpinBox *m_lineArtScaleSpin;
    // 线稿阈值曲线预设。
    QComboBox *m_lineArtThresholdCurveCombo;
    // 线稿边缘提取模式。
    QComboBox *m_lineArtEdgeModeCombo;
    // 透明线稿描边粗细。
    QDoubleSpinBox *m_lineArtTransparentStrokeSpin;
    // 是否保留灰度底图。
    QCheckBox *m_lineArtGrayBaseCheck;
    // 是否显示原图/线稿对比预览。
    QCheckBox *m_lineArtComparePreviewCheck;
    // 批量导出线稿按钮。
    QPushButton *m_lineArtBatchExportButton;
    // 保存线稿配置按钮。
    QPushButton *m_saveLineArtConfigButton;
    // 载入线稿配置按钮。
    QPushButton *m_loadLineArtConfigButton;
    // 后处理总开关。
    QCheckBox *m_postProcessEnabledCheck;
    // tone mapping 模式。
    QComboBox *m_toneMappingCombo;
    // 曝光。
    QDoubleSpinBox *m_postExposureSpin;
    // 显示 gamma。
    QDoubleSpinBox *m_postGammaSpin;
    // 对比度。
    QDoubleSpinBox *m_postContrastSpin;
    // 饱和度。
    QDoubleSpinBox *m_postSaturationSpin;
    // 截图按钮。
    QPushButton *m_saveScreenshotButton;
    // 序列导出按钮。
    QPushButton *m_exportSequenceButton;
    // 调试视图批量导出按钮。
    QPushButton *m_exportDebugViewsButton;
    // 序列帧数。
    QSpinBox *m_sequenceFrameCountSpin;
    // 环绕角度。
    QDoubleSpinBox *m_sequenceOrbitDegreesSpin;
    // 并行光栅化开关。
    QCheckBox *m_parallelRasterEnabledCheck;
    // 工作线程请求数。
    QSpinBox *m_parallelWorkerThreadsSpin;
    // tile 尺寸。
    QSpinBox *m_parallelTileSizeSpin;
    // 并行最小 tile 门槛。
    QSpinBox *m_parallelMinTileCountSpin;
    // 并行最小像素门槛。
    QSpinBox *m_parallelMinPixelCountSpin;
    // 每任务 tile 数。
    QSpinBox *m_parallelTilesPerTaskSpin;
    // 调度摘要说明。
    QLabel *m_parallelSummaryLabel;
    // 调度耗时说明。
    QLabel *m_parallelTimingLabel;
    // 撤销动作。
    QAction *m_undoAction;
    // 重做动作。
    QAction *m_redoAction;
    // 当前已提交的历史快照。
    QByteArray m_lastHistorySnapshot;
    // 当前历史事务的起始快照。
    QByteArray m_pendingHistorySnapshot;
    // 撤销栈。
    QVector<QByteArray> m_undoHistory;
    // 重做栈。
    QVector<QByteArray> m_redoHistory;
    // 当前是否正在恢复历史，避免递归记录。
    bool m_restoringHistory;
    // 当前是否存在打开的历史事务。
    bool m_historyTransactionOpen;

    // 创建中心 UI 结构。
    void createUi();
    // 创建底部状态栏。
    void createStatusBar();
    // 创建顶部菜单命令。
    void createMenus();
    // 创建对象编辑面板。
    void createObjectDock();
    // 创建右侧材质与光照面板。
    void createMaterialDock();
    // 创建独立灯光面板。
    void createLightingDock();
    // 创建线稿参数面板。
    void createLineArtDock();
    // 创建相机控制面板。
    void createCameraDock();
    // 创建后处理 / 导出面板。
    void createPostProcessDock();
    // 创建性能 / 并行调优面板。
    void createPerformanceDock();
    // 把当前 widget 状态同步回各个下拉框。
    void syncStateControls();
    // 把当前场景预设同步回下拉框。
    void syncScenePresetControl();
    // 把当前 demo 材质参数同步回面板控件。
    void syncMaterialControls();
    // 把当前灯光列表和参数同步回面板控件。
    void syncLightingControls();
    // 把当前线稿参数同步回面板控件。
    void syncLineArtControls();
    // 把当前场景模型名字同步到状态栏。
    void syncSceneContentLabel();
    // 把对象列表和当前对象变换同步到面板。
    void syncObjectControls();
    // 把相机面板的可用性同步到 UI。
    void syncCameraControls();
    // 把后处理面板同步到 UI。
    void syncPostProcessControls();
    // 把并行参数和调度统计同步到 UI。
    void syncPerformanceControls();
    // 统一配置表单型 Dock 的换行和间距策略。
    void configureDockFormLayout(QFormLayout *layout) const;
    // 给 Dock 内容包一层滚动区，避免高 DPI 下内容溢出。
    void setScrollableDockWidget(QDockWidget *dock, QWidget *panel, int minimumWidth = 360);
    // 把右侧多个 Dock 组织成标签页，减少竖向拥挤。
    void arrangeRightDockTabs();
    // 当前选中的灯光是否为方向光。
    bool selectedLightIsDirectional() const;
    // 当前选中的灯光是否为点光。
    bool selectedLightIsPoint() const;
    // 当前选中的灯光是否为聚光。
    bool selectedLightIsSpot() const;
    // 当前选中的灯光类型。
    RasterWidget::SelectedLightKind selectedLightKind() const;
    // 当前选中的方向光下标；若当前不是方向光则返回 -1。
    int selectedDirectionalLightIndex() const;
    // 当前选中的点光源下标；若当前不是点光源则返回 -1。
    int selectedPointLightIndex() const;
    // 当前选中的聚光灯下标；若当前不是聚光灯则返回 -1。
    int selectedSpotLightIndex() const;
    // 把当前场景序列化成历史快照。
    QByteArray currentSceneHistorySnapshot() const;
    // 为连续数值输入打开一个可合并的历史事务。
    void beginMergedHistoryTransaction(const QString &groupKey);
    // 打开一个历史事务。
    void beginHistoryTransaction();
    // 立即把当前可合并事务落到历史里。
    void flushMergedHistoryTransaction();
    // 提交当前历史事务。
    void commitHistoryTransaction();
    // 用一次性事务记录一个立即完成的编辑。
    void commitImmediateHistoryChange();
    // 把一个快照恢复到场景并同步所有面板。
    bool restoreHistorySnapshot(const QByteArray &snapshot);
    // 刷新撤销/重做动作可用性。
    void updateHistoryActions();
    // 统一同步所有编辑面板。
    void syncAllControls();
    // 安装属性控件焦点监听。
    void installInspectorHighlightFilters();
    // 根据当前聚焦控件同步 gizmo/数值框高亮。
    void updateInspectorHighlightFromFocus(QWidget *focusWidget);
    // 应用当前 gizmo 高亮到对应属性控件。
    void applyInspectorHighlightStyles(int handleKind, int operation, int axis);
    // 清空所有属性控件高亮样式。
    void clearInspectorHighlightStyles();
    // 判断一个控件是否参与连续输入的历史合并。
    QString historyMergeGroupForEditor(const QObject *editor) const;
    // 手动改单项状态后，把预设回退到 Custom。
    void markPresetCustom();
    // 手动改任意场景组成项后，把场景预设回退到 Custom。
    void markScenePresetCustom();

    // 连续输入历史的延迟提交定时器。
    QTimer *m_historyMergeTimer;
    // 当前正在合并的历史分组键。
    QString m_historyMergeGroup;
};

#endif // MAINWINDOW_H
