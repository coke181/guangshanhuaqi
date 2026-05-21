#include "MainWindow.h"
#include "RasterWidget.h"
#include "SoftwareRenderer.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float radiansToDegrees(float radians)
{
    return radians * 180.0f / kPi;
}

float degreesToRadians(double degrees)
{
    return static_cast<float>(degrees * kPi / 180.0);
}

RenderPreset renderPresetForScenePreset(ScenePreset preset)
{
    switch (preset) {
    case ScenePreset::Custom:
        return RenderPreset::Custom;
    case ScenePreset::DefaultOrbit:
    case ScenePreset::TextureStudy:
    case ScenePreset::LightingStudy:
        return RenderPreset::Shaded;
    case ScenePreset::WireframeInspect:
        return RenderPreset::Wireframe;
    case ScenePreset::UvInspect:
        return RenderPreset::UvDebug;
    case ScenePreset::OverdrawInspect:
        return RenderPreset::OverdrawDebug;
    }

    return RenderPreset::Custom;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_rasterWidget(nullptr),
      m_resolutionLabel(nullptr),
      m_statsLabel(nullptr),
      m_parallelStatsLabel(nullptr),
      m_modelLabel(nullptr),
      m_debugViewCombo(nullptr),
      m_scenePresetCombo(nullptr),
      m_objectCombo(nullptr),
      m_objectNameEdit(nullptr),
      m_objectDock(nullptr),
      m_materialDock(nullptr),
      m_lightingDock(nullptr),
      m_lineArtDock(nullptr),
      m_cameraDock(nullptr),
      m_postProcessDock(nullptr),
      m_performanceDock(nullptr),
      m_presetCombo(nullptr),
      m_fillModeCombo(nullptr),
      m_cullModeCombo(nullptr),
      m_depthFuncCombo(nullptr),
      m_antiAliasingCombo(nullptr),
      m_textureFilterCombo(nullptr),
      m_materialTypeCombo(nullptr),
      m_materialSurfaceModeCombo(nullptr),
      m_textureSourceLabel(nullptr),
      m_materialSourceLabel(nullptr),
      m_sharedMaterialWarningPanel(nullptr),
      m_sharedMaterialWarningLabel(nullptr),
      m_normalTextureSourceLabel(nullptr),
      m_metallicRoughnessTextureSourceLabel(nullptr),
      m_metallicRoughnessChannelLabel(nullptr),
      m_texturePresetCombo(nullptr),
      m_materialUsageModeCombo(nullptr),
      m_materialAssetCombo(nullptr),
      m_sceneMaterialAssetCombo(nullptr),
      m_sceneMaterialAssetUsageLabel(nullptr),
      m_sceneTextureAssetCombo(nullptr),
      m_sceneTextureAssetUsageLabel(nullptr),
      m_objectTextureAssetCombo(nullptr),
      m_objectNormalTextureAssetCombo(nullptr),
      m_objectMetallicRoughnessTextureAssetCombo(nullptr),
      m_addressModeUCombo(nullptr),
      m_addressModeVCombo(nullptr),
      m_normalTextureFilterCombo(nullptr),
      m_normalAddressModeUCombo(nullptr),
      m_normalAddressModeVCombo(nullptr),
      m_metallicRoughnessTextureFilterCombo(nullptr),
      m_metallicRoughnessAddressModeUCombo(nullptr),
      m_metallicRoughnessAddressModeVCombo(nullptr),
      m_lightDirectionXSpin(nullptr),
      m_lightDirectionYSpin(nullptr),
      m_lightDirectionZSpin(nullptr),
      m_lightListCombo(nullptr),
      m_lightTypeCombo(nullptr),
      m_lightEnabledCheck(nullptr),
      m_lightNameEdit(nullptr),
      m_lightAmbientSpin(nullptr),
      m_lightIntensitySpin(nullptr),
      m_addDirectionalLightButton(nullptr),
      m_pointLightCombo(nullptr),
      m_addPointLightButton(nullptr),
      m_addSpotLightButton(nullptr),
      m_duplicateLightButton(nullptr),
      m_removeLightButton(nullptr),
      m_pointLightPositionXSpin(nullptr),
      m_pointLightPositionYSpin(nullptr),
      m_pointLightPositionZSpin(nullptr),
      m_pointLightColorRSpin(nullptr),
      m_pointLightColorGSpin(nullptr),
      m_pointLightColorBSpin(nullptr),
      m_pointLightIntensitySpin(nullptr),
      m_pointLightRangeSpin(nullptr),
      m_selectedLightIndex(0),
      m_specularColorRSpin(nullptr),
      m_specularColorGSpin(nullptr),
      m_specularColorBSpin(nullptr),
      m_specularStrengthSpin(nullptr),
      m_shininessSpin(nullptr),
      m_normalStrengthSpin(nullptr),
      m_metallicSpin(nullptr),
      m_roughnessSpin(nullptr),
      m_shadowStrengthSpin(nullptr),
      m_shadowBiasSpin(nullptr),
      m_shadowMapSizeSpin(nullptr),
      m_shadowCoverageSpin(nullptr),
      m_shadowFilterQualityCombo(nullptr),
      m_spotInnerConeSpin(nullptr),
      m_spotOuterConeSpin(nullptr),
      m_materialOpacitySpin(nullptr),
      m_materialDepthWriteCheck(nullptr),
      m_shadowCastCheck(nullptr),
      m_loadTextureButton(nullptr),
      m_loadNormalTextureButton(nullptr),
      m_loadMetallicRoughnessTextureButton(nullptr),
      m_resetCameraButton(nullptr),
      m_cameraProjectionCombo(nullptr),
      m_cameraFovSpin(nullptr),
      m_cameraOrthoHeightSpin(nullptr),
      m_cameraNearSpin(nullptr),
      m_cameraFarSpin(nullptr),
      m_cameraMoveSpeedSpin(nullptr),
      m_saveCameraPresetButton(nullptr),
      m_loadCameraPresetButton(nullptr),
      m_cameraFrontButton(nullptr),
      m_cameraBackButton(nullptr),
      m_cameraLeftButton(nullptr),
      m_cameraRightButton(nullptr),
      m_cameraTopButton(nullptr),
      m_cameraBottomButton(nullptr),
      m_positionXSpin(nullptr),
      m_positionYSpin(nullptr),
      m_positionZSpin(nullptr),
      m_rotationXSpin(nullptr),
      m_rotationYSpin(nullptr),
      m_rotationZSpin(nullptr),
      m_scaleXSpin(nullptr),
      m_scaleYSpin(nullptr),
      m_scaleZSpin(nullptr),
      m_gizmoSpaceCombo(nullptr),
      m_gizmoTranslationSnapSpin(nullptr),
      m_gizmoRotationSnapSpin(nullptr),
      m_gizmoScaleSnapSpin(nullptr),
      m_addCubeButton(nullptr),
      m_removeObjectButton(nullptr),
      m_resetMaterialButton(nullptr),
      m_duplicateMaterialAssetButton(nullptr),
      m_makeMaterialInstanceButton(nullptr),
      m_duplicateSceneMaterialAssetButton(nullptr),
      m_assignSceneMaterialAssetToSelectedButton(nullptr),
      m_assignSceneMaterialAssetToAllButton(nullptr),
      m_duplicateSceneTextureAssetButton(nullptr),
      m_assignSceneTextureToSelectedColorButton(nullptr),
      m_assignSceneTextureToSelectedNormalButton(nullptr),
      m_assignSceneTextureToSelectedMetallicRoughnessButton(nullptr),
      m_assignSceneTextureToAllObjectsColorButton(nullptr),
      m_clearObjectTextureAssetButton(nullptr),
      m_clearObjectNormalTextureAssetButton(nullptr),
      m_clearObjectMetallicRoughnessTextureAssetButton(nullptr),
      m_renameMaterialAssetButton(nullptr),
      m_renameTextureAssetButton(nullptr),
      m_removeMaterialAssetButton(nullptr),
      m_removeTextureAssetButton(nullptr),
      m_cleanupMaterialAssetsButton(nullptr),
      m_cleanupTextureAssetsButton(nullptr),
      m_loadMaterialAssetButton(nullptr),
      m_saveMaterialAssetButton(nullptr),
      m_lineArtThresholdSpin(nullptr),
      m_lineArtStrengthSpin(nullptr),
      m_lineArtScaleSpin(nullptr),
      m_lineArtThresholdCurveCombo(nullptr),
      m_lineArtEdgeModeCombo(nullptr),
      m_lineArtTransparentStrokeSpin(nullptr),
      m_lineArtGrayBaseCheck(nullptr),
      m_lineArtComparePreviewCheck(nullptr),
      m_lineArtBatchExportButton(nullptr),
      m_saveLineArtConfigButton(nullptr),
      m_loadLineArtConfigButton(nullptr),
      m_postProcessEnabledCheck(nullptr),
      m_toneMappingCombo(nullptr),
      m_postExposureSpin(nullptr),
      m_postGammaSpin(nullptr),
      m_postContrastSpin(nullptr),
      m_postSaturationSpin(nullptr),
      m_saveScreenshotButton(nullptr),
      m_exportSequenceButton(nullptr),
      m_exportDebugViewsButton(nullptr),
      m_sequenceFrameCountSpin(nullptr),
      m_sequenceOrbitDegreesSpin(nullptr),
      m_parallelRasterEnabledCheck(nullptr),
      m_parallelWorkerThreadsSpin(nullptr),
      m_parallelTileSizeSpin(nullptr),
      m_parallelMinTileCountSpin(nullptr),
      m_parallelMinPixelCountSpin(nullptr),
      m_parallelTilesPerTaskSpin(nullptr),
      m_parallelSummaryLabel(nullptr),
      m_parallelTimingLabel(nullptr),
      m_undoAction(nullptr),
      m_redoAction(nullptr),
      m_historyMergeTimer(nullptr),
      m_restoringHistory(false),
      m_historyTransactionOpen(false)
{
    // UI 结构非常薄，只负责承载 RasterWidget 和状态栏。
    createUi();
    createMenus();
    createStatusBar();
    createObjectDock();
    createMaterialDock();
    createLightingDock();
    createLineArtDock();
    createCameraDock();
    createPostProcessDock();
    createPerformanceDock();
    arrangeRightDockTabs();

    m_historyMergeTimer = new QTimer(this);
    m_historyMergeTimer->setSingleShot(true);
    m_historyMergeTimer->setInterval(450);
    connect(m_historyMergeTimer, &QTimer::timeout, this, &MainWindow::flushMergedHistoryTransaction);

    installInspectorHighlightFilters();

    setWindowTitle(QStringLiteral("软光栅化器"));
    resize(1280, 800);

    // 每当渲染控件完成一帧，就刷新状态文本。
    connect(m_rasterWidget, &RasterWidget::frameReady, this, &MainWindow::updateStatus);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncSceneContentLabel);
    connect(m_rasterWidget, &RasterWidget::cameraChanged, this, &MainWindow::syncScenePresetControl);
    connect(m_rasterWidget, &RasterWidget::cameraChanged, this, &MainWindow::syncCameraControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncObjectControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncMaterialControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncLightingControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncLineArtControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncCameraControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncPostProcessControls);
    connect(m_rasterWidget, &RasterWidget::sceneContentChanged, this, &MainWindow::syncPerformanceControls);
    connect(m_rasterWidget, &RasterWidget::gizmoInteractionChanged, this,
            [this](bool active, int handleKind, int operation, int axis) {
                applyInspectorHighlightStyles(handleKind, operation, axis);
                if (active) {
                    beginHistoryTransaction();
                } else {
                    commitHistoryTransaction();
                    updateInspectorHighlightFromFocus(QApplication::focusWidget());
                }
            });
    connect(m_rasterWidget, &RasterWidget::lightPicked, this, [this](int lightKindValue, int index) {
        const auto lightKind = static_cast<RasterWidget::SelectedLightKind>(lightKindValue);
        if (index < 0) {
            m_selectedLightIndex = -1;
        } else if (lightKind == RasterWidget::SelectedLightKind::Directional) {
            m_selectedLightIndex = index;
        } else if (lightKind == RasterWidget::SelectedLightKind::Point) {
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() + index;
        } else if (lightKind == RasterWidget::SelectedLightKind::Spot) {
            m_selectedLightIndex = m_rasterWidget->directionalLightCount()
                + m_rasterWidget->pointLightCount()
                + index;
        } else {
            m_selectedLightIndex = -1;
        }
        syncLightingControls();
    });
    connect(m_debugViewCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeDebugView);
    connect(m_scenePresetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeScenePreset);
    connect(m_objectCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedObject);
    connect(m_objectNameEdit, &QLineEdit::editingFinished, this, &MainWindow::renameSelectedObject);
    connect(m_presetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeRenderPreset);
    connect(m_fillModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeFillMode);
    connect(m_cullModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeCullMode);
    connect(m_depthFuncCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeDepthFunc);
    connect(m_antiAliasingCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeAntiAliasing);
    connect(m_postProcessEnabledCheck, &QCheckBox::toggled, this, &MainWindow::changePostProcessEnabled);
    connect(m_toneMappingCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeToneMappingMode);
    connect(m_postExposureSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePostExposure);
    connect(m_postGammaSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePostGamma);
    connect(m_postContrastSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePostContrast);
    connect(m_postSaturationSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePostSaturation);
    connect(m_saveScreenshotButton, &QPushButton::clicked, this, &MainWindow::saveViewportScreenshot);
    connect(m_exportSequenceButton, &QPushButton::clicked, this, &MainWindow::exportViewportSequence);
    connect(m_exportDebugViewsButton, &QPushButton::clicked, this, &MainWindow::exportDebugViews);
    connect(m_textureFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeTextureFilter);
    connect(m_materialTypeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeMaterialType);
    connect(m_materialSurfaceModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeMaterialSurfaceMode);
    connect(m_materialUsageModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeMaterialUsageMode);
    connect(m_materialAssetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedMaterialAsset);
    connect(m_sceneMaterialAssetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSceneMaterialAsset);
    connect(m_sceneTextureAssetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSceneTextureAsset);
    connect(m_objectTextureAssetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedObjectTextureAsset);
    connect(m_objectNormalTextureAssetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedObjectNormalTextureAsset);
    connect(m_objectMetallicRoughnessTextureAssetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedObjectMetallicRoughnessTextureAsset);
    connect(m_texturePresetCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeTexturePreset);
    connect(m_addressModeUCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeAddressModeU);
    connect(m_addressModeVCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeAddressModeV);
    connect(m_normalTextureFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeNormalTextureFilter);
    connect(m_normalAddressModeUCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeNormalAddressModeU);
    connect(m_normalAddressModeVCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeNormalAddressModeV);
    connect(m_metallicRoughnessTextureFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeMetallicRoughnessTextureFilter);
    connect(m_metallicRoughnessAddressModeUCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeMetallicRoughnessAddressModeU);
    connect(m_metallicRoughnessAddressModeVCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeMetallicRoughnessAddressModeV);
    connect(m_lightDirectionXSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLightDirection);
    connect(m_lightDirectionYSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLightDirection);
    connect(m_lightDirectionZSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLightDirection);
    connect(m_lightAmbientSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLightAmbient);
    connect(m_lightIntensitySpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLightIntensity);
    connect(m_lightListCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedLight);
    connect(m_lightTypeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedLightType);
    connect(m_lightEnabledCheck, &QCheckBox::toggled, this, &MainWindow::changeLightEnabled);
    connect(m_lightNameEdit, &QLineEdit::editingFinished, this, &MainWindow::renameSelectedLight);
    connect(m_addDirectionalLightButton, &QPushButton::clicked, this, &MainWindow::addDirectionalLight);
    connect(m_pointLightCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeSelectedPointLight);
    connect(m_addPointLightButton, &QPushButton::clicked, this, &MainWindow::addPointLight);
    connect(m_addSpotLightButton, &QPushButton::clicked, this, &MainWindow::addSpotLight);
    connect(m_duplicateLightButton, &QPushButton::clicked, this, &MainWindow::duplicateSelectedLight);
    connect(m_removeLightButton, &QPushButton::clicked, this, &MainWindow::removeSelectedLight);
    connect(m_pointLightPositionXSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightPosition);
    connect(m_pointLightPositionYSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightPosition);
    connect(m_pointLightPositionZSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightPosition);
    connect(m_pointLightColorRSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightColor);
    connect(m_pointLightColorGSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightColor);
    connect(m_pointLightColorBSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightColor);
    connect(m_pointLightIntensitySpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightIntensity);
    connect(m_pointLightRangeSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changePointLightRange);
    connect(m_shadowMapSizeSpin, &QSpinBox::valueChanged, this, &MainWindow::changeShadowMapSize);
    connect(m_shadowCoverageSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeShadowCoverage);
    connect(m_shadowFilterQualityCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeShadowFilterQuality);
    connect(m_spotInnerConeSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSpotLightInnerCone);
    connect(m_spotOuterConeSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSpotLightOuterCone);
    connect(m_specularColorRSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSpecularColor);
    connect(m_specularColorGSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSpecularColor);
    connect(m_specularColorBSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSpecularColor);
    connect(m_specularStrengthSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSpecularStrength);
    connect(m_shininessSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeShininess);
    connect(m_normalStrengthSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeNormalStrength);
    connect(m_metallicSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeMetallic);
    connect(m_roughnessSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeRoughness);
    connect(m_shadowStrengthSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeShadowStrength);
    connect(m_shadowBiasSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeShadowBias);
    connect(m_materialOpacitySpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeMaterialOpacity);
    connect(m_materialDepthWriteCheck, &QCheckBox::toggled, this, &MainWindow::changeMaterialDepthWrite);
    connect(m_shadowCastCheck, &QCheckBox::toggled, this, &MainWindow::changeShadowCast);
    connect(m_loadTextureButton, &QPushButton::clicked, this, &MainWindow::loadSelectedObjectTexture);
    connect(m_loadNormalTextureButton, &QPushButton::clicked, this, &MainWindow::loadSelectedObjectNormalTexture);
    connect(m_loadMetallicRoughnessTextureButton, &QPushButton::clicked, this, &MainWindow::loadSelectedObjectMetallicRoughnessTexture);
    connect(m_positionXSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_positionYSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_positionZSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_rotationXSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_rotationYSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_rotationZSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_scaleXSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_scaleYSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_scaleZSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeSelectedObjectTransform);
    connect(m_gizmoSpaceCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeGizmoSpaceMode);
    connect(m_gizmoTranslationSnapSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeGizmoTranslationSnapStep);
    connect(m_gizmoRotationSnapSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeGizmoRotationSnapDegrees);
    connect(m_gizmoScaleSnapSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeGizmoScaleSnapStep);
    connect(m_addCubeButton, &QPushButton::clicked, this, &MainWindow::addDemoCubeObject);
    connect(m_removeObjectButton, &QPushButton::clicked, this, &MainWindow::removeSelectedObject);
    connect(m_resetMaterialButton, &QPushButton::clicked, this, &MainWindow::resetDemoMaterial);
    connect(m_duplicateMaterialAssetButton, &QPushButton::clicked, this, &MainWindow::duplicateSelectedMaterialAsAsset);
    connect(m_makeMaterialInstanceButton, &QPushButton::clicked, this, &MainWindow::makeSelectedMaterialInstanceForEditing);
    connect(m_duplicateSceneMaterialAssetButton, &QPushButton::clicked, this, &MainWindow::duplicateSceneMaterialAsset);
    connect(m_assignSceneMaterialAssetToSelectedButton, &QPushButton::clicked, this, &MainWindow::assignSceneMaterialAssetToSelectedObject);
    connect(m_assignSceneMaterialAssetToAllButton, &QPushButton::clicked, this, &MainWindow::assignSceneMaterialAssetToAllObjects);
    connect(m_duplicateSceneTextureAssetButton, &QPushButton::clicked, this, &MainWindow::duplicateSceneTextureAsset);
    connect(m_assignSceneTextureToSelectedColorButton, &QPushButton::clicked, this, &MainWindow::assignSceneTextureAssetToSelectedColor);
    connect(m_assignSceneTextureToSelectedNormalButton, &QPushButton::clicked, this, &MainWindow::assignSceneTextureAssetToSelectedNormal);
    connect(m_assignSceneTextureToSelectedMetallicRoughnessButton, &QPushButton::clicked, this, &MainWindow::assignSceneTextureAssetToSelectedMetallicRoughness);
    connect(m_assignSceneTextureToAllObjectsColorButton, &QPushButton::clicked, this, &MainWindow::assignSceneTextureAssetToAllObjectsColor);
    connect(m_clearObjectTextureAssetButton, &QPushButton::clicked, m_rasterWidget, &RasterWidget::clearSelectedObjectExternalTexture);
    connect(m_clearObjectNormalTextureAssetButton, &QPushButton::clicked, m_rasterWidget, &RasterWidget::clearSelectedObjectExternalNormalTexture);
    connect(m_clearObjectMetallicRoughnessTextureAssetButton, &QPushButton::clicked, m_rasterWidget, &RasterWidget::clearSelectedObjectExternalMetallicRoughnessTexture);
    connect(m_renameMaterialAssetButton, &QPushButton::clicked, this, &MainWindow::renameSelectedMaterialAsset);
    connect(m_renameTextureAssetButton, &QPushButton::clicked, this, &MainWindow::renameSelectedTextureAsset);
    connect(m_removeMaterialAssetButton, &QPushButton::clicked, this, &MainWindow::removeSelectedMaterialAsset);
    connect(m_removeTextureAssetButton, &QPushButton::clicked, this, &MainWindow::removeSelectedTextureAsset);
    connect(m_cleanupMaterialAssetsButton, &QPushButton::clicked, this, &MainWindow::cleanupUnusedMaterialAssets);
    connect(m_cleanupTextureAssetsButton, &QPushButton::clicked, this, &MainWindow::cleanupUnusedTextureAssets);
    connect(m_loadMaterialAssetButton, &QPushButton::clicked, this, &MainWindow::loadSelectedMaterialAsset);
    connect(m_saveMaterialAssetButton, &QPushButton::clicked, this, &MainWindow::saveSelectedMaterialAsset);
    connect(m_resetCameraButton, &QPushButton::clicked, this, &MainWindow::resetCameraView);
    connect(m_saveCameraPresetButton, &QPushButton::clicked, this, &MainWindow::saveCameraPreset);
    connect(m_loadCameraPresetButton, &QPushButton::clicked, this, &MainWindow::loadCameraPreset);
    connect(m_cameraProjectionCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeCameraProjectionMode);
    connect(m_cameraFovSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeCameraVerticalFov);
    connect(m_cameraOrthoHeightSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeCameraOrthographicHeight);
    connect(m_cameraNearSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeCameraNearPlane);
    connect(m_cameraFarSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeCameraFarPlane);
    connect(m_cameraMoveSpeedSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeCameraMoveSpeed);
    connect(m_cameraFrontButton, &QPushButton::clicked, this, &MainWindow::setCameraFrontView);
    connect(m_cameraBackButton, &QPushButton::clicked, this, &MainWindow::setCameraBackView);
    connect(m_cameraLeftButton, &QPushButton::clicked, this, &MainWindow::setCameraLeftView);
    connect(m_cameraRightButton, &QPushButton::clicked, this, &MainWindow::setCameraRightView);
    connect(m_cameraTopButton, &QPushButton::clicked, this, &MainWindow::setCameraTopView);
    connect(m_cameraBottomButton, &QPushButton::clicked, this, &MainWindow::setCameraBottomView);
    connect(m_lineArtThresholdSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLineArtThreshold);
    connect(m_lineArtStrengthSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLineArtStrength);
    connect(m_lineArtScaleSpin, &QSpinBox::valueChanged, this, &MainWindow::changeLineArtProcessScale);
    connect(m_lineArtThresholdCurveCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeLineArtThresholdCurvePreset);
    connect(m_lineArtEdgeModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::changeLineArtEdgeMode);
    connect(m_lineArtTransparentStrokeSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::changeLineArtTransparentStrokeWidth);
    connect(m_lineArtGrayBaseCheck, &QCheckBox::toggled, this, &MainWindow::changeLineArtGrayBase);
    connect(m_lineArtComparePreviewCheck, &QCheckBox::toggled, this, &MainWindow::changeLineArtComparePreview);
    connect(m_lineArtBatchExportButton, &QPushButton::clicked, this, &MainWindow::batchExportLineArt);
    connect(m_saveLineArtConfigButton, &QPushButton::clicked, this, &MainWindow::saveLineArtConfig);
    connect(m_loadLineArtConfigButton, &QPushButton::clicked, this, &MainWindow::loadLineArtConfig);
    connect(m_parallelRasterEnabledCheck, &QCheckBox::toggled, this, &MainWindow::changeParallelRasterEnabled);
    connect(m_parallelWorkerThreadsSpin, &QSpinBox::valueChanged, this, &MainWindow::changeParallelWorkerThreadCount);
    connect(m_parallelTileSizeSpin, &QSpinBox::valueChanged, this, &MainWindow::changeParallelTileSize);
    connect(m_parallelMinTileCountSpin, &QSpinBox::valueChanged, this, &MainWindow::changeParallelMinTileCount);
    connect(m_parallelMinPixelCountSpin, &QSpinBox::valueChanged, this, &MainWindow::changeParallelMinPixelCount);
    connect(m_parallelTilesPerTaskSpin, &QSpinBox::valueChanged, this, &MainWindow::changeParallelTilesPerTask);

    m_lastHistorySnapshot = currentSceneHistorySnapshot();
    updateHistoryActions();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const QString watchedMergeGroup = historyMergeGroupForEditor(watched);
    if (event->type() == QEvent::FocusIn) {
        updateInspectorHighlightFromFocus(qobject_cast<QWidget *>(watched));
    } else if (event->type() == QEvent::FocusOut) {
        QTimer::singleShot(0, this, [this, watchedMergeGroup]() {
            QWidget *focusWidget = QApplication::focusWidget();
            updateInspectorHighlightFromFocus(focusWidget);
            if (!watchedMergeGroup.isEmpty()
                && watchedMergeGroup != historyMergeGroupForEditor(focusWidget)) {
                flushMergedHistoryTransaction();
            }
        });
    }

    return QMainWindow::eventFilter(watched, event);
}

QByteArray MainWindow::currentSceneHistorySnapshot() const
{
    return QJsonDocument(m_rasterWidget->saveSceneState()).toJson(QJsonDocument::Compact);
}

void MainWindow::beginMergedHistoryTransaction(const QString &groupKey)
{
    if (m_restoringHistory || groupKey.isEmpty())
        return;

    if (m_historyTransactionOpen && m_historyMergeGroup == groupKey) {
        if (m_historyMergeTimer != nullptr)
            m_historyMergeTimer->start();
        return;
    }

    if (m_historyTransactionOpen)
        flushMergedHistoryTransaction();

    beginHistoryTransaction();
    if (!m_historyTransactionOpen)
        return;

    m_historyMergeGroup = groupKey;
    if (m_historyMergeTimer != nullptr)
        m_historyMergeTimer->start();
}

void MainWindow::beginHistoryTransaction()
{
    if (m_restoringHistory)
        return;

    if (m_historyTransactionOpen && !m_historyMergeGroup.isEmpty())
        flushMergedHistoryTransaction();

    if (m_historyTransactionOpen)
        return;

    m_pendingHistorySnapshot = m_lastHistorySnapshot;
    m_historyTransactionOpen = true;
}

void MainWindow::flushMergedHistoryTransaction()
{
    if (m_historyMergeTimer != nullptr)
        m_historyMergeTimer->stop();

    if (m_historyMergeGroup.isEmpty())
        return;

    commitHistoryTransaction();
}

void MainWindow::commitHistoryTransaction()
{
    if (m_historyMergeTimer != nullptr)
        m_historyMergeTimer->stop();
    m_historyMergeGroup.clear();

    if (m_restoringHistory || !m_historyTransactionOpen)
        return;

    const QByteArray currentSnapshot = currentSceneHistorySnapshot();
    m_historyTransactionOpen = false;
    if (currentSnapshot == m_pendingHistorySnapshot)
        return;

    m_undoHistory.push_back(m_pendingHistorySnapshot);
    m_redoHistory.clear();
    m_lastHistorySnapshot = currentSnapshot;
    updateHistoryActions();
}

void MainWindow::commitImmediateHistoryChange()
{
    beginHistoryTransaction();
    commitHistoryTransaction();
}

bool MainWindow::restoreHistorySnapshot(const QByteArray &snapshot)
{
    const QJsonDocument document = QJsonDocument::fromJson(snapshot);
    if (!document.isObject())
        return false;

    QString errorMessage;
    m_restoringHistory = true;
    const bool loaded = m_rasterWidget->loadSceneState(document.object(), &errorMessage);
    m_restoringHistory = false;
    if (!loaded)
        return false;

    syncAllControls();
    m_lastHistorySnapshot = snapshot;
    updateInspectorHighlightFromFocus(QApplication::focusWidget());
    return true;
}

void MainWindow::updateHistoryActions()
{
    if (m_undoAction != nullptr)
        m_undoAction->setEnabled(!m_undoHistory.isEmpty());
    if (m_redoAction != nullptr)
        m_redoAction->setEnabled(!m_redoHistory.isEmpty());
}

void MainWindow::syncAllControls()
{
    syncScenePresetControl();
    syncStateControls();
    syncPostProcessControls();
    syncMaterialControls();
    syncLineArtControls();
    syncSceneContentLabel();
    syncObjectControls();
    syncLightingControls();
    syncCameraControls();
    syncPerformanceControls();
}

void MainWindow::installInspectorHighlightFilters()
{
    for (QWidget *widget : {static_cast<QWidget *>(m_positionXSpin),
                            static_cast<QWidget *>(m_positionYSpin),
                            static_cast<QWidget *>(m_positionZSpin),
                            static_cast<QWidget *>(m_rotationXSpin),
                            static_cast<QWidget *>(m_rotationYSpin),
                            static_cast<QWidget *>(m_rotationZSpin),
                            static_cast<QWidget *>(m_scaleXSpin),
                            static_cast<QWidget *>(m_scaleYSpin),
                            static_cast<QWidget *>(m_scaleZSpin),
                            static_cast<QWidget *>(m_pointLightPositionXSpin),
                            static_cast<QWidget *>(m_pointLightPositionYSpin),
                            static_cast<QWidget *>(m_pointLightPositionZSpin),
                            static_cast<QWidget *>(m_pointLightColorRSpin),
                            static_cast<QWidget *>(m_pointLightColorGSpin),
                            static_cast<QWidget *>(m_pointLightColorBSpin),
                            static_cast<QWidget *>(m_pointLightIntensitySpin),
                            static_cast<QWidget *>(m_pointLightRangeSpin),
                            static_cast<QWidget *>(m_lightDirectionXSpin),
                            static_cast<QWidget *>(m_lightDirectionYSpin),
                            static_cast<QWidget *>(m_lightDirectionZSpin),
                            static_cast<QWidget *>(m_lightAmbientSpin),
                            static_cast<QWidget *>(m_lightIntensitySpin),
                            static_cast<QWidget *>(m_specularColorRSpin),
                            static_cast<QWidget *>(m_specularColorGSpin),
                            static_cast<QWidget *>(m_specularColorBSpin),
                            static_cast<QWidget *>(m_specularStrengthSpin),
                            static_cast<QWidget *>(m_shininessSpin),
                            static_cast<QWidget *>(m_normalStrengthSpin),
                            static_cast<QWidget *>(m_metallicSpin),
                            static_cast<QWidget *>(m_roughnessSpin),
                            static_cast<QWidget *>(m_shadowStrengthSpin),
                            static_cast<QWidget *>(m_shadowBiasSpin),
                            static_cast<QWidget *>(m_materialOpacitySpin),
                            static_cast<QWidget *>(m_gizmoTranslationSnapSpin),
                            static_cast<QWidget *>(m_gizmoRotationSnapSpin),
                            static_cast<QWidget *>(m_gizmoScaleSnapSpin)}) {
        if (widget != nullptr)
            widget->installEventFilter(this);
    }
}

QString MainWindow::historyMergeGroupForEditor(const QObject *editor) const
{
    if (editor == nullptr)
        return {};

    if (editor == m_positionXSpin || editor == m_positionYSpin || editor == m_positionZSpin
        || editor == m_rotationXSpin || editor == m_rotationYSpin || editor == m_rotationZSpin
        || editor == m_scaleXSpin || editor == m_scaleYSpin || editor == m_scaleZSpin) {
        return QStringLiteral("objectTransform");
    }
    if (editor == m_pointLightPositionXSpin || editor == m_pointLightPositionYSpin || editor == m_pointLightPositionZSpin)
        return QStringLiteral("pointLightPosition");
    if (editor == m_lightDirectionXSpin || editor == m_lightDirectionYSpin || editor == m_lightDirectionZSpin)
        return QStringLiteral("directionalLightDirection");
    if (editor == m_lightAmbientSpin)
        return QStringLiteral("lightAmbient");
    if (editor == m_lightIntensitySpin)
        return QStringLiteral("lightIntensity");
    if (editor == m_pointLightColorRSpin || editor == m_pointLightColorGSpin || editor == m_pointLightColorBSpin)
        return QStringLiteral("lightColor");
    if (editor == m_pointLightIntensitySpin)
        return QStringLiteral("pointLightIntensity");
    if (editor == m_pointLightRangeSpin)
        return QStringLiteral("pointLightRange");
    if (editor == m_specularColorRSpin || editor == m_specularColorGSpin || editor == m_specularColorBSpin)
        return QStringLiteral("specularColor");
    if (editor == m_specularStrengthSpin)
        return QStringLiteral("specularStrength");
    if (editor == m_shininessSpin)
        return QStringLiteral("shininess");
    if (editor == m_normalStrengthSpin)
        return QStringLiteral("normalStrength");
    if (editor == m_metallicSpin)
        return QStringLiteral("metallic");
    if (editor == m_roughnessSpin)
        return QStringLiteral("roughness");
    if (editor == m_shadowStrengthSpin)
        return QStringLiteral("shadowStrength");
    if (editor == m_shadowBiasSpin)
        return QStringLiteral("shadowBias");
    if (editor == m_materialOpacitySpin)
        return QStringLiteral("materialOpacity");
    if (editor == m_gizmoTranslationSnapSpin)
        return QStringLiteral("gizmoTranslationSnap");
    if (editor == m_gizmoRotationSnapSpin)
        return QStringLiteral("gizmoRotationSnap");
    if (editor == m_gizmoScaleSnapSpin)
        return QStringLiteral("gizmoScaleSnap");

    return {};
}

void MainWindow::updateInspectorHighlightFromFocus(QWidget *focusWidget)
{
    if (m_rasterWidget == nullptr) {
        clearInspectorHighlightStyles();
        return;
    }

    auto applyHighlight = [this](int handleKind, int operation, int axis) {
        m_rasterWidget->setInspectorGizmoHighlight(handleKind, operation, axis);
        applyInspectorHighlightStyles(handleKind, operation, axis);
    };

    if (focusWidget == m_positionXSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Translate),
                       static_cast<int>(RasterWidget::ViewportAxis::X));
        return;
    }
    if (focusWidget == m_positionYSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Translate),
                       static_cast<int>(RasterWidget::ViewportAxis::Y));
        return;
    }
    if (focusWidget == m_positionZSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Translate),
                       static_cast<int>(RasterWidget::ViewportAxis::Z));
        return;
    }
    if (focusWidget == m_rotationXSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Rotate),
                       static_cast<int>(RasterWidget::ViewportAxis::X));
        return;
    }
    if (focusWidget == m_rotationYSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Rotate),
                       static_cast<int>(RasterWidget::ViewportAxis::Y));
        return;
    }
    if (focusWidget == m_rotationZSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Rotate),
                       static_cast<int>(RasterWidget::ViewportAxis::Z));
        return;
    }
    if (focusWidget == m_scaleXSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Scale),
                       static_cast<int>(RasterWidget::ViewportAxis::X));
        return;
    }
    if (focusWidget == m_scaleYSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Scale),
                       static_cast<int>(RasterWidget::ViewportAxis::Y));
        return;
    }
    if (focusWidget == m_scaleZSpin)
    {
        applyHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::SceneObject),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Scale),
                       static_cast<int>(RasterWidget::ViewportAxis::Z));
        return;
    }
    if (focusWidget == m_pointLightPositionXSpin)
    {
        applyHighlight(static_cast<int>(selectedLightIsSpot() ? RasterWidget::ViewportHandleKind::SpotLight
                                                              : RasterWidget::ViewportHandleKind::PointLight),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Translate),
                       static_cast<int>(RasterWidget::ViewportAxis::X));
        return;
    }
    if (focusWidget == m_pointLightPositionYSpin)
    {
        applyHighlight(static_cast<int>(selectedLightIsSpot() ? RasterWidget::ViewportHandleKind::SpotLight
                                                              : RasterWidget::ViewportHandleKind::PointLight),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Translate),
                       static_cast<int>(RasterWidget::ViewportAxis::Y));
        return;
    }
    if (focusWidget == m_pointLightPositionZSpin)
    {
        applyHighlight(static_cast<int>(selectedLightIsSpot() ? RasterWidget::ViewportHandleKind::SpotLight
                                                              : RasterWidget::ViewportHandleKind::PointLight),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Translate),
                       static_cast<int>(RasterWidget::ViewportAxis::Z));
        return;
    }
    if (focusWidget == m_lightDirectionXSpin || focusWidget == m_lightDirectionYSpin || focusWidget == m_lightDirectionZSpin)
    {
        applyHighlight(static_cast<int>(selectedLightIsSpot() ? RasterWidget::ViewportHandleKind::SpotLight
                                                              : RasterWidget::ViewportHandleKind::DirectionalLight),
                       static_cast<int>(RasterWidget::ViewportHandleOperation::Direction),
                       static_cast<int>(RasterWidget::ViewportAxis::None));
        return;
    }

    m_rasterWidget->setInspectorGizmoHighlight(static_cast<int>(RasterWidget::ViewportHandleKind::None),
                                               static_cast<int>(RasterWidget::ViewportHandleOperation::None),
                                               static_cast<int>(RasterWidget::ViewportAxis::None));
    clearInspectorHighlightStyles();
}

void MainWindow::applyInspectorHighlightStyles(int handleKind, int operation, int axis)
{
    clearInspectorHighlightStyles();

    const QString style = QStringLiteral("QDoubleSpinBox { background-color: rgb(39, 78, 118); color: white; border: 1px solid rgb(72, 196, 255); }");
    const auto setStyle = [&style](QWidget *widget) {
        if (widget != nullptr)
            widget->setStyleSheet(style);
    };

    const auto kind = static_cast<RasterWidget::ViewportHandleKind>(handleKind);
    const auto op = static_cast<RasterWidget::ViewportHandleOperation>(operation);
    const auto ax = static_cast<RasterWidget::ViewportAxis>(axis);

    if (kind == RasterWidget::ViewportHandleKind::SceneObject && op == RasterWidget::ViewportHandleOperation::Translate) {
        if (ax == RasterWidget::ViewportAxis::X) setStyle(m_positionXSpin);
        if (ax == RasterWidget::ViewportAxis::Y) setStyle(m_positionYSpin);
        if (ax == RasterWidget::ViewportAxis::Z) setStyle(m_positionZSpin);
    } else if (kind == RasterWidget::ViewportHandleKind::SceneObject && op == RasterWidget::ViewportHandleOperation::Rotate) {
        if (ax == RasterWidget::ViewportAxis::X) setStyle(m_rotationXSpin);
        if (ax == RasterWidget::ViewportAxis::Y) setStyle(m_rotationYSpin);
        if (ax == RasterWidget::ViewportAxis::Z) setStyle(m_rotationZSpin);
    } else if (kind == RasterWidget::ViewportHandleKind::SceneObject && op == RasterWidget::ViewportHandleOperation::Scale) {
        if (ax == RasterWidget::ViewportAxis::X) setStyle(m_scaleXSpin);
        if (ax == RasterWidget::ViewportAxis::Y) setStyle(m_scaleYSpin);
        if (ax == RasterWidget::ViewportAxis::Z) setStyle(m_scaleZSpin);
    } else if ((kind == RasterWidget::ViewportHandleKind::PointLight || kind == RasterWidget::ViewportHandleKind::SpotLight)
               && op == RasterWidget::ViewportHandleOperation::Translate) {
        if (ax == RasterWidget::ViewportAxis::X) setStyle(m_pointLightPositionXSpin);
        if (ax == RasterWidget::ViewportAxis::Y) setStyle(m_pointLightPositionYSpin);
        if (ax == RasterWidget::ViewportAxis::Z) setStyle(m_pointLightPositionZSpin);
    } else if ((kind == RasterWidget::ViewportHandleKind::DirectionalLight || kind == RasterWidget::ViewportHandleKind::SpotLight)
               && op == RasterWidget::ViewportHandleOperation::Direction) {
        setStyle(m_lightDirectionXSpin);
        setStyle(m_lightDirectionYSpin);
        setStyle(m_lightDirectionZSpin);
    }
}

void MainWindow::clearInspectorHighlightStyles()
{
    for (QWidget *widget : {static_cast<QWidget *>(m_positionXSpin),
                            static_cast<QWidget *>(m_positionYSpin),
                            static_cast<QWidget *>(m_positionZSpin),
                            static_cast<QWidget *>(m_rotationXSpin),
                            static_cast<QWidget *>(m_rotationYSpin),
                            static_cast<QWidget *>(m_rotationZSpin),
                            static_cast<QWidget *>(m_scaleXSpin),
                            static_cast<QWidget *>(m_scaleYSpin),
                            static_cast<QWidget *>(m_scaleZSpin),
                            static_cast<QWidget *>(m_pointLightPositionXSpin),
                            static_cast<QWidget *>(m_pointLightPositionYSpin),
                            static_cast<QWidget *>(m_pointLightPositionZSpin),
                            static_cast<QWidget *>(m_lightDirectionXSpin),
                            static_cast<QWidget *>(m_lightDirectionYSpin),
                            static_cast<QWidget *>(m_lightDirectionZSpin)}) {
        if (widget != nullptr)
            widget->setStyleSheet(QString());
    }
}

void MainWindow::updateStatus(const RenderStats &stats)
{
    // 状态栏只暴露最核心的运行指标，避免 UI 逻辑污染渲染器。
    m_resolutionLabel->setText(QStringLiteral("%1 x %2").arg(stats.width).arg(stats.height));
    m_statsLabel->setText(QStringLiteral("三角形 %1  像素 %2")
                              .arg(stats.trianglesRasterized)
                              .arg(stats.pixelsDrawn));
    const ParallelRasterStats parallelStats = m_rasterWidget->parallelRasterStats();
    const QString parallelMode = !m_rasterWidget->parallelRasterEnabled()
        ? QStringLiteral("关")
        : (parallelStats.parallelTaskCount > 0 ? QStringLiteral("开") : QStringLiteral("串"));
    m_parallelStatsLabel->setText(QStringLiteral("并行 %1 | 线程 %2 | Tile %3 | 任务 %4")
                                      .arg(parallelMode)
                                      .arg(parallelStats.workerThreadCount)
                                      .arg(parallelStats.tileCount)
                                      .arg(parallelStats.taskCount));
    syncPerformanceControls();
}

void MainWindow::changeParallelRasterEnabled(bool checked)
{
    beginHistoryTransaction();
    m_rasterWidget->setParallelRasterEnabled(checked);
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeParallelWorkerThreadCount(int value)
{
    beginHistoryTransaction();
    m_rasterWidget->setParallelWorkerThreadCount(value);
    markScenePresetCustom();
    syncPerformanceControls();
    commitHistoryTransaction();
}

void MainWindow::changeParallelTileSize(int value)
{
    beginHistoryTransaction();
    m_rasterWidget->setParallelTileSize(value);
    markScenePresetCustom();
    syncPerformanceControls();
    commitHistoryTransaction();
}

void MainWindow::changeParallelMinTileCount(int value)
{
    beginHistoryTransaction();
    m_rasterWidget->setParallelMinTileCount(value);
    markScenePresetCustom();
    syncPerformanceControls();
    commitHistoryTransaction();
}

void MainWindow::changeParallelMinPixelCount(int value)
{
    beginHistoryTransaction();
    m_rasterWidget->setParallelMinPixelCount(value);
    markScenePresetCustom();
    syncPerformanceControls();
    commitHistoryTransaction();
}

void MainWindow::changeParallelTilesPerTask(int value)
{
    beginHistoryTransaction();
    m_rasterWidget->setParallelTilesPerTask(value);
    markScenePresetCustom();
    syncPerformanceControls();
    commitHistoryTransaction();
}

void MainWindow::changeDebugView(int index)
{
    m_rasterWidget->setDebugView(static_cast<DebugView>(index));
    markPresetCustom();
    markScenePresetCustom();
}

void MainWindow::changeFillMode(int index)
{
    m_rasterWidget->setFillMode(static_cast<FillMode>(index));
    markPresetCustom();
    markScenePresetCustom();
}

void MainWindow::changeCullMode(int index)
{
    m_rasterWidget->setCullMode(static_cast<CullMode>(index));
    markPresetCustom();
    markScenePresetCustom();
}

void MainWindow::changeDepthFunc(int index)
{
    m_rasterWidget->setDepthFunc(static_cast<DepthFunc>(index));
    markPresetCustom();
    markScenePresetCustom();
}

void MainWindow::changeAntiAliasing(int index)
{
    m_rasterWidget->setAntiAliasingMode(static_cast<AntiAliasingMode>(index));
    markPresetCustom();
    markScenePresetCustom();
}

void MainWindow::changePostProcessEnabled(bool checked)
{
    beginHistoryTransaction();
    m_rasterWidget->setPostProcessEnabled(checked);
    markScenePresetCustom();
    syncPostProcessControls();
    commitHistoryTransaction();
}

void MainWindow::changeToneMappingMode(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setToneMappingMode(static_cast<ToneMappingMode>(index));
    markScenePresetCustom();
    syncPostProcessControls();
    commitHistoryTransaction();
}

void MainWindow::changePostExposure(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("postExposure"));
    m_rasterWidget->setPostExposure(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changePostGamma(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("postGamma"));
    m_rasterWidget->setPostGamma(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changePostContrast(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("postContrast"));
    m_rasterWidget->setPostContrast(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changePostSaturation(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("postSaturation"));
    m_rasterWidget->setPostSaturation(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeScenePreset(int index)
{
    const ScenePreset preset = static_cast<ScenePreset>(index);
    if (preset == ScenePreset::Custom)
        return;

    m_rasterWidget->applyScenePreset(preset);

    m_presetCombo->blockSignals(true);
    m_presetCombo->setCurrentIndex(static_cast<int>(renderPresetForScenePreset(preset)));
    m_presetCombo->blockSignals(false);

    syncScenePresetControl();
    syncStateControls();
    syncMaterialControls();
}

void MainWindow::changeRenderPreset(int index)
{
    const RenderPreset preset = static_cast<RenderPreset>(index);
    if (preset == RenderPreset::Custom)
        return;

    m_rasterWidget->applyRenderPreset(preset);
    markScenePresetCustom();
    syncStateControls();
}

void MainWindow::changeTextureFilter(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setTextureFilter(static_cast<TextureFilter>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeMaterialType(int index)//修改材质类型
{
    beginHistoryTransaction();
    m_rasterWidget->setMaterialType(static_cast<MaterialType>(index));
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeMaterialSurfaceMode(int index)//修改材质表面模式
{
    beginHistoryTransaction();
    m_rasterWidget->setMaterialSurfaceMode(static_cast<MaterialSurfaceMode>(index));
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeMaterialUsageMode(int index)
{
    if (m_rasterWidget->isLineArtMode() || m_rasterWidget->sceneObjectCount() <= 0)
        return;

    beginHistoryTransaction();
    if (index == 0) {
        m_rasterWidget->makeSelectedObjectUseMaterialInstance();
    } else if (m_rasterWidget->materialAssetCount() <= 0) {
        m_rasterWidget->duplicateSelectedMaterialAsAsset();
    } else {
        const int assetIndex = std::max(0, m_materialAssetCombo != nullptr ? m_materialAssetCombo->currentIndex() : 0);
        m_rasterWidget->assignSelectedObjectMaterialAsset(assetIndex);
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeSelectedMaterialAsset(int index)
{
    if (index < 0 || !m_rasterWidget->selectedObjectUsesMaterialAsset())
        return;

    beginHistoryTransaction();
    m_rasterWidget->assignSelectedObjectMaterialAsset(index);
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeSceneMaterialAsset(int)
{
    syncMaterialControls();
}

void MainWindow::changeSceneTextureAsset(int)
{
    syncMaterialControls();
}

void MainWindow::changeSelectedObjectTextureAsset(int index)
{
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->assignSelectedObjectTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeSelectedObjectNormalTextureAsset(int index)
{
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->assignSelectedObjectNormalTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeSelectedObjectMetallicRoughnessTextureAsset(int index)
{
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->assignSelectedObjectMetallicRoughnessTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::duplicateSelectedMaterialAsAsset()
{
    beginHistoryTransaction();
    m_rasterWidget->duplicateSelectedMaterialAsAsset();
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::makeSelectedMaterialInstanceForEditing()
{
    if (!m_rasterWidget->selectedObjectUsesMaterialAsset())
        return;

    beginHistoryTransaction();
    m_rasterWidget->makeSelectedObjectUseMaterialInstance();
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::duplicateSceneMaterialAsset()
{
    const int index = m_sceneMaterialAssetCombo != nullptr ? m_sceneMaterialAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->duplicateMaterialAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::duplicateSceneTextureAsset()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->duplicateTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::renameSelectedMaterialAsset()
{
    const int index = m_sceneMaterialAssetCombo != nullptr ? m_sceneMaterialAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    bool accepted = false;
    const QString currentName = m_rasterWidget->materialAssetName(index);
    const QString newName = QInputDialog::getText(this,
                                                  QStringLiteral("重命名共享材质"),
                                                  QStringLiteral("材质名称"),
                                                  QLineEdit::Normal,
                                                  currentName,
                                                  &accepted).trimmed();
    if (!accepted || newName.isEmpty() || newName == currentName)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->renameMaterialAsset(index, newName)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::renameSelectedTextureAsset()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    bool accepted = false;
    const QString currentName = m_rasterWidget->textureAssetName(index);
    const QString newName = QInputDialog::getText(this,
                                                  QStringLiteral("重命名纹理资产"),
                                                  QStringLiteral("纹理名称"),
                                                  QLineEdit::Normal,
                                                  currentName,
                                                  &accepted).trimmed();
    if (!accepted || newName.isEmpty() || newName == currentName)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->renameTextureAsset(index, newName)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::removeSelectedMaterialAsset()
{
    const int index = m_sceneMaterialAssetCombo != nullptr ? m_sceneMaterialAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    const QString assetName = m_rasterWidget->materialAssetName(index);
    const auto result = QMessageBox::question(this,
                                              QStringLiteral("删除共享材质"),
                                              QStringLiteral("确定删除共享材质“%1”吗？\n仍在引用它的对象会自动改为独立材质实例。")
                                                  .arg(assetName));
    if (result != QMessageBox::Yes)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->removeMaterialAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::removeSelectedTextureAsset()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    const QString assetName = m_rasterWidget->textureAssetName(index);
    const auto result = QMessageBox::question(this,
                                              QStringLiteral("删除纹理资产"),
                                              QStringLiteral("确定删除纹理资产“%1”吗？\n所有引用它的颜色 / 法线 / 金属粗糙槽都会被清空。")
                                                  .arg(assetName));
    if (result != QMessageBox::Yes)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->removeTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::cleanupUnusedMaterialAssets()
{
    beginHistoryTransaction();
    const int removedCount = m_rasterWidget->removeUnusedMaterialAssets();
    if (removedCount <= 0) {
        m_historyTransactionOpen = false;
        QMessageBox::information(this,
                                 QStringLiteral("清理完成"),
                                 QStringLiteral("当前没有未使用的共享材质。"));
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
    QMessageBox::information(this,
                             QStringLiteral("清理完成"),
                             QStringLiteral("已清理 %1 个未使用的共享材质。").arg(removedCount));
}

void MainWindow::cleanupUnusedTextureAssets()
{
    beginHistoryTransaction();
    const int removedCount = m_rasterWidget->removeUnusedTextureAssets();
    if (removedCount <= 0) {
        m_historyTransactionOpen = false;
        QMessageBox::information(this,
                                 QStringLiteral("清理完成"),
                                 QStringLiteral("当前没有未使用的纹理资产。"));
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
    QMessageBox::information(this,
                             QStringLiteral("清理完成"),
                             QStringLiteral("已清理 %1 个未使用的纹理资产。").arg(removedCount));
}

void MainWindow::assignSceneMaterialAssetToSelectedObject()
{
    const int index = m_sceneMaterialAssetCombo != nullptr ? m_sceneMaterialAssetCombo->currentIndex() : -1;
    if (index < 0 || m_rasterWidget->sceneObjectCount() <= 0)
        return;

    beginHistoryTransaction();
    m_rasterWidget->assignSelectedObjectMaterialAsset(index);
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::assignSceneMaterialAssetToAllObjects()
{
    const int index = m_sceneMaterialAssetCombo != nullptr ? m_sceneMaterialAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    const int changedCount = m_rasterWidget->assignMaterialAssetToAllObjects(index);
    if (changedCount <= 0) {
        m_historyTransactionOpen = false;
        QMessageBox::information(this,
                                 QStringLiteral("批量复用"),
                                 QStringLiteral("当前没有需要切换到该共享材质的对象。"));
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
    QMessageBox::information(this,
                             QStringLiteral("批量复用"),
                             QStringLiteral("已将该共享材质应用到 %1 个对象。").arg(changedCount));
}

void MainWindow::assignSceneTextureAssetToSelectedColor()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->assignSelectedObjectTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::assignSceneTextureAssetToSelectedNormal()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->assignSelectedObjectNormalTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::assignSceneTextureAssetToSelectedMetallicRoughness()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    if (!m_rasterWidget->assignSelectedObjectMetallicRoughnessTextureAsset(index)) {
        m_historyTransactionOpen = false;
        return;
    }
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::assignSceneTextureAssetToAllObjectsColor()
{
    const int index = m_sceneTextureAssetCombo != nullptr ? m_sceneTextureAssetCombo->currentIndex() : -1;
    if (index < 0)
        return;

    beginHistoryTransaction();
    const int changedCount = m_rasterWidget->assignTextureAssetToAllObjectsColor(index);
    if (changedCount <= 0) {
        m_historyTransactionOpen = false;
        QMessageBox::information(this,
                                 QStringLiteral("批量替换纹理"),
                                 QStringLiteral("当前没有需要切换颜色纹理的对象。"));
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
    QMessageBox::information(this,
                             QStringLiteral("批量替换纹理"),
                             QStringLiteral("已将该纹理应用到 %1 个对象的颜色槽。").arg(changedCount));
}

void MainWindow::loadSelectedMaterialAsset()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开材质资产"),
                                                      QString(),
                                                      QStringLiteral("材质资产 (*.json)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    beginHistoryTransaction();
    if (!m_rasterWidget->loadMaterialAssetFromFile(path, &errorMessage)) {
        m_historyTransactionOpen = false;
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("材质资产加载失败。") : errorMessage);
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::saveSelectedMaterialAsset()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("另存材质资产"),
                                                      QStringLiteral("material_asset.json"),
                                                      QStringLiteral("材质资产 (*.json)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->saveSelectedMaterialAssetToFile(path, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             errorMessage.isEmpty() ? QStringLiteral("材质资产保存失败。") : errorMessage);
    }
}

void MainWindow::changeMaterialOpacity(double value)//修改材质不透明度
{
    beginMergedHistoryTransaction(QStringLiteral("materialOpacity"));
    m_rasterWidget->setMaterialOpacity(static_cast<float>(value));
    markScenePresetCustom();
    syncMaterialControls();
}

void MainWindow::changeMaterialDepthWrite(bool checked)//切换材质深度写入
{
    beginHistoryTransaction();
    m_rasterWidget->setMaterialDepthWriteEnable(checked);
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::loadSelectedObjectTexture()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开对象纹理"),
                                                      QString(),
                                                      QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.tga)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    beginHistoryTransaction();
    if (!m_rasterWidget->loadTextureForSelectedObject(path, &errorMessage)) {
        m_historyTransactionOpen = false;
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("对象纹理加载失败。") : errorMessage);
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::loadSelectedObjectNormalTexture()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开法线贴图"),
                                                      QString(),
                                                      QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.tga)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    beginHistoryTransaction();
    if (!m_rasterWidget->loadNormalTextureForSelectedObject(path, &errorMessage)) {
        m_historyTransactionOpen = false;
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("法线贴图加载失败。") : errorMessage);
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::loadSelectedObjectMetallicRoughnessTexture()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开金属度粗糙度贴图"),
                                                      QString(),
                                                      QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.tga)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    beginHistoryTransaction();
    if (!m_rasterWidget->loadMetallicRoughnessTextureForSelectedObject(path, &errorMessage)) {
        m_historyTransactionOpen = false;
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("金属度粗糙度贴图加载失败。") : errorMessage);
        return;
    }

    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeTexturePreset(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setDemoTexturePreset(static_cast<DemoTexturePreset>(index));
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::changeAddressModeU(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setAddressModeU(static_cast<AddressMode>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeAddressModeV(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setAddressModeV(static_cast<AddressMode>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeNormalTextureFilter(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setNormalTextureFilter(static_cast<TextureFilter>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeNormalAddressModeU(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setNormalAddressModeU(static_cast<AddressMode>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeNormalAddressModeV(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setNormalAddressModeV(static_cast<AddressMode>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeMetallicRoughnessTextureFilter(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setMetallicRoughnessTextureFilter(static_cast<TextureFilter>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeMetallicRoughnessAddressModeU(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setMetallicRoughnessAddressModeU(static_cast<AddressMode>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeMetallicRoughnessAddressModeV(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setMetallicRoughnessAddressModeV(static_cast<AddressMode>(index));
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeLightDirection()
{
    const int directionalIndex = selectedDirectionalLightIndex();
    const int spotIndex = selectedSpotLightIndex();
    if (directionalIndex < 0 && spotIndex < 0)
        return;

    beginMergedHistoryTransaction(QStringLiteral("directionalLightDirection"));
    const Vec3f direction{static_cast<float>(m_lightDirectionXSpin->value()),
                          static_cast<float>(m_lightDirectionYSpin->value()),
                          static_cast<float>(m_lightDirectionZSpin->value())};
    if (directionalIndex >= 0) {
        m_rasterWidget->setDirectionalLightDirection(directionalIndex, direction);
    } else {
        m_rasterWidget->setSpotLightDirection(spotIndex, direction);
    }
    markScenePresetCustom();
    syncLightingControls();
}

void MainWindow::changeLightAmbient(double value)
{
    if (selectedLightIsDirectional()) {
        beginMergedHistoryTransaction(QStringLiteral("lightAmbient"));
        m_rasterWidget->setDirectionalLightAmbient(selectedDirectionalLightIndex(), static_cast<float>(value));
    } else if (selectedPointLightIndex() >= 0) {
        beginMergedHistoryTransaction(QStringLiteral("lightAmbient"));
        m_rasterWidget->setPointLightAmbient(selectedPointLightIndex(), static_cast<float>(value));
    } else if (selectedSpotLightIndex() >= 0) {
        beginMergedHistoryTransaction(QStringLiteral("lightAmbient"));
        m_rasterWidget->setSpotLightAmbient(selectedSpotLightIndex(), static_cast<float>(value));
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeLightIntensity(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("lightIntensity"));
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightIntensity(selectedDirectionalLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsPoint()) {
        m_rasterWidget->setPointLightIntensity(selectedPointLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsSpot()) {
        m_rasterWidget->setSpotLightIntensity(selectedSpotLightIndex(), static_cast<float>(value));
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeSelectedLight(int index)
{
    m_selectedLightIndex = index;
    syncLightingControls();
}

void MainWindow::changeSelectedLightType(int index)
{
    if (m_selectedLightIndex < 0)
        return;

    beginHistoryTransaction();
    if (index == 0) {
        if (selectedLightIsPoint()) {
            m_rasterWidget->convertPointLightToDirectional(selectedPointLightIndex());
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() - 1;
        } else if (selectedLightIsSpot()) {
            m_rasterWidget->convertSpotLightToDirectional(selectedSpotLightIndex());
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() - 1;
        } else {
            m_historyTransactionOpen = false;
            return;
        }
    } else if (index == 1) {
        if (selectedLightIsDirectional()) {
            const int pointCountBefore = m_rasterWidget->pointLightCount();
            m_rasterWidget->convertDirectionalLightToPoint(selectedDirectionalLightIndex());
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() + pointCountBefore;
        } else if (selectedLightIsSpot()) {
            const int pointCountBefore = m_rasterWidget->pointLightCount();
            m_rasterWidget->convertSpotLightToPoint(selectedSpotLightIndex());
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() + pointCountBefore;
        } else {
            m_historyTransactionOpen = false;
            return;
        }
    } else if (index == 2) {
        if (selectedLightIsDirectional()) {
            const int spotCountBefore = m_rasterWidget->spotLightCount();
            m_rasterWidget->convertDirectionalLightToSpot(selectedDirectionalLightIndex());
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() + m_rasterWidget->pointLightCount() + spotCountBefore;
        } else if (selectedLightIsPoint()) {
            const int spotCountBefore = m_rasterWidget->spotLightCount();
            m_rasterWidget->convertPointLightToSpot(selectedPointLightIndex());
            m_selectedLightIndex = m_rasterWidget->directionalLightCount() + m_rasterWidget->pointLightCount() + spotCountBefore;
        } else {
            m_historyTransactionOpen = false;
            return;
        }
    } else {
        m_historyTransactionOpen = false;
        return;
    }

    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::changeLightEnabled(bool checked)
{
    beginHistoryTransaction();
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightEnabled(selectedDirectionalLightIndex(), checked);
    } else if (selectedPointLightIndex() >= 0) {
        m_rasterWidget->setPointLightEnabled(selectedPointLightIndex(), checked);
    } else if (selectedSpotLightIndex() >= 0) {
        m_rasterWidget->setSpotLightEnabled(selectedSpotLightIndex(), checked);
    }
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::addDirectionalLight()
{
    beginHistoryTransaction();
    m_rasterWidget->addDirectionalLight();
    m_selectedLightIndex = std::max(0, m_rasterWidget->directionalLightCount() - 1);
    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::renameSelectedLight()
{
    if (m_lightNameEdit == nullptr)
        return;

    const QString name = m_lightNameEdit->text();
    if (selectedLightIsDirectional()) {
        const int lightIndex = selectedDirectionalLightIndex();
        if (lightIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->setDirectionalLightName(lightIndex, name);
    } else if (selectedLightIsPoint()) {
        const int pointIndex = selectedPointLightIndex();
        if (pointIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->setPointLightName(pointIndex, name);
    } else if (selectedLightIsSpot()) {
        const int spotIndex = selectedSpotLightIndex();
        if (spotIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->setSpotLightName(spotIndex, name);
    } else {
        return;
    }
    markScenePresetCustom();
    commitHistoryTransaction();
}

void MainWindow::changeSelectedPointLight(int index)
{
    const int directionalCount = m_rasterWidget->directionalLightCount();
    m_selectedLightIndex = index >= 0 ? directionalCount + index : -1;
    syncLightingControls();
}

void MainWindow::addPointLight()
{
    beginHistoryTransaction();
    m_rasterWidget->addPointLight();
    m_selectedLightIndex = m_rasterWidget->directionalLightCount() + std::max(0, m_rasterWidget->pointLightCount() - 1);
    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::addSpotLight()
{
    beginHistoryTransaction();
    m_rasterWidget->addSpotLight();
    m_selectedLightIndex = m_rasterWidget->directionalLightCount()
        + m_rasterWidget->pointLightCount()
        + std::max(0, m_rasterWidget->spotLightCount() - 1);
    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::duplicateSelectedLight()
{
    if (selectedLightIsDirectional()) {
        const int lightIndex = selectedDirectionalLightIndex();
        if (lightIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->duplicateDirectionalLight(lightIndex);
        m_selectedLightIndex = m_rasterWidget->directionalLightCount() - 1;
    } else if (selectedLightIsPoint()) {
        const int pointIndex = selectedPointLightIndex();
        if (pointIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->duplicatePointLight(pointIndex);
        m_selectedLightIndex = m_rasterWidget->directionalLightCount() + m_rasterWidget->pointLightCount() - 1;
    } else if (selectedLightIsSpot()) {
        const int spotIndex = selectedSpotLightIndex();
        if (spotIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->duplicateSpotLight(spotIndex);
        m_selectedLightIndex = m_rasterWidget->directionalLightCount()
            + m_rasterWidget->pointLightCount()
            + m_rasterWidget->spotLightCount() - 1;
    } else {
        return;
    }
    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::removeSelectedLight()
{
    if (selectedLightIsDirectional()) {
        const int lightIndex = selectedDirectionalLightIndex();
        if (lightIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->removeDirectionalLight(lightIndex);
    } else if (selectedLightIsPoint()) {
        const int pointIndex = selectedPointLightIndex();
        if (pointIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->removePointLight(pointIndex);
    } else if (selectedLightIsSpot()) {
        const int spotIndex = selectedSpotLightIndex();
        if (spotIndex < 0)
            return;
        beginHistoryTransaction();
        m_rasterWidget->removeSpotLight(spotIndex);
    } else {
        return;
    }
    const int totalLightCount = m_rasterWidget->directionalLightCount()
        + m_rasterWidget->pointLightCount()
        + m_rasterWidget->spotLightCount();
    m_selectedLightIndex = totalLightCount > 0 ? std::clamp(m_selectedLightIndex, 0, totalLightCount - 1) : -1;
    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::removeSelectedPointLight()
{
    removeSelectedLight();
}

void MainWindow::changePointLightPosition()
{
    if (!selectedLightIsPoint() && !selectedLightIsSpot())
        return;

    beginMergedHistoryTransaction(QStringLiteral("lightPosition"));
    const Vec3f position{static_cast<float>(m_pointLightPositionXSpin->value()),
                         static_cast<float>(m_pointLightPositionYSpin->value()),
                         static_cast<float>(m_pointLightPositionZSpin->value())};
    if (selectedLightIsPoint())
        m_rasterWidget->setPointLightPosition(selectedPointLightIndex(), position);
    else
        m_rasterWidget->setSpotLightPosition(selectedSpotLightIndex(), position);
    markScenePresetCustom();
}

void MainWindow::changePointLightColor()
{
    const Vec3f color{
        static_cast<float>(m_pointLightColorRSpin->value()),
        static_cast<float>(m_pointLightColorGSpin->value()),
        static_cast<float>(m_pointLightColorBSpin->value())
    };

    if (selectedLightIsDirectional()) {
        const int lightIndex = selectedDirectionalLightIndex();
        if (lightIndex < 0)
            return;
        beginMergedHistoryTransaction(QStringLiteral("lightColor"));
        m_rasterWidget->setDirectionalLightColor(lightIndex, color);
    } else if (selectedLightIsPoint()) {
        const int pointIndex = selectedPointLightIndex();
        if (pointIndex < 0)
            return;
        beginMergedHistoryTransaction(QStringLiteral("lightColor"));
        m_rasterWidget->setPointLightColor(pointIndex, color);
    } else if (selectedLightIsSpot()) {
        const int spotIndex = selectedSpotLightIndex();
        if (spotIndex < 0)
            return;
        beginMergedHistoryTransaction(QStringLiteral("lightColor"));
        m_rasterWidget->setSpotLightColor(spotIndex, color);
    } else {
        return;
    }

    markScenePresetCustom();
}

void MainWindow::changePointLightIntensity(double value)
{
    if (!selectedLightIsPoint() && !selectedLightIsSpot())
        return;

    beginMergedHistoryTransaction(QStringLiteral("lightIntensity"));
    if (selectedLightIsPoint())
        m_rasterWidget->setPointLightIntensity(selectedPointLightIndex(), static_cast<float>(value));
    else
        m_rasterWidget->setSpotLightIntensity(selectedSpotLightIndex(), static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changePointLightRange(double value)
{
    if (!selectedLightIsPoint() && !selectedLightIsSpot())
        return;

    beginMergedHistoryTransaction(QStringLiteral("lightRange"));
    if (selectedLightIsPoint())
        m_rasterWidget->setPointLightRange(selectedPointLightIndex(), static_cast<float>(value));
    else
        m_rasterWidget->setSpotLightRange(selectedSpotLightIndex(), static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeSpecularColor()//修改高光颜色
{
    beginMergedHistoryTransaction(QStringLiteral("specularColor"));
    m_rasterWidget->setSpecularColor({
        static_cast<float>(m_specularColorRSpin->value()),
        static_cast<float>(m_specularColorGSpin->value()),
        static_cast<float>(m_specularColorBSpin->value())
    });
    markScenePresetCustom();
    syncMaterialControls();
}

void MainWindow::changeSpecularStrength(double value)//修改高光强度
{
    beginMergedHistoryTransaction(QStringLiteral("specularStrength"));
    m_rasterWidget->setSpecularStrength(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeShininess(double value)//修改高光锐度
{
    beginMergedHistoryTransaction(QStringLiteral("shininess"));
    m_rasterWidget->setShininess(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeNormalStrength(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("normalStrength"));
    m_rasterWidget->setNormalStrength(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeMetallic(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("metallic"));
    m_rasterWidget->setMetallic(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeRoughness(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("roughness"));
    m_rasterWidget->setRoughness(static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeShadowCast(bool checked)
{
    const int directionalIndex = selectedDirectionalLightIndex();
    const int pointIndex = selectedPointLightIndex();
    const int spotIndex = selectedSpotLightIndex();
    if (directionalIndex < 0 && pointIndex < 0 && spotIndex < 0)
        return;

    beginHistoryTransaction();
    if (directionalIndex >= 0) {
        m_rasterWidget->setDirectionalLightShadowCastEnable(directionalIndex, checked);
    } else if (pointIndex >= 0) {
        m_rasterWidget->setPointLightShadowCastEnable(pointIndex, checked);
    } else {
        m_rasterWidget->setSpotLightShadowCastEnable(spotIndex, checked);
    }
    markScenePresetCustom();
    syncLightingControls();
    commitHistoryTransaction();
}

void MainWindow::changeShadowStrength(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("shadowStrength"));
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightShadowStrength(selectedDirectionalLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsPoint()) {
        m_rasterWidget->setPointLightShadowStrength(selectedPointLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsSpot()) {
        m_rasterWidget->setSpotLightShadowStrength(selectedSpotLightIndex(), static_cast<float>(value));
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeShadowBias(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("shadowBias"));
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightShadowBias(selectedDirectionalLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsPoint()) {
        m_rasterWidget->setPointLightShadowBias(selectedPointLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsSpot()) {
        m_rasterWidget->setSpotLightShadowBias(selectedSpotLightIndex(), static_cast<float>(value));
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeShadowMapSize(int value)
{
    beginMergedHistoryTransaction(QStringLiteral("shadowMapSize"));
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightShadowMapSize(selectedDirectionalLightIndex(), value);
    } else if (selectedLightIsPoint()) {
        m_rasterWidget->setPointLightShadowMapSize(selectedPointLightIndex(), value);
    } else if (selectedLightIsSpot()) {
        m_rasterWidget->setSpotLightShadowMapSize(selectedSpotLightIndex(), value);
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeShadowCoverage(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("shadowCoverage"));
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightShadowCoverage(selectedDirectionalLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsPoint()) {
        m_rasterWidget->setPointLightShadowRange(selectedPointLightIndex(), static_cast<float>(value));
    } else if (selectedLightIsSpot()) {
        m_rasterWidget->setSpotLightShadowRange(selectedSpotLightIndex(), static_cast<float>(value));
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeShadowFilterQuality(int index)
{
    beginMergedHistoryTransaction(QStringLiteral("shadowFilterQuality"));
    const auto quality = static_cast<ShadowFilterQuality>(index);
    if (selectedLightIsDirectional()) {
        m_rasterWidget->setDirectionalLightShadowFilterQuality(selectedDirectionalLightIndex(), quality);
    } else if (selectedLightIsPoint()) {
        m_rasterWidget->setPointLightShadowFilterQuality(selectedPointLightIndex(), quality);
    } else if (selectedLightIsSpot()) {
        m_rasterWidget->setSpotLightShadowFilterQuality(selectedSpotLightIndex(), quality);
    } else {
        return;
    }
    markScenePresetCustom();
}

void MainWindow::changeSpotLightInnerCone(double value)
{
    const int spotIndex = selectedSpotLightIndex();
    if (spotIndex < 0)
        return;
    beginMergedHistoryTransaction(QStringLiteral("spotInnerCone"));
    m_rasterWidget->setSpotLightInnerConeDegrees(spotIndex, static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::changeSpotLightOuterCone(double value)
{
    const int spotIndex = selectedSpotLightIndex();
    if (spotIndex < 0)
        return;
    beginMergedHistoryTransaction(QStringLiteral("spotOuterCone"));
    m_rasterWidget->setSpotLightOuterConeDegrees(spotIndex, static_cast<float>(value));
    markScenePresetCustom();
}

void MainWindow::resetDemoMaterial()
{
    beginHistoryTransaction();
    m_rasterWidget->resetDemoMaterial();
    markScenePresetCustom();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::loadObjModel()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开 OBJ 模型"),
                                                      QString(),
                                                      QStringLiteral("OBJ 模型 (*.obj)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    beginHistoryTransaction();
    if (!m_rasterWidget->loadObjModel(path, &errorMessage)) {
        m_historyTransactionOpen = false;
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("OBJ 模型加载失败。") : errorMessage);
        return;
    }

    markPresetCustom();
    markScenePresetCustom();
    syncSceneContentLabel();
    syncObjectControls();
    syncMaterialControls();
    commitHistoryTransaction();
}

void MainWindow::loadPhotoLineArt()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开照片"),
                                                      QString(),
                                                      QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->loadPhotoLineArt(path, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("生成失败"),
                             errorMessage.isEmpty() ? QStringLiteral("照片线稿生成失败。") : errorMessage);
        return;
    }

    syncSceneContentLabel();
    syncObjectControls();
    syncMaterialControls();
    syncLineArtControls();
}

void MainWindow::saveLineArt()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存线稿"),
                                                      QStringLiteral("line_art.png"),
                                                      QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;BMP 图片 (*.bmp)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->saveLineArtImage(path, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             errorMessage.isEmpty() ? QStringLiteral("线稿图保存失败。") : errorMessage);
    }
}

void MainWindow::saveTransparentLineArt()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存透明线稿"),
                                                      QStringLiteral("line_art_transparent.png"),
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->saveTransparentLineArtImage(path, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             errorMessage.isEmpty() ? QStringLiteral("透明线稿保存失败。") : errorMessage);
    }
}

void MainWindow::saveViewportScreenshot()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存截图"),
                                                      QStringLiteral("viewport.png"),
                                                      QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;BMP 图片 (*.bmp)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->saveViewportScreenshot(path, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             errorMessage.isEmpty() ? QStringLiteral("截图保存失败。") : errorMessage);
    }
}

void MainWindow::exportViewportSequence()
{
    const QString outputDirectory = QFileDialog::getExistingDirectory(this,
                                                                      QStringLiteral("选择序列导出目录"),
                                                                      QString());
    if (outputDirectory.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->exportOrbitSequence(outputDirectory,
                                             m_sequenceFrameCountSpin->value(),
                                             static_cast<float>(m_sequenceOrbitDegreesSpin->value()),
                                             &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("导出失败"),
                             errorMessage.isEmpty() ? QStringLiteral("序列导出失败。") : errorMessage);
        return;
    }

    QMessageBox::information(this,
                             QStringLiteral("序列导出"),
                             QStringLiteral("视口序列导出完成。"));
}

void MainWindow::exportDebugViews()
{
    const QString outputDirectory = QFileDialog::getExistingDirectory(this,
                                                                      QStringLiteral("选择调试视图导出目录"),
                                                                      QString());
    if (outputDirectory.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->exportDebugViews(outputDirectory, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("导出失败"),
                             errorMessage.isEmpty() ? QStringLiteral("调试视图导出失败。") : errorMessage);
        return;
    }

    QMessageBox::information(this,
                             QStringLiteral("调试视图导出"),
                             QStringLiteral("调试视图批量导出完成。"));
}

void MainWindow::changeLineArtThreshold(double value)
{
    m_rasterWidget->setLineArtThresholdScale(static_cast<float>(value));
}

void MainWindow::changeLineArtStrength(double value)
{
    m_rasterWidget->setLineArtLineStrength(static_cast<float>(value));
}

void MainWindow::changeLineArtProcessScale(int value)
{
    m_rasterWidget->setLineArtProcessScale(static_cast<float>(value) / 100.0f);
}

void MainWindow::changeLineArtGrayBase(bool checked)
{
    m_rasterWidget->setLineArtKeepGrayBase(checked);
}

void MainWindow::changeLineArtComparePreview(bool checked)
{
    m_rasterWidget->setLineArtComparePreview(checked);
}

void MainWindow::changeLineArtThresholdCurvePreset(int index)
{
    m_rasterWidget->setLineArtThresholdCurvePreset(static_cast<LineArtThresholdCurvePreset>(index));
}

void MainWindow::changeLineArtEdgeMode(int index)
{
    m_rasterWidget->setLineArtEdgeMode(static_cast<LineArtEdgeMode>(index));
}

void MainWindow::changeLineArtTransparentStrokeWidth(double value)
{
    m_rasterWidget->setLineArtTransparentStrokeWidth(static_cast<float>(value));
}

void MainWindow::batchExportLineArt()
{
    const QStringList inputPaths = QFileDialog::getOpenFileNames(this,
                                                                 QStringLiteral("选择要批量导出的图片"),
                                                                 QString(),
                                                                 QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (inputPaths.isEmpty())
        return;

    const QString outputDirectory = QFileDialog::getExistingDirectory(this,
                                                                      QStringLiteral("选择线稿导出目录"),
                                                                      QString());
    if (outputDirectory.isEmpty())
        return;

    QString errorMessage;
    const bool success = m_rasterWidget->batchExportLineArt(inputPaths, outputDirectory, &errorMessage);
    if (!success) {
        QMessageBox::warning(this,
                             QStringLiteral("批量导出"),
                             errorMessage.isEmpty() ? QStringLiteral("批量导出未全部成功。") : errorMessage);
        return;
    }

    QMessageBox::information(this,
                             QStringLiteral("批量导出"),
                             QStringLiteral("线稿批量导出完成。"));
}

void MainWindow::saveLineArtConfig()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存线稿配置"),
                                                      QStringLiteral("line_art_config.json"),
                                                      QStringLiteral("线稿配置 (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             QStringLiteral("无法写入文件：%1").arg(path));
        return;
    }

    const QJsonDocument document(m_rasterWidget->saveLineArtConfig());
    file.write(document.toJson(QJsonDocument::Indented));
}

void MainWindow::loadLineArtConfig()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开线稿配置"),
                                                      QString(),
                                                      QStringLiteral("线稿配置 (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             QStringLiteral("无法读取文件：%1").arg(path));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             QStringLiteral("线稿配置文件不是有效的 JSON 对象。"));
        return;
    }

    QString errorMessage;
    if (!m_rasterWidget->loadLineArtConfig(document.object(), &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("线稿配置恢复失败。") : errorMessage);
        return;
    }

    syncSceneContentLabel();
    syncObjectControls();
    syncMaterialControls();
    syncLineArtControls();
    syncCameraControls();
}

void MainWindow::saveScene()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存场景"),
                                                      QString(),
                                                      QStringLiteral("软光栅场景 (*.json)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    if (!m_rasterWidget->saveSceneToFile(path, &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             errorMessage.isEmpty() ? QStringLiteral("场景保存失败。") : errorMessage);
    }
}

void MainWindow::loadScene()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开场景"),
                                                      QString(),
                                                      QStringLiteral("软光栅场景 (*.json)"));
    if (path.isEmpty())
        return;

    QString errorMessage;
    beginHistoryTransaction();
    if (!m_rasterWidget->loadSceneFromFile(path, &errorMessage)) {
        m_historyTransactionOpen = false;
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty() ? QStringLiteral("场景恢复失败。") : errorMessage);
        return;
    }

    syncAllControls();
    commitHistoryTransaction();
}

void MainWindow::addDemoCubeObject()
{
    beginHistoryTransaction();
    m_rasterWidget->addDemoCubeObject();
    markScenePresetCustom();
    syncSceneContentLabel();
    syncObjectControls();
    syncMaterialControls();
    syncLineArtControls();
    commitHistoryTransaction();
}

void MainWindow::renameSelectedObject()
{
    beginHistoryTransaction();
    m_rasterWidget->renameSelectedSceneObject(m_objectNameEdit->text());
    syncObjectControls();
    commitHistoryTransaction();
}

void MainWindow::changeSelectedObject(int index)
{
    m_rasterWidget->setSelectedSceneObjectIndex(index);
    syncObjectControls();
    syncMaterialControls();
    syncLineArtControls();
}

void MainWindow::changeSelectedObjectTransform()
{
    beginMergedHistoryTransaction(QStringLiteral("objectTransform"));
    Transform transform = m_rasterWidget->selectedSceneObjectTransform();
    transform.position = {
        static_cast<float>(m_positionXSpin->value()),
        static_cast<float>(m_positionYSpin->value()),
        static_cast<float>(m_positionZSpin->value())
    };
    transform.rotationRadians = {
        degreesToRadians(m_rotationXSpin->value()),
        degreesToRadians(m_rotationYSpin->value()),
        degreesToRadians(m_rotationZSpin->value())
    };
    transform.scale = {
        static_cast<float>(m_scaleXSpin->value()),
        static_cast<float>(m_scaleYSpin->value()),
        static_cast<float>(m_scaleZSpin->value())
    };
    m_rasterWidget->setSelectedSceneObjectTransform(transform);
    markScenePresetCustom();
}

void MainWindow::changeGizmoSpaceMode(int index)
{
    beginHistoryTransaction();
    m_rasterWidget->setGizmoSpaceMode(static_cast<GizmoSpaceMode>(index));
    commitHistoryTransaction();
}

void MainWindow::changeGizmoTranslationSnapStep(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("gizmoTranslationSnap"));
    m_rasterWidget->setGizmoTranslationSnapStep(static_cast<float>(value));
}

void MainWindow::changeGizmoRotationSnapDegrees(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("gizmoRotationSnap"));
    m_rasterWidget->setGizmoRotationSnapDegrees(static_cast<float>(value));
}

void MainWindow::changeGizmoScaleSnapStep(double value)
{
    beginMergedHistoryTransaction(QStringLiteral("gizmoScaleSnap"));
    m_rasterWidget->setGizmoScaleSnapStep(static_cast<float>(value));
}

void MainWindow::removeSelectedObject()
{
    beginHistoryTransaction();
    m_rasterWidget->removeSelectedSceneObject();
    markScenePresetCustom();
    syncSceneContentLabel();
    syncObjectControls();
    syncLineArtControls();
    commitHistoryTransaction();
}

void MainWindow::useDemoCube()
{
    beginHistoryTransaction();
    m_rasterWidget->useDemoCube();
    syncSceneContentLabel();
    syncLineArtControls();
    commitHistoryTransaction();
}

void MainWindow::saveCameraPreset()
{
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存相机预设"),
                                                      QString(),
                                                      QStringLiteral("相机预设 (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this,
                             QStringLiteral("保存失败"),
                             QStringLiteral("无法写入相机预设文件：%1").arg(path));
        return;
    }

    const QJsonDocument document(m_rasterWidget->saveCameraPreset());
    file.write(document.toJson(QJsonDocument::Indented));
}

void MainWindow::loadCameraPreset()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("加载相机预设"),
                                                      QString(),
                                                      QStringLiteral("相机预设 (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             QStringLiteral("无法打开相机预设文件：%1").arg(path));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             QStringLiteral("相机预设文件格式无效：%1").arg(path));
        return;
    }

    QString errorMessage;
    if (!m_rasterWidget->loadCameraPreset(document.object(), &errorMessage)) {
        QMessageBox::warning(this,
                             QStringLiteral("加载失败"),
                             errorMessage.isEmpty()
                                 ? QStringLiteral("相机预设恢复失败。")
                                 : errorMessage);
        return;
    }

    syncScenePresetControl();
    syncCameraControls();
}

void MainWindow::changeCameraProjectionMode(int index)
{
    m_rasterWidget->setCameraProjectionMode(static_cast<CameraProjectionMode>(index));
    syncScenePresetControl();
}

void MainWindow::changeCameraVerticalFov(double value)
{
    m_rasterWidget->setCameraVerticalFovDegrees(static_cast<float>(value));
    syncScenePresetControl();
}

void MainWindow::changeCameraOrthographicHeight(double value)
{
    m_rasterWidget->setCameraOrthographicHeight(static_cast<float>(value));
    syncScenePresetControl();
}

void MainWindow::changeCameraNearPlane(double value)
{
    m_rasterWidget->setCameraNearPlane(static_cast<float>(value));
    syncScenePresetControl();
}

void MainWindow::changeCameraFarPlane(double value)
{
    m_rasterWidget->setCameraFarPlane(static_cast<float>(value));
    syncScenePresetControl();
}

void MainWindow::changeCameraMoveSpeed(double value)
{
    m_rasterWidget->setCameraMoveSpeed(static_cast<float>(value));
    syncScenePresetControl();
}

void MainWindow::resetCameraView()
{
    m_rasterWidget->resetCameraView();
    syncScenePresetControl();
}

void MainWindow::setCameraFrontView()
{
    m_rasterWidget->setCameraAxisView(CameraAxisView::Front);
    syncScenePresetControl();
}

void MainWindow::setCameraBackView()
{
    m_rasterWidget->setCameraAxisView(CameraAxisView::Back);
    syncScenePresetControl();
}

void MainWindow::setCameraLeftView()
{
    m_rasterWidget->setCameraAxisView(CameraAxisView::Left);
    syncScenePresetControl();
}

void MainWindow::setCameraRightView()
{
    m_rasterWidget->setCameraAxisView(CameraAxisView::Right);
    syncScenePresetControl();
}

void MainWindow::setCameraTopView()
{
    m_rasterWidget->setCameraAxisView(CameraAxisView::Top);
    syncScenePresetControl();
}

void MainWindow::setCameraBottomView()
{
    m_rasterWidget->setCameraAxisView(CameraAxisView::Bottom);
    syncScenePresetControl();
}

void MainWindow::undoSceneEdit()
{
    flushMergedHistoryTransaction();
    if (m_undoHistory.isEmpty())
        return;

    const QByteArray currentSnapshot = currentSceneHistorySnapshot();
    const QByteArray targetSnapshot = m_undoHistory.takeLast();
    m_redoHistory.push_back(currentSnapshot);
    if (!restoreHistorySnapshot(targetSnapshot)) {
        m_redoHistory.pop_back();
        m_undoHistory.push_back(targetSnapshot);
        updateHistoryActions();
        return;
    }

    updateHistoryActions();
}

void MainWindow::redoSceneEdit()
{
    flushMergedHistoryTransaction();
    if (m_redoHistory.isEmpty())
        return;

    const QByteArray currentSnapshot = currentSceneHistorySnapshot();
    const QByteArray targetSnapshot = m_redoHistory.takeLast();
    m_undoHistory.push_back(currentSnapshot);
    if (!restoreHistorySnapshot(targetSnapshot)) {
        m_undoHistory.pop_back();
        m_redoHistory.push_back(targetSnapshot);
        updateHistoryActions();
        return;
    }

    updateHistoryActions();
}

void MainWindow::createUi()
{
    // 把软光栅化显示控件直接作为中央区域。
    m_rasterWidget = new RasterWidget(this);
    m_rasterWidget->setToolTip(QStringLiteral("左键拖动：轨道旋转 / Gizmo\n中键拖动：平移\n右键拖动：看向\n滚轮：缩放\nWASD/QE：自由飞行\nShift：吸附 / 飞行加速\nCtrl：Gizmo 细调"));
    setCentralWidget(m_rasterWidget);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction *loadSceneAction = fileMenu->addAction(QStringLiteral("打开场景..."));
    QAction *saveSceneAction = fileMenu->addAction(QStringLiteral("保存场景..."));
    fileMenu->addSeparator();
    QAction *saveScreenshotAction = fileMenu->addAction(QStringLiteral("保存截图..."));
    QAction *exportSequenceAction = fileMenu->addAction(QStringLiteral("导出环绕序列..."));
    QAction *exportDebugViewsAction = fileMenu->addAction(QStringLiteral("批量导出调试视图..."));
    fileMenu->addSeparator();
    QAction *loadPhotoLineArtAction = fileMenu->addAction(QStringLiteral("打开照片生成线稿..."));
    QAction *saveLineArtAction = fileMenu->addAction(QStringLiteral("保存线稿..."));
    QAction *saveTransparentLineArtAction = fileMenu->addAction(QStringLiteral("保存透明线稿..."));
    QAction *batchExportLineArtAction = fileMenu->addAction(QStringLiteral("批量导出线稿..."));
    QAction *saveLineArtConfigAction = fileMenu->addAction(QStringLiteral("保存线稿配置..."));
    QAction *loadLineArtConfigAction = fileMenu->addAction(QStringLiteral("加载线稿配置..."));
    fileMenu->addSeparator();
    QAction *loadObjAction = fileMenu->addAction(QStringLiteral("加载 OBJ..."));
    QAction *loadTextureAction = fileMenu->addAction(QStringLiteral("加载对象纹理..."));
    QAction *useDemoCubeAction = fileMenu->addAction(QStringLiteral("恢复示例立方体"));

    connect(loadSceneAction, &QAction::triggered, this, &MainWindow::loadScene);
    connect(saveSceneAction, &QAction::triggered, this, &MainWindow::saveScene);
    connect(saveScreenshotAction, &QAction::triggered, this, &MainWindow::saveViewportScreenshot);
    connect(exportSequenceAction, &QAction::triggered, this, &MainWindow::exportViewportSequence);
    connect(exportDebugViewsAction, &QAction::triggered, this, &MainWindow::exportDebugViews);
    connect(loadPhotoLineArtAction, &QAction::triggered, this, &MainWindow::loadPhotoLineArt);
    connect(saveLineArtAction, &QAction::triggered, this, &MainWindow::saveLineArt);
    connect(saveTransparentLineArtAction, &QAction::triggered, this, &MainWindow::saveTransparentLineArt);
    connect(batchExportLineArtAction, &QAction::triggered, this, &MainWindow::batchExportLineArt);
    connect(saveLineArtConfigAction, &QAction::triggered, this, &MainWindow::saveLineArtConfig);
    connect(loadLineArtConfigAction, &QAction::triggered, this, &MainWindow::loadLineArtConfig);
    connect(loadObjAction, &QAction::triggered, this, &MainWindow::loadObjModel);
    connect(loadTextureAction, &QAction::triggered, this, &MainWindow::loadSelectedObjectTexture);
    connect(useDemoCubeAction, &QAction::triggered, this, &MainWindow::useDemoCube);

    QMenu *editMenu = menuBar()->addMenu(QStringLiteral("编辑"));
    m_undoAction = editMenu->addAction(QStringLiteral("撤销"));
    m_redoAction = editMenu->addAction(QStringLiteral("重做"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoSceneEdit);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoSceneEdit);

    QMenu *objectMenu = menuBar()->addMenu(QStringLiteral("对象"));
    QAction *addCubeAction = objectMenu->addAction(QStringLiteral("添加示例立方体"));
    QAction *removeObjectAction = objectMenu->addAction(QStringLiteral("删除当前对象"));
    connect(addCubeAction, &QAction::triggered, this, &MainWindow::addDemoCubeObject);
    connect(removeObjectAction, &QAction::triggered, this, &MainWindow::removeSelectedObject);

    QMenu *cameraMenu = menuBar()->addMenu(QStringLiteral("相机"));
    QAction *resetCameraAction = cameraMenu->addAction(QStringLiteral("重置相机"));
    QAction *saveCameraPresetAction = cameraMenu->addAction(QStringLiteral("保存相机预设..."));
    QAction *loadCameraPresetAction = cameraMenu->addAction(QStringLiteral("加载相机预设..."));
    cameraMenu->addSeparator();
    QAction *frontViewAction = cameraMenu->addAction(QStringLiteral("前视图"));
    QAction *backViewAction = cameraMenu->addAction(QStringLiteral("后视图"));
    QAction *leftViewAction = cameraMenu->addAction(QStringLiteral("左视图"));
    QAction *rightViewAction = cameraMenu->addAction(QStringLiteral("右视图"));
    QAction *topViewAction = cameraMenu->addAction(QStringLiteral("俯视图"));
    QAction *bottomViewAction = cameraMenu->addAction(QStringLiteral("底视图"));
    connect(resetCameraAction, &QAction::triggered, this, &MainWindow::resetCameraView);
    connect(saveCameraPresetAction, &QAction::triggered, this, &MainWindow::saveCameraPreset);
    connect(loadCameraPresetAction, &QAction::triggered, this, &MainWindow::loadCameraPreset);
    connect(frontViewAction, &QAction::triggered, this, &MainWindow::setCameraFrontView);
    connect(backViewAction, &QAction::triggered, this, &MainWindow::setCameraBackView);
    connect(leftViewAction, &QAction::triggered, this, &MainWindow::setCameraLeftView);
    connect(rightViewAction, &QAction::triggered, this, &MainWindow::setCameraRightView);
    connect(topViewAction, &QAction::triggered, this, &MainWindow::setCameraTopView);
    connect(bottomViewAction, &QAction::triggered, this, &MainWindow::setCameraBottomView);
}

void MainWindow::configureDockFormLayout(QFormLayout *layout) const
{
    if (layout == nullptr)
        return;

    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(8);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

void MainWindow::setScrollableDockWidget(QDockWidget *dock, QWidget *panel, int minimumWidth)
{
    if (dock == nullptr || panel == nullptr)
        return;

    panel->setMinimumWidth(std::max(300, minimumWidth - 24));
    QScrollArea *scrollArea = new QScrollArea(dock);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(panel);

    dock->setMinimumWidth(minimumWidth);
    dock->setWidget(scrollArea);
}

void MainWindow::arrangeRightDockTabs()
{
    setDockOptions(dockOptions() | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    if (m_objectDock == nullptr || m_materialDock == nullptr || m_lightingDock == nullptr
        || m_lineArtDock == nullptr || m_cameraDock == nullptr
        || m_postProcessDock == nullptr || m_performanceDock == nullptr) {
        return;
    }

    tabifyDockWidget(m_objectDock, m_materialDock);
    tabifyDockWidget(m_materialDock, m_lightingDock);
    tabifyDockWidget(m_lightingDock, m_lineArtDock);
    tabifyDockWidget(m_lineArtDock, m_cameraDock);
    tabifyDockWidget(m_cameraDock, m_postProcessDock);
    tabifyDockWidget(m_postProcessDock, m_performanceDock);
    m_objectDock->raise();
}

bool MainWindow::selectedLightIsDirectional() const
{
    return m_selectedLightIndex >= 0 && m_selectedLightIndex < m_rasterWidget->directionalLightCount();
}

bool MainWindow::selectedLightIsPoint() const
{
    return selectedPointLightIndex() >= 0;
}

bool MainWindow::selectedLightIsSpot() const
{
    return selectedSpotLightIndex() >= 0;
}

RasterWidget::SelectedLightKind MainWindow::selectedLightKind() const
{
    if (selectedLightIsDirectional())
        return RasterWidget::SelectedLightKind::Directional;
    if (selectedLightIsPoint())
        return RasterWidget::SelectedLightKind::Point;
    if (selectedLightIsSpot())
        return RasterWidget::SelectedLightKind::Spot;
    return RasterWidget::SelectedLightKind::None;
}

int MainWindow::selectedDirectionalLightIndex() const
{
    return selectedLightIsDirectional() ? m_selectedLightIndex : -1;
}

int MainWindow::selectedPointLightIndex() const
{
    if (m_selectedLightIndex < 0)
        return -1;
    const int directionalCount = m_rasterWidget->directionalLightCount();
    const int pointIndex = m_selectedLightIndex - directionalCount;
    return pointIndex >= 0 && pointIndex < m_rasterWidget->pointLightCount() ? pointIndex : -1;
}

int MainWindow::selectedSpotLightIndex() const
{
    if (m_selectedLightIndex < 0)
        return -1;
    const int directionalCount = m_rasterWidget->directionalLightCount();
    const int pointCount = m_rasterWidget->pointLightCount();
    const int spotIndex = m_selectedLightIndex - directionalCount - pointCount;
    return spotIndex >= 0 && spotIndex < m_rasterWidget->spotLightCount() ? spotIndex : -1;
}

void MainWindow::createObjectDock()
{
    m_objectDock = new QDockWidget(QStringLiteral("对象"), this);
    m_objectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_objectDock);
    QFormLayout *layout = new QFormLayout(panel);
    configureDockFormLayout(layout);

    m_objectCombo = new QComboBox(panel);
    m_objectNameEdit = new QLineEdit(panel);

    m_positionXSpin = new QDoubleSpinBox(panel);
    m_positionYSpin = new QDoubleSpinBox(panel);
    m_positionZSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_positionXSpin, m_positionYSpin, m_positionZSpin}) {
        spin->setRange(-1000.0, 1000.0);
        spin->setSingleStep(0.1);
        spin->setDecimals(2);
    }

    m_rotationXSpin = new QDoubleSpinBox(panel);
    m_rotationYSpin = new QDoubleSpinBox(panel);
    m_rotationZSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_rotationXSpin, m_rotationYSpin, m_rotationZSpin}) {
        spin->setRange(-3600.0, 3600.0);
        spin->setSingleStep(5.0);
        spin->setDecimals(1);
    }

    m_scaleXSpin = new QDoubleSpinBox(panel);
    m_scaleYSpin = new QDoubleSpinBox(panel);
    m_scaleZSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_scaleXSpin, m_scaleYSpin, m_scaleZSpin}) {
        spin->setRange(0.01, 1000.0);
        spin->setSingleStep(0.1);
        spin->setDecimals(2);
    }

    m_gizmoSpaceCombo = new QComboBox(panel);
    m_gizmoSpaceCombo->addItem(QStringLiteral("世界坐标"));
    m_gizmoSpaceCombo->addItem(QStringLiteral("局部坐标"));

    m_gizmoTranslationSnapSpin = new QDoubleSpinBox(panel);
    m_gizmoTranslationSnapSpin->setRange(0.01, 100.0);
    m_gizmoTranslationSnapSpin->setSingleStep(0.05);
    m_gizmoTranslationSnapSpin->setDecimals(2);

    m_gizmoRotationSnapSpin = new QDoubleSpinBox(panel);
    m_gizmoRotationSnapSpin->setRange(1.0, 180.0);
    m_gizmoRotationSnapSpin->setSingleStep(1.0);
    m_gizmoRotationSnapSpin->setDecimals(1);
    m_gizmoRotationSnapSpin->setSuffix(QStringLiteral("°"));

    m_gizmoScaleSnapSpin = new QDoubleSpinBox(panel);
    m_gizmoScaleSnapSpin->setRange(0.01, 10.0);
    m_gizmoScaleSnapSpin->setSingleStep(0.05);
    m_gizmoScaleSnapSpin->setDecimals(2);

    m_addCubeButton = new QPushButton(QStringLiteral("添加立方体"), panel);
    m_removeObjectButton = new QPushButton(QStringLiteral("删除当前对象"), panel);

    layout->addRow(QStringLiteral("当前对象"), m_objectCombo);
    layout->addRow(QStringLiteral("对象名称"), m_objectNameEdit);
    layout->addRow(QStringLiteral("位置 X"), m_positionXSpin);
    layout->addRow(QStringLiteral("位置 Y"), m_positionYSpin);
    layout->addRow(QStringLiteral("位置 Z"), m_positionZSpin);
    layout->addRow(QStringLiteral("旋转 X"), m_rotationXSpin);
    layout->addRow(QStringLiteral("旋转 Y"), m_rotationYSpin);
    layout->addRow(QStringLiteral("旋转 Z"), m_rotationZSpin);
    layout->addRow(QStringLiteral("缩放 X"), m_scaleXSpin);
    layout->addRow(QStringLiteral("缩放 Y"), m_scaleYSpin);
    layout->addRow(QStringLiteral("缩放 Z"), m_scaleZSpin);
    layout->addRow(QStringLiteral("Gizmo 坐标"), m_gizmoSpaceCombo);
    layout->addRow(QStringLiteral("平移吸附"), m_gizmoTranslationSnapSpin);
    layout->addRow(QStringLiteral("旋转吸附"), m_gizmoRotationSnapSpin);
    layout->addRow(QStringLiteral("缩放吸附"), m_gizmoScaleSnapSpin);
    layout->addRow(QString(), m_addCubeButton);
    layout->addRow(QString(), m_removeObjectButton);

    setScrollableDockWidget(m_objectDock, panel);
    addDockWidget(Qt::RightDockWidgetArea, m_objectDock);
    syncObjectControls();
}

void MainWindow::createStatusBar()
{
    // 初始值先写死，等第一帧渲染完成后再被真实统计覆盖。
    m_resolutionLabel = new QLabel(QStringLiteral("0 x 0"), this);
    m_statsLabel = new QLabel(QStringLiteral("三角形 0  像素 0"), this);
    m_parallelStatsLabel = new QLabel(QStringLiteral("并行 串 | 线程 0 | Tile 0 | 任务 0"), this);
    m_modelLabel = new QLabel(this);
    m_debugViewCombo = new QComboBox(this);
    m_scenePresetCombo = new QComboBox(this);
    m_presetCombo = new QComboBox(this);
    m_fillModeCombo = new QComboBox(this);
    m_cullModeCombo = new QComboBox(this);
    m_depthFuncCombo = new QComboBox(this);
    m_antiAliasingCombo = new QComboBox(this);

    m_scenePresetCombo->addItem(QStringLiteral("自定义"));
    m_scenePresetCombo->addItem(QStringLiteral("默认环绕"));
    m_scenePresetCombo->addItem(QStringLiteral("纹理观察"));
    m_scenePresetCombo->addItem(QStringLiteral("光照观察"));
    m_scenePresetCombo->addItem(QStringLiteral("线框检查"));
    m_scenePresetCombo->addItem(QStringLiteral("UV 检查"));
    m_scenePresetCombo->addItem(QStringLiteral("过绘制检查"));

    m_presetCombo->addItem(QStringLiteral("自定义"));
    m_presetCombo->addItem(QStringLiteral("着色"));
    m_presetCombo->addItem(QStringLiteral("线框"));
    m_presetCombo->addItem(QStringLiteral("深度"));
    m_presetCombo->addItem(QStringLiteral("法线"));
    m_presetCombo->addItem(QStringLiteral("UV"));
    m_presetCombo->addItem(QStringLiteral("过绘制"));
    m_presetCombo->setCurrentIndex(static_cast<int>(RenderPreset::Shaded));

    m_debugViewCombo->addItem(QStringLiteral("着色"));
    m_debugViewCombo->addItem(QStringLiteral("深度"));
    m_debugViewCombo->addItem(QStringLiteral("法线"));
    m_debugViewCombo->addItem(QStringLiteral("UV"));
    m_debugViewCombo->addItem(QStringLiteral("过绘制"));
    m_debugViewCombo->addItem(QStringLiteral("对象 ID"));
    m_debugViewCombo->addItem(QStringLiteral("材质 ID"));
    m_debugViewCombo->addItem(QStringLiteral("三角形 ID"));
    m_debugViewCombo->addItem(QStringLiteral("正反面朝向"));
    m_debugViewCombo->addItem(QStringLiteral("重心坐标"));
    m_debugViewCombo->addItem(QStringLiteral("阴影视图"));
    m_debugViewCombo->addItem(QStringLiteral("灯光调试"));

    m_fillModeCombo->addItem(QStringLiteral("实体"));
    m_fillModeCombo->addItem(QStringLiteral("线框"));

    m_cullModeCombo->addItem(QStringLiteral("无"));
    m_cullModeCombo->addItem(QStringLiteral("背面"));
    m_cullModeCombo->addItem(QStringLiteral("正面"));

    m_depthFuncCombo->addItem(QStringLiteral("从不"));
    m_depthFuncCombo->addItem(QStringLiteral("小于"));
    m_depthFuncCombo->addItem(QStringLiteral("小于等于"));
    m_depthFuncCombo->addItem(QStringLiteral("等于"));
    m_depthFuncCombo->addItem(QStringLiteral("不等于"));
    m_depthFuncCombo->addItem(QStringLiteral("大于"));
    m_depthFuncCombo->addItem(QStringLiteral("大于等于"));
    m_depthFuncCombo->addItem(QStringLiteral("总是"));

    m_antiAliasingCombo->addItem(QStringLiteral("关闭"));
    m_antiAliasingCombo->addItem(QStringLiteral("4x MSAA"));
    syncScenePresetControl();
    syncStateControls();

    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("场景"), this));
    statusBar()->addPermanentWidget(m_scenePresetCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("预设"), this));
    statusBar()->addPermanentWidget(m_presetCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("填充"), this));
    statusBar()->addPermanentWidget(m_fillModeCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("剔除"), this));
    statusBar()->addPermanentWidget(m_cullModeCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("深度"), this));
    statusBar()->addPermanentWidget(m_depthFuncCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("抗锯齿"), this));
    statusBar()->addPermanentWidget(m_antiAliasingCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("视图"), this));
    statusBar()->addPermanentWidget(m_debugViewCombo);
    statusBar()->addPermanentWidget(new QLabel(QStringLiteral("模型"), this));
    statusBar()->addPermanentWidget(m_modelLabel);
    statusBar()->addPermanentWidget(m_resolutionLabel);
    statusBar()->addPermanentWidget(m_statsLabel, 1);
    statusBar()->addPermanentWidget(m_parallelStatsLabel);
    syncSceneContentLabel();
    syncPerformanceControls();
}

void MainWindow::createMaterialDock()
{
    m_materialDock = new QDockWidget(QStringLiteral("材质"), this);
    m_materialDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_materialDock);
    QFormLayout *layout = new QFormLayout(panel);
    configureDockFormLayout(layout);

    m_textureFilterCombo = new QComboBox(panel);
    m_materialTypeCombo = new QComboBox(panel);
    m_materialSurfaceModeCombo = new QComboBox(panel);
    m_textureSourceLabel = new QLabel(panel);
    m_materialSourceLabel = new QLabel(panel);
    m_sharedMaterialWarningPanel = new QWidget(panel);
    m_sharedMaterialWarningLabel = new QLabel(panel);
    m_normalTextureSourceLabel = new QLabel(panel);
    m_metallicRoughnessTextureSourceLabel = new QLabel(panel);
    m_metallicRoughnessChannelLabel = new QLabel(panel);
    m_texturePresetCombo = new QComboBox(panel);
    m_materialUsageModeCombo = new QComboBox(panel);
    m_materialAssetCombo = new QComboBox(panel);
    m_sceneMaterialAssetCombo = new QComboBox(panel);
    m_sceneMaterialAssetUsageLabel = new QLabel(panel);
    m_sceneTextureAssetCombo = new QComboBox(panel);
    m_sceneTextureAssetUsageLabel = new QLabel(panel);
    m_objectTextureAssetCombo = new QComboBox(panel);
    m_objectNormalTextureAssetCombo = new QComboBox(panel);
    m_objectMetallicRoughnessTextureAssetCombo = new QComboBox(panel);
    m_materialTypeCombo->addItem(QStringLiteral("Lambert 贴图"));
    m_materialTypeCombo->addItem(QStringLiteral("Lambert 顶点色"));
    m_materialTypeCombo->addItem(QStringLiteral("无光照贴图"));
    m_materialTypeCombo->addItem(QStringLiteral("无光照顶点色"));
    m_materialTypeCombo->addItem(QStringLiteral("Blinn-Phong 贴图"));
    m_materialTypeCombo->addItem(QStringLiteral("Blinn-Phong 顶点色"));
    m_materialTypeCombo->addItem(QStringLiteral("PBR 贴图"));
    m_materialTypeCombo->addItem(QStringLiteral("PBR 顶点色"));
    m_textureFilterCombo->addItem(QStringLiteral("最近点"));
    m_textureFilterCombo->addItem(QStringLiteral("双线性"));
    m_textureFilterCombo->addItem(QStringLiteral("三线性 Mipmap"));
    m_materialSurfaceModeCombo->addItem(QStringLiteral("不透明"));
    m_materialSurfaceModeCombo->addItem(QStringLiteral("透明混合"));
    m_materialUsageModeCombo->addItem(QStringLiteral("对象独立"));
    m_materialUsageModeCombo->addItem(QStringLiteral("共享材质"));

    m_texturePresetCombo->addItem(QStringLiteral("暖色棋盘"));
    m_texturePresetCombo->addItem(QStringLiteral("黑白棋盘"));
    m_texturePresetCombo->addItem(QStringLiteral("纯白"));
    m_texturePresetCombo->addItem(QStringLiteral("渐变"));

    m_addressModeUCombo = new QComboBox(panel);
    m_addressModeUCombo->addItem(QStringLiteral("重复"));
    m_addressModeUCombo->addItem(QStringLiteral("夹取"));

    m_addressModeVCombo = new QComboBox(panel);
    m_addressModeVCombo->addItem(QStringLiteral("重复"));
    m_addressModeVCombo->addItem(QStringLiteral("夹取"));

    m_normalTextureFilterCombo = new QComboBox(panel);
    m_normalTextureFilterCombo->addItem(QStringLiteral("最近点"));
    m_normalTextureFilterCombo->addItem(QStringLiteral("双线性"));
    m_normalTextureFilterCombo->addItem(QStringLiteral("三线性 Mipmap"));

    m_normalAddressModeUCombo = new QComboBox(panel);
    m_normalAddressModeUCombo->addItem(QStringLiteral("重复"));
    m_normalAddressModeUCombo->addItem(QStringLiteral("夹取"));

    m_normalAddressModeVCombo = new QComboBox(panel);
    m_normalAddressModeVCombo->addItem(QStringLiteral("重复"));
    m_normalAddressModeVCombo->addItem(QStringLiteral("夹取"));

    m_metallicRoughnessTextureFilterCombo = new QComboBox(panel);
    m_metallicRoughnessTextureFilterCombo->addItem(QStringLiteral("最近点"));
    m_metallicRoughnessTextureFilterCombo->addItem(QStringLiteral("双线性"));
    m_metallicRoughnessTextureFilterCombo->addItem(QStringLiteral("三线性 Mipmap"));

    m_metallicRoughnessAddressModeUCombo = new QComboBox(panel);
    m_metallicRoughnessAddressModeUCombo->addItem(QStringLiteral("重复"));
    m_metallicRoughnessAddressModeUCombo->addItem(QStringLiteral("夹取"));

    m_metallicRoughnessAddressModeVCombo = new QComboBox(panel);
    m_metallicRoughnessAddressModeVCombo->addItem(QStringLiteral("重复"));
    m_metallicRoughnessAddressModeVCombo->addItem(QStringLiteral("夹取"));

    m_specularColorRSpin = new QDoubleSpinBox(panel);
    m_specularColorGSpin = new QDoubleSpinBox(panel);
    m_specularColorBSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_specularColorRSpin, m_specularColorGSpin, m_specularColorBSpin}) {
        spin->setRange(0.0, 1.0);
        spin->setSingleStep(0.05);
        spin->setDecimals(2);
    }

    m_specularStrengthSpin = new QDoubleSpinBox(panel);
    m_specularStrengthSpin->setRange(0.0, 4.0);
    m_specularStrengthSpin->setSingleStep(0.05);
    m_specularStrengthSpin->setDecimals(2);

    m_shininessSpin = new QDoubleSpinBox(panel);
    m_shininessSpin->setRange(1.0, 256.0);
    m_shininessSpin->setSingleStep(1.0);
    m_shininessSpin->setDecimals(1);

    m_normalStrengthSpin = new QDoubleSpinBox(panel);
    m_normalStrengthSpin->setRange(0.0, 2.0);
    m_normalStrengthSpin->setSingleStep(0.05);
    m_normalStrengthSpin->setDecimals(2);

    m_metallicSpin = new QDoubleSpinBox(panel);
    m_metallicSpin->setRange(0.0, 1.0);
    m_metallicSpin->setSingleStep(0.05);
    m_metallicSpin->setDecimals(2);

    m_roughnessSpin = new QDoubleSpinBox(panel);
    m_roughnessSpin->setRange(0.05, 1.0);
    m_roughnessSpin->setSingleStep(0.05);
    m_roughnessSpin->setDecimals(2);

    m_materialOpacitySpin = new QDoubleSpinBox(panel);
    m_materialOpacitySpin->setRange(0.0, 1.0);
    m_materialOpacitySpin->setSingleStep(0.05);
    m_materialOpacitySpin->setDecimals(2);

    m_materialDepthWriteCheck = new QCheckBox(QStringLiteral("透明对象写入深度"), panel);
    m_loadTextureButton = new QPushButton(QStringLiteral("加载外部纹理"), panel);
    m_loadNormalTextureButton = new QPushButton(QStringLiteral("加载法线贴图"), panel);
    m_loadMetallicRoughnessTextureButton = new QPushButton(QStringLiteral("加载金属粗糙贴图"), panel);
    m_duplicateMaterialAssetButton = new QPushButton(QStringLiteral("复制为共享材质"), panel);
    m_makeMaterialInstanceButton = new QPushButton(QStringLiteral("仅当前对象转为实例后编辑"), panel);
    m_duplicateSceneMaterialAssetButton = new QPushButton(QStringLiteral("复制场景材质"), panel);
    m_assignSceneMaterialAssetToSelectedButton = new QPushButton(QStringLiteral("应用到当前对象"), panel);
    m_assignSceneMaterialAssetToAllButton = new QPushButton(QStringLiteral("应用到全部对象"), panel);
    m_duplicateSceneTextureAssetButton = new QPushButton(QStringLiteral("复制场景纹理"), panel);
    m_assignSceneTextureToSelectedColorButton = new QPushButton(QStringLiteral("设为当前颜色纹理"), panel);
    m_assignSceneTextureToSelectedNormalButton = new QPushButton(QStringLiteral("设为当前法线贴图"), panel);
    m_assignSceneTextureToSelectedMetallicRoughnessButton = new QPushButton(QStringLiteral("设为当前金属粗糙贴图"), panel);
    m_assignSceneTextureToAllObjectsColorButton = new QPushButton(QStringLiteral("设为全部对象颜色纹理"), panel);
    m_clearObjectTextureAssetButton = new QPushButton(QStringLiteral("清空当前颜色纹理"), panel);
    m_clearObjectNormalTextureAssetButton = new QPushButton(QStringLiteral("清空当前法线贴图"), panel);
    m_clearObjectMetallicRoughnessTextureAssetButton = new QPushButton(QStringLiteral("清空当前金属粗糙贴图"), panel);
    m_renameMaterialAssetButton = new QPushButton(QStringLiteral("重命名共享材质"), panel);
    m_renameTextureAssetButton = new QPushButton(QStringLiteral("重命名纹理资产"), panel);
    m_removeMaterialAssetButton = new QPushButton(QStringLiteral("删除共享材质"), panel);
    m_removeTextureAssetButton = new QPushButton(QStringLiteral("删除纹理资产"), panel);
    m_cleanupMaterialAssetsButton = new QPushButton(QStringLiteral("清理未使用共享材质"), panel);
    m_cleanupTextureAssetsButton = new QPushButton(QStringLiteral("清理未使用纹理资产"), panel);
    m_loadMaterialAssetButton = new QPushButton(QStringLiteral("加载材质资产"), panel);
    m_saveMaterialAssetButton = new QPushButton(QStringLiteral("另存当前材质"), panel);

    m_metallicRoughnessChannelLabel->setWordWrap(true);
    m_metallicRoughnessChannelLabel->setText(QStringLiteral("当前实现读取：G = 粗糙度，B = 金属度，R/A 暂未使用"));
    m_loadMetallicRoughnessTextureButton->setToolTip(QStringLiteral("当前实现读取 G 通道作为粗糙度，B 通道作为金属度。"));
    m_metallicRoughnessTextureSourceLabel->setToolTip(QStringLiteral("贴图来源；当前实现读取 G=粗糙度，B=金属度。"));
    m_makeMaterialInstanceButton->setToolTip(QStringLiteral("若当前对象正在引用共享材质，会先复制一份为当前对象的独立实例，再继续编辑。"));
    m_sharedMaterialWarningLabel->setWordWrap(true);
    m_sharedMaterialWarningLabel->setText(QStringLiteral("当前对象正在编辑共享材质。这里的修改会同步影响所有引用这份共享材质的对象。"));
    m_sharedMaterialWarningLabel->setStyleSheet(QStringLiteral("QLabel { background-color: rgb(120, 38, 24); color: rgb(255, 242, 228); border: 1px solid rgb(220, 120, 88); border-radius: 4px; padding: 8px; font-weight: 600; }"));
    QHBoxLayout *sharedMaterialWarningLayout = new QHBoxLayout(m_sharedMaterialWarningPanel);
    sharedMaterialWarningLayout->setContentsMargins(0, 0, 0, 0);
    sharedMaterialWarningLayout->setSpacing(8);
    sharedMaterialWarningLayout->addWidget(m_sharedMaterialWarningLabel, 1);
    sharedMaterialWarningLayout->addWidget(m_makeMaterialInstanceButton, 0, Qt::AlignTop);
    m_sharedMaterialWarningPanel->setVisible(false);

    m_resetMaterialButton = new QPushButton(QStringLiteral("重置材质"), panel);

    layout->addRow(QStringLiteral("编辑提示"), m_sharedMaterialWarningPanel);
    layout->addRow(QStringLiteral("材质类型"), m_materialTypeCombo);
    layout->addRow(QStringLiteral("混合模式"), m_materialSurfaceModeCombo);
    layout->addRow(QStringLiteral("不透明度"), m_materialOpacitySpin);
    layout->addRow(QStringLiteral("材质来源"), m_materialSourceLabel);
    layout->addRow(QStringLiteral("使用方式"), m_materialUsageModeCombo);
    layout->addRow(QStringLiteral("共享材质"), m_materialAssetCombo);
    layout->addRow(QStringLiteral("场景材质列表"), m_sceneMaterialAssetCombo);
    layout->addRow(QStringLiteral("引用对象数"), m_sceneMaterialAssetUsageLabel);
    layout->addRow(QStringLiteral("场景纹理列表"), m_sceneTextureAssetCombo);
    layout->addRow(QStringLiteral("纹理使用数"), m_sceneTextureAssetUsageLabel);
    layout->addRow(QStringLiteral("对象颜色纹理"), m_objectTextureAssetCombo);
    layout->addRow(QStringLiteral("对象法线纹理"), m_objectNormalTextureAssetCombo);
    layout->addRow(QStringLiteral("对象金属粗糙纹理"), m_objectMetallicRoughnessTextureAssetCombo);
    layout->addRow(QStringLiteral("当前来源"), m_textureSourceLabel);
    layout->addRow(QStringLiteral("法线来源"), m_normalTextureSourceLabel);
    layout->addRow(QStringLiteral("金属粗糙来源"), m_metallicRoughnessTextureSourceLabel);
    layout->addRow(QStringLiteral("通道约定"), m_metallicRoughnessChannelLabel);
    layout->addRow(QStringLiteral("纹理"), m_texturePresetCombo);
    layout->addRow(QStringLiteral("过滤"), m_textureFilterCombo);
    layout->addRow(QStringLiteral("地址 U"), m_addressModeUCombo);
    layout->addRow(QStringLiteral("地址 V"), m_addressModeVCombo);
    layout->addRow(QStringLiteral("法线过滤"), m_normalTextureFilterCombo);
    layout->addRow(QStringLiteral("法线地址 U"), m_normalAddressModeUCombo);
    layout->addRow(QStringLiteral("法线地址 V"), m_normalAddressModeVCombo);
    layout->addRow(QStringLiteral("金属粗糙过滤"), m_metallicRoughnessTextureFilterCombo);
    layout->addRow(QStringLiteral("金属粗糙地址 U"), m_metallicRoughnessAddressModeUCombo);
    layout->addRow(QStringLiteral("金属粗糙地址 V"), m_metallicRoughnessAddressModeVCombo);
    layout->addRow(QStringLiteral("高光 R"), m_specularColorRSpin);
    layout->addRow(QStringLiteral("高光 G"), m_specularColorGSpin);
    layout->addRow(QStringLiteral("高光 B"), m_specularColorBSpin);
    layout->addRow(QStringLiteral("高光强度"), m_specularStrengthSpin);
    layout->addRow(QStringLiteral("高光指数"), m_shininessSpin);
    layout->addRow(QStringLiteral("法线强度"), m_normalStrengthSpin);
    layout->addRow(QStringLiteral("金属度"), m_metallicSpin);
    layout->addRow(QStringLiteral("粗糙度"), m_roughnessSpin);
    layout->addRow(QString(), m_loadTextureButton);
    layout->addRow(QString(), m_loadNormalTextureButton);
    layout->addRow(QString(), m_loadMetallicRoughnessTextureButton);
    layout->addRow(QString(), m_duplicateMaterialAssetButton);
    layout->addRow(QString(), m_assignSceneMaterialAssetToSelectedButton);
    layout->addRow(QString(), m_assignSceneMaterialAssetToAllButton);
    layout->addRow(QString(), m_duplicateSceneMaterialAssetButton);
    layout->addRow(QString(), m_assignSceneTextureToSelectedColorButton);
    layout->addRow(QString(), m_assignSceneTextureToSelectedNormalButton);
    layout->addRow(QString(), m_assignSceneTextureToSelectedMetallicRoughnessButton);
    layout->addRow(QString(), m_assignSceneTextureToAllObjectsColorButton);
    layout->addRow(QString(), m_clearObjectTextureAssetButton);
    layout->addRow(QString(), m_clearObjectNormalTextureAssetButton);
    layout->addRow(QString(), m_clearObjectMetallicRoughnessTextureAssetButton);
    layout->addRow(QString(), m_duplicateSceneTextureAssetButton);
    layout->addRow(QString(), m_renameMaterialAssetButton);
    layout->addRow(QString(), m_renameTextureAssetButton);
    layout->addRow(QString(), m_removeMaterialAssetButton);
    layout->addRow(QString(), m_removeTextureAssetButton);
    layout->addRow(QString(), m_cleanupMaterialAssetsButton);
    layout->addRow(QString(), m_cleanupTextureAssetsButton);
    layout->addRow(QString(), m_loadMaterialAssetButton);
    layout->addRow(QString(), m_saveMaterialAssetButton);
    layout->addRow(QString(), m_materialDepthWriteCheck);
    layout->addRow(QString(), m_resetMaterialButton);

    m_textureSourceLabel->setWordWrap(true);
    m_materialSourceLabel->setWordWrap(true);
    m_sceneMaterialAssetUsageLabel->setWordWrap(true);
    m_sceneTextureAssetUsageLabel->setWordWrap(true);
    m_normalTextureSourceLabel->setWordWrap(true);
    m_metallicRoughnessTextureSourceLabel->setWordWrap(true);

    setScrollableDockWidget(m_materialDock, panel, 400);
    addDockWidget(Qt::RightDockWidgetArea, m_materialDock);
    syncMaterialControls();
}

void MainWindow::createLightingDock()
{
    m_lightingDock = new QDockWidget(QStringLiteral("灯光"), this);
    m_lightingDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_lightingDock);
    QFormLayout *layout = new QFormLayout(panel);
    configureDockFormLayout(layout);

    m_lightListCombo = new QComboBox(panel);
    m_lightTypeCombo = new QComboBox(panel);
    m_lightTypeCombo->addItem(QStringLiteral("方向光"));
    m_lightTypeCombo->addItem(QStringLiteral("点光源"));
    m_lightTypeCombo->addItem(QStringLiteral("聚光灯"));
    m_lightEnabledCheck = new QCheckBox(QStringLiteral("启用当前灯光"), panel);
    m_lightNameEdit = new QLineEdit(panel);
    m_addDirectionalLightButton = new QPushButton(QStringLiteral("添加方向光"), panel);
    m_addPointLightButton = new QPushButton(QStringLiteral("添加点光"), panel);
    m_addSpotLightButton = new QPushButton(QStringLiteral("添加聚光"), panel);
    m_duplicateLightButton = new QPushButton(QStringLiteral("复制当前灯光"), panel);
    m_removeLightButton = new QPushButton(QStringLiteral("删除当前灯光"), panel);

    m_lightDirectionXSpin = new QDoubleSpinBox(panel);
    m_lightDirectionYSpin = new QDoubleSpinBox(panel);
    m_lightDirectionZSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_lightDirectionXSpin, m_lightDirectionYSpin, m_lightDirectionZSpin}) {
        spin->setRange(-1.0, 1.0);
        spin->setSingleStep(0.05);
        spin->setDecimals(2);
    }

    m_lightAmbientSpin = new QDoubleSpinBox(panel);
    m_lightAmbientSpin->setRange(0.0, 1.0);
    m_lightAmbientSpin->setSingleStep(0.02);
    m_lightAmbientSpin->setDecimals(2);

    m_lightIntensitySpin = new QDoubleSpinBox(panel);
    m_lightIntensitySpin->setRange(0.0, 16.0);
    m_lightIntensitySpin->setSingleStep(0.05);
    m_lightIntensitySpin->setDecimals(2);

    m_shadowCastCheck = new QCheckBox(QStringLiteral("方向光投射阴影"), panel);
    m_shadowStrengthSpin = new QDoubleSpinBox(panel);
    m_shadowStrengthSpin->setRange(0.0, 1.0);
    m_shadowStrengthSpin->setSingleStep(0.05);
    m_shadowStrengthSpin->setDecimals(2);

    m_shadowBiasSpin = new QDoubleSpinBox(panel);
    m_shadowBiasSpin->setRange(0.0, 0.02);
    m_shadowBiasSpin->setSingleStep(0.0005);
    m_shadowBiasSpin->setDecimals(4);

    m_shadowMapSizeSpin = new QSpinBox(panel);
    m_shadowMapSizeSpin->setRange(64, 2048);
    m_shadowMapSizeSpin->setSingleStep(64);

    m_shadowCoverageSpin = new QDoubleSpinBox(panel);
    m_shadowCoverageSpin->setRange(0.1, 1000.0);
    m_shadowCoverageSpin->setSingleStep(0.25);
    m_shadowCoverageSpin->setDecimals(2);

    m_shadowFilterQualityCombo = new QComboBox(panel);
    m_shadowFilterQualityCombo->addItem(QStringLiteral("硬阴影"));
    m_shadowFilterQualityCombo->addItem(QStringLiteral("PCF 3x3"));
    m_shadowFilterQualityCombo->addItem(QStringLiteral("PCF 5x5"));

    m_pointLightCombo = new QComboBox(panel);
    m_pointLightPositionXSpin = new QDoubleSpinBox(panel);
    m_pointLightPositionYSpin = new QDoubleSpinBox(panel);
    m_pointLightPositionZSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_pointLightPositionXSpin, m_pointLightPositionYSpin, m_pointLightPositionZSpin}) {
        spin->setRange(-1000.0, 1000.0);
        spin->setSingleStep(0.1);
        spin->setDecimals(2);
    }

    m_pointLightColorRSpin = new QDoubleSpinBox(panel);
    m_pointLightColorGSpin = new QDoubleSpinBox(panel);
    m_pointLightColorBSpin = new QDoubleSpinBox(panel);
    for (QDoubleSpinBox *spin : {m_pointLightColorRSpin, m_pointLightColorGSpin, m_pointLightColorBSpin}) {
        spin->setRange(0.0, 1.0);
        spin->setSingleStep(0.05);
        spin->setDecimals(2);
    }

    m_pointLightIntensitySpin = new QDoubleSpinBox(panel);
    m_pointLightIntensitySpin->setRange(0.0, 128.0);
    m_pointLightIntensitySpin->setSingleStep(0.25);
    m_pointLightIntensitySpin->setDecimals(2);

    m_pointLightRangeSpin = new QDoubleSpinBox(panel);
    m_pointLightRangeSpin->setRange(0.1, 1000.0);
    m_pointLightRangeSpin->setSingleStep(0.25);
    m_pointLightRangeSpin->setDecimals(2);

    m_spotInnerConeSpin = new QDoubleSpinBox(panel);
    m_spotInnerConeSpin->setRange(1.0, 89.0);
    m_spotInnerConeSpin->setSingleStep(1.0);
    m_spotInnerConeSpin->setDecimals(1);
    m_spotInnerConeSpin->setSuffix(QStringLiteral("°"));

    m_spotOuterConeSpin = new QDoubleSpinBox(panel);
    m_spotOuterConeSpin->setRange(1.0, 89.5);
    m_spotOuterConeSpin->setSingleStep(1.0);
    m_spotOuterConeSpin->setDecimals(1);
    m_spotOuterConeSpin->setSuffix(QStringLiteral("°"));

    layout->addRow(QStringLiteral("当前灯光"), m_lightListCombo);
    layout->addRow(QStringLiteral("灯光类型"), m_lightTypeCombo);
    layout->addRow(QStringLiteral("灯光名称"), m_lightNameEdit);
    layout->addRow(QString(), m_lightEnabledCheck);
    layout->addRow(QStringLiteral("方向 X"), m_lightDirectionXSpin);
    layout->addRow(QStringLiteral("方向 Y"), m_lightDirectionYSpin);
    layout->addRow(QStringLiteral("方向 Z"), m_lightDirectionZSpin);
    layout->addRow(QStringLiteral("环境光"), m_lightAmbientSpin);
    layout->addRow(QStringLiteral("方向光强度"), m_lightIntensitySpin);
    layout->addRow(QString(), m_shadowCastCheck);
    layout->addRow(QStringLiteral("阴影强度"), m_shadowStrengthSpin);
    layout->addRow(QStringLiteral("阴影偏移"), m_shadowBiasSpin);
    layout->addRow(QStringLiteral("阴影分辨率"), m_shadowMapSizeSpin);
    layout->addRow(QStringLiteral("阴影覆盖"), m_shadowCoverageSpin);
    layout->addRow(QStringLiteral("阴影质量"), m_shadowFilterQualityCombo);
    layout->addRow(QStringLiteral("点光源列表"), m_pointLightCombo);
    layout->addRow(QStringLiteral("位置 X"), m_pointLightPositionXSpin);
    layout->addRow(QStringLiteral("位置 Y"), m_pointLightPositionYSpin);
    layout->addRow(QStringLiteral("位置 Z"), m_pointLightPositionZSpin);
    layout->addRow(QStringLiteral("颜色 R"), m_pointLightColorRSpin);
    layout->addRow(QStringLiteral("颜色 G"), m_pointLightColorGSpin);
    layout->addRow(QStringLiteral("颜色 B"), m_pointLightColorBSpin);
    layout->addRow(QStringLiteral("点光强度"), m_pointLightIntensitySpin);
    layout->addRow(QStringLiteral("影响范围"), m_pointLightRangeSpin);
    layout->addRow(QStringLiteral("内锥角"), m_spotInnerConeSpin);
    layout->addRow(QStringLiteral("外锥角"), m_spotOuterConeSpin);
    layout->addRow(QString(), m_addDirectionalLightButton);
    layout->addRow(QString(), m_addPointLightButton);
    layout->addRow(QString(), m_addSpotLightButton);
    layout->addRow(QString(), m_duplicateLightButton);
    layout->addRow(QString(), m_removeLightButton);

    setScrollableDockWidget(m_lightingDock, panel, 360);
    addDockWidget(Qt::RightDockWidgetArea, m_lightingDock);
    syncLightingControls();
}

void MainWindow::createLineArtDock()
{
    m_lineArtDock = new QDockWidget(QStringLiteral("线稿"), this);
    m_lineArtDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_lineArtDock);
    QFormLayout *layout = new QFormLayout(panel);
    configureDockFormLayout(layout);

    m_lineArtThresholdSpin = new QDoubleSpinBox(panel);
    m_lineArtThresholdSpin->setRange(0.25, 4.0);
    m_lineArtThresholdSpin->setSingleStep(0.05);
    m_lineArtThresholdSpin->setDecimals(2);

    m_lineArtStrengthSpin = new QDoubleSpinBox(panel);
    m_lineArtStrengthSpin->setRange(0.1, 4.0);
    m_lineArtStrengthSpin->setSingleStep(0.05);
    m_lineArtStrengthSpin->setDecimals(2);

    m_lineArtScaleSpin = new QSpinBox(panel);
    m_lineArtScaleSpin->setRange(10, 100);
    m_lineArtScaleSpin->setSingleStep(5);
    m_lineArtScaleSpin->setSuffix(QStringLiteral("%"));

    m_lineArtThresholdCurveCombo = new QComboBox(panel);
    m_lineArtThresholdCurveCombo->addItem(QStringLiteral("均衡"));
    m_lineArtThresholdCurveCombo->addItem(QStringLiteral("柔和"));
    m_lineArtThresholdCurveCombo->addItem(QStringLiteral("强对比"));

    m_lineArtEdgeModeCombo = new QComboBox(panel);
    m_lineArtEdgeModeCombo->addItem(QStringLiteral("标准"));
    m_lineArtEdgeModeCombo->addItem(QStringLiteral("细线"));
    m_lineArtEdgeModeCombo->addItem(QStringLiteral("细节"));

    m_lineArtTransparentStrokeSpin = new QDoubleSpinBox(panel);
    m_lineArtTransparentStrokeSpin->setRange(0.25, 8.0);
    m_lineArtTransparentStrokeSpin->setSingleStep(0.25);
    m_lineArtTransparentStrokeSpin->setDecimals(2);

    m_lineArtGrayBaseCheck = new QCheckBox(QStringLiteral("保留灰度底图"), panel);
    m_lineArtComparePreviewCheck = new QCheckBox(QStringLiteral("实时前后对比"), panel);
    m_lineArtBatchExportButton = new QPushButton(QStringLiteral("批量导出线稿"), panel);
    m_saveLineArtConfigButton = new QPushButton(QStringLiteral("保存线稿配置"), panel);
    m_loadLineArtConfigButton = new QPushButton(QStringLiteral("加载线稿配置"), panel);

    layout->addRow(QStringLiteral("阈值倍率"), m_lineArtThresholdSpin);
    layout->addRow(QStringLiteral("线条强度"), m_lineArtStrengthSpin);
    layout->addRow(QStringLiteral("处理缩放"), m_lineArtScaleSpin);
    layout->addRow(QStringLiteral("阈值曲线"), m_lineArtThresholdCurveCombo);
    layout->addRow(QStringLiteral("边缘模式"), m_lineArtEdgeModeCombo);
    layout->addRow(QStringLiteral("透明描边"), m_lineArtTransparentStrokeSpin);
    layout->addRow(QString(), m_lineArtGrayBaseCheck);
    layout->addRow(QString(), m_lineArtComparePreviewCheck);
    layout->addRow(QString(), m_lineArtBatchExportButton);
    layout->addRow(QString(), m_saveLineArtConfigButton);
    layout->addRow(QString(), m_loadLineArtConfigButton);

    setScrollableDockWidget(m_lineArtDock, panel);
    addDockWidget(Qt::RightDockWidgetArea, m_lineArtDock);
    syncLineArtControls();
}

void MainWindow::createCameraDock()
{
    m_cameraDock = new QDockWidget(QStringLiteral("相机"), this);
    m_cameraDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_cameraDock);
    QVBoxLayout *rootLayout = new QVBoxLayout(panel);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    QLabel *hintLabel = new QLabel(QStringLiteral("左键轨道旋转  中键平移\n右键看向  滚轮缩放\nWASD/QE 自由飞行  Shift 加速\nGizmo: Shift 吸附  Ctrl 细调"), panel);
    hintLabel->setWordWrap(true);

    QFormLayout *formLayout = new QFormLayout();
    configureDockFormLayout(formLayout);

    m_cameraProjectionCombo = new QComboBox(panel);
    m_cameraProjectionCombo->addItem(QStringLiteral("透视"));
    m_cameraProjectionCombo->addItem(QStringLiteral("正交"));

    m_cameraFovSpin = new QDoubleSpinBox(panel);
    m_cameraFovSpin->setRange(10.0, 140.0);
    m_cameraFovSpin->setSingleStep(1.0);
    m_cameraFovSpin->setDecimals(1);
    m_cameraFovSpin->setSuffix(QStringLiteral("°"));

    m_cameraOrthoHeightSpin = new QDoubleSpinBox(panel);
    m_cameraOrthoHeightSpin->setRange(0.1, 1000.0);
    m_cameraOrthoHeightSpin->setSingleStep(0.1);
    m_cameraOrthoHeightSpin->setDecimals(2);

    m_cameraNearSpin = new QDoubleSpinBox(panel);
    m_cameraNearSpin->setRange(0.001, 1000.0);
    m_cameraNearSpin->setSingleStep(0.01);
    m_cameraNearSpin->setDecimals(3);

    m_cameraFarSpin = new QDoubleSpinBox(panel);
    m_cameraFarSpin->setRange(0.02, 5000.0);
    m_cameraFarSpin->setSingleStep(0.5);
    m_cameraFarSpin->setDecimals(2);

    m_cameraMoveSpeedSpin = new QDoubleSpinBox(panel);
    m_cameraMoveSpeedSpin->setRange(0.05, 200.0);
    m_cameraMoveSpeedSpin->setSingleStep(0.1);
    m_cameraMoveSpeedSpin->setDecimals(2);

    m_saveCameraPresetButton = new QPushButton(QStringLiteral("保存相机预设"), panel);
    m_loadCameraPresetButton = new QPushButton(QStringLiteral("加载相机预设"), panel);

    m_resetCameraButton = new QPushButton(QStringLiteral("重置相机"), panel);
    m_cameraFrontButton = new QPushButton(QStringLiteral("前"), panel);
    m_cameraBackButton = new QPushButton(QStringLiteral("后"), panel);
    m_cameraLeftButton = new QPushButton(QStringLiteral("左"), panel);
    m_cameraRightButton = new QPushButton(QStringLiteral("右"), panel);
    m_cameraTopButton = new QPushButton(QStringLiteral("上"), panel);
    m_cameraBottomButton = new QPushButton(QStringLiteral("下"), panel);

    QGridLayout *gridLayout = new QGridLayout();
    gridLayout->addWidget(m_cameraTopButton, 0, 1);
    gridLayout->addWidget(m_cameraLeftButton, 1, 0);
    gridLayout->addWidget(m_cameraFrontButton, 1, 1);
    gridLayout->addWidget(m_cameraRightButton, 1, 2);
    gridLayout->addWidget(m_cameraBackButton, 2, 1);
    gridLayout->addWidget(m_cameraBottomButton, 3, 1);

    rootLayout->addWidget(hintLabel);
    formLayout->addRow(QStringLiteral("投影模式"), m_cameraProjectionCombo);
    formLayout->addRow(QStringLiteral("视场角"), m_cameraFovSpin);
    formLayout->addRow(QStringLiteral("正交高度"), m_cameraOrthoHeightSpin);
    formLayout->addRow(QStringLiteral("近裁剪面"), m_cameraNearSpin);
    formLayout->addRow(QStringLiteral("远裁剪面"), m_cameraFarSpin);
    formLayout->addRow(QStringLiteral("移动速度"), m_cameraMoveSpeedSpin);
    rootLayout->addLayout(formLayout);
    rootLayout->addWidget(m_saveCameraPresetButton);
    rootLayout->addWidget(m_loadCameraPresetButton);
    rootLayout->addWidget(m_resetCameraButton);
    rootLayout->addLayout(gridLayout);
    rootLayout->addStretch(1);

    setScrollableDockWidget(m_cameraDock, panel, 320);
    addDockWidget(Qt::RightDockWidgetArea, m_cameraDock);
    syncCameraControls();
}

void MainWindow::createPostProcessDock()
{
    m_postProcessDock = new QDockWidget(QStringLiteral("后处理 / 导出"), this);
    m_postProcessDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_postProcessDock);
    QFormLayout *layout = new QFormLayout(panel);
    configureDockFormLayout(layout);

    m_postProcessEnabledCheck = new QCheckBox(QStringLiteral("启用后处理"), panel);

    m_toneMappingCombo = new QComboBox(panel);
    m_toneMappingCombo->addItem(QStringLiteral("关闭"));
    m_toneMappingCombo->addItem(QStringLiteral("Reinhard"));
    m_toneMappingCombo->addItem(QStringLiteral("ACES 近似"));

    m_postExposureSpin = new QDoubleSpinBox(panel);
    m_postExposureSpin->setRange(0.05, 8.0);
    m_postExposureSpin->setSingleStep(0.05);
    m_postExposureSpin->setDecimals(2);

    m_postGammaSpin = new QDoubleSpinBox(panel);
    m_postGammaSpin->setRange(0.5, 4.0);
    m_postGammaSpin->setSingleStep(0.05);
    m_postGammaSpin->setDecimals(2);

    m_postContrastSpin = new QDoubleSpinBox(panel);
    m_postContrastSpin->setRange(0.0, 2.5);
    m_postContrastSpin->setSingleStep(0.05);
    m_postContrastSpin->setDecimals(2);

    m_postSaturationSpin = new QDoubleSpinBox(panel);
    m_postSaturationSpin->setRange(0.0, 2.5);
    m_postSaturationSpin->setSingleStep(0.05);
    m_postSaturationSpin->setDecimals(2);

    m_sequenceFrameCountSpin = new QSpinBox(panel);
    m_sequenceFrameCountSpin->setRange(1, 2000);
    m_sequenceFrameCountSpin->setValue(120);

    m_sequenceOrbitDegreesSpin = new QDoubleSpinBox(panel);
    m_sequenceOrbitDegreesSpin->setRange(1.0, 1440.0);
    m_sequenceOrbitDegreesSpin->setSingleStep(15.0);
    m_sequenceOrbitDegreesSpin->setDecimals(1);
    m_sequenceOrbitDegreesSpin->setSuffix(QStringLiteral("°"));
    m_sequenceOrbitDegreesSpin->setValue(360.0);

    m_saveScreenshotButton = new QPushButton(QStringLiteral("保存截图"), panel);
    m_exportSequenceButton = new QPushButton(QStringLiteral("导出环绕序列"), panel);
    m_exportDebugViewsButton = new QPushButton(QStringLiteral("批量导出调试视图"), panel);

    layout->addRow(QString(), m_postProcessEnabledCheck);
    layout->addRow(QStringLiteral("Tone Mapping"), m_toneMappingCombo);
    layout->addRow(QStringLiteral("曝光"), m_postExposureSpin);
    layout->addRow(QStringLiteral("Gamma"), m_postGammaSpin);
    layout->addRow(QStringLiteral("对比度"), m_postContrastSpin);
    layout->addRow(QStringLiteral("饱和度"), m_postSaturationSpin);
    layout->addRow(QStringLiteral("序列帧数"), m_sequenceFrameCountSpin);
    layout->addRow(QStringLiteral("环绕角度"), m_sequenceOrbitDegreesSpin);
    layout->addRow(QString(), m_saveScreenshotButton);
    layout->addRow(QString(), m_exportSequenceButton);
    layout->addRow(QString(), m_exportDebugViewsButton);

    setScrollableDockWidget(m_postProcessDock, panel, 320);
    addDockWidget(Qt::RightDockWidgetArea, m_postProcessDock);
    syncPostProcessControls();
}

void MainWindow::createPerformanceDock()
{
    m_performanceDock = new QDockWidget(QStringLiteral("性能 / 并行"), this);
    m_performanceDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *panel = new QWidget(m_performanceDock);
    QFormLayout *layout = new QFormLayout(panel);
    configureDockFormLayout(layout);

    m_parallelRasterEnabledCheck = new QCheckBox(QStringLiteral("启用并行光栅化"), panel);

    m_parallelWorkerThreadsSpin = new QSpinBox(panel);
    m_parallelWorkerThreadsSpin->setRange(-1, 64);
    m_parallelWorkerThreadsSpin->setSpecialValueText(QStringLiteral("自动"));

    m_parallelTileSizeSpin = new QSpinBox(panel);
    m_parallelTileSizeSpin->setRange(1, 256);

    m_parallelMinTileCountSpin = new QSpinBox(panel);
    m_parallelMinTileCountSpin->setRange(1, 1000000);

    m_parallelMinPixelCountSpin = new QSpinBox(panel);
    m_parallelMinPixelCountSpin->setRange(1, 100000000);
    m_parallelMinPixelCountSpin->setSingleStep(256);

    m_parallelTilesPerTaskSpin = new QSpinBox(panel);
    m_parallelTilesPerTaskSpin->setRange(1, 1024);

    m_parallelSummaryLabel = new QLabel(panel);
    m_parallelSummaryLabel->setWordWrap(true);

    m_parallelTimingLabel = new QLabel(panel);
    m_parallelTimingLabel->setWordWrap(true);

    layout->addRow(QString(), m_parallelRasterEnabledCheck);
    layout->addRow(QStringLiteral("请求线程数"), m_parallelWorkerThreadsSpin);
    layout->addRow(QStringLiteral("Tile 尺寸"), m_parallelTileSizeSpin);
    layout->addRow(QStringLiteral("并行最少 Tile"), m_parallelMinTileCountSpin);
    layout->addRow(QStringLiteral("并行最少像素"), m_parallelMinPixelCountSpin);
    layout->addRow(QStringLiteral("每任务 Tile"), m_parallelTilesPerTaskSpin);
    layout->addRow(QStringLiteral("调度摘要"), m_parallelSummaryLabel);
    layout->addRow(QStringLiteral("耗时统计"), m_parallelTimingLabel);

    setScrollableDockWidget(m_performanceDock, panel, 340);
    addDockWidget(Qt::RightDockWidgetArea, m_performanceDock);
    syncPerformanceControls();
}

void MainWindow::syncStateControls()
{
    m_debugViewCombo->blockSignals(true);
    m_fillModeCombo->blockSignals(true);
    m_cullModeCombo->blockSignals(true);
    m_depthFuncCombo->blockSignals(true);
    m_antiAliasingCombo->blockSignals(true);

    m_debugViewCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->debugView()));
    m_fillModeCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->fillMode()));
    m_cullModeCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->cullMode()));
    m_depthFuncCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->depthFunc()));
    m_antiAliasingCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->antiAliasingMode()));

    m_debugViewCombo->blockSignals(false);
    m_fillModeCombo->blockSignals(false);
    m_cullModeCombo->blockSignals(false);
    m_depthFuncCombo->blockSignals(false);
    m_antiAliasingCombo->blockSignals(false);
}

void MainWindow::syncScenePresetControl()
{
    const int targetIndex = static_cast<int>(m_rasterWidget->scenePreset());
    if (m_scenePresetCombo->currentIndex() == targetIndex)
        return;

    m_scenePresetCombo->blockSignals(true);
    m_scenePresetCombo->setCurrentIndex(targetIndex);
    m_scenePresetCombo->blockSignals(false);
}

void MainWindow::syncMaterialControls()
{
    const bool lineArtMode = m_rasterWidget->isLineArtMode();
    const bool hasObject = m_rasterWidget->sceneObjectCount() > 0;
    const bool usingMaterialAsset = hasObject && m_rasterWidget->selectedObjectUsesMaterialAsset();
    const bool hasAnyMaterialAsset = m_rasterWidget->materialAssetCount() > 0;
    const bool hasAnyTextureAsset = m_rasterWidget->textureAssetCount() > 0;
    const int previousSceneMaterialAssetIndex = m_sceneMaterialAssetCombo != nullptr
        ? m_sceneMaterialAssetCombo->currentIndex()
        : -1;
    const int previousSceneTextureAssetIndex = m_sceneTextureAssetCombo != nullptr
        ? m_sceneTextureAssetCombo->currentIndex()
        : -1;
    const int previousObjectTextureAssetIndex = m_objectTextureAssetCombo != nullptr
        ? m_objectTextureAssetCombo->currentIndex()
        : -1;
    const int previousObjectNormalTextureAssetIndex = m_objectNormalTextureAssetCombo != nullptr
        ? m_objectNormalTextureAssetCombo->currentIndex()
        : -1;
    const int previousObjectMetallicRoughnessTextureAssetIndex = m_objectMetallicRoughnessTextureAssetCombo != nullptr
        ? m_objectMetallicRoughnessTextureAssetCombo->currentIndex()
        : -1;
    const Vec3f specularColor = m_rasterWidget->specularColor();
    const QString texturePath = m_rasterWidget->selectedObjectTexturePath();
    const QString normalTexturePath = m_rasterWidget->selectedObjectNormalTexturePath();
    const QString metallicRoughnessTexturePath = m_rasterWidget->selectedObjectMetallicRoughnessTexturePath();

    m_textureFilterCombo->blockSignals(true);
    m_normalTextureFilterCombo->blockSignals(true);
    m_metallicRoughnessTextureFilterCombo->blockSignals(true);
    m_materialTypeCombo->blockSignals(true);
    m_materialSurfaceModeCombo->blockSignals(true);
    m_materialUsageModeCombo->blockSignals(true);
    m_materialAssetCombo->blockSignals(true);
    m_sceneMaterialAssetCombo->blockSignals(true);
    m_sceneTextureAssetCombo->blockSignals(true);
    m_objectTextureAssetCombo->blockSignals(true);
    m_objectNormalTextureAssetCombo->blockSignals(true);
    m_objectMetallicRoughnessTextureAssetCombo->blockSignals(true);
    m_texturePresetCombo->blockSignals(true);
    m_addressModeUCombo->blockSignals(true);
    m_addressModeVCombo->blockSignals(true);
    m_normalAddressModeUCombo->blockSignals(true);
    m_normalAddressModeVCombo->blockSignals(true);
    m_metallicRoughnessAddressModeUCombo->blockSignals(true);
    m_metallicRoughnessAddressModeVCombo->blockSignals(true);
    m_specularColorRSpin->blockSignals(true);
    m_specularColorGSpin->blockSignals(true);
    m_specularColorBSpin->blockSignals(true);
    m_specularStrengthSpin->blockSignals(true);
    m_shininessSpin->blockSignals(true);
    m_normalStrengthSpin->blockSignals(true);
    m_metallicSpin->blockSignals(true);
    m_roughnessSpin->blockSignals(true);
    m_materialOpacitySpin->blockSignals(true);
    m_materialDepthWriteCheck->blockSignals(true);

    m_materialTypeCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->materialType()));
    m_materialSurfaceModeCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->materialSurfaceMode()));
    m_materialOpacitySpin->setValue(m_rasterWidget->materialOpacity());
    m_materialUsageModeCombo->setCurrentIndex(usingMaterialAsset ? 1 : 0);
    m_materialAssetCombo->clear();
    for (int i = 0; i < m_rasterWidget->materialAssetCount(); ++i)
        m_materialAssetCombo->addItem(m_rasterWidget->materialAssetName(i));
    m_materialAssetCombo->setCurrentIndex(m_rasterWidget->selectedObjectMaterialAssetIndex());
    m_sceneMaterialAssetCombo->clear();
    for (int i = 0; i < m_rasterWidget->materialAssetCount(); ++i)
        m_sceneMaterialAssetCombo->addItem(m_rasterWidget->materialAssetName(i));
    const int sceneMaterialAssetIndex = hasAnyMaterialAsset
        ? std::clamp(previousSceneMaterialAssetIndex >= 0 ? previousSceneMaterialAssetIndex
                                                          : std::max(0, m_rasterWidget->selectedObjectMaterialAssetIndex()),
                     0,
                     m_rasterWidget->materialAssetCount() - 1)
        : -1;
    m_sceneMaterialAssetCombo->setCurrentIndex(sceneMaterialAssetIndex);
    m_sceneTextureAssetCombo->clear();
    for (int i = 0; i < m_rasterWidget->textureAssetCount(); ++i)
        m_sceneTextureAssetCombo->addItem(m_rasterWidget->textureAssetName(i));
    const int sceneTextureAssetIndex = hasAnyTextureAsset
        ? std::clamp(previousSceneTextureAssetIndex >= 0 ? previousSceneTextureAssetIndex : 0,
                     0,
                     m_rasterWidget->textureAssetCount() - 1)
        : -1;
    m_sceneTextureAssetCombo->setCurrentIndex(sceneTextureAssetIndex);
    m_objectTextureAssetCombo->clear();
    m_objectNormalTextureAssetCombo->clear();
    m_objectMetallicRoughnessTextureAssetCombo->clear();
    for (int i = 0; i < m_rasterWidget->textureAssetCount(); ++i) {
        const QString name = m_rasterWidget->textureAssetName(i);
        m_objectTextureAssetCombo->addItem(name);
        m_objectNormalTextureAssetCombo->addItem(name);
        m_objectMetallicRoughnessTextureAssetCombo->addItem(name);
    }
    const int objectTextureAssetIndex = hasAnyTextureAsset
        ? std::clamp(m_rasterWidget->selectedObjectTextureAssetIndex() >= 0
                         ? m_rasterWidget->selectedObjectTextureAssetIndex()
                         : previousObjectTextureAssetIndex,
                     -1,
                     m_rasterWidget->textureAssetCount() - 1)
        : -1;
    const int objectNormalTextureAssetIndex = hasAnyTextureAsset
        ? std::clamp(m_rasterWidget->selectedObjectNormalTextureAssetIndex() >= 0
                         ? m_rasterWidget->selectedObjectNormalTextureAssetIndex()
                         : previousObjectNormalTextureAssetIndex,
                     -1,
                     m_rasterWidget->textureAssetCount() - 1)
        : -1;
    const int objectMetallicRoughnessTextureAssetIndex = hasAnyTextureAsset
        ? std::clamp(m_rasterWidget->selectedObjectMetallicRoughnessTextureAssetIndex() >= 0
                         ? m_rasterWidget->selectedObjectMetallicRoughnessTextureAssetIndex()
                         : previousObjectMetallicRoughnessTextureAssetIndex,
                     -1,
                     m_rasterWidget->textureAssetCount() - 1)
        : -1;
    m_objectTextureAssetCombo->setCurrentIndex(objectTextureAssetIndex);
    m_objectNormalTextureAssetCombo->setCurrentIndex(objectNormalTextureAssetIndex);
    m_objectMetallicRoughnessTextureAssetCombo->setCurrentIndex(objectMetallicRoughnessTextureAssetIndex);
    m_texturePresetCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->demoTexturePreset()));
    m_textureFilterCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->textureFilter()));
    m_normalTextureFilterCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->normalTextureFilter()));
    m_metallicRoughnessTextureFilterCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->metallicRoughnessTextureFilter()));
    m_addressModeUCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->addressModeU()));
    m_addressModeVCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->addressModeV()));
    m_normalAddressModeUCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->normalAddressModeU()));
    m_normalAddressModeVCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->normalAddressModeV()));
    m_metallicRoughnessAddressModeUCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->metallicRoughnessAddressModeU()));
    m_metallicRoughnessAddressModeVCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->metallicRoughnessAddressModeV()));
    m_specularColorRSpin->setValue(specularColor.x);
    m_specularColorGSpin->setValue(specularColor.y);
    m_specularColorBSpin->setValue(specularColor.z);
    m_specularStrengthSpin->setValue(m_rasterWidget->specularStrength());
    m_shininessSpin->setValue(m_rasterWidget->shininess());
    m_normalStrengthSpin->setValue(m_rasterWidget->normalStrength());
    m_metallicSpin->setValue(m_rasterWidget->metallic());
    m_roughnessSpin->setValue(m_rasterWidget->roughness());
    m_materialDepthWriteCheck->setChecked(m_rasterWidget->materialDepthWriteEnable());
    if (lineArtMode) {
        m_materialSourceLabel->setText(QStringLiteral("线稿模式下不可用"));
        m_sharedMaterialWarningPanel->setVisible(false);
        m_sceneMaterialAssetUsageLabel->setText(QStringLiteral("线稿模式下不可用"));
        m_sceneTextureAssetUsageLabel->setText(QStringLiteral("线稿模式下不可用"));
        m_textureSourceLabel->setText(QStringLiteral("线稿模式下不可用"));
        m_normalTextureSourceLabel->setText(QStringLiteral("线稿模式下不可用"));
        m_metallicRoughnessTextureSourceLabel->setText(QStringLiteral("线稿模式下不可用"));
    } else if (!hasObject) {
        m_materialSourceLabel->setText(QStringLiteral("当前没有对象"));
        m_sharedMaterialWarningPanel->setVisible(false);
        if (hasAnyMaterialAsset && sceneMaterialAssetIndex >= 0) {
            m_sceneMaterialAssetUsageLabel->setText(
                QStringLiteral("%1 个对象引用")
                    .arg(m_rasterWidget->materialAssetUsageCount(sceneMaterialAssetIndex)));
        } else {
            m_sceneMaterialAssetUsageLabel->setText(QStringLiteral("当前没有共享材质"));
        }
        if (hasAnyTextureAsset && sceneTextureAssetIndex >= 0) {
            m_sceneTextureAssetUsageLabel->setText(
                QStringLiteral("%1 处对象纹理引用")
                    .arg(m_rasterWidget->textureAssetUsageCount(sceneTextureAssetIndex)));
        } else {
            m_sceneTextureAssetUsageLabel->setText(QStringLiteral("当前没有纹理资产"));
        }
        m_textureSourceLabel->setText(QStringLiteral("当前没有对象"));
        m_normalTextureSourceLabel->setText(QStringLiteral("当前没有对象"));
        m_metallicRoughnessTextureSourceLabel->setText(QStringLiteral("当前没有对象"));
    } else {
        m_materialSourceLabel->setText(usingMaterialAsset
                                           ? QStringLiteral("正在编辑共享材质：%1").arg(m_rasterWidget->selectedObjectMaterialDisplayName())
                                           : QStringLiteral("正在编辑对象独立材质"));
        if (usingMaterialAsset) {
            const int sharedUsageCount = m_rasterWidget->selectedObjectMaterialAssetIndex() >= 0
                ? m_rasterWidget->materialAssetUsageCount(m_rasterWidget->selectedObjectMaterialAssetIndex())
                : 0;
            m_sharedMaterialWarningLabel->setText(
                QStringLiteral("当前对象正在编辑共享材质。当前有 %1 个对象会一起受影响。")
                    .arg(sharedUsageCount));
        }
        m_sharedMaterialWarningPanel->setVisible(usingMaterialAsset);
        if (hasAnyMaterialAsset && sceneMaterialAssetIndex >= 0) {
            m_sceneMaterialAssetUsageLabel->setText(
                QStringLiteral("%1 个对象引用")
                    .arg(m_rasterWidget->materialAssetUsageCount(sceneMaterialAssetIndex)));
        } else {
            m_sceneMaterialAssetUsageLabel->setText(QStringLiteral("当前没有共享材质"));
        }
        if (hasAnyTextureAsset && sceneTextureAssetIndex >= 0) {
            m_sceneTextureAssetUsageLabel->setText(
                QStringLiteral("%1 处对象纹理引用")
                    .arg(m_rasterWidget->textureAssetUsageCount(sceneTextureAssetIndex)));
        } else {
            m_sceneTextureAssetUsageLabel->setText(QStringLiteral("当前没有纹理资产"));
        }
        if (!texturePath.isEmpty()) {
            const QString fileName = QFileInfo(texturePath).fileName();
            m_textureSourceLabel->setText(QStringLiteral("外部图片：%1").arg(fileName.isEmpty() ? texturePath : fileName));
        } else {
            m_textureSourceLabel->setText(QStringLiteral("内置预设：%1").arg(m_texturePresetCombo->currentText()));
        }
        if (!normalTexturePath.isEmpty()) {
            const QString fileName = QFileInfo(normalTexturePath).fileName();
            m_normalTextureSourceLabel->setText(QStringLiteral("外部图片：%1").arg(fileName.isEmpty() ? normalTexturePath : fileName));
        } else {
            m_normalTextureSourceLabel->setText(QStringLiteral("当前未设置"));
        }
        if (!metallicRoughnessTexturePath.isEmpty()) {
            const QString fileName = QFileInfo(metallicRoughnessTexturePath).fileName();
            m_metallicRoughnessTextureSourceLabel->setText(QStringLiteral("外部图片：%1").arg(fileName.isEmpty() ? metallicRoughnessTexturePath : fileName));
        } else {
            m_metallicRoughnessTextureSourceLabel->setText(QStringLiteral("当前未设置"));
        }
    }

    const bool materialEditable = !lineArtMode && hasObject;
    const bool assetSelectionEnabled = materialEditable
        && usingMaterialAsset
        && hasAnyMaterialAsset;
    for (QWidget *widget : {static_cast<QWidget *>(m_textureFilterCombo),
                            static_cast<QWidget *>(m_normalTextureFilterCombo),
                            static_cast<QWidget *>(m_metallicRoughnessTextureFilterCombo),
                            static_cast<QWidget *>(m_materialTypeCombo),
                            static_cast<QWidget *>(m_materialSurfaceModeCombo),
                            static_cast<QWidget *>(m_materialUsageModeCombo),
                            static_cast<QWidget *>(m_materialOpacitySpin),
                            static_cast<QWidget *>(m_texturePresetCombo),
                            static_cast<QWidget *>(m_addressModeUCombo),
                            static_cast<QWidget *>(m_addressModeVCombo),
                            static_cast<QWidget *>(m_normalAddressModeUCombo),
                            static_cast<QWidget *>(m_normalAddressModeVCombo),
                            static_cast<QWidget *>(m_metallicRoughnessAddressModeUCombo),
                            static_cast<QWidget *>(m_metallicRoughnessAddressModeVCombo),
                            static_cast<QWidget *>(m_specularColorRSpin),
                            static_cast<QWidget *>(m_specularColorGSpin),
                            static_cast<QWidget *>(m_specularColorBSpin),
                            static_cast<QWidget *>(m_specularStrengthSpin),
                            static_cast<QWidget *>(m_shininessSpin),
                            static_cast<QWidget *>(m_normalStrengthSpin),
                            static_cast<QWidget *>(m_metallicSpin),
                            static_cast<QWidget *>(m_roughnessSpin),
                            static_cast<QWidget *>(m_loadTextureButton),
                            static_cast<QWidget *>(m_loadNormalTextureButton),
                            static_cast<QWidget *>(m_loadMetallicRoughnessTextureButton),
                            static_cast<QWidget *>(m_materialDepthWriteCheck),
                            static_cast<QWidget *>(m_resetMaterialButton),
                            static_cast<QWidget *>(m_duplicateMaterialAssetButton),
                            static_cast<QWidget *>(m_makeMaterialInstanceButton),
                            static_cast<QWidget *>(m_duplicateSceneMaterialAssetButton),
                            static_cast<QWidget *>(m_assignSceneMaterialAssetToSelectedButton),
                            static_cast<QWidget *>(m_assignSceneMaterialAssetToAllButton),
                            static_cast<QWidget *>(m_duplicateSceneTextureAssetButton),
                            static_cast<QWidget *>(m_assignSceneTextureToSelectedColorButton),
                            static_cast<QWidget *>(m_assignSceneTextureToSelectedNormalButton),
                            static_cast<QWidget *>(m_assignSceneTextureToSelectedMetallicRoughnessButton),
                            static_cast<QWidget *>(m_assignSceneTextureToAllObjectsColorButton),
                            static_cast<QWidget *>(m_objectTextureAssetCombo),
                            static_cast<QWidget *>(m_objectNormalTextureAssetCombo),
                            static_cast<QWidget *>(m_objectMetallicRoughnessTextureAssetCombo),
                            static_cast<QWidget *>(m_clearObjectTextureAssetButton),
                            static_cast<QWidget *>(m_clearObjectNormalTextureAssetButton),
                            static_cast<QWidget *>(m_clearObjectMetallicRoughnessTextureAssetButton),
                            static_cast<QWidget *>(m_renameMaterialAssetButton),
                            static_cast<QWidget *>(m_renameTextureAssetButton),
                            static_cast<QWidget *>(m_removeMaterialAssetButton),
                            static_cast<QWidget *>(m_removeTextureAssetButton),
                            static_cast<QWidget *>(m_loadMaterialAssetButton),
                            static_cast<QWidget *>(m_saveMaterialAssetButton)}) {
        widget->setEnabled(materialEditable);
    }
    m_materialAssetCombo->setEnabled(assetSelectionEnabled);
    m_sceneMaterialAssetCombo->setEnabled(!lineArtMode && hasAnyMaterialAsset);
    m_sceneTextureAssetCombo->setEnabled(!lineArtMode && hasAnyTextureAsset);
    m_duplicateSceneMaterialAssetButton->setEnabled(!lineArtMode && hasAnyMaterialAsset);
    m_makeMaterialInstanceButton->setEnabled(materialEditable && usingMaterialAsset);
    m_assignSceneMaterialAssetToSelectedButton->setEnabled(materialEditable && hasAnyMaterialAsset);
    m_assignSceneMaterialAssetToAllButton->setEnabled(!lineArtMode && hasAnyMaterialAsset);
    m_duplicateSceneTextureAssetButton->setEnabled(!lineArtMode && hasAnyTextureAsset);
    m_assignSceneTextureToSelectedColorButton->setEnabled(materialEditable && hasAnyTextureAsset);
    m_assignSceneTextureToSelectedNormalButton->setEnabled(materialEditable && hasAnyTextureAsset);
    m_assignSceneTextureToSelectedMetallicRoughnessButton->setEnabled(materialEditable && hasAnyTextureAsset);
    m_assignSceneTextureToAllObjectsColorButton->setEnabled(!lineArtMode && hasAnyTextureAsset);
    m_objectTextureAssetCombo->setEnabled(materialEditable && hasAnyTextureAsset);
    m_objectNormalTextureAssetCombo->setEnabled(materialEditable && hasAnyTextureAsset);
    m_objectMetallicRoughnessTextureAssetCombo->setEnabled(materialEditable && hasAnyTextureAsset);
    m_clearObjectTextureAssetButton->setEnabled(materialEditable);
    m_clearObjectNormalTextureAssetButton->setEnabled(materialEditable);
    m_clearObjectMetallicRoughnessTextureAssetButton->setEnabled(materialEditable);
    m_renameMaterialAssetButton->setEnabled(!lineArtMode && hasAnyMaterialAsset);
    m_renameTextureAssetButton->setEnabled(!lineArtMode && hasAnyTextureAsset);
    m_removeMaterialAssetButton->setEnabled(!lineArtMode && hasAnyMaterialAsset);
    m_removeTextureAssetButton->setEnabled(!lineArtMode && hasAnyTextureAsset);
    m_cleanupMaterialAssetsButton->setEnabled(!lineArtMode && hasAnyMaterialAsset);
    m_cleanupTextureAssetsButton->setEnabled(!lineArtMode && hasAnyTextureAsset);
    m_materialSourceLabel->setEnabled(true);
    m_sharedMaterialWarningPanel->setEnabled(true);
    m_sceneMaterialAssetUsageLabel->setEnabled(true);
    m_sceneTextureAssetUsageLabel->setEnabled(true);
    m_textureSourceLabel->setEnabled(true);
    m_normalTextureSourceLabel->setEnabled(true);
    m_metallicRoughnessTextureSourceLabel->setEnabled(true);

    m_materialDepthWriteCheck->setEnabled(materialEditable
                                          && m_rasterWidget->materialSurfaceMode() == MaterialSurfaceMode::AlphaBlend);

    m_textureFilterCombo->blockSignals(false);
    m_normalTextureFilterCombo->blockSignals(false);
    m_metallicRoughnessTextureFilterCombo->blockSignals(false);
    m_materialTypeCombo->blockSignals(false);
    m_materialSurfaceModeCombo->blockSignals(false);
    m_materialUsageModeCombo->blockSignals(false);
    m_materialAssetCombo->blockSignals(false);
    m_sceneMaterialAssetCombo->blockSignals(false);
    m_sceneTextureAssetCombo->blockSignals(false);
    m_objectTextureAssetCombo->blockSignals(false);
    m_objectNormalTextureAssetCombo->blockSignals(false);
    m_objectMetallicRoughnessTextureAssetCombo->blockSignals(false);
    m_texturePresetCombo->blockSignals(false);
    m_addressModeUCombo->blockSignals(false);
    m_addressModeVCombo->blockSignals(false);
    m_normalAddressModeUCombo->blockSignals(false);
    m_normalAddressModeVCombo->blockSignals(false);
    m_metallicRoughnessAddressModeUCombo->blockSignals(false);
    m_metallicRoughnessAddressModeVCombo->blockSignals(false);
    m_specularColorRSpin->blockSignals(false);
    m_specularColorGSpin->blockSignals(false);
    m_specularColorBSpin->blockSignals(false);
    m_specularStrengthSpin->blockSignals(false);
    m_shininessSpin->blockSignals(false);
    m_normalStrengthSpin->blockSignals(false);
    m_metallicSpin->blockSignals(false);
    m_roughnessSpin->blockSignals(false);
    m_materialOpacitySpin->blockSignals(false);
    m_materialDepthWriteCheck->blockSignals(false);
}

void MainWindow::syncLightingControls()
{
    const bool lineArtMode = m_rasterWidget->isLineArtMode();
    const int directionalCount = m_rasterWidget->directionalLightCount();
    const int pointCount = m_rasterWidget->pointLightCount();
    const int spotCount = m_rasterWidget->spotLightCount();
    const int totalLightCount = directionalCount + pointCount + spotCount;
    if (m_selectedLightIndex >= totalLightCount)
        m_selectedLightIndex = totalLightCount > 0 ? totalLightCount - 1 : -1;

    const RasterWidget::SelectedLightKind lightKind = selectedLightKind();
    const bool directionalSelected = lightKind == RasterWidget::SelectedLightKind::Directional;
    const bool pointSelected = lightKind == RasterWidget::SelectedLightKind::Point;
    const bool spotSelected = lightKind == RasterWidget::SelectedLightKind::Spot;
    const int directionalIndex = selectedDirectionalLightIndex();
    const int pointIndex = selectedPointLightIndex();
    const int spotIndex = selectedSpotLightIndex();
    const DirectionalLight selectedDirectional = directionalIndex >= 0
        ? m_rasterWidget->directionalLight(directionalIndex)
        : DirectionalLight{};
    const PointLight selectedPoint = pointIndex >= 0
        ? m_rasterWidget->pointLight(pointIndex)
        : PointLight{};
    const SpotLight selectedSpot = spotIndex >= 0
        ? m_rasterWidget->spotLight(spotIndex)
        : SpotLight{};
    const QString selectedLightName = directionalSelected
        ? (directionalIndex >= 0 ? m_rasterWidget->directionalLightName(directionalIndex) : QString())
        : pointSelected
            ? (pointIndex >= 0 ? m_rasterWidget->pointLightName(pointIndex) : QString())
            : (spotIndex >= 0 ? m_rasterWidget->spotLightName(spotIndex) : QString());

    m_rasterWidget->setSelectedLightSelection(lightKind,
                                              directionalSelected ? directionalIndex
                                              : pointSelected ? pointIndex
                                              : spotSelected ? spotIndex
                                                             : -1);

    m_lightListCombo->blockSignals(true);
    m_lightTypeCombo->blockSignals(true);
    m_lightEnabledCheck->blockSignals(true);
    m_lightNameEdit->blockSignals(true);
    m_lightDirectionXSpin->blockSignals(true);
    m_lightDirectionYSpin->blockSignals(true);
    m_lightDirectionZSpin->blockSignals(true);
    m_lightAmbientSpin->blockSignals(true);
    m_lightIntensitySpin->blockSignals(true);
    m_shadowCastCheck->blockSignals(true);
    m_shadowStrengthSpin->blockSignals(true);
    m_shadowBiasSpin->blockSignals(true);
    m_shadowMapSizeSpin->blockSignals(true);
    m_shadowCoverageSpin->blockSignals(true);
    m_shadowFilterQualityCombo->blockSignals(true);
    m_spotInnerConeSpin->blockSignals(true);
    m_spotOuterConeSpin->blockSignals(true);
    m_pointLightCombo->blockSignals(true);
    m_pointLightPositionXSpin->blockSignals(true);
    m_pointLightPositionYSpin->blockSignals(true);
    m_pointLightPositionZSpin->blockSignals(true);
    m_pointLightColorRSpin->blockSignals(true);
    m_pointLightColorGSpin->blockSignals(true);
    m_pointLightColorBSpin->blockSignals(true);
    m_pointLightIntensitySpin->blockSignals(true);
    m_pointLightRangeSpin->blockSignals(true);

    m_lightListCombo->clear();
    for (int i = 0; i < directionalCount; ++i)
        m_lightListCombo->addItem(m_rasterWidget->directionalLightName(i));
    for (int i = 0; i < pointCount; ++i)
        m_lightListCombo->addItem(m_rasterWidget->pointLightName(i));
    for (int i = 0; i < spotCount; ++i)
        m_lightListCombo->addItem(m_rasterWidget->spotLightName(i));
    m_lightListCombo->setCurrentIndex(m_selectedLightIndex);
    m_lightTypeCombo->setCurrentIndex(directionalSelected ? 0 : pointSelected ? 1 : spotSelected ? 2 : 0);
    m_lightEnabledCheck->setChecked(lightKind != RasterWidget::SelectedLightKind::None
                                    && (directionalSelected ? selectedDirectional.enabled
                                                            : pointSelected ? selectedPoint.enabled
                                                                            : selectedSpot.enabled));
    m_lightNameEdit->setText(selectedLightName);

    const Vec3f selectedDirection = directionalSelected ? selectedDirectional.direction
                                  : spotSelected ? selectedSpot.direction
                                                 : Vec3f{0.0f, 0.0f, -1.0f};
    const Vec3f selectedPosition = pointSelected ? selectedPoint.position
                                 : spotSelected ? selectedSpot.position
                                                : Vec3f{};
    const Vec3f selectedColor = directionalSelected ? selectedDirectional.color
                               : pointSelected ? selectedPoint.color
                                               : selectedSpot.color;
    const float selectedAmbient = directionalSelected ? selectedDirectional.ambient
                                : pointSelected ? selectedPoint.ambient
                                                : selectedSpot.ambient;
    const float selectedIntensity = directionalSelected ? selectedDirectional.intensity
                                  : pointSelected ? selectedPoint.intensity
                                                  : selectedSpot.intensity;
    const float selectedShadowExtent = directionalSelected ? selectedDirectional.shadowCoverage
                                    : pointSelected ? selectedPoint.shadowRange
                                                    : selectedSpot.shadowRange;
    const bool selectedCastShadow = directionalSelected ? selectedDirectional.castShadow
                                   : pointSelected ? selectedPoint.castShadow
                                                   : selectedSpot.castShadow;
    const float selectedShadowStrength = directionalSelected ? selectedDirectional.shadowStrength
                                     : pointSelected ? selectedPoint.shadowStrength
                                                     : selectedSpot.shadowStrength;
    const float selectedShadowBias = directionalSelected ? selectedDirectional.shadowBias
                                  : pointSelected ? selectedPoint.shadowBias
                                                  : selectedSpot.shadowBias;
    const int selectedShadowMapSize = directionalSelected ? selectedDirectional.shadowMapSize
                                   : pointSelected ? selectedPoint.shadowMapSize
                                                   : selectedSpot.shadowMapSize;
    const ShadowFilterQuality selectedShadowFilterQuality = directionalSelected ? selectedDirectional.shadowFilterQuality
                                                     : pointSelected ? selectedPoint.shadowFilterQuality
                                                                     : selectedSpot.shadowFilterQuality;
    const float selectedLightRange = pointSelected ? selectedPoint.range
                                   : spotSelected ? selectedSpot.range
                                                  : 0.0f;

    m_lightDirectionXSpin->setValue(selectedDirection.x);
    m_lightDirectionYSpin->setValue(selectedDirection.y);
    m_lightDirectionZSpin->setValue(selectedDirection.z);
    m_lightAmbientSpin->setValue(selectedAmbient);
    m_lightIntensitySpin->setValue(selectedIntensity);
    m_shadowCastCheck->setText(directionalSelected ? QStringLiteral("方向光投射阴影")
                               : pointSelected ? QStringLiteral("点光源投射阴影")
                               : spotSelected ? QStringLiteral("聚光灯投射阴影")
                                              : QStringLiteral("投射阴影"));
    m_shadowCastCheck->setChecked(selectedCastShadow);
    m_shadowStrengthSpin->setValue(selectedShadowStrength);
    m_shadowBiasSpin->setValue(selectedShadowBias);
    m_shadowMapSizeSpin->setValue(selectedShadowMapSize);
    m_shadowCoverageSpin->setValue(selectedShadowExtent);
    m_shadowFilterQualityCombo->setCurrentIndex(static_cast<int>(selectedShadowFilterQuality));
    m_spotInnerConeSpin->setValue(selectedSpot.innerConeDegrees);
    m_spotOuterConeSpin->setValue(selectedSpot.outerConeDegrees);

    m_pointLightCombo->clear();
    for (int i = 0; i < pointCount; ++i)
        m_pointLightCombo->addItem(m_rasterWidget->pointLightName(i));
    m_pointLightCombo->setCurrentIndex(pointIndex);
    m_pointLightPositionXSpin->setValue(selectedPosition.x);
    m_pointLightPositionYSpin->setValue(selectedPosition.y);
    m_pointLightPositionZSpin->setValue(selectedPosition.z);
    m_pointLightColorRSpin->setValue(selectedColor.x);
    m_pointLightColorGSpin->setValue(selectedColor.y);
    m_pointLightColorBSpin->setValue(selectedColor.z);
    m_pointLightIntensitySpin->setValue(selectedIntensity);
    m_pointLightRangeSpin->setValue(selectedLightRange);

    const bool hasAnyLight = totalLightCount > 0;
    const bool listEditable = !lineArtMode && hasAnyLight;
    const bool lightingEditable = !lineArtMode && lightKind != RasterWidget::SelectedLightKind::None;
    const bool spotEditable = lightingEditable && spotSelected;
    const bool directionEditable = lightingEditable && (directionalSelected || spotSelected);
    const bool positionEditable = lightingEditable && (pointSelected || spotSelected);
    const bool pointPayloadEditable = lightingEditable && (pointSelected || spotSelected);

    m_lightListCombo->setEnabled(listEditable);
    for (QWidget *widget : {static_cast<QWidget *>(m_lightTypeCombo),
                            static_cast<QWidget *>(m_lightEnabledCheck),
                            static_cast<QWidget *>(m_lightNameEdit),
                            static_cast<QWidget *>(m_duplicateLightButton),
                            static_cast<QWidget *>(m_removeLightButton)}) {
        widget->setEnabled(lightingEditable);
    }
    for (QWidget *widget : {static_cast<QWidget *>(m_addDirectionalLightButton),
                            static_cast<QWidget *>(m_addPointLightButton)}) {
        widget->setEnabled(!lineArtMode);
    }
    m_addSpotLightButton->setEnabled(!lineArtMode);
    for (QWidget *widget : {static_cast<QWidget *>(m_lightDirectionXSpin),
                            static_cast<QWidget *>(m_lightDirectionYSpin),
                            static_cast<QWidget *>(m_lightDirectionZSpin),
                            static_cast<QWidget *>(m_lightAmbientSpin),
                            static_cast<QWidget *>(m_lightIntensitySpin),
                            static_cast<QWidget *>(m_shadowCastCheck)}) {
        widget->setEnabled(lightingEditable);
    }
    m_lightDirectionXSpin->setEnabled(directionEditable);
    m_lightDirectionYSpin->setEnabled(directionEditable);
    m_lightDirectionZSpin->setEnabled(directionEditable);
    m_lightIntensitySpin->setEnabled(lightingEditable);
    m_shadowCastCheck->setEnabled(lightingEditable);
    const bool shadowEditable = lightingEditable && m_shadowCastCheck->isChecked();
    m_shadowStrengthSpin->setEnabled(shadowEditable);
    m_shadowBiasSpin->setEnabled(shadowEditable);
    m_shadowMapSizeSpin->setEnabled(shadowEditable);
    m_shadowCoverageSpin->setEnabled(shadowEditable);
    m_shadowFilterQualityCombo->setEnabled(shadowEditable);
    m_spotInnerConeSpin->setEnabled(spotEditable);
    m_spotOuterConeSpin->setEnabled(spotEditable);
    for (QWidget *widget : {static_cast<QWidget *>(m_pointLightColorRSpin),
                            static_cast<QWidget *>(m_pointLightColorGSpin),
                            static_cast<QWidget *>(m_pointLightColorBSpin)}) {
        widget->setEnabled(lightingEditable);
    }
    for (QWidget *widget : {static_cast<QWidget *>(m_pointLightCombo),
                            static_cast<QWidget *>(m_pointLightPositionXSpin),
                            static_cast<QWidget *>(m_pointLightPositionYSpin),
                            static_cast<QWidget *>(m_pointLightPositionZSpin),
                            static_cast<QWidget *>(m_pointLightIntensitySpin),
                            static_cast<QWidget *>(m_pointLightRangeSpin)}) {
        widget->setEnabled(positionEditable || pointPayloadEditable);
    }
    m_pointLightCombo->setEnabled(!lineArtMode && pointCount > 0);

    m_lightListCombo->blockSignals(false);
    m_lightTypeCombo->blockSignals(false);
    m_lightEnabledCheck->blockSignals(false);
    m_lightNameEdit->blockSignals(false);
    m_lightDirectionXSpin->blockSignals(false);
    m_lightDirectionYSpin->blockSignals(false);
    m_lightDirectionZSpin->blockSignals(false);
    m_lightAmbientSpin->blockSignals(false);
    m_lightIntensitySpin->blockSignals(false);
    m_shadowCastCheck->blockSignals(false);
    m_shadowStrengthSpin->blockSignals(false);
    m_shadowBiasSpin->blockSignals(false);
    m_shadowMapSizeSpin->blockSignals(false);
    m_shadowCoverageSpin->blockSignals(false);
    m_shadowFilterQualityCombo->blockSignals(false);
    m_spotInnerConeSpin->blockSignals(false);
    m_spotOuterConeSpin->blockSignals(false);
    m_pointLightCombo->blockSignals(false);
    m_pointLightPositionXSpin->blockSignals(false);
    m_pointLightPositionYSpin->blockSignals(false);
    m_pointLightPositionZSpin->blockSignals(false);
    m_pointLightColorRSpin->blockSignals(false);
    m_pointLightColorGSpin->blockSignals(false);
    m_pointLightColorBSpin->blockSignals(false);
    m_pointLightIntensitySpin->blockSignals(false);
    m_pointLightRangeSpin->blockSignals(false);
}

void MainWindow::syncLineArtControls()
{
    m_lineArtThresholdSpin->blockSignals(true);
    m_lineArtStrengthSpin->blockSignals(true);
    m_lineArtScaleSpin->blockSignals(true);
    m_lineArtThresholdCurveCombo->blockSignals(true);
    m_lineArtEdgeModeCombo->blockSignals(true);
    m_lineArtTransparentStrokeSpin->blockSignals(true);
    m_lineArtGrayBaseCheck->blockSignals(true);
    m_lineArtComparePreviewCheck->blockSignals(true);

    m_lineArtThresholdSpin->setValue(m_rasterWidget->lineArtThresholdScale());
    m_lineArtStrengthSpin->setValue(m_rasterWidget->lineArtLineStrength());
    m_lineArtScaleSpin->setValue(static_cast<int>(m_rasterWidget->lineArtProcessScale() * 100.0f + 0.5f));
    m_lineArtThresholdCurveCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->lineArtThresholdCurvePreset()));
    m_lineArtEdgeModeCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->lineArtEdgeMode()));
    m_lineArtTransparentStrokeSpin->setValue(m_rasterWidget->lineArtTransparentStrokeWidth());
    m_lineArtGrayBaseCheck->setChecked(m_rasterWidget->lineArtKeepGrayBase());
    m_lineArtComparePreviewCheck->setChecked(m_rasterWidget->lineArtComparePreview());

    for (QWidget *widget : {static_cast<QWidget *>(m_lineArtThresholdSpin),
                            static_cast<QWidget *>(m_lineArtStrengthSpin),
                            static_cast<QWidget *>(m_lineArtScaleSpin),
                            static_cast<QWidget *>(m_lineArtThresholdCurveCombo),
                            static_cast<QWidget *>(m_lineArtEdgeModeCombo),
                            static_cast<QWidget *>(m_lineArtTransparentStrokeSpin),
                            static_cast<QWidget *>(m_lineArtGrayBaseCheck),
                            static_cast<QWidget *>(m_lineArtComparePreviewCheck),
                            static_cast<QWidget *>(m_lineArtBatchExportButton),
                            static_cast<QWidget *>(m_saveLineArtConfigButton),
                            static_cast<QWidget *>(m_loadLineArtConfigButton)}) {
        widget->setEnabled(true);
    }

    m_lineArtThresholdSpin->blockSignals(false);
    m_lineArtStrengthSpin->blockSignals(false);
    m_lineArtScaleSpin->blockSignals(false);
    m_lineArtThresholdCurveCombo->blockSignals(false);
    m_lineArtEdgeModeCombo->blockSignals(false);
    m_lineArtTransparentStrokeSpin->blockSignals(false);
    m_lineArtGrayBaseCheck->blockSignals(false);
    m_lineArtComparePreviewCheck->blockSignals(false);
}

void MainWindow::syncSceneContentLabel()
{
    const QString modelName = QFileInfo(m_rasterWidget->loadedModelName()).fileName();
    const QString label = modelName.isEmpty() ? m_rasterWidget->loadedModelName() : modelName;
    m_modelLabel->setText(m_rasterWidget->isLineArtMode() ? QStringLiteral("%1").arg(label) : label);
}

void MainWindow::syncObjectControls()
{
    const bool lineArtMode = m_rasterWidget->isLineArtMode();

    m_objectCombo->blockSignals(true);
    m_objectNameEdit->blockSignals(true);
    m_positionXSpin->blockSignals(true);
    m_positionYSpin->blockSignals(true);
    m_positionZSpin->blockSignals(true);
    m_rotationXSpin->blockSignals(true);
    m_rotationYSpin->blockSignals(true);
    m_rotationZSpin->blockSignals(true);
    m_scaleXSpin->blockSignals(true);
    m_scaleYSpin->blockSignals(true);
    m_scaleZSpin->blockSignals(true);
    m_gizmoSpaceCombo->blockSignals(true);
    m_gizmoTranslationSnapSpin->blockSignals(true);
    m_gizmoRotationSnapSpin->blockSignals(true);
    m_gizmoScaleSnapSpin->blockSignals(true);

    m_objectCombo->clear();
    for (int i = 0; i < m_rasterWidget->sceneObjectCount(); ++i) {
        const QString displayName = QFileInfo(m_rasterWidget->sceneObjectName(i)).fileName();
        m_objectCombo->addItem(displayName.isEmpty() ? m_rasterWidget->sceneObjectName(i) : displayName);
    }

    const int selectedIndex = m_rasterWidget->selectedSceneObjectIndex();
    m_objectCombo->setCurrentIndex(m_rasterWidget->sceneObjectCount() > 0 ? selectedIndex : -1);
    m_objectNameEdit->setText(m_rasterWidget->sceneObjectCount() > 0 ? m_rasterWidget->sceneObjectName(selectedIndex) : QString());

    const Transform transform = m_rasterWidget->selectedSceneObjectTransform();
    m_positionXSpin->setValue(transform.position.x);
    m_positionYSpin->setValue(transform.position.y);
    m_positionZSpin->setValue(transform.position.z);
    m_rotationXSpin->setValue(radiansToDegrees(transform.rotationRadians.x));
    m_rotationYSpin->setValue(radiansToDegrees(transform.rotationRadians.y));
    m_rotationZSpin->setValue(radiansToDegrees(transform.rotationRadians.z));
    m_scaleXSpin->setValue(transform.scale.x);
    m_scaleYSpin->setValue(transform.scale.y);
    m_scaleZSpin->setValue(transform.scale.z);
    m_gizmoSpaceCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->gizmoSpaceMode()));
    m_gizmoTranslationSnapSpin->setValue(m_rasterWidget->gizmoTranslationSnapStep());
    m_gizmoRotationSnapSpin->setValue(m_rasterWidget->gizmoRotationSnapDegrees());
    m_gizmoScaleSnapSpin->setValue(m_rasterWidget->gizmoScaleSnapStep());

    const bool hasObject = m_rasterWidget->sceneObjectCount() > 0 && !lineArtMode;
    const bool gizmoEditable = !lineArtMode;
    for (QWidget *widget : {static_cast<QWidget *>(m_objectCombo),
                            static_cast<QWidget *>(m_objectNameEdit),
                            static_cast<QWidget *>(m_positionXSpin),
                            static_cast<QWidget *>(m_positionYSpin),
                            static_cast<QWidget *>(m_positionZSpin),
                            static_cast<QWidget *>(m_rotationXSpin),
                            static_cast<QWidget *>(m_rotationYSpin),
                            static_cast<QWidget *>(m_rotationZSpin),
                            static_cast<QWidget *>(m_scaleXSpin),
                            static_cast<QWidget *>(m_scaleYSpin),
                            static_cast<QWidget *>(m_scaleZSpin)}) {
        widget->setEnabled(hasObject);
    }
    for (QWidget *widget : {static_cast<QWidget *>(m_gizmoSpaceCombo),
                            static_cast<QWidget *>(m_gizmoTranslationSnapSpin),
                            static_cast<QWidget *>(m_gizmoRotationSnapSpin),
                            static_cast<QWidget *>(m_gizmoScaleSnapSpin)}) {
        widget->setEnabled(gizmoEditable);
    }
    m_removeObjectButton->setEnabled(hasObject);

    m_objectCombo->blockSignals(false);
    m_objectNameEdit->blockSignals(false);
    m_positionXSpin->blockSignals(false);
    m_positionYSpin->blockSignals(false);
    m_positionZSpin->blockSignals(false);
    m_rotationXSpin->blockSignals(false);
    m_rotationYSpin->blockSignals(false);
    m_rotationZSpin->blockSignals(false);
    m_scaleXSpin->blockSignals(false);
    m_scaleYSpin->blockSignals(false);
    m_scaleZSpin->blockSignals(false);
    m_gizmoSpaceCombo->blockSignals(false);
    m_gizmoTranslationSnapSpin->blockSignals(false);
    m_gizmoRotationSnapSpin->blockSignals(false);
    m_gizmoScaleSnapSpin->blockSignals(false);
}

void MainWindow::syncCameraControls()
{
    const bool cameraEditable = !m_rasterWidget->isLineArtMode();
    const bool orthographic = m_rasterWidget->cameraProjectionMode() == CameraProjectionMode::Orthographic;

    m_cameraProjectionCombo->blockSignals(true);
    m_cameraFovSpin->blockSignals(true);
    m_cameraOrthoHeightSpin->blockSignals(true);
    m_cameraNearSpin->blockSignals(true);
    m_cameraFarSpin->blockSignals(true);
    m_cameraMoveSpeedSpin->blockSignals(true);

    m_cameraProjectionCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->cameraProjectionMode()));
    m_cameraFovSpin->setValue(m_rasterWidget->cameraVerticalFovDegrees());
    m_cameraOrthoHeightSpin->setValue(m_rasterWidget->cameraOrthographicHeight());
    m_cameraNearSpin->setValue(m_rasterWidget->cameraNearPlane());
    m_cameraFarSpin->setValue(m_rasterWidget->cameraFarPlane());
    m_cameraMoveSpeedSpin->setValue(m_rasterWidget->cameraMoveSpeed());

    for (QWidget *widget : {static_cast<QWidget *>(m_resetCameraButton),
                            static_cast<QWidget *>(m_cameraProjectionCombo),
                            static_cast<QWidget *>(m_cameraNearSpin),
                            static_cast<QWidget *>(m_cameraFarSpin),
                            static_cast<QWidget *>(m_cameraMoveSpeedSpin),
                            static_cast<QWidget *>(m_saveCameraPresetButton),
                            static_cast<QWidget *>(m_loadCameraPresetButton),
                            static_cast<QWidget *>(m_cameraFrontButton),
                            static_cast<QWidget *>(m_cameraBackButton),
                            static_cast<QWidget *>(m_cameraLeftButton),
                            static_cast<QWidget *>(m_cameraRightButton),
                            static_cast<QWidget *>(m_cameraTopButton),
                            static_cast<QWidget *>(m_cameraBottomButton)}) {
        if (widget != nullptr)
            widget->setEnabled(cameraEditable);
    }
    m_cameraFovSpin->setEnabled(cameraEditable && !orthographic);
    m_cameraOrthoHeightSpin->setEnabled(cameraEditable && orthographic);

    m_cameraProjectionCombo->blockSignals(false);
    m_cameraFovSpin->blockSignals(false);
    m_cameraOrthoHeightSpin->blockSignals(false);
    m_cameraNearSpin->blockSignals(false);
    m_cameraFarSpin->blockSignals(false);
    m_cameraMoveSpeedSpin->blockSignals(false);
}

void MainWindow::syncPostProcessControls()
{
    const bool editable = !m_rasterWidget->isLineArtMode();

    if (m_postProcessEnabledCheck != nullptr) {
        m_postProcessEnabledCheck->blockSignals(true);
        m_postProcessEnabledCheck->setChecked(m_rasterWidget->postProcessEnabled());
        m_postProcessEnabledCheck->blockSignals(false);
        m_postProcessEnabledCheck->setEnabled(editable);
    }
    if (m_toneMappingCombo != nullptr) {
        m_toneMappingCombo->blockSignals(true);
        m_toneMappingCombo->setCurrentIndex(static_cast<int>(m_rasterWidget->toneMappingMode()));
        m_toneMappingCombo->blockSignals(false);
        m_toneMappingCombo->setEnabled(editable);
    }
    if (m_postExposureSpin != nullptr) {
        m_postExposureSpin->blockSignals(true);
        m_postExposureSpin->setValue(m_rasterWidget->postExposure());
        m_postExposureSpin->blockSignals(false);
        m_postExposureSpin->setEnabled(editable);
    }
    if (m_postGammaSpin != nullptr) {
        m_postGammaSpin->blockSignals(true);
        m_postGammaSpin->setValue(m_rasterWidget->postGamma());
        m_postGammaSpin->blockSignals(false);
        m_postGammaSpin->setEnabled(editable);
    }
    if (m_postContrastSpin != nullptr) {
        m_postContrastSpin->blockSignals(true);
        m_postContrastSpin->setValue(m_rasterWidget->postContrast());
        m_postContrastSpin->blockSignals(false);
        m_postContrastSpin->setEnabled(editable);
    }
    if (m_postSaturationSpin != nullptr) {
        m_postSaturationSpin->blockSignals(true);
        m_postSaturationSpin->setValue(m_rasterWidget->postSaturation());
        m_postSaturationSpin->blockSignals(false);
        m_postSaturationSpin->setEnabled(editable);
    }
    if (m_saveScreenshotButton != nullptr)
        m_saveScreenshotButton->setEnabled(true);
    if (m_exportSequenceButton != nullptr)
        m_exportSequenceButton->setEnabled(editable);
    if (m_exportDebugViewsButton != nullptr)
        m_exportDebugViewsButton->setEnabled(editable);
    if (m_sequenceFrameCountSpin != nullptr)
        m_sequenceFrameCountSpin->setEnabled(editable);
    if (m_sequenceOrbitDegreesSpin != nullptr)
        m_sequenceOrbitDegreesSpin->setEnabled(editable);
}

void MainWindow::syncPerformanceControls()
{
    if (m_parallelStatsLabel == nullptr)
        return;

    const ParallelRasterStats stats = m_rasterWidget->parallelRasterStats();
    const bool enabled = m_rasterWidget->parallelRasterEnabled();
    const bool editable = !m_rasterWidget->isLineArtMode();
    const int requestedWorkers = m_rasterWidget->parallelWorkerThreadCount();
    const QString requestedText = requestedWorkers < 0
        ? QStringLiteral("自动")
        : QString::number(requestedWorkers);

    if (m_parallelRasterEnabledCheck != nullptr) {
        m_parallelRasterEnabledCheck->blockSignals(true);
        m_parallelRasterEnabledCheck->setChecked(enabled);
        m_parallelRasterEnabledCheck->blockSignals(false);
        m_parallelRasterEnabledCheck->setEnabled(editable);
    }
    if (m_parallelWorkerThreadsSpin != nullptr) {
        m_parallelWorkerThreadsSpin->blockSignals(true);
        m_parallelWorkerThreadsSpin->setValue(requestedWorkers);
        m_parallelWorkerThreadsSpin->blockSignals(false);
        m_parallelWorkerThreadsSpin->setEnabled(editable && enabled);
    }
    if (m_parallelTileSizeSpin != nullptr) {
        m_parallelTileSizeSpin->blockSignals(true);
        m_parallelTileSizeSpin->setValue(m_rasterWidget->parallelTileSize());
        m_parallelTileSizeSpin->blockSignals(false);
        m_parallelTileSizeSpin->setEnabled(editable);
    }
    if (m_parallelMinTileCountSpin != nullptr) {
        m_parallelMinTileCountSpin->blockSignals(true);
        m_parallelMinTileCountSpin->setValue(m_rasterWidget->parallelMinTileCount());
        m_parallelMinTileCountSpin->blockSignals(false);
        m_parallelMinTileCountSpin->setEnabled(editable && enabled);
    }
    if (m_parallelMinPixelCountSpin != nullptr) {
        m_parallelMinPixelCountSpin->blockSignals(true);
        m_parallelMinPixelCountSpin->setValue(m_rasterWidget->parallelMinPixelCount());
        m_parallelMinPixelCountSpin->blockSignals(false);
        m_parallelMinPixelCountSpin->setEnabled(editable && enabled);
    }
    if (m_parallelTilesPerTaskSpin != nullptr) {
        m_parallelTilesPerTaskSpin->blockSignals(true);
        m_parallelTilesPerTaskSpin->setValue(m_rasterWidget->parallelTilesPerTask());
        m_parallelTilesPerTaskSpin->blockSignals(false);
        m_parallelTilesPerTaskSpin->setEnabled(editable && enabled);
    }

    if (m_parallelSummaryLabel != nullptr) {
        const QString summaryText = QStringLiteral(
            "请求线程 %1，实际线程 %2，tile %3，任务 %4。\n并行批次 %5，串行批次 %6，跳过分发 %7。")
                                        .arg(requestedText)
                                        .arg(m_rasterWidget->parallelActiveWorkerThreadCount())
                                        .arg(stats.tileCount)
                                        .arg(stats.taskCount)
                                        .arg(stats.parallelTaskCount)
                                        .arg(stats.serialTaskCount)
                                        .arg(stats.skippedParallelDispatchCount);
        m_parallelSummaryLabel->setText(summaryText);
        m_parallelSummaryLabel->setEnabled(true);
    }

    if (m_parallelTimingLabel != nullptr) {
        const QString timingText = QStringLiteral(
            "构建 tile %1 us，分发 %2 us，等待 %3 us。\n并行 tile %4，串行 tile %5。")
                                       .arg(stats.tileBuildMicroseconds)
                                       .arg(stats.dispatchMicroseconds)
                                       .arg(stats.waitMicroseconds)
                                       .arg(stats.parallelTileCount)
                                       .arg(stats.serialTileCount);
        m_parallelTimingLabel->setText(timingText);
        m_parallelTimingLabel->setEnabled(true);
    }
}

void MainWindow::markPresetCustom()
{
    if (m_presetCombo->currentIndex() == static_cast<int>(RenderPreset::Custom))
        return;

    m_presetCombo->blockSignals(true);
    m_presetCombo->setCurrentIndex(static_cast<int>(RenderPreset::Custom));
    m_presetCombo->blockSignals(false);
}

void MainWindow::markScenePresetCustom()
{
    if (m_rasterWidget->scenePreset() != ScenePreset::Custom)
        return;

    if (m_scenePresetCombo->currentIndex() == static_cast<int>(ScenePreset::Custom))
        return;

    m_scenePresetCombo->blockSignals(true);
    m_scenePresetCombo->setCurrentIndex(static_cast<int>(ScenePreset::Custom));
    m_scenePresetCombo->blockSignals(false);
}
