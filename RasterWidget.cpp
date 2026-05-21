#include "RasterWidget.h"
#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSaveFile>
#include <QWheelEvent>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kCurrentSceneStateVersion = 3;
constexpr int kMinimumSupportedSceneStateVersion = 1;

QString supportedImageFormatsSummary()
{
    QStringList formats;
    for (const QByteArray &format : QImageReader::supportedImageFormats())
        formats.push_back(QString::fromLatin1(format).toLower());
    formats.removeDuplicates();
    formats.sort();
    return formats.join(QStringLiteral(", "));
}

QString debugViewDisplayName(DebugView view)
{
    switch (view) {
    case DebugView::None: return QStringLiteral("常规");
    case DebugView::Depth: return QStringLiteral("深度");
    case DebugView::Normal: return QStringLiteral("法线");
    case DebugView::UV: return QStringLiteral("UV");
    case DebugView::Overdraw: return QStringLiteral("过绘制");
    case DebugView::ObjectId: return QStringLiteral("对象 ID");
    case DebugView::MaterialId: return QStringLiteral("材质 ID");
    case DebugView::TriangleId: return QStringLiteral("三角形 ID");
    case DebugView::FaceOrientation: return QStringLiteral("正反面");
    case DebugView::Barycentric: return QStringLiteral("重心坐标");
    case DebugView::Shadow: return QStringLiteral("阴影视图");
    }

    return QStringLiteral("未知");
}

QString fillModeDisplayName(FillMode mode)
{
    switch (mode) {
    case FillMode::Solid: return QStringLiteral("实体");
    case FillMode::Wireframe: return QStringLiteral("线框");
    }

    return QStringLiteral("未知");
}

QString cullModeDisplayName(CullMode mode)
{
    switch (mode) {
    case CullMode::None: return QStringLiteral("关闭");
    case CullMode::Back: return QStringLiteral("背面");
    case CullMode::Front: return QStringLiteral("正面");
    }

    return QStringLiteral("未知");
}

QString depthFuncDisplayName(DepthFunc func)
{
    switch (func) {
    case DepthFunc::Less: return QStringLiteral("Less");
    case DepthFunc::LessEqual: return QStringLiteral("LessEqual");
    case DepthFunc::Greater: return QStringLiteral("Greater");
    case DepthFunc::GreaterEqual: return QStringLiteral("GreaterEqual");
    case DepthFunc::Always: return QStringLiteral("Always");
    }

    return QStringLiteral("未知");
}

QString antiAliasingDisplayName(AntiAliasingMode mode)
{
    switch (mode) {
    case AntiAliasingMode::None: return QStringLiteral("关闭");
    case AntiAliasingMode::Coverage4x: return QStringLiteral("MSAA 4x");
    }

    return QStringLiteral("未知");
}

Camera makeCamera(const Vec3f &position, const Vec3f &target, float verticalFovDegrees)
{
    return {position, target, {0.0f, 1.0f, 0.0f}, verticalFovDegrees * kPi / 180.0f, 0.1f, 100.0f};
}

float cameraDistance(const Camera &camera)
{
    return std::max(0.05f, length(camera.position - camera.target));
}

Vec3f cameraForward(const Camera &camera)
{
    return normalize(camera.target - camera.position);
}

Vec3f cameraRight(const Camera &camera)
{
    const Vec3f right = cross(cameraForward(camera), camera.up);
    if (length(right) <= 1e-5f)
        return {1.0f, 0.0f, 0.0f};
    return normalize(right);
}

Vec3f cameraOrthoUp(const Camera &camera)
{
    const Vec3f up = cross(cameraRight(camera), cameraForward(camera));
    if (length(up) <= 1e-5f)
        return {0.0f, 1.0f, 0.0f};
    return normalize(up);
}

void orbitCamera(Camera &camera, float yawDeltaRadians, float pitchDeltaRadians)
{
    const Vec3f offset = camera.position - camera.target;
    const float radius = std::max(0.05f, length(offset));
    const float yaw = std::atan2(offset.x, offset.z);
    const float pitch = std::asin(std::clamp(offset.y / radius, -1.0f, 1.0f));
    const float nextYaw = yaw + yawDeltaRadians;
    const float nextPitch = std::clamp(pitch + pitchDeltaRadians, -1.45f, 1.45f);
    const float cosPitch = std::cos(nextPitch);

    camera.position = camera.target + Vec3f{
        radius * std::sin(nextYaw) * cosPitch,
        radius * std::sin(nextPitch),
        radius * std::cos(nextYaw) * cosPitch
    };
    camera.up = {0.0f, 1.0f, 0.0f};
}

void panCamera(Camera &camera, float deltaPixelsX, float deltaPixelsY, int viewportHeight)
{
    float pixelToWorld = 0.0f;
    if (camera.projectionMode == CameraProjectionMode::Orthographic) {
        pixelToWorld = std::max(1e-4f, camera.orthographicHeight / static_cast<float>(std::max(1, viewportHeight)));
    } else {
        const float distance = cameraDistance(camera);
        pixelToWorld = std::max(1e-4f,
                                2.0f * distance * std::tan(camera.verticalFovRadians * 0.5f)
                                    / static_cast<float>(std::max(1, viewportHeight)));
    }
    const Vec3f translation = cameraRight(camera) * (-deltaPixelsX * pixelToWorld)
                              + cameraOrthoUp(camera) * (deltaPixelsY * pixelToWorld);
    camera.position = camera.position + translation;
    camera.target = camera.target + translation;
}

void dollyCamera(Camera &camera, float wheelSteps)
{
    if (camera.projectionMode == CameraProjectionMode::Orthographic) {
        camera.orthographicHeight = std::max(0.1f, camera.orthographicHeight * std::pow(0.88f, wheelSteps));
        return;
    }

    const float distance = cameraDistance(camera);
    const float nextDistance = std::max(0.2f, distance * std::pow(0.88f, wheelSteps));
    const Vec3f offsetDirection = normalize(camera.position - camera.target);
    camera.position = camera.target + offsetDirection * nextDistance;
}

void freeLookCamera(Camera &camera, float yawDeltaRadians, float pitchDeltaRadians)
{
    const Vec3f forward = cameraForward(camera);
    const float focusDistance = std::max(0.5f, cameraDistance(camera));
    const float yaw = std::atan2(forward.x, -forward.z);
    const float pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
    const float nextYaw = yaw + yawDeltaRadians;
    const float nextPitch = std::clamp(pitch + pitchDeltaRadians, -1.45f, 1.45f);
    const float cosPitch = std::cos(nextPitch);
    const Vec3f nextForward{
        std::sin(nextYaw) * cosPitch,
        std::sin(nextPitch),
        -std::cos(nextYaw) * cosPitch
    };

    camera.target = camera.position + nextForward * focusDistance;
    camera.up = {0.0f, 1.0f, 0.0f};
}

Camera makeAxisViewCamera(const Camera &sourceCamera, CameraAxisView view)
{
    Camera camera = sourceCamera;
    const float distance = cameraDistance(sourceCamera);
    switch (view) {
    case CameraAxisView::Front:
        camera.position = camera.target + Vec3f{0.0f, 0.0f, distance};
        camera.up = {0.0f, 1.0f, 0.0f};
        break;
    case CameraAxisView::Back:
        camera.position = camera.target + Vec3f{0.0f, 0.0f, -distance};
        camera.up = {0.0f, 1.0f, 0.0f};
        break;
    case CameraAxisView::Left:
        camera.position = camera.target + Vec3f{-distance, 0.0f, 0.0f};
        camera.up = {0.0f, 1.0f, 0.0f};
        break;
    case CameraAxisView::Right:
        camera.position = camera.target + Vec3f{distance, 0.0f, 0.0f};
        camera.up = {0.0f, 1.0f, 0.0f};
        break;
    case CameraAxisView::Top:
        camera.position = camera.target + Vec3f{0.0f, distance, 0.0f};
        camera.up = {0.0f, 0.0f, -1.0f};
        break;
    case CameraAxisView::Bottom:
        camera.position = camera.target + Vec3f{0.0f, -distance, 0.0f};
        camera.up = {0.0f, 0.0f, 1.0f};
        break;
    }

    return camera;
}

Camera makeResetCamera(ScenePreset preset)
{
    switch (preset) {
    case ScenePreset::Custom:
    case ScenePreset::DefaultOrbit:
        return makeCamera({1.6f, 1.1f, 2.25f}, {0.0f, 0.0f, -4.5f}, 55.0f);
    case ScenePreset::TextureStudy:
        return makeCamera({0.0f, 0.15f, 1.4f}, {0.0f, 0.0f, -4.5f}, 38.0f);
    case ScenePreset::LightingStudy:
        return makeCamera({2.35f, 1.65f, 1.8f}, {0.0f, 0.0f, -4.5f}, 50.0f);
    case ScenePreset::WireframeInspect:
        return makeCamera({2.25f, 1.55f, 2.6f}, {0.0f, 0.0f, -4.5f}, 60.0f);
    case ScenePreset::UvInspect:
        return makeCamera({0.0f, 0.1f, 1.6f}, {0.0f, 0.0f, -4.5f}, 40.0f);
    case ScenePreset::OverdrawInspect:
        return makeCamera({0.0f, 0.0f, 1.25f}, {0.0f, 0.0f, -4.5f}, 34.0f);
    }

    return makeCamera({1.6f, 1.1f, 2.25f}, {0.0f, 0.0f, -4.5f}, 55.0f);
}

RenderState makeRenderState(RenderPreset preset)
{
    RenderState state;
    state.antiAliasing = AntiAliasingMode::Coverage4x;

    switch (preset) {
    case RenderPreset::Custom:
    case RenderPreset::Shaded:
        state.depthTestEnable = true;
        state.depthWriteEnable = true;
        state.depthFunc = DepthFunc::Less;
        state.cullMode = CullMode::Back;
        state.fillMode = FillMode::Solid;
        state.debugView = DebugView::None;
        break;
    case RenderPreset::Wireframe:
        state.depthTestEnable = true;
        state.depthWriteEnable = true;
        state.depthFunc = DepthFunc::Less;
        state.cullMode = CullMode::None;
        state.fillMode = FillMode::Wireframe;
        state.debugView = DebugView::None;
        break;
    case RenderPreset::DepthDebug:
        state.depthTestEnable = true;
        state.depthWriteEnable = true;
        state.depthFunc = DepthFunc::Less;
        state.cullMode = CullMode::None;
        state.fillMode = FillMode::Solid;
        state.debugView = DebugView::Depth;
        break;
    case RenderPreset::NormalDebug:
        state.depthTestEnable = true;
        state.depthWriteEnable = true;
        state.depthFunc = DepthFunc::Less;
        state.cullMode = CullMode::Back;
        state.fillMode = FillMode::Solid;
        state.debugView = DebugView::Normal;
        break;
    case RenderPreset::UvDebug:
        state.depthTestEnable = true;
        state.depthWriteEnable = true;
        state.depthFunc = DepthFunc::Less;
        state.cullMode = CullMode::Back;
        state.fillMode = FillMode::Solid;
        state.debugView = DebugView::UV;
        break;
    case RenderPreset::OverdrawDebug:
        state.depthTestEnable = false;
        state.depthWriteEnable = false;
        state.depthFunc = DepthFunc::Always;
        state.cullMode = CullMode::None;
        state.fillMode = FillMode::Solid;
        state.debugView = DebugView::Overdraw;
        break;
    }

    return state;
}

Texture2D makeGradientTexture(int width, int height)//生成渐变纹理
{
    Texture2D texture;
    texture.width = std::max(1, width);
    texture.height = std::max(1, height);
    texture.texels.resize(static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height));

    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const float u = texture.width > 1 ? static_cast<float>(x) / static_cast<float>(texture.width - 1) : 0.0f;
            const float v = texture.height > 1 ? static_cast<float>(y) / static_cast<float>(texture.height - 1) : 0.0f;
            const std::uint32_t r = static_cast<std::uint32_t>(u * 255.0f + 0.5f);
            const std::uint32_t g = static_cast<std::uint32_t>(v * 255.0f + 0.5f);
            const std::uint32_t b = static_cast<std::uint32_t>((1.0f - u) * 255.0f + 0.5f);
            texture.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width)
                           + static_cast<std::size_t>(x)] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }

    texture.rebuildMipChain();
    return texture;
}

Texture2D makeDemoTexture(DemoTexturePreset preset)
{
    switch (preset) {
    case DemoTexturePreset::WarmChecker:
        return Texture2D::makeCheckerboard(64, 64, 8, 0xfff4f0e8u, 0xff2d6a4fu);
    case DemoTexturePreset::MonoChecker:
        return Texture2D::makeCheckerboard(64, 64, 8, 0xffffffffu, 0xff1a1a1au);
    case DemoTexturePreset::White: {
        Texture2D texture;
        texture.width = 1;
        texture.height = 1;
        texture.texels = {0xffffffffu};
        texture.rebuildMipChain();
        return texture;
    }
    case DemoTexturePreset::Gradient:
        return makeGradientTexture(64, 64);
    }

    return Texture2D::makeCheckerboard(64, 64, 8, 0xfff4f0e8u, 0xff2d6a4fu);
}

Texture2D makeTextureFromImage(const QImage &sourceImage)
{
    const QImage image = sourceImage.convertToFormat(QImage::Format_ARGB32);

    Texture2D texture;
    texture.width = image.width();
    texture.height = image.height();
    texture.texels.resize(static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height));

    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            texture.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width)
                           + static_cast<std::size_t>(x)] = image.pixel(x, y);
        }
    }

    texture.rebuildMipChain();
    return texture;
}

bool loadTextureFromImageFile(const QString &path, Texture2D &texture, QString *errorMessage)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("纹理文件不存在或不是有效文件：%1").arg(
                QDir::toNativeSeparators(fileInfo.absoluteFilePath()));
        }
        return false;
    }

    QImageReader reader(fileInfo.absoluteFilePath());
    if (!reader.canRead()) {
        if (errorMessage != nullptr) {
            const QString detail = reader.errorString().trimmed();
            const QString formats = supportedImageFormatsSummary();
            *errorMessage = QStringLiteral("无法读取图片纹理：%1\n原因：%2\n支持格式：%3")
                                .arg(QDir::toNativeSeparators(fileInfo.absoluteFilePath()),
                                     detail.isEmpty() ? QStringLiteral("未知图片格式或文件损坏。") : detail,
                                     formats.isEmpty() ? QStringLiteral("由 Qt 当前插件决定") : formats);
        }
        return false;
    }

    const QImage image = reader.read();
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            const QString detail = reader.errorString().trimmed();
            *errorMessage = QStringLiteral("图片已识别但解码失败：%1\n原因：%2")
                                .arg(QDir::toNativeSeparators(fileInfo.absoluteFilePath()),
                                     detail.isEmpty() ? QStringLiteral("图片数据为空或损坏。") : detail);
        }
        return false;
    }

    texture = makeTextureFromImage(image);
    if (!texture.isValid()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("图片纹理数据无效：%1").arg(QDir::toNativeSeparators(fileInfo.absoluteFilePath()));
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

MaterialType texturedMaterialVariant(MaterialType type)
{
    switch (type) {
    case MaterialType::LambertVertexColor:
        return MaterialType::LambertTextured;
    case MaterialType::UnlitVertexColor:
        return MaterialType::UnlitTextured;
    case MaterialType::BlinnPhongVertexColor:
        return MaterialType::BlinnPhongTextured;
    case MaterialType::PbrVertexColor:
        return MaterialType::PbrTextured;
    case MaterialType::LambertTextured:
    case MaterialType::UnlitTextured:
    case MaterialType::BlinnPhongTextured:
    case MaterialType::PbrTextured:
        return type;
    }

    return type;
}

Transform makeLoadedMeshTransform(const Mesh &mesh)
{
    if (mesh.vertices.empty())
        return {};

    Vec3f minPoint{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    Vec3f maxPoint{
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()
    };

    for (const VertexInput &vertex : mesh.vertices) {
        minPoint.x = std::min(minPoint.x, vertex.position.x);
        minPoint.y = std::min(minPoint.y, vertex.position.y);
        minPoint.z = std::min(minPoint.z, vertex.position.z);
        maxPoint.x = std::max(maxPoint.x, vertex.position.x);
        maxPoint.y = std::max(maxPoint.y, vertex.position.y);
        maxPoint.z = std::max(maxPoint.z, vertex.position.z);
    }

    const Vec3f center = (minPoint + maxPoint) * 0.5f;
    float radius = 0.0f;
    for (const VertexInput &vertex : mesh.vertices)
        radius = std::max(radius, length(vertex.position - center));

    const float scale = radius > 1e-4f ? 1.6f / radius : 1.0f;

    Transform transform;
    transform.scale = {scale, scale, scale};
    transform.position = {-center.x * scale, -center.y * scale, -4.5f - center.z * scale};
    return transform;
}

Transform makeDemoCubeTransform()
{
    Transform transform;
    transform.position = {0.0f, 0.0f, -4.5f};
    return transform;
}

QString makeSceneObjectDisplayName(const QString &sourcePath, bool isDemoCube, int index)
{
    if (isDemoCube)
        return QStringLiteral("示例立方体 %1").arg(index + 1);

    const QString fileName = QFileInfo(sourcePath).fileName();
    if (!fileName.isEmpty())
        return fileName;
    return QStringLiteral("OBJ 对象 %1").arg(index + 1);
}

Vec3f rotateByEulerRadians(const Vec3f &value, const Vec3f &rotationRadians)
{
    const Mat4f rotationMatrix = Mat4f::rotationZ(rotationRadians.z)
                                 * Mat4f::rotationY(rotationRadians.y)
                                 * Mat4f::rotationX(rotationRadians.x);
    const Vec4f rotated = rotationMatrix * Vec4f{value.x, value.y, value.z, 0.0f};
    return normalize(Vec3f{rotated.x, rotated.y, rotated.z});
}

QString fallbackDirectionalLightName(int index)
{
    return index == 0 ? QStringLiteral("主方向光") : QStringLiteral("方向光 %1").arg(index + 1);
}

QString fallbackPointLightName(int index)
{
    return QStringLiteral("点光源 %1").arg(index + 1);
}

QString fallbackSpotLightName(int index)
{
    return QStringLiteral("聚光灯 %1").arg(index + 1);
}

QString trimmedOrFallbackLightName(const QString &name, const QString &fallback)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString cameraProjectionModeName(CameraProjectionMode mode)
{
    return mode == CameraProjectionMode::Orthographic
        ? QStringLiteral("正交")
        : QStringLiteral("透视");
}

QRectF directionalLightCardRect(int index)
{
    return QRectF(16.0, 16.0 + static_cast<qreal>(index) * 78.0, 164.0, 64.0);
}

float worldUnitsPerPixelAtViewDepth(const Camera &camera, float viewDepth, int viewportHeight)
{
    if (camera.projectionMode == CameraProjectionMode::Orthographic) {
        return std::max(1e-4f, camera.orthographicHeight / static_cast<float>(std::max(1, viewportHeight)));
    }

    return std::max(1e-4f,
                    2.0f * std::max(0.05f, viewDepth) * std::tan(camera.verticalFovRadians * 0.5f)
                        / static_cast<float>(std::max(1, viewportHeight)));
}

float snapToStep(float value, float step)
{
    if (step <= 1e-6f)
        return value;
    return std::round(value / step) * step;
}

Vec3f viewportAxisVector(RasterWidget::ViewportAxis axis)
{
    switch (axis) {
    case RasterWidget::ViewportAxis::X:
        return {1.0f, 0.0f, 0.0f};
    case RasterWidget::ViewportAxis::Y:
        return {0.0f, 1.0f, 0.0f};
    case RasterWidget::ViewportAxis::Z:
        return {0.0f, 0.0f, 1.0f};
    case RasterWidget::ViewportAxis::None:
        break;
    }

    return {0.0f, 0.0f, 0.0f};
}

QColor viewportAxisColor(RasterWidget::ViewportAxis axis)
{
    switch (axis) {
    case RasterWidget::ViewportAxis::X:
        return QColor(255, 96, 96);
    case RasterWidget::ViewportAxis::Y:
        return QColor(104, 214, 112);
    case RasterWidget::ViewportAxis::Z:
        return QColor(90, 160, 255);
    case RasterWidget::ViewportAxis::None:
        break;
    }

    return QColor(208, 208, 208);
}

int viewportAxisIndex(RasterWidget::ViewportAxis axis)
{
    switch (axis) {
    case RasterWidget::ViewportAxis::X:
        return 0;
    case RasterWidget::ViewportAxis::Y:
        return 1;
    case RasterWidget::ViewportAxis::Z:
        return 2;
    case RasterWidget::ViewportAxis::None:
        break;
    }

    return -1;
}

float pointSegmentDistanceSquared(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const qreal abx = b.x() - a.x();
    const qreal aby = b.y() - a.y();
    const qreal lengthSquared = abx * abx + aby * aby;
    if (lengthSquared <= 1e-6)
        return static_cast<float>((point.x() - a.x()) * (point.x() - a.x()) + (point.y() - a.y()) * (point.y() - a.y()));

    const qreal apx = point.x() - a.x();
    const qreal apy = point.y() - a.y();
    const qreal t = std::clamp((apx * abx + apy * aby) / lengthSquared, 0.0, 1.0);
    const qreal closestX = a.x() + abx * t;
    const qreal closestY = a.y() + aby * t;
    const qreal dx = point.x() - closestX;
    const qreal dy = point.y() - closestY;
    return static_cast<float>(dx * dx + dy * dy);
}

QRectF directionalLightSphereRect()
{
    return QRectF(196.0, 20.0, 96.0, 96.0);
}

Vec3f clampSphereVector(const QPointF &point, const QRectF &rect)
{
    const float radius = static_cast<float>(rect.width() * 0.5);
    const float x = static_cast<float>((point.x() - rect.center().x()) / radius);
    const float y = static_cast<float>((rect.center().y() - point.y()) / radius);
    const float lengthSquared = x * x + y * y;
    if (lengthSquared >= 1.0f) {
        const float invLength = 1.0f / std::sqrt(std::max(1e-6f, lengthSquared));
        return {x * invLength, y * invLength, 0.0f};
    }

    return {x, y, std::sqrt(std::max(0.0f, 1.0f - lengthSquared))};
}

bool projectWorldToViewport(const Camera &camera,
                            int viewportWidth,
                            int viewportHeight,
                            const Vec3f &worldPosition,
                            QPointF &screenPosition,
                            float *depth = nullptr)
{
    if (viewportWidth <= 0 || viewportHeight <= 0)
        return false;

    const float aspect = static_cast<float>(viewportWidth) / static_cast<float>(std::max(1, viewportHeight));
    const Mat4f viewProjection = camera.projectionMatrix(aspect) * camera.viewMatrix();
    const Vec4f clipPosition = viewProjection * Vec4f{worldPosition.x, worldPosition.y, worldPosition.z, 1.0f};
    if (std::fabs(clipPosition.w) <= 1e-6f)
        return false;

    const float inverseW = 1.0f / clipPosition.w;
    const float ndcX = clipPosition.x * inverseW;
    const float ndcY = clipPosition.y * inverseW;
    const float ndcZ = clipPosition.z * inverseW;
    if (ndcZ < -1.0f || ndcZ > 1.0f)
        return false;

    screenPosition.setX((ndcX * 0.5f + 0.5f) * static_cast<float>(viewportWidth - 1));
    screenPosition.setY((1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewportHeight - 1));
    if (depth != nullptr)
        *depth = ndcZ * 0.5f + 0.5f;
    return true;
}

bool loadMeshFromObjText(const QString &sourceText, Mesh &mesh, QString *errorMessage)
{
    std::string parseError;
    if (!Mesh::loadObjFromText(sourceText.toUtf8().toStdString(), mesh, &parseError)) {
        if (errorMessage != nullptr)
            *errorMessage = QString::fromStdString(parseError);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

int grayscaleFromArgb(std::uint32_t color)
{
    const int r = static_cast<int>((color >> 16) & 0xffu);
    const int g = static_cast<int>((color >> 8) & 0xffu);
    const int b = static_cast<int>(color & 0xffu);
    return (77 * r + 150 * g + 29 * b) / 256;
}

std::vector<float> makeGrayscaleBuffer(const QImage &image)
{
    std::vector<float> values(static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height()), 255.0f);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            values[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()) + static_cast<std::size_t>(x)] =
                static_cast<float>(grayscaleFromArgb(image.pixel(x, y)));
        }
    }
    return values;
}

std::vector<float> blur3x3(const std::vector<float> &input, int width, int height)
{
    std::vector<float> output(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 255.0f);
    static const int kernel[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1}
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float weightedSum = 0.0f;
            float weightSum = 0.0f;
            for (int ky = -1; ky <= 1; ++ky) {
                const int sampleY = std::clamp(y + ky, 0, height - 1);
                for (int kx = -1; kx <= 1; ++kx) {
                    const int sampleX = std::clamp(x + kx, 0, width - 1);
                    const float weight = static_cast<float>(kernel[ky + 1][kx + 1]);
                    weightedSum += input[static_cast<std::size_t>(sampleY) * static_cast<std::size_t>(width)
                                         + static_cast<std::size_t>(sampleX)] * weight;
                    weightSum += weight;
                }
            }
            output[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                weightSum > 0.0f ? weightedSum / weightSum : 255.0f;
        }
    }

    return output;
}

struct LineArtBuildResult {
    QImage opaqueImage;
    QImage transparentImage;
};

float sampleScalar(const std::vector<float> &values, int width, int height, int x, int y)
{
    const int clampedX = std::clamp(x, 0, width - 1);
    const int clampedY = std::clamp(y, 0, height - 1);
    return values[static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(clampedX)];
}

std::vector<float> nonMaximumSuppression(const std::vector<float> &magnitude,
                                         const std::vector<float> &gradientX,
                                         const std::vector<float> &gradientY,
                                         int width,
                                         int height)
{
    std::vector<float> suppressed(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const float angle = std::atan2(gradientY[index], gradientX[index]) * 180.0f / kPi;
            const float wrappedAngle = angle < 0.0f ? angle + 180.0f : angle;
            float forward = 0.0f;
            float backward = 0.0f;

            if ((wrappedAngle >= 0.0f && wrappedAngle < 22.5f) || wrappedAngle >= 157.5f) {
                forward = sampleScalar(magnitude, width, height, x + 1, y);
                backward = sampleScalar(magnitude, width, height, x - 1, y);
            } else if (wrappedAngle < 67.5f) {
                forward = sampleScalar(magnitude, width, height, x + 1, y - 1);
                backward = sampleScalar(magnitude, width, height, x - 1, y + 1);
            } else if (wrappedAngle < 112.5f) {
                forward = sampleScalar(magnitude, width, height, x, y - 1);
                backward = sampleScalar(magnitude, width, height, x, y + 1);
            } else {
                forward = sampleScalar(magnitude, width, height, x - 1, y - 1);
                backward = sampleScalar(magnitude, width, height, x + 1, y + 1);
            }

            suppressed[index] = (magnitude[index] >= forward && magnitude[index] >= backward) ? magnitude[index] : 0.0f;
        }
    }
    return suppressed;
}

float applyThresholdCurve(float value, LineArtThresholdCurvePreset preset)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    switch (preset) {
    case LineArtThresholdCurvePreset::Balanced:
        return clamped;
    case LineArtThresholdCurvePreset::Soft:
        return std::sqrt(clamped);
    case LineArtThresholdCurvePreset::Strong:
        return clamped * clamped;
    }

    return clamped;
}

QImage dilateAlphaImage(const QImage &source, float strokeWidth)
{
    const float clampedWidth = std::clamp(strokeWidth, 0.25f, 8.0f);
    if (clampedWidth <= 1.01f) {
        if (clampedWidth >= 0.99f)
            return source;

        QImage softened = source;
        for (int y = 0; y < softened.height(); ++y) {
            QRgb *scanline = reinterpret_cast<QRgb *>(softened.scanLine(y));
            for (int x = 0; x < softened.width(); ++x) {
                const int alpha = static_cast<int>(qAlpha(scanline[x]) * clampedWidth + 0.5f);
                scanline[x] = qRgba(0, 0, 0, std::clamp(alpha, 0, 255));
            }
        }
        return softened;
    }

    const int radius = std::max(1, static_cast<int>(std::ceil(clampedWidth - 1.0f)));
    QImage dilated(source.size(), QImage::Format_ARGB32);
    dilated.fill(0x00000000u);
    for (int y = 0; y < source.height(); ++y) {
        QRgb *outputScanline = reinterpret_cast<QRgb *>(dilated.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            int maxAlpha = 0;
            for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
                const int sampleY = std::clamp(y + offsetY, 0, source.height() - 1);
                const QRgb *inputScanline = reinterpret_cast<const QRgb *>(source.constScanLine(sampleY));
                for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
                    const int sampleX = std::clamp(x + offsetX, 0, source.width() - 1);
                    maxAlpha = std::max(maxAlpha, qAlpha(inputScanline[sampleX]));
                }
            }
            outputScanline[x] = qRgba(0, 0, 0, maxAlpha);
        }
    }
    return dilated;
}

QRect fitImageRectForArea(const QSize &imageSize, const QRect &area)
{
    const QSize targetSize = imageSize.scaled(area.size(), Qt::KeepAspectRatio);
    return QRect(area.x() + (area.width() - targetSize.width()) / 2,
                 area.y() + (area.height() - targetSize.height()) / 2,
                 targetSize.width(),
                 targetSize.height());
}

float degreesToRadians(float degrees)
{
    return degrees * kPi / 180.0f;
}

Vec3f unpackRgb(QRgb packed)
{
    return {
        static_cast<float>(qRed(packed)) / 255.0f,
        static_cast<float>(qGreen(packed)) / 255.0f,
        static_cast<float>(qBlue(packed)) / 255.0f
    };
}

QRgb packRgb(const Vec3f &color, int alpha = 255)
{
    const Vec3f clamped = clamp01(color);
    return qRgba(static_cast<int>(clamped.x * 255.0f + 0.5f),
                 static_cast<int>(clamped.y * 255.0f + 0.5f),
                 static_cast<int>(clamped.z * 255.0f + 0.5f),
                 std::clamp(alpha, 0, 255));
}

Vec3f applyToneMapping(Vec3f color, ToneMappingMode mode)
{
    switch (mode) {
    case ToneMappingMode::None:
        return color;
    case ToneMappingMode::Reinhard:
        return {
            color.x / (1.0f + color.x),
            color.y / (1.0f + color.y),
            color.z / (1.0f + color.z)
        };
    case ToneMappingMode::AcesApprox:
        break;
    }

    const auto aces = [](float x) {
        const float numerator = x * (2.51f * x + 0.03f);
        const float denominator = x * (2.43f * x + 0.59f) + 0.14f;
        return denominator > 1e-6f ? numerator / denominator : 0.0f;
    };
    return {aces(color.x), aces(color.y), aces(color.z)};
}

Vec3f applyContrastAndSaturation(const Vec3f &color, float contrast, float saturation)
{
    const float luminance = color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
    const Vec3f gray{luminance, luminance, luminance};
    Vec3f saturated = gray + (color - gray) * saturation;
    saturated = (saturated - Vec3f{0.5f, 0.5f, 0.5f}) * contrast + Vec3f{0.5f, 0.5f, 0.5f};
    return saturated;
}

QString debugViewFileName(DebugView view)
{
    switch (view) {
    case DebugView::None:
        return QStringLiteral("shaded");
    case DebugView::Depth:
        return QStringLiteral("depth");
    case DebugView::Normal:
        return QStringLiteral("normal");
    case DebugView::UV:
        return QStringLiteral("uv");
    case DebugView::Overdraw:
        return QStringLiteral("overdraw");
    case DebugView::ObjectId:
        return QStringLiteral("object_id");
    case DebugView::MaterialId:
        return QStringLiteral("material_id");
    case DebugView::TriangleId:
        return QStringLiteral("triangle_id");
    case DebugView::FaceOrientation:
        return QStringLiteral("face_orientation");
    case DebugView::Barycentric:
        return QStringLiteral("barycentric");
    case DebugView::Shadow:
        return QStringLiteral("shadow");
    case DebugView::Lighting:
        return QStringLiteral("lighting");
    }

    return QStringLiteral("debug");
}

LineArtBuildResult makeLineArtImages(const QImage &sourceImage,
                                     float thresholdScale,
                                     float lineStrength,
                                     float processScale,
                                     bool keepGrayBase,
                                     LineArtThresholdCurvePreset thresholdCurvePreset,
                                     LineArtEdgeMode edgeMode,
                                     float transparentStrokeWidth)
{
    const QImage image = sourceImage.convertToFormat(QImage::Format_ARGB32);
    const int width = image.width();
    const int height = image.height();
    if (width <= 0 || height <= 0)
        return {};

    const float clampedScale = std::clamp(processScale, 0.1f, 1.0f);
    const QSize processedSize(std::max(1, static_cast<int>(width * clampedScale + 0.5f)),
                              std::max(1, static_cast<int>(height * clampedScale + 0.5f)));
    const QImage processedImage = (processedSize.width() == width && processedSize.height() == height)
        ? image
        : image.scaled(processedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const int processedWidth = processedImage.width();
    const int processedHeight = processedImage.height();
    const std::vector<float> grayscale = makeGrayscaleBuffer(processedImage);
    const std::vector<float> blurred = blur3x3(grayscale, processedWidth, processedHeight);
    std::vector<float> edgeInput = blurred;
    if (edgeMode == LineArtEdgeMode::Detail) {
        edgeInput.resize(grayscale.size(), 0.0f);
        for (std::size_t i = 0; i < edgeInput.size(); ++i)
            edgeInput[i] = grayscale[i] * 0.65f + blurred[i] * 0.35f;
    }

    std::vector<float> gradientMagnitude(static_cast<std::size_t>(processedWidth) * static_cast<std::size_t>(processedHeight), 0.0f);
    std::vector<float> gradientX(static_cast<std::size_t>(processedWidth) * static_cast<std::size_t>(processedHeight), 0.0f);
    std::vector<float> gradientY(static_cast<std::size_t>(processedWidth) * static_cast<std::size_t>(processedHeight), 0.0f);

    float maxMagnitude = 0.0f;
    float magnitudeSum = 0.0f;
    int magnitudeCount = 0;
    for (int y = 1; y + 1 < processedHeight; ++y) {
        for (int x = 1; x + 1 < processedWidth; ++x) {
            const auto sample = [&](int sx, int sy) {
                return edgeInput[static_cast<std::size_t>(sy) * static_cast<std::size_t>(processedWidth)
                                 + static_cast<std::size_t>(sx)];
            };

            const float gx = -sample(x - 1, y - 1) + sample(x + 1, y - 1)
                             - 2.0f * sample(x - 1, y) + 2.0f * sample(x + 1, y)
                             - sample(x - 1, y + 1) + sample(x + 1, y + 1);
            const float gy = -sample(x - 1, y - 1) - 2.0f * sample(x, y - 1) - sample(x + 1, y - 1)
                             + sample(x - 1, y + 1) + 2.0f * sample(x, y + 1) + sample(x + 1, y + 1);
            const float magnitude = std::sqrt(gx * gx + gy * gy);
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(processedWidth) + static_cast<std::size_t>(x);
            gradientX[index] = gx;
            gradientY[index] = gy;
            gradientMagnitude[index] = magnitude;
            maxMagnitude = std::max(maxMagnitude, magnitude);
            magnitudeSum += magnitude;
            ++magnitudeCount;
        }
    }

    if (edgeMode == LineArtEdgeMode::Thin)
        gradientMagnitude = nonMaximumSuppression(gradientMagnitude, gradientX, gradientY, processedWidth, processedHeight);

    const float averageMagnitude = magnitudeCount > 0 ? magnitudeSum / static_cast<float>(magnitudeCount) : 0.0f;
    const float thresholdBias = edgeMode == LineArtEdgeMode::Detail ? 0.92f : 1.0f;
    const float threshold = std::max(averageMagnitude * 1.35f, maxMagnitude * 0.18f)
                            * std::clamp(thresholdScale, 0.25f, 4.0f)
                            * thresholdBias;
    const float normalizationRange = std::max(1.0f, maxMagnitude - threshold);

    QImage processedLineArt(processedWidth, processedHeight, QImage::Format_ARGB32);
    QImage processedTransparentLineArt(processedWidth, processedHeight, QImage::Format_ARGB32);
    processedLineArt.fill(0xffffffffu);
    processedTransparentLineArt.fill(0x00000000u);
    const float clampedStrength = std::clamp(lineStrength, 0.1f, 4.0f);
    const float backgroundMix = keepGrayBase ? 0.35f : 1.0f;
    for (int y = 0; y < processedHeight; ++y) {
        for (int x = 0; x < processedWidth; ++x) {
            const float magnitude = gradientMagnitude[static_cast<std::size_t>(y) * static_cast<std::size_t>(processedWidth)
                                                      + static_cast<std::size_t>(x)];
            const float normalizedEdge = std::clamp((magnitude - threshold) / normalizationRange, 0.0f, 1.0f);
            const float curvedEdge = applyThresholdCurve(normalizedEdge, thresholdCurvePreset);
            const float edgeStrength = std::clamp(curvedEdge * clampedStrength, 0.0f, 1.0f);
            const float grayBase = keepGrayBase ? grayscale[static_cast<std::size_t>(y) * static_cast<std::size_t>(processedWidth)
                                                          + static_cast<std::size_t>(x)] / 255.0f
                                                : 1.0f;
            const float mixed = std::clamp(grayBase * backgroundMix * (1.0f - edgeStrength) + (1.0f - backgroundMix), 0.0f, 1.0f);
            const int shade = static_cast<int>(mixed * 255.0f + 0.5f);
            processedLineArt.setPixel(x, y, 0xff000000u | (static_cast<std::uint32_t>(shade) << 16)
                                              | (static_cast<std::uint32_t>(shade) << 8)
                                              | static_cast<std::uint32_t>(shade));
            const int alpha = static_cast<int>(edgeStrength * 255.0f + 0.5f);
            processedTransparentLineArt.setPixel(x, y, (static_cast<std::uint32_t>(alpha) << 24));
        }
    }

    processedTransparentLineArt = dilateAlphaImage(processedTransparentLineArt, transparentStrokeWidth);

    if (processedWidth == width && processedHeight == height)
        return {processedLineArt, processedTransparentLineArt};

    return {
        processedLineArt.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
        processedTransparentLineArt.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
    };
}

Material makeMaterialFromType(MaterialType type)
{
    switch (type) {
    case MaterialType::LambertTextured:
        return Material::makeLambertTextured();
    case MaterialType::LambertVertexColor:
        return Material::makeLambertVertexColor();
    case MaterialType::UnlitTextured:
        return Material::makeUnlitTextured();
    case MaterialType::UnlitVertexColor:
        return Material::makeUnlitVertexColor();
    case MaterialType::BlinnPhongTextured:
        return Material::makeBlinnPhongTextured();
    case MaterialType::BlinnPhongVertexColor:
        return Material::makeBlinnPhongVertexColor();
    case MaterialType::PbrTextured:
        return Material::makePbrTextured();
    case MaterialType::PbrVertexColor:
        return Material::makePbrVertexColor();
    }

    return Material::makeLambertTextured();
}

void applyMaterialType(Material &material, MaterialType type)
{
    const Texture2D texture = material.texture;
    const Texture2D normalTexture = material.normalTexture;
    const Texture2D metallicRoughnessTexture = material.metallicRoughnessTexture;
    const SamplerState sampler = material.sampler;
    const SamplerState normalSampler = material.normalSampler;
    const SamplerState metallicRoughnessSampler = material.metallicRoughnessSampler;
    const Vec3f specularColor = material.specularColor;
    const float specularStrength = material.specularStrength;
    const float shininess = material.shininess;
    const float normalStrength = material.normalStrength;
    const float metallic = material.metallic;
    const float roughness = material.roughness;
    const bool receiveShadow = material.receiveShadow;
    const MaterialSurfaceMode surfaceMode = material.surfaceMode;
    const float opacity = material.opacity;
    const bool depthWriteEnable = material.depthWriteEnable;

    Material rebuilt = makeMaterialFromType(type);
    rebuilt.texture = texture;
    rebuilt.normalTexture = normalTexture;
    rebuilt.metallicRoughnessTexture = metallicRoughnessTexture;
    rebuilt.sampler = sampler;
    rebuilt.normalSampler = normalSampler;
    rebuilt.metallicRoughnessSampler = metallicRoughnessSampler;
    rebuilt.specularColor = specularColor;
    rebuilt.specularStrength = specularStrength;
    rebuilt.shininess = shininess;
    rebuilt.normalStrength = normalStrength;
    rebuilt.metallic = metallic;
    rebuilt.roughness = roughness;
    rebuilt.receiveShadow = receiveShadow;
    rebuilt.surfaceMode = surfaceMode;
    rebuilt.opacity = opacity;
    rebuilt.depthWriteEnable = depthWriteEnable;
    material = rebuilt;
}

DirectionalLight &primarySceneLight(LightingContext &lighting)
{
    if (lighting.directionalLights.empty())
        lighting = LightingContext::makeDefault();
    return lighting.directionalLights.front();
}

const DirectionalLight &primarySceneLight(const LightingContext &lighting)
{
    static const LightingContext fallbackLighting = LightingContext::makeDefault();
    if (lighting.directionalLights.empty())
        return fallbackLighting.directionalLights.front();
    return lighting.directionalLights.front();
}

PointLight *pointLightAt(LightingContext &lighting, int index)
{
    if (index < 0 || index >= static_cast<int>(lighting.pointLights.size()))
        return nullptr;
    return &lighting.pointLights[static_cast<std::size_t>(index)];
}

const PointLight *pointLightAt(const LightingContext &lighting, int index)
{
    if (index < 0 || index >= static_cast<int>(lighting.pointLights.size()))
        return nullptr;
    return &lighting.pointLights[static_cast<std::size_t>(index)];
}

SpotLight *spotLightAt(LightingContext &lighting, int index)
{
    if (index < 0 || index >= static_cast<int>(lighting.spotLights.size()))
        return nullptr;
    return &lighting.spotLights[static_cast<std::size_t>(index)];
}

const SpotLight *spotLightAt(const LightingContext &lighting, int index)
{
    if (index < 0 || index >= static_cast<int>(lighting.spotLights.size()))
        return nullptr;
    return &lighting.spotLights[static_cast<std::size_t>(index)];
}

PointLight makeDefaultPointLight()
{
    PointLight light;
    light.position = {0.0f, 1.5f, 0.5f};
    light.color = {1.0f, 0.92f, 0.8f};
    light.intensity = 8.0f;
    light.range = 8.0f;
    light.ambient = 0.0f;
    light.enabled = true;
    light.name = QStringLiteral("点光源").toStdString();
    return light;
}

SpotLight makeDefaultSpotLight()
{
    SpotLight light;
    light.position = {0.0f, 2.5f, 1.0f};
    light.direction = normalize(Vec3f{0.0f, -1.0f, -0.25f});
    light.color = {1.0f, 0.96f, 0.86f};
    light.intensity = 10.0f;
    light.range = 10.0f;
    light.innerConeDegrees = 22.5f;
    light.outerConeDegrees = 32.0f;
    light.shadowRange = 10.0f;
    light.enabled = true;
    light.name = QStringLiteral("聚光灯").toStdString();
    return light;
}

} // namespace

RasterWidget::RasterWidget(QWidget *parent)
    : QWidget(parent),
      m_lightingContext(LightingContext::makeDefault()),
      m_selectedSceneObjectIndex(0),
      m_gizmoSpaceMode(GizmoSpaceMode::World),
      m_gizmoTranslationSnapStep(0.25f),
      m_gizmoRotationSnapRadians(kPi / 12.0f),
      m_gizmoScaleSnapStep(0.1f),
      m_gizmoFineTuneFactor(0.2f),
      m_scenePreset(ScenePreset::DefaultOrbit),
      m_selectedLightKind(SelectedLightKind::None),
      m_selectedLightIndex(-1),
      m_isLineArtMode(false),
      m_lineArtThresholdScale(1.0f),
      m_lineArtLineStrength(1.6f),
      m_lineArtProcessScale(0.6f),
      m_lineArtKeepGrayBase(false),
      m_lineArtComparePreview(true),
      m_lineArtThresholdCurvePreset(LineArtThresholdCurvePreset::Balanced),
      m_lineArtEdgeMode(LineArtEdgeMode::Standard),
      m_lineArtTransparentStrokeWidth(1.0f),
      m_lineArtCompareSplit(0.5f),
      m_isDraggingLineArtSplit(false),
      m_renderQueued(false),
      m_sceneDirty(true),
      m_isOrbiting(false),
      m_isPanning(false),
      m_isFreeLooking(false),
      m_isDraggingViewportHandle(false),
      m_dragHandleKind(ViewportHandleKind::None),
      m_dragHandleIndex(-1),
      m_dragHandleAxis(ViewportAxis::None),
      m_dragHandleOperation(ViewportHandleOperation::None),
      m_dragStartWorldPosition{0.0f, 0.0f, 0.0f},
      m_dragStartViewDepth(0.0f),
      m_dragAxisLengthWorld(0.0f),
      m_dragStartDirection{0.0f, -1.0f, 0.0f},
      m_dragStartRotationRadians{0.0f, 0.0f, 0.0f},
      m_dragStartScale{1.0f, 1.0f, 1.0f},
      m_inspectorHighlightKind(ViewportHandleKind::None),
      m_inspectorHighlightOperation(ViewportHandleOperation::None),
      m_inspectorHighlightAxis(ViewportAxis::None),
      m_lastFrameElapsedMs(0),
      m_cameraMoveSpeed(3.5f),
      m_moveForward(false),
      m_moveBackward(false),
      m_moveLeft(false),
      m_moveRight(false),
      m_moveUp(false),
      m_moveDown(false),
      m_moveFast(false),
      m_gizmoFineTune(false)
{
    // 告诉 Qt 这个控件会完全自己绘制，避免多余背景填充。
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    // 连续刷新只在动画或自由飞行等确实需要时开启，静止场景保持按需渲染。
    connect(&m_timer, &QTimer::timeout, this, &RasterWidget::renderFrame);
    m_elapsed.start();
    syncFrameClock();
    resetSceneToDemoCube();
    RenderState initialState = m_renderer.renderState();
    initialState.antiAliasing = AntiAliasingMode::Coverage4x;
    m_renderer.setRenderState(initialState);
    updateRenderLoopState();
    requestRender();
}

RasterWidget::SceneObjectEntry *RasterWidget::selectedSceneObject()
{
    if (m_sceneObjects.empty())
        return nullptr;
    return &m_sceneObjects[static_cast<std::size_t>(m_selectedSceneObjectIndex)];
}

const RasterWidget::SceneObjectEntry *RasterWidget::selectedSceneObject() const
{
    if (m_sceneObjects.empty())
        return nullptr;
    return &m_sceneObjects[static_cast<std::size_t>(m_selectedSceneObjectIndex)];
}

RasterWidget::MaterialBinding *RasterWidget::selectedEditableMaterialBinding()
{
    SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr)
        return nullptr;
    if (object->useMaterialAsset) {
        MaterialAssetEntry *asset = materialAssetById(object->materialAssetId);
        if (asset != nullptr)
            return &asset->binding;
    }
    return &object->materialInstance;
}

const RasterWidget::MaterialBinding *RasterWidget::selectedEditableMaterialBinding() const
{
    const SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr)
        return nullptr;
    if (object->useMaterialAsset) {
        const MaterialAssetEntry *asset = materialAssetById(object->materialAssetId);
        if (asset != nullptr)
            return &asset->binding;
    }
    return &object->materialInstance;
}

Material RasterWidget::resolveMaterialBinding(const MaterialBinding &binding) const
{
    Material resolved = binding.material;
    resolved.texture = binding.textureAssetId >= 0
        ? (textureAssetById(binding.textureAssetId) != nullptr ? textureAssetById(binding.textureAssetId)->texture
                                                               : makeDemoTexture(binding.texturePreset))
        : makeDemoTexture(binding.texturePreset);
    resolved.normalTexture = binding.normalTextureAssetId >= 0
        ? (textureAssetById(binding.normalTextureAssetId) != nullptr ? textureAssetById(binding.normalTextureAssetId)->texture
                                                                     : Texture2D{})
        : Texture2D{};
    resolved.metallicRoughnessTexture = binding.metallicRoughnessTextureAssetId >= 0
        ? (textureAssetById(binding.metallicRoughnessTextureAssetId) != nullptr
               ? textureAssetById(binding.metallicRoughnessTextureAssetId)->texture
               : Texture2D{})
        : Texture2D{};
    return resolved;
}

RasterWidget::TextureAssetEntry *RasterWidget::textureAssetById(int id)
{
    auto it = std::find_if(m_textureAssets.begin(), m_textureAssets.end(), [id](const TextureAssetEntry &asset) {
        return asset.id == id;
    });
    return it != m_textureAssets.end() ? &(*it) : nullptr;
}

const RasterWidget::TextureAssetEntry *RasterWidget::textureAssetById(int id) const
{
    auto it = std::find_if(m_textureAssets.begin(), m_textureAssets.end(), [id](const TextureAssetEntry &asset) {
        return asset.id == id;
    });
    return it != m_textureAssets.end() ? &(*it) : nullptr;
}

RasterWidget::MaterialAssetEntry *RasterWidget::materialAssetById(int id)
{
    auto it = std::find_if(m_materialAssets.begin(), m_materialAssets.end(), [id](const MaterialAssetEntry &asset) {
        return asset.id == id;
    });
    return it != m_materialAssets.end() ? &(*it) : nullptr;
}

const RasterWidget::MaterialAssetEntry *RasterWidget::materialAssetById(int id) const
{
    auto it = std::find_if(m_materialAssets.begin(), m_materialAssets.end(), [id](const MaterialAssetEntry &asset) {
        return asset.id == id;
    });
    return it != m_materialAssets.end() ? &(*it) : nullptr;
}

bool RasterWidget::registerTextureAssetFromFile(const QString &path,
                                                const QString &suggestedName,
                                                int &assetId,
                                                QString *errorMessage)
{
    Texture2D texture;
    if (!loadTextureFromImageFile(path, texture, errorMessage))
        return false;

    TextureAssetEntry asset;
    asset.id = m_nextTextureAssetId++;
    asset.displayName = suggestedName.trimmed().isEmpty() ? QFileInfo(path).completeBaseName() : suggestedName.trimmed();
    if (asset.displayName.isEmpty())
        asset.displayName = QStringLiteral("纹理 %1").arg(asset.id);
    asset.sourcePath = QFileInfo(path).absoluteFilePath();
    asset.texture = std::move(texture);
    m_textureAssets.push_back(std::move(asset));
    assetId = m_textureAssets.back().id;
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

int RasterWidget::createMaterialAssetFromBinding(const MaterialBinding &binding,
                                                 const QString &suggestedName,
                                                 const QString &sourcePath)
{
    MaterialAssetEntry asset;
    asset.id = m_nextMaterialAssetId++;
    asset.displayName = suggestedName.trimmed().isEmpty() ? QStringLiteral("材质 %1").arg(asset.id) : suggestedName.trimmed();
    asset.sourcePath = sourcePath;
    asset.binding = binding;
    m_materialAssets.push_back(std::move(asset));
    return m_materialAssets.back().id;
}

QString RasterWidget::relativizePathForScene(const QString &absolutePath, const QString &sceneFilePath) const
{
    if (absolutePath.isEmpty() || sceneFilePath.isEmpty())
        return absolutePath;
    const QDir sceneDir(QFileInfo(sceneFilePath).absolutePath());
    return sceneDir.relativeFilePath(absolutePath);
}

QString RasterWidget::resolveSceneRelativePath(const QString &storedPath, const QString &sceneFilePath) const
{
    if (storedPath.isEmpty())
        return QString();
    const QFileInfo fileInfo(storedPath);
    if (fileInfo.isAbsolute() || sceneFilePath.isEmpty())
        return storedPath;
    return QDir(QFileInfo(sceneFilePath).absolutePath()).filePath(storedPath);
}

bool RasterWidget::regenerateLineArt(QString *errorMessage)
{
    if (m_lineArtSourceImage.isNull()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可用于重算的原始照片。");
        return false;
    }

    const LineArtBuildResult result = makeLineArtImages(m_lineArtSourceImage,
                                                        m_lineArtThresholdScale,
                                                        m_lineArtLineStrength,
                                                        m_lineArtProcessScale,
                                                        m_lineArtKeepGrayBase,
                                                        m_lineArtThresholdCurvePreset,
                                                        m_lineArtEdgeMode,
                                                        m_lineArtTransparentStrokeWidth);
    if (result.opaqueImage.isNull() || result.transparentImage.isNull()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("线稿重算失败。");
        return false;
    }

    m_lineArtImage = result.opaqueImage;
    m_lineArtTransparentImage = result.transparentImage;
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

void RasterWidget::exitLineArtMode()
{
    clearCameraMoveState();
    m_isLineArtMode = false;
    m_isDraggingLineArtSplit = false;
    m_lineArtImage = QImage();
    m_lineArtTransparentImage = QImage();
    m_lineArtSourceImage = QImage();
    m_lineArtSourcePath.clear();
    updateRenderLoopState();
}

void RasterWidget::clearCameraMoveState()
{
    m_isOrbiting = false;
    m_isPanning = false;
    m_isFreeLooking = false;
    endViewportHandleDrag();
    m_moveForward = false;
    m_moveBackward = false;
    m_moveLeft = false;
    m_moveRight = false;
    m_moveUp = false;
    m_moveDown = false;
    m_moveFast = false;
    m_gizmoFineTune = false;
    unsetCursor();
    updateRenderLoopState();
}

bool RasterWidget::updateFreeFlyCamera(float deltaSeconds)
{
    if (m_isLineArtMode)
        return false;

    const float directionForward = (m_moveForward ? 1.0f : 0.0f) - (m_moveBackward ? 1.0f : 0.0f);
    const float directionRight = (m_moveRight ? 1.0f : 0.0f) - (m_moveLeft ? 1.0f : 0.0f);
    const float directionUp = (m_moveUp ? 1.0f : 0.0f) - (m_moveDown ? 1.0f : 0.0f);
    if (directionForward == 0.0f && directionRight == 0.0f && directionUp == 0.0f)
        return false;

    Camera camera = m_renderer.camera();
    const float speed = std::max(0.05f, m_cameraMoveSpeed) * (m_moveFast ? 3.0f : 1.0f);
    const Vec3f translation = cameraForward(camera) * (directionForward * speed * deltaSeconds)
                              + cameraRight(camera) * (directionRight * speed * deltaSeconds)
                              + cameraOrthoUp(camera) * (directionUp * speed * deltaSeconds);
    camera.position = camera.position + translation;
    camera.target = camera.target + translation;
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    return true;
}

bool RasterWidget::renderSceneNow(bool force)
{
    if (m_isLineArtMode)
        return false;

    if (width() <= 0 || height() <= 0)
        return false;

    const bool continuousRendering = needsContinuousRendering();
    if (!force && !m_sceneDirty && !continuousRendering)
        return false;

    if (m_renderer.width() != width() || m_renderer.height() != height())
        m_renderer.resize(width(), height());

    const qint64 elapsedMs = m_elapsed.elapsed();
    const float deltaSeconds = std::clamp(static_cast<float>(elapsedMs - m_lastFrameElapsedMs) / 1000.0f, 0.0f, 0.1f);
    m_lastFrameElapsedMs = elapsedMs;
    updateFreeFlyCamera(deltaSeconds);

    Scene scene;
    scene.camera = m_renderer.camera();
    scene.clearColor = 0xff101722u;
    scene.lighting = m_lightingContext;

    std::vector<Material> resolvedMaterials;
    resolvedMaterials.reserve(m_sceneObjects.size());
    for (const SceneObjectEntry &object : m_sceneObjects) {
        const MaterialBinding *binding = object.useMaterialAsset
            ? (materialAssetById(object.materialAssetId) != nullptr ? &materialAssetById(object.materialAssetId)->binding
                                                                    : &object.materialInstance)
            : &object.materialInstance;
        resolvedMaterials.push_back(resolveMaterialBinding(*binding));
        RenderItem item;
        item.mesh = &object.mesh;
        item.material = &resolvedMaterials.back();
        item.transform = object.transform;
        scene.items.push_back(item);
    }

    m_renderer.renderScene(scene);
    m_sceneDirty = false;
    m_scenePresentationImage = buildRendererPresentationImage(true);
    return true;
}

QImage RasterWidget::buildRendererPresentationImage(bool includePostProcess)
{
    const QImage source(reinterpret_cast<const uchar *>(m_renderer.colorBufferData()),
                        m_renderer.width(),
                        m_renderer.height(),
                        QImage::Format_ARGB32);
    const QImage copied = source.copy();
    if (!includePostProcess || !shouldApplyPostProcessToCurrentView())
        return copied;
    return applyPostProcessToImage(copied);
}

QImage RasterWidget::buildViewportPresentationImage(bool includeOverlays)
{
    if (m_isLineArtMode) {
        if (m_lineArtImage.isNull())
            return {};

        QImage image(size(), QImage::Format_ARGB32);
        QPainter painter(&image);
        painter.fillRect(rect(), QColor(246, 246, 246));

        if (m_lineArtComparePreview && !m_lineArtSourceImage.isNull()) {
            const QRect imageArea = rect().adjusted(20, 20, -20, -20);
            const QRect targetRect = fitImageRectForArea(m_lineArtImage.size(), imageArea);
            const int dividerX = targetRect.left()
                                 + static_cast<int>(std::clamp(m_lineArtCompareSplit, 0.0f, 1.0f)
                                                    * static_cast<float>(std::max(1, targetRect.width() - 1)));

            painter.fillRect(targetRect.adjusted(-1, -1, 1, 1), Qt::white);
            painter.drawImage(targetRect, m_lineArtSourceImage);

            painter.save();
            painter.setClipRect(QRect(dividerX, targetRect.top(), targetRect.right() - dividerX + 1, targetRect.height()));
            painter.drawImage(targetRect, m_lineArtImage);
            painter.restore();

            painter.setPen(QColor(210, 210, 210));
            painter.drawRect(targetRect.adjusted(0, 0, -1, -1));
            painter.fillRect(QRect(dividerX - 1, targetRect.top(), 3, targetRect.height()), QColor(36, 36, 36, 210));
            painter.setPen(Qt::white);
            painter.drawText(QRect(targetRect.left() + 12, targetRect.top() + 8, 80, 24),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("原图"));
            painter.drawText(QRect(targetRect.right() - 92, targetRect.top() + 8, 80, 24),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QStringLiteral("线稿"));
        } else {
            painter.fillRect(rect(), Qt::white);
            const QRect targetRect = fitImageRectForArea(m_lineArtImage.size(), rect());
            painter.drawImage(targetRect, m_lineArtImage);
        }
        return image;
    }

    const QImage baseImage = !m_scenePresentationImage.isNull()
        ? m_scenePresentationImage
        : buildRendererPresentationImage(true);
    if (baseImage.isNull())
        return {};

    QImage image(size(), QImage::Format_ARGB32);
    QPainter painter(&image);
    painter.drawImage(rect(), baseImage);
    if (includeOverlays) {
        drawSceneObjectGizmos(painter);
        drawLightGizmos(painter);
        drawCameraOverlay(painter);
    }
    return image;
}

QImage RasterWidget::applyPostProcessToImage(const QImage &source) const
{
    if (source.isNull() || !m_postProcessSettings.enabled)
        return source;

    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const float exposure = m_postProcessSettings.exposure;
    const float gammaInverse = 1.0f / std::max(0.5f, m_postProcessSettings.gamma);
    const float contrast = m_postProcessSettings.contrast;
    const float saturation = m_postProcessSettings.saturation;

    for (int y = 0; y < image.height(); ++y) {
        QRgb *scanline = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int alpha = qAlpha(scanline[x]);
            Vec3f color = unpackRgb(scanline[x]) * exposure;
            color = applyToneMapping(color, m_postProcessSettings.toneMapping);
            color = applyContrastAndSaturation(color, contrast, saturation);
            color = clamp01(color);
            color = {
                std::pow(color.x, gammaInverse),
                std::pow(color.y, gammaInverse),
                std::pow(color.z, gammaInverse)
            };
            scanline[x] = packRgb(color, alpha);
        }
    }

    return image;
}

bool RasterWidget::shouldApplyPostProcessToCurrentView() const
{
    return m_postProcessSettings.enabled && m_renderer.renderState().debugView == DebugView::None;
}

void RasterWidget::requestRender()
{
    m_sceneDirty = true;
    updateRenderLoopState();
    if (!isVisible())
        return;

    if (m_isLineArtMode) {
        update();
        return;
    }

    if (m_renderQueued)
        return;

    m_renderQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_renderQueued = false;
        renderFrame();
    });
}

void RasterWidget::updateRenderLoopState()
{
    if (needsContinuousRendering()) {
        if (!m_timer.isActive()) {
            syncFrameClock();
            m_timer.start(16);
        }
        return;
    }

    if (m_timer.isActive())
        m_timer.stop();
}

bool RasterWidget::needsContinuousRendering() const
{
    if (!isVisible() || m_isLineArtMode)
        return false;

    return m_moveForward
           || m_moveBackward
           || m_moveLeft
           || m_moveRight
           || m_moveUp
           || m_moveDown
           || hasAnimatedSceneObjects();
}

bool RasterWidget::hasAnimatedSceneObjects() const
{
    return false;
}

void RasterWidget::syncFrameClock()
{
    m_lastFrameElapsedMs = m_elapsed.elapsed();
}

void RasterWidget::applyScenePreset(ScenePreset preset)
{
    if (preset == ScenePreset::Custom)
        return;

    Camera camera = m_renderer.camera();
    RenderState state = m_renderer.renderState();
    MaterialBinding materialBinding;
    materialBinding.material = Material::makeLambertTextured();
    LightingContext lighting = LightingContext::makeDefault();
    DemoTexturePreset texturePreset = DemoTexturePreset::WarmChecker;

    switch (preset) {
    case ScenePreset::Custom:
        return;
    case ScenePreset::DefaultOrbit://默认轨道旋转
        camera = makeCamera({1.6f, 1.1f, 2.25f}, {0.0f, 0.0f, -4.5f}, 55.0f);
        state = makeRenderState(RenderPreset::Shaded);
        materialBinding.material = Material::makeLambertTextured();
        lighting.directionalLights[0].direction = normalize(Vec3f{-0.45f, -0.65f, -0.75f});
        lighting.directionalLights[0].color = {1.0f, 0.98f, 0.92f};
        lighting.directionalLights[0].ambient = 0.18f;
        texturePreset = DemoTexturePreset::WarmChecker;//默认纹理
        break;
    case ScenePreset::TextureStudy://纹理研究
        camera = makeCamera({0.0f, 0.15f, 1.4f}, {0.0f, 0.0f, -4.5f}, 38.0f);
        state = makeRenderState(RenderPreset::Shaded);
        materialBinding.material = Material::makeLambertTextured();
        materialBinding.material.sampler.filter = TextureFilter::Trilinear;
        materialBinding.material.sampler.addressU = AddressMode::Clamp;
        materialBinding.material.sampler.addressV = AddressMode::Clamp;
        lighting.directionalLights[0].direction = normalize(Vec3f{-0.25f, -0.2f, -1.0f});
        lighting.directionalLights[0].ambient = 0.35f;//增加环境光强度，突出纹理细节
        lighting.directionalLights[0].intensity = 0.7f;//适当降低光源强度，避免过曝导致纹理细节丢失
        texturePreset = DemoTexturePreset::Gradient;//纹理研究使用渐变纹理
        break;
    case ScenePreset::LightingStudy://光照研究
        camera = makeCamera({2.35f, 1.65f, 1.8f}, {0.0f, 0.0f, -4.5f}, 50.0f);
        state = makeRenderState(RenderPreset::Shaded);
        materialBinding.material = Material::makeLambertTextured();
        materialBinding.material.sampler.filter = TextureFilter::Nearest;
        lighting.directionalLights[0].direction = normalize(Vec3f{-0.8f, -0.45f, -0.3f});
        lighting.directionalLights[0].color = {1.0f, 0.96f, 0.9f};
        lighting.directionalLights[0].ambient = 0.08f;
        lighting.directionalLights[0].intensity = 1.45f;
        lighting.directionalLights[0].castShadow = true;
        lighting.directionalLights[0].shadowStrength = 0.7f;
        texturePreset = DemoTexturePreset::White;//光照研究使用纯白纹理，突出光照效果
        break;
    case ScenePreset::WireframeInspect://线框检查
        camera = makeCamera({2.25f, 1.55f, 2.6f}, {0.0f, 0.0f, -4.5f}, 60.0f);
        state = makeRenderState(RenderPreset::Wireframe);
        materialBinding.material = Material::makeLambertTextured();
        texturePreset = DemoTexturePreset::White;
        break;
    case ScenePreset::UvInspect://UV检查
        camera = makeCamera({0.0f, 0.1f, 1.6f}, {0.0f, 0.0f, -4.5f}, 40.0f);
        state = makeRenderState(RenderPreset::UvDebug);
        materialBinding.material = Material::makeLambertTextured();
        materialBinding.material.sampler.addressU = AddressMode::Clamp;
        materialBinding.material.sampler.addressV = AddressMode::Clamp;
        texturePreset = DemoTexturePreset::Gradient;
        break;
    case ScenePreset::OverdrawInspect://过度绘制检查
        camera = makeCamera({0.0f, 0.0f, 1.25f}, {0.0f, 0.0f, -4.5f}, 34.0f);
        state = makeRenderState(RenderPreset::OverdrawDebug);
        materialBinding.material = Material::makeLambertTextured();
        texturePreset = DemoTexturePreset::White;
        break;
    }

    m_renderer.setCamera(camera);
    m_renderer.setRenderState(state);
    m_lightingContext = lighting;
    for (SceneObjectEntry &object : m_sceneObjects) {
        object.useMaterialAsset = false;
        object.materialAssetId = -1;
        object.materialInstance = materialBinding;
        object.materialInstance.texturePreset = texturePreset;
        object.materialInstance.textureAssetId = -1;
        object.materialInstance.normalTextureAssetId = -1;
        object.materialInstance.metallicRoughnessTextureAssetId = -1;
    }
    m_scenePreset = preset;
    emit cameraChanged();
    requestRender();
}

ScenePreset RasterWidget::scenePreset() const
{
    return m_scenePreset;
}

void RasterWidget::applyRenderPreset(RenderPreset preset)
{
    if (preset == RenderPreset::Custom)
        return;

    m_renderer.setRenderState(makeRenderState(preset));
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDebugView(DebugView view)
{
    RenderState state = m_renderer.renderState();
    if (state.debugView == view)
        return;

    state.debugView = view;
    m_renderer.setRenderState(state);
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

DebugView RasterWidget::debugView() const
{
    return m_renderer.renderState().debugView;
}

void RasterWidget::setFillMode(FillMode mode)
{
    RenderState state = m_renderer.renderState();
    if (state.fillMode == mode)
        return;

    state.fillMode = mode;
    m_renderer.setRenderState(state);
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

FillMode RasterWidget::fillMode() const
{
    return m_renderer.renderState().fillMode;
}

void RasterWidget::setCullMode(CullMode mode)
{
    RenderState state = m_renderer.renderState();
    if (state.cullMode == mode)
        return;

    state.cullMode = mode;
    m_renderer.setRenderState(state);
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

CullMode RasterWidget::cullMode() const
{
    return m_renderer.renderState().cullMode;
}

void RasterWidget::setDepthFunc(DepthFunc func)
{
    RenderState state = m_renderer.renderState();
    if (state.depthFunc == func)
        return;

    state.depthFunc = func;
    m_renderer.setRenderState(state);
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

DepthFunc RasterWidget::depthFunc() const
{
    return m_renderer.renderState().depthFunc;
}

void RasterWidget::setAntiAliasingMode(AntiAliasingMode mode)
{
    RenderState state = m_renderer.renderState();
    if (state.antiAliasing == mode)
        return;

    state.antiAliasing = mode;
    m_renderer.setRenderState(state);
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AntiAliasingMode RasterWidget::antiAliasingMode() const
{
    return m_renderer.renderState().antiAliasing;
}

void RasterWidget::setParallelRasterEnabled(bool enabled)
{
    if (m_renderer.parallelRasterEnabled() == enabled)
        return;

    m_renderer.setParallelRasterEnabled(enabled);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

bool RasterWidget::parallelRasterEnabled() const
{
    return m_renderer.parallelRasterEnabled();
}

void RasterWidget::setParallelWorkerThreadCount(int count)
{
    if (m_renderer.requestedWorkerThreadCount() == count)
        return;

    m_renderer.setWorkerThreadCount(count);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

int RasterWidget::parallelWorkerThreadCount() const
{
    return m_renderer.requestedWorkerThreadCount();
}

int RasterWidget::parallelActiveWorkerThreadCount() const
{
    return m_renderer.workerThreadCount();
}

void RasterWidget::setParallelTileSize(int size)
{
    const int clampedSize = std::max(1, size);
    if (m_renderer.rasterTileSize() == clampedSize)
        return;

    m_renderer.setRasterTileSize(clampedSize);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

int RasterWidget::parallelTileSize() const
{
    return m_renderer.rasterTileSize();
}

void RasterWidget::setParallelMinTileCount(int count)
{
    const int clampedCount = std::max(1, count);
    if (m_renderer.minParallelTileCount() == clampedCount)
        return;

    m_renderer.setParallelThresholds(clampedCount, m_renderer.minParallelPixelCount());
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

int RasterWidget::parallelMinTileCount() const
{
    return m_renderer.minParallelTileCount();
}

void RasterWidget::setParallelMinPixelCount(int count)
{
    const int clampedCount = std::max(1, count);
    if (m_renderer.minParallelPixelCount() == clampedCount)
        return;

    m_renderer.setParallelThresholds(m_renderer.minParallelTileCount(), clampedCount);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

int RasterWidget::parallelMinPixelCount() const
{
    return m_renderer.minParallelPixelCount();
}

void RasterWidget::setParallelTilesPerTask(int count)
{
    const int clampedCount = std::max(1, count);
    if (m_renderer.parallelTilesPerTask() == clampedCount)
        return;

    m_renderer.setParallelTilesPerTask(clampedCount);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

int RasterWidget::parallelTilesPerTask() const
{
    return m_renderer.parallelTilesPerTask();
}

ParallelRasterStats RasterWidget::parallelRasterStats() const
{
    return m_renderer.parallelStats();
}

void RasterWidget::setTextureFilter(TextureFilter filter)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.sampler.filter == filter)
        return;

    binding->material.sampler.filter = filter;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

TextureFilter RasterWidget::textureFilter() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.sampler.filter : TextureFilter::Bilinear;
}

void RasterWidget::setMaterialType(MaterialType type)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.type == type)
        return;

    applyMaterialType(binding->material, type);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

MaterialType RasterWidget::materialType() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.type : MaterialType::LambertTextured;
}

void RasterWidget::setMaterialSurfaceMode(MaterialSurfaceMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.surfaceMode == mode)
        return;

    binding->material.surfaceMode = mode;
    binding->material.depthWriteEnable = mode == MaterialSurfaceMode::Opaque;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

MaterialSurfaceMode RasterWidget::materialSurfaceMode() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.surfaceMode : MaterialSurfaceMode::Opaque;
}

void RasterWidget::setMaterialOpacity(float opacity)//设置材质透明度
{
    const float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.opacity == clampedOpacity)
        return;

    binding->material.opacity = clampedOpacity;
    if (clampedOpacity < 0.999f && binding->material.surfaceMode == MaterialSurfaceMode::Opaque)
        binding->material.surfaceMode = MaterialSurfaceMode::AlphaBlend;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

float RasterWidget::materialOpacity() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.opacity : 1.0f;
}

void RasterWidget::setMaterialDepthWriteEnable(bool enable)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.depthWriteEnable == enable)
        return;

    binding->material.depthWriteEnable = enable;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

bool RasterWidget::materialDepthWriteEnable() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.depthWriteEnable : true;
}

bool RasterWidget::loadTextureForSelectedObject(const QString &path, QString *errorMessage)
{
    if (m_isLineArtMode) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前处于线稿模式，不能给 3D 对象加载纹理。");
        return false;
    }

    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可编辑的对象。");
        return false;
    }

    int textureAssetId = -1;
    if (!registerTextureAssetFromFile(path, QFileInfo(path).completeBaseName(), textureAssetId, errorMessage))
        return false;

    applyMaterialType(binding->material, texturedMaterialVariant(binding->material.type));
    binding->textureAssetId = textureAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

void RasterWidget::clearSelectedObjectExternalTexture()
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->textureAssetId < 0)
        return;

    binding->textureAssetId = -1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

QString RasterWidget::selectedObjectTexturePath() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    const TextureAssetEntry *asset = binding != nullptr ? textureAssetById(binding->textureAssetId) : nullptr;
    return asset != nullptr ? asset->sourcePath : QString();
}

int RasterWidget::selectedObjectTextureAssetIndex() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->textureAssetId < 0)
        return -1;
    for (int i = 0; i < static_cast<int>(m_textureAssets.size()); ++i) {
        if (m_textureAssets[static_cast<std::size_t>(i)].id == binding->textureAssetId)
            return i;
    }
    return -1;
}

bool RasterWidget::loadNormalTextureForSelectedObject(const QString &path, QString *errorMessage)
{
    if (m_isLineArtMode) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前处于线稿模式，不能给 3D 对象加载法线贴图。");
        return false;
    }

    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可编辑的对象。");
        return false;
    }

    int textureAssetId = -1;
    if (!registerTextureAssetFromFile(path, QFileInfo(path).completeBaseName(), textureAssetId, errorMessage))
        return false;

    binding->normalTextureAssetId = textureAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::loadMetallicRoughnessTextureForSelectedObject(const QString &path, QString *errorMessage)
{
    if (m_isLineArtMode) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前处于线稿模式，不能给 3D 对象加载金属粗糙贴图。");
        return false;
    }

    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可编辑的对象。");
        return false;
    }

    int textureAssetId = -1;
    if (!registerTextureAssetFromFile(path, QFileInfo(path).completeBaseName(), textureAssetId, errorMessage))
        return false;

    binding->metallicRoughnessTextureAssetId = textureAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

void RasterWidget::clearSelectedObjectExternalNormalTexture()
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->normalTextureAssetId < 0)
        return;

    binding->normalTextureAssetId = -1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

QString RasterWidget::selectedObjectNormalTexturePath() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    const TextureAssetEntry *asset = binding != nullptr ? textureAssetById(binding->normalTextureAssetId) : nullptr;
    return asset != nullptr ? asset->sourcePath : QString();
}

int RasterWidget::selectedObjectNormalTextureAssetIndex() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->normalTextureAssetId < 0)
        return -1;
    for (int i = 0; i < static_cast<int>(m_textureAssets.size()); ++i) {
        if (m_textureAssets[static_cast<std::size_t>(i)].id == binding->normalTextureAssetId)
            return i;
    }
    return -1;
}

void RasterWidget::clearSelectedObjectExternalMetallicRoughnessTexture()
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->metallicRoughnessTextureAssetId < 0)
        return;

    binding->metallicRoughnessTextureAssetId = -1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

QString RasterWidget::selectedObjectMetallicRoughnessTexturePath() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    const TextureAssetEntry *asset = binding != nullptr ? textureAssetById(binding->metallicRoughnessTextureAssetId) : nullptr;
    return asset != nullptr ? asset->sourcePath : QString();
}

int RasterWidget::selectedObjectMetallicRoughnessTextureAssetIndex() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->metallicRoughnessTextureAssetId < 0)
        return -1;
    for (int i = 0; i < static_cast<int>(m_textureAssets.size()); ++i) {
        if (m_textureAssets[static_cast<std::size_t>(i)].id == binding->metallicRoughnessTextureAssetId)
            return i;
    }
    return -1;
}

void RasterWidget::setDemoTexturePreset(DemoTexturePreset preset)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->texturePreset == preset)
        return;

    binding->texturePreset = preset;
    binding->textureAssetId = -1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

DemoTexturePreset RasterWidget::demoTexturePreset() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->texturePreset : DemoTexturePreset::WarmChecker;
}

void RasterWidget::setAddressModeU(AddressMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.sampler.addressU == mode)
        return;

    binding->material.sampler.addressU = mode;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AddressMode RasterWidget::addressModeU() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.sampler.addressU : AddressMode::Wrap;
}

void RasterWidget::setAddressModeV(AddressMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.sampler.addressV == mode)
        return;

    binding->material.sampler.addressV = mode;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AddressMode RasterWidget::addressModeV() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.sampler.addressV : AddressMode::Wrap;
}

void RasterWidget::setNormalTextureFilter(TextureFilter filter)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.normalSampler.filter == filter)
        return;

    binding->material.normalSampler.filter = filter;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

TextureFilter RasterWidget::normalTextureFilter() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.normalSampler.filter : TextureFilter::Bilinear;
}

void RasterWidget::setNormalAddressModeU(AddressMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.normalSampler.addressU == mode)
        return;

    binding->material.normalSampler.addressU = mode;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AddressMode RasterWidget::normalAddressModeU() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.normalSampler.addressU : AddressMode::Wrap;
}

void RasterWidget::setNormalAddressModeV(AddressMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.normalSampler.addressV == mode)
        return;

    binding->material.normalSampler.addressV = mode;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AddressMode RasterWidget::normalAddressModeV() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.normalSampler.addressV : AddressMode::Wrap;
}

void RasterWidget::setMetallicRoughnessTextureFilter(TextureFilter filter)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.metallicRoughnessSampler.filter == filter)
        return;

    binding->material.metallicRoughnessSampler.filter = filter;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

TextureFilter RasterWidget::metallicRoughnessTextureFilter() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.metallicRoughnessSampler.filter : TextureFilter::Bilinear;
}

void RasterWidget::setMetallicRoughnessAddressModeU(AddressMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.metallicRoughnessSampler.addressU == mode)
        return;

    binding->material.metallicRoughnessSampler.addressU = mode;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AddressMode RasterWidget::metallicRoughnessAddressModeU() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.metallicRoughnessSampler.addressU : AddressMode::Wrap;
}

void RasterWidget::setMetallicRoughnessAddressModeV(AddressMode mode)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || binding->material.metallicRoughnessSampler.addressV == mode)
        return;

    binding->material.metallicRoughnessSampler.addressV = mode;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

AddressMode RasterWidget::metallicRoughnessAddressModeV() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.metallicRoughnessSampler.addressV : AddressMode::Wrap;
}

void RasterWidget::setLightDirection(const Vec3f &direction)
{
    const Vec3f normalized = normalize(direction);
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    if (light.direction.x == normalized.x
        && light.direction.y == normalized.y
        && light.direction.z == normalized.z) {
        return;
    }

    light.direction = normalized;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

Vec3f RasterWidget::lightDirection() const
{
    return primarySceneLight(m_lightingContext).direction;
}

void RasterWidget::setLightAmbient(float ambient)
{
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    if (light.ambient == ambient)
        return;

    light.ambient = ambient;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::lightAmbient() const
{
    return primarySceneLight(m_lightingContext).ambient;
}

void RasterWidget::setLightIntensity(float intensity)
{
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    if (light.intensity == intensity)
        return;

    light.intensity = intensity;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::lightIntensity() const
{
    return primarySceneLight(m_lightingContext).intensity;
}

int RasterWidget::directionalLightCount() const
{
    return static_cast<int>(m_lightingContext.directionalLights.size());
}

QString RasterWidget::directionalLightName(int index) const
{
    if (index < 0 || index >= directionalLightCount())
        return fallbackDirectionalLightName(0);

    const DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    return trimmedOrFallbackLightName(QString::fromStdString(light.name), fallbackDirectionalLightName(index));
}

void RasterWidget::setDirectionalLightName(int index, const QString &name)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    const QString nextName = trimmedOrFallbackLightName(name, fallbackDirectionalLightName(index));
    if (QString::fromStdString(light.name) == nextName)
        return;

    light.name = nextName.toStdString();
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    update();
}

DirectionalLight RasterWidget::directionalLight(int index) const
{
    if (index < 0 || index >= directionalLightCount())
        return LightingContext::makeDefault().directionalLights.front();
    return m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
}

void RasterWidget::addDirectionalLight()
{
    DirectionalLight light = LightingContext::makeDefault().directionalLights.front();
    light.name = fallbackDirectionalLightName(directionalLightCount()).toStdString();
    m_lightingContext.directionalLights.push_back(light);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::duplicateDirectionalLight(int index)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    DirectionalLight duplicated = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    duplicated.name = QStringLiteral("%1 副本").arg(directionalLightName(index)).toStdString();
    m_lightingContext.directionalLights.push_back(std::move(duplicated));
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::removeDirectionalLight(int index)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    m_lightingContext.directionalLights.erase(m_lightingContext.directionalLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::convertDirectionalLightToPoint(int index)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const DirectionalLight directional = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    PointLight point = makeDefaultPointLight();
    point.color = directional.color;
    point.intensity = std::max(0.0f, directional.intensity * 8.0f);
    point.ambient = directional.ambient;
    point.enabled = directional.enabled;
    point.name = directionalLightName(index).toStdString();
    m_lightingContext.pointLights.push_back(point);
    m_lightingContext.directionalLights.erase(m_lightingContext.directionalLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setDirectionalLightEnabled(int index, bool enabled)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.enabled == enabled)
        return;

    light.enabled = enabled;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDirectionalLightDirection(int index, const Vec3f &direction)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const Vec3f normalized = normalize(direction);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.direction.x == normalized.x && light.direction.y == normalized.y && light.direction.z == normalized.z)
        return;

    light.direction = normalized;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setDirectionalLightColor(int index, const Vec3f &color)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const Vec3f clampedColor = clamp01(color);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.color.x == clampedColor.x && light.color.y == clampedColor.y && light.color.z == clampedColor.z)
        return;

    light.color = clampedColor;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDirectionalLightAmbient(int index, float ambient)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const float clampedAmbient = std::clamp(ambient, 0.0f, 1.0f);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.ambient == clampedAmbient)
        return;

    light.ambient = clampedAmbient;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDirectionalLightIntensity(int index, float intensity)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const float clampedIntensity = std::max(0.0f, intensity);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.intensity == clampedIntensity)
        return;

    light.intensity = clampedIntensity;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDirectionalLightShadowCastEnable(int index, bool enable)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.castShadow == enable)
        return;

    light.castShadow = enable;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setDirectionalLightShadowStrength(int index, float strength)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.shadowStrength == clampedStrength)
        return;

    light.shadowStrength = clampedStrength;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDirectionalLightShadowBias(int index, float bias)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const float clampedBias = std::clamp(bias, 0.0f, 0.02f);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.shadowBias == clampedBias)
        return;

    light.shadowBias = clampedBias;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setDirectionalLightShadowMapSize(int index, int size)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const int clampedSize = std::clamp(size, 64, 2048);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.shadowMapSize == clampedSize)
        return;

    light.shadowMapSize = clampedSize;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setDirectionalLightShadowCoverage(int index, float coverage)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const float clampedCoverage = std::max(0.5f, coverage);
    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.shadowCoverage == clampedCoverage)
        return;

    light.shadowCoverage = clampedCoverage;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setDirectionalLightShadowFilterQuality(int index, ShadowFilterQuality quality)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    if (light.shadowFilterQuality == quality)
        return;

    light.shadowFilterQuality = quality;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

int RasterWidget::pointLightCount() const
{
    return static_cast<int>(m_lightingContext.pointLights.size());
}

QString RasterWidget::pointLightName(int index) const
{
    if (index < 0 || index >= pointLightCount())
        return fallbackPointLightName(0);

    const PointLight &light = m_lightingContext.pointLights[static_cast<std::size_t>(index)];
    return trimmedOrFallbackLightName(QString::fromStdString(light.name), fallbackPointLightName(index));
}

void RasterWidget::setPointLightName(int index, const QString &name)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    if (light == nullptr)
        return;

    const QString nextName = trimmedOrFallbackLightName(name, fallbackPointLightName(index));
    if (QString::fromStdString(light->name) == nextName)
        return;

    light->name = nextName.toStdString();
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    update();
}

PointLight RasterWidget::pointLight(int index) const
{
    const PointLight *light = pointLightAt(m_lightingContext, index);
    return light != nullptr ? *light : PointLight{};
}

void RasterWidget::addPointLight()
{
    PointLight light = makeDefaultPointLight();
    light.name = fallbackPointLightName(pointLightCount()).toStdString();
    m_lightingContext.pointLights.push_back(std::move(light));
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::duplicatePointLight(int index)
{
    if (index < 0 || index >= pointLightCount())
        return;

    PointLight duplicated = m_lightingContext.pointLights[static_cast<std::size_t>(index)];
    duplicated.name = QStringLiteral("%1 副本").arg(pointLightName(index)).toStdString();
    m_lightingContext.pointLights.push_back(std::move(duplicated));
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::removePointLight(int index)
{
    if (index < 0 || index >= pointLightCount())
        return;

    m_lightingContext.pointLights.erase(m_lightingContext.pointLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::convertPointLightToDirectional(int index)
{
    if (index < 0 || index >= pointLightCount())
        return;

    const PointLight point = m_lightingContext.pointLights[static_cast<std::size_t>(index)];
    DirectionalLight directional = LightingContext::makeDefault().directionalLights.front();
    directional.color = point.color;
    directional.intensity = std::max(0.0f, point.intensity * 0.125f);
    directional.ambient = point.ambient;
    directional.enabled = point.enabled;
    directional.name = pointLightName(index).toStdString();
    m_lightingContext.directionalLights.push_back(directional);
    m_lightingContext.pointLights.erase(m_lightingContext.pointLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setPointLightEnabled(int index, bool enabled)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    if (light == nullptr || light->enabled == enabled)
        return;

    light->enabled = enabled;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightPosition(int index, const Vec3f &position)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    if (light == nullptr)
        return;
    if (light->position.x == position.x && light->position.y == position.y && light->position.z == position.z)
        return;

    light->position = position;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setPointLightAmbient(int index, float ambient)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const float clampedAmbient = std::clamp(ambient, 0.0f, 1.0f);
    if (light == nullptr || light->ambient == clampedAmbient)
        return;

    light->ambient = clampedAmbient;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightColor(int index, const Vec3f &color)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const Vec3f clampedColor = clamp01(color);
    if (light == nullptr)
        return;
    if (light->color.x == clampedColor.x && light->color.y == clampedColor.y && light->color.z == clampedColor.z)
        return;

    light->color = clampedColor;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightIntensity(int index, float intensity)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const float clampedIntensity = std::max(0.0f, intensity);
    if (light == nullptr || light->intensity == clampedIntensity)
        return;

    light->intensity = clampedIntensity;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightRange(int index, float range)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const float clampedRange = std::max(0.1f, range);
    if (light == nullptr || light->range == clampedRange)
        return;

    light->range = clampedRange;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightShadowCastEnable(int index, bool enable)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    if (light == nullptr || light->castShadow == enable)
        return;

    light->castShadow = enable;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setPointLightShadowStrength(int index, float strength)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    if (light == nullptr || light->shadowStrength == clampedStrength)
        return;

    light->shadowStrength = clampedStrength;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightShadowBias(int index, float bias)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const float clampedBias = std::clamp(bias, 0.0f, 0.05f);
    if (light == nullptr || light->shadowBias == clampedBias)
        return;

    light->shadowBias = clampedBias;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setPointLightShadowMapSize(int index, int size)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const int clampedSize = std::clamp(size, 64, 1024);
    if (light == nullptr || light->shadowMapSize == clampedSize)
        return;

    light->shadowMapSize = clampedSize;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setPointLightShadowRange(int index, float range)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    const float clampedRange = std::max(0.1f, range);
    if (light == nullptr || light->shadowRange == clampedRange)
        return;

    light->shadowRange = clampedRange;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setPointLightShadowFilterQuality(int index, ShadowFilterQuality quality)
{
    PointLight *light = pointLightAt(m_lightingContext, index);
    if (light == nullptr || light->shadowFilterQuality == quality)
        return;

    light->shadowFilterQuality = quality;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

int RasterWidget::spotLightCount() const
{
    return static_cast<int>(m_lightingContext.spotLights.size());
}

QString RasterWidget::spotLightName(int index) const
{
    if (index < 0 || index >= spotLightCount())
        return fallbackSpotLightName(0);

    const SpotLight &light = m_lightingContext.spotLights[static_cast<std::size_t>(index)];
    return trimmedOrFallbackLightName(QString::fromStdString(light.name), fallbackSpotLightName(index));
}

void RasterWidget::setSpotLightName(int index, const QString &name)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    if (light == nullptr)
        return;

    const QString nextName = trimmedOrFallbackLightName(name, fallbackSpotLightName(index));
    if (QString::fromStdString(light->name) == nextName)
        return;

    light->name = nextName.toStdString();
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    update();
}

SpotLight RasterWidget::spotLight(int index) const
{
    const SpotLight *light = spotLightAt(m_lightingContext, index);
    return light != nullptr ? *light : makeDefaultSpotLight();
}

void RasterWidget::addSpotLight()
{
    SpotLight light = makeDefaultSpotLight();
    light.name = fallbackSpotLightName(spotLightCount()).toStdString();
    m_lightingContext.spotLights.push_back(std::move(light));
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::duplicateSpotLight(int index)
{
    if (index < 0 || index >= spotLightCount())
        return;

    SpotLight duplicated = m_lightingContext.spotLights[static_cast<std::size_t>(index)];
    duplicated.name = QStringLiteral("%1 副本").arg(spotLightName(index)).toStdString();
    m_lightingContext.spotLights.push_back(std::move(duplicated));
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::removeSpotLight(int index)
{
    if (index < 0 || index >= spotLightCount())
        return;

    m_lightingContext.spotLights.erase(m_lightingContext.spotLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::convertDirectionalLightToSpot(int index)
{
    if (index < 0 || index >= directionalLightCount())
        return;

    const DirectionalLight directional = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
    SpotLight spot = makeDefaultSpotLight();
    spot.direction = directional.direction;
    spot.color = directional.color;
    spot.intensity = std::max(0.0f, directional.intensity * 8.0f);
    spot.ambient = directional.ambient;
    spot.enabled = directional.enabled;
    spot.castShadow = directional.castShadow;
    spot.shadowStrength = directional.shadowStrength;
    spot.shadowBias = directional.shadowBias;
    spot.shadowMapSize = directional.shadowMapSize;
    spot.shadowRange = directional.shadowCoverage;
    spot.shadowFilterQuality = directional.shadowFilterQuality;
    spot.name = directionalLightName(index).toStdString();
    m_lightingContext.spotLights.push_back(spot);
    m_lightingContext.directionalLights.erase(m_lightingContext.directionalLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::convertPointLightToSpot(int index)
{
    if (index < 0 || index >= pointLightCount())
        return;

    const PointLight point = m_lightingContext.pointLights[static_cast<std::size_t>(index)];
    SpotLight spot = makeDefaultSpotLight();
    spot.position = point.position;
    spot.color = point.color;
    spot.intensity = point.intensity;
    spot.ambient = point.ambient;
    spot.range = point.range;
    spot.enabled = point.enabled;
    spot.castShadow = point.castShadow;
    spot.shadowStrength = point.shadowStrength;
    spot.shadowBias = point.shadowBias;
    spot.shadowMapSize = point.shadowMapSize;
    spot.shadowRange = point.shadowRange;
    spot.shadowFilterQuality = point.shadowFilterQuality;
    spot.name = pointLightName(index).toStdString();
    m_lightingContext.spotLights.push_back(spot);
    m_lightingContext.pointLights.erase(m_lightingContext.pointLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::convertSpotLightToDirectional(int index)
{
    if (index < 0 || index >= spotLightCount())
        return;

    const SpotLight spot = m_lightingContext.spotLights[static_cast<std::size_t>(index)];
    DirectionalLight directional = LightingContext::makeDefault().directionalLights.front();
    directional.direction = spot.direction;
    directional.color = spot.color;
    directional.intensity = std::max(0.0f, spot.intensity * 0.125f);
    directional.ambient = spot.ambient;
    directional.enabled = spot.enabled;
    directional.castShadow = spot.castShadow;
    directional.shadowStrength = spot.shadowStrength;
    directional.shadowBias = spot.shadowBias;
    directional.shadowMapSize = spot.shadowMapSize;
    directional.shadowCoverage = std::max(0.5f, spot.shadowRange);
    directional.shadowFilterQuality = spot.shadowFilterQuality;
    directional.name = spotLightName(index).toStdString();
    m_lightingContext.directionalLights.push_back(directional);
    m_lightingContext.spotLights.erase(m_lightingContext.spotLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::convertSpotLightToPoint(int index)
{
    if (index < 0 || index >= spotLightCount())
        return;

    const SpotLight spot = m_lightingContext.spotLights[static_cast<std::size_t>(index)];
    PointLight point = makeDefaultPointLight();
    point.position = spot.position;
    point.color = spot.color;
    point.intensity = spot.intensity;
    point.ambient = spot.ambient;
    point.range = spot.range;
    point.enabled = spot.enabled;
    point.castShadow = spot.castShadow;
    point.shadowStrength = spot.shadowStrength;
    point.shadowBias = spot.shadowBias;
    point.shadowMapSize = std::min(1024, spot.shadowMapSize);
    point.shadowRange = spot.shadowRange;
    point.shadowFilterQuality = spot.shadowFilterQuality;
    point.name = spotLightName(index).toStdString();
    m_lightingContext.pointLights.push_back(point);
    m_lightingContext.spotLights.erase(m_lightingContext.spotLights.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setSpotLightEnabled(int index, bool enabled)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    if (light == nullptr || light->enabled == enabled)
        return;

    light->enabled = enabled;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightPosition(int index, const Vec3f &position)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    if (light == nullptr)
        return;
    if (light->position.x == position.x && light->position.y == position.y && light->position.z == position.z)
        return;

    light->position = position;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setSpotLightDirection(int index, const Vec3f &direction)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    if (light == nullptr)
        return;

    const Vec3f normalized = normalize(direction);
    if (light->direction.x == normalized.x && light->direction.y == normalized.y && light->direction.z == normalized.z)
        return;

    light->direction = normalized;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setSpotLightAmbient(int index, float ambient)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedAmbient = std::clamp(ambient, 0.0f, 1.0f);
    if (light == nullptr || light->ambient == clampedAmbient)
        return;

    light->ambient = clampedAmbient;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightColor(int index, const Vec3f &color)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const Vec3f clampedColor = clamp01(color);
    if (light == nullptr)
        return;
    if (light->color.x == clampedColor.x && light->color.y == clampedColor.y && light->color.z == clampedColor.z)
        return;

    light->color = clampedColor;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightIntensity(int index, float intensity)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedIntensity = std::max(0.0f, intensity);
    if (light == nullptr || light->intensity == clampedIntensity)
        return;

    light->intensity = clampedIntensity;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightRange(int index, float range)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedRange = std::max(0.1f, range);
    if (light == nullptr || light->range == clampedRange)
        return;

    light->range = clampedRange;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightInnerConeDegrees(int index, float degrees)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedDegrees = std::clamp(degrees, 1.0f, 89.0f);
    if (light == nullptr || light->innerConeDegrees == clampedDegrees)
        return;

    light->innerConeDegrees = std::min(clampedDegrees, light->outerConeDegrees);
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightOuterConeDegrees(int index, float degrees)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedDegrees = std::clamp(degrees, 1.0f, 89.5f);
    if (light == nullptr)
        return;

    const float nextOuter = std::max(clampedDegrees, light->innerConeDegrees);
    if (light->outerConeDegrees == nextOuter)
        return;

    light->outerConeDegrees = nextOuter;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightShadowCastEnable(int index, bool enable)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    if (light == nullptr || light->castShadow == enable)
        return;

    light->castShadow = enable;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setSpotLightShadowStrength(int index, float strength)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    if (light == nullptr || light->shadowStrength == clampedStrength)
        return;

    light->shadowStrength = clampedStrength;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightShadowBias(int index, float bias)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedBias = std::clamp(bias, 0.0f, 0.02f);
    if (light == nullptr || light->shadowBias == clampedBias)
        return;

    light->shadowBias = clampedBias;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpotLightShadowMapSize(int index, int size)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const int clampedSize = std::clamp(size, 64, 2048);
    if (light == nullptr || light->shadowMapSize == clampedSize)
        return;

    light->shadowMapSize = clampedSize;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setSpotLightShadowRange(int index, float range)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    const float clampedRange = std::max(0.1f, range);
    if (light == nullptr || light->shadowRange == clampedRange)
        return;

    light->shadowRange = clampedRange;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setSpotLightShadowFilterQuality(int index, ShadowFilterQuality quality)
{
    SpotLight *light = spotLightAt(m_lightingContext, index);
    if (light == nullptr || light->shadowFilterQuality == quality)
        return;

    light->shadowFilterQuality = quality;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

void RasterWidget::setSpecularColor(const Vec3f &color)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    const Vec3f clampedColor = clamp01(color);
    if (binding == nullptr
        || (binding->material.specularColor.x == clampedColor.x
            && binding->material.specularColor.y == clampedColor.y
            && binding->material.specularColor.z == clampedColor.z)) {
        return;
    }

    binding->material.specularColor = clampedColor;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

Vec3f RasterWidget::specularColor() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.specularColor : Vec3f{1.0f, 1.0f, 1.0f};
}

void RasterWidget::setSpecularStrength(float strength)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    const float clampedStrength = std::clamp(strength, 0.0f, 4.0f);
    if (binding == nullptr || binding->material.specularStrength == clampedStrength)
        return;

    binding->material.specularStrength = clampedStrength;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::specularStrength() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.specularStrength : 0.35f;
}

void RasterWidget::setShininess(float shininess)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    const float clampedShininess = std::clamp(shininess, 1.0f, 256.0f);
    if (binding == nullptr || binding->material.shininess == clampedShininess)
        return;

    binding->material.shininess = clampedShininess;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::shininess() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.shininess : 32.0f;
}

void RasterWidget::setNormalStrength(float strength)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    const float clampedStrength = std::clamp(strength, 0.0f, 2.0f);
    if (binding == nullptr || binding->material.normalStrength == clampedStrength)
        return;

    binding->material.normalStrength = clampedStrength;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::normalStrength() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.normalStrength : 1.0f;
}

void RasterWidget::setMetallic(float metallic)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    const float clampedMetallic = std::clamp(metallic, 0.0f, 1.0f);
    if (binding == nullptr || binding->material.metallic == clampedMetallic)
        return;

    binding->material.metallic = clampedMetallic;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::metallic() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.metallic : 0.0f;
}

void RasterWidget::setRoughness(float roughness)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    const float clampedRoughness = std::clamp(roughness, 0.045f, 1.0f);
    if (binding == nullptr || binding->material.roughness == clampedRoughness)
        return;

    binding->material.roughness = clampedRoughness;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::roughness() const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    return binding != nullptr ? binding->material.roughness : 0.55f;
}

void RasterWidget::setShadowCastEnable(bool enable)
{
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    if (light.castShadow == enable)
        return;

    light.castShadow = enable;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

bool RasterWidget::shadowCastEnable() const
{
    return primarySceneLight(m_lightingContext).castShadow;
}

void RasterWidget::setShadowStrength(float strength)
{
    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    if (light.shadowStrength == clampedStrength)
        return;

    light.shadowStrength = clampedStrength;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::shadowStrength() const
{
    return primarySceneLight(m_lightingContext).shadowStrength;
}

void RasterWidget::setShadowBias(float bias)
{
    const float clampedBias = std::clamp(bias, 0.0f, 0.02f);
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    if (light.shadowBias == clampedBias)
        return;

    light.shadowBias = clampedBias;
    m_scenePreset = ScenePreset::Custom;
    requestRender();
}

float RasterWidget::shadowBias() const
{
    return primarySceneLight(m_lightingContext).shadowBias;
}

bool RasterWidget::loadObjModel(const QString &path, QString *errorMessage)
{
    exitLineArtMode();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法打开文件：%1").arg(path);
        return false;
    }

    const QString sourceText = QString::fromUtf8(file.readAll());
    Mesh mesh;
    if (!loadMeshFromObjText(sourceText, mesh, errorMessage))
        return false;

    if (m_sceneObjects.size() == 1 && m_sceneObjects.front().isDemoCube)
        m_sceneObjects.clear();

    SceneObjectEntry object;
    object.mesh = std::move(mesh);
    object.transform = makeLoadedMeshTransform(object.mesh);
    object.transform.position.x += static_cast<float>(m_sceneObjects.size()) * 2.6f;
    object.materialInstance.material = Material::makeLambertTextured();
    object.materialInstance.texturePreset = DemoTexturePreset::WarmChecker;
    object.sourcePath = path;
    object.sourceText = sourceText;
    object.displayName = makeSceneObjectDisplayName(path, false, static_cast<int>(m_sceneObjects.size()));
    object.isDemoCube = false;
    m_sceneObjects.push_back(std::move(object));
    m_scenePreset = ScenePreset::Custom;
    m_selectedSceneObjectIndex = static_cast<int>(m_sceneObjects.size()) - 1;
    if (errorMessage != nullptr)
        errorMessage->clear();
    emit sceneContentChanged();
    requestRender();
    return true;
}

bool RasterWidget::loadPhotoLineArt(const QString &path, QString *errorMessage)
{
    QImage image(path);
    if (image.isNull()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法读取图片：%1").arg(path);
        return false;
    }

    m_lineArtSourceImage = std::move(image);
    m_lineArtSourcePath = path;
    m_isLineArtMode = true;
    updateRenderLoopState();
    if (!regenerateLineArt(errorMessage)) {
        m_isLineArtMode = false;
        m_lineArtSourceImage = QImage();
        m_lineArtSourcePath.clear();
        updateRenderLoopState();
        return false;
    }
    emit frameReady(RenderStats{});
    emit sceneContentChanged();
    update();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::saveLineArtImage(const QString &path, QString *errorMessage) const
{
    if (!m_isLineArtMode || m_lineArtImage.isNull()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可保存的线稿图。");
        return false;
    }

    if (!m_lineArtImage.save(path)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法保存线稿图：%1").arg(path);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::saveTransparentLineArtImage(const QString &path, QString *errorMessage) const
{
    if (!m_isLineArtMode || m_lineArtTransparentImage.isNull()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可保存的透明线稿图。");
        return false;
    }

    if (!m_lineArtTransparentImage.save(path)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法保存透明线稿图：%1").arg(path);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::saveViewportScreenshot(const QString &path, QString *errorMessage)
{
    if (!m_isLineArtMode && !renderSceneNow(true)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前无法生成可导出的视口图像。");
        return false;
    }

    const QImage image = buildViewportPresentationImage(true);
    if (image.isNull()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可导出的视口图像。");
        return false;
    }

    if (!image.save(path)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法保存截图：%1").arg(path);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::exportOrbitSequence(const QString &outputDirectory,
                                       int frameCount,
                                       float orbitDegrees,
                                       QString *errorMessage)
{
    if (m_isLineArtMode) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("线稿模式下不支持导出 3D 视口序列。");
        return false;
    }
    if (frameCount <= 0) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("导出帧数必须大于 0。");
        return false;
    }

    QDir directory(outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建导出目录：%1").arg(outputDirectory);
        return false;
    }

    const Camera originalCamera = m_renderer.camera();
    const int padding = std::max(4, static_cast<int>(QString::number(std::max(0, frameCount - 1)).size()));
    const float totalOrbitRadians = degreesToRadians(orbitDegrees);
    QString failedPath;

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        Camera frameCamera = originalCamera;
        const float ratio = frameCount > 1 ? static_cast<float>(frameIndex) / static_cast<float>(frameCount) : 0.0f;
        orbitCamera(frameCamera, totalOrbitRadians * ratio, 0.0f);
        m_renderer.setCamera(frameCamera);
        if (!renderSceneNow(true)) {
            failedPath = QStringLiteral("frame_%1.png").arg(frameIndex, padding, 10, QLatin1Char('0'));
            break;
        }

        const QImage image = buildRendererPresentationImage(true);
        const QString fileName = QStringLiteral("frame_%1.png").arg(frameIndex, padding, 10, QLatin1Char('0'));
        const QString outputPath = directory.filePath(fileName);
        if (image.isNull() || !image.save(outputPath)) {
            failedPath = outputPath;
            break;
        }
    }

    m_renderer.setCamera(originalCamera);
    emit cameraChanged();
    requestRender();

    if (!failedPath.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("序列导出失败：%1").arg(failedPath);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::exportDebugViews(const QString &outputDirectory, QString *errorMessage)
{
    if (m_isLineArtMode) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("线稿模式下不支持批量导出调试视图。");
        return false;
    }

    QDir directory(outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建导出目录：%1").arg(outputDirectory);
        return false;
    }

    RenderState state = m_renderer.renderState();
    const RenderState originalState = state;
    QString failedPath;
    const DebugView debugViews[] = {
        DebugView::None,
        DebugView::Depth,
        DebugView::Normal,
        DebugView::UV,
        DebugView::Overdraw,
        DebugView::ObjectId,
        DebugView::MaterialId,
        DebugView::TriangleId,
        DebugView::FaceOrientation,
        DebugView::Barycentric,
        DebugView::Shadow,
        DebugView::Lighting
    };

    for (DebugView view : debugViews) {
        state.debugView = view;
        m_renderer.setRenderState(state);
        if (!renderSceneNow(true)) {
            failedPath = debugViewFileName(view);
            break;
        }

        const QImage image = buildRendererPresentationImage(true);
        const QString outputPath = directory.filePath(QStringLiteral("%1.png").arg(debugViewFileName(view)));
        if (image.isNull() || !image.save(outputPath)) {
            failedPath = outputPath;
            break;
        }
    }

    m_renderer.setRenderState(originalState);
    requestRender();

    if (!failedPath.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("调试视图导出失败：%1").arg(failedPath);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::isLineArtMode() const
{
    return m_isLineArtMode;
}

void RasterWidget::setPostProcessEnabled(bool enabled)
{
    if (m_postProcessSettings.enabled == enabled)
        return;

    m_postProcessSettings.enabled = enabled;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

bool RasterWidget::postProcessEnabled() const
{
    return m_postProcessSettings.enabled;
}

void RasterWidget::setToneMappingMode(ToneMappingMode mode)
{
    if (m_postProcessSettings.toneMapping == mode)
        return;

    m_postProcessSettings.toneMapping = mode;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

ToneMappingMode RasterWidget::toneMappingMode() const
{
    return m_postProcessSettings.toneMapping;
}

void RasterWidget::setPostExposure(float exposure)
{
    const float clampedExposure = std::clamp(exposure, 0.05f, 8.0f);
    if (m_postProcessSettings.exposure == clampedExposure)
        return;

    m_postProcessSettings.exposure = clampedExposure;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

float RasterWidget::postExposure() const
{
    return m_postProcessSettings.exposure;
}

void RasterWidget::setPostGamma(float gamma)
{
    const float clampedGamma = std::clamp(gamma, 0.5f, 4.0f);
    if (m_postProcessSettings.gamma == clampedGamma)
        return;

    m_postProcessSettings.gamma = clampedGamma;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

float RasterWidget::postGamma() const
{
    return m_postProcessSettings.gamma;
}

void RasterWidget::setPostContrast(float contrast)
{
    const float clampedContrast = std::clamp(contrast, 0.0f, 2.5f);
    if (m_postProcessSettings.contrast == clampedContrast)
        return;

    m_postProcessSettings.contrast = clampedContrast;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

float RasterWidget::postContrast() const
{
    return m_postProcessSettings.contrast;
}

void RasterWidget::setPostSaturation(float saturation)
{
    const float clampedSaturation = std::clamp(saturation, 0.0f, 2.5f);
    if (m_postProcessSettings.saturation == clampedSaturation)
        return;

    m_postProcessSettings.saturation = clampedSaturation;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

float RasterWidget::postSaturation() const
{
    return m_postProcessSettings.saturation;
}

void RasterWidget::setLineArtThresholdScale(float scale)
{
    const float clamped = std::clamp(scale, 0.25f, 4.0f);
    if (m_lineArtThresholdScale == clamped)
        return;
    m_lineArtThresholdScale = clamped;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

float RasterWidget::lineArtThresholdScale() const
{
    return m_lineArtThresholdScale;
}

void RasterWidget::setLineArtLineStrength(float strength)
{
    const float clamped = std::clamp(strength, 0.1f, 4.0f);
    if (m_lineArtLineStrength == clamped)
        return;
    m_lineArtLineStrength = clamped;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

float RasterWidget::lineArtLineStrength() const
{
    return m_lineArtLineStrength;
}

void RasterWidget::setLineArtProcessScale(float scale)
{
    const float clamped = std::clamp(scale, 0.1f, 1.0f);
    if (m_lineArtProcessScale == clamped)
        return;
    m_lineArtProcessScale = clamped;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

float RasterWidget::lineArtProcessScale() const
{
    return m_lineArtProcessScale;
}

void RasterWidget::setLineArtKeepGrayBase(bool keepGrayBase)
{
    if (m_lineArtKeepGrayBase == keepGrayBase)
        return;
    m_lineArtKeepGrayBase = keepGrayBase;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

bool RasterWidget::lineArtKeepGrayBase() const
{
    return m_lineArtKeepGrayBase;
}

void RasterWidget::setLineArtComparePreview(bool comparePreview)
{
    if (m_lineArtComparePreview == comparePreview)
        return;
    m_lineArtComparePreview = comparePreview;
    if (m_isLineArtMode)
        update();
}

bool RasterWidget::lineArtComparePreview() const
{
    return m_lineArtComparePreview;
}

void RasterWidget::setLineArtThresholdCurvePreset(LineArtThresholdCurvePreset preset)
{
    if (m_lineArtThresholdCurvePreset == preset)
        return;
    m_lineArtThresholdCurvePreset = preset;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

LineArtThresholdCurvePreset RasterWidget::lineArtThresholdCurvePreset() const
{
    return m_lineArtThresholdCurvePreset;
}

void RasterWidget::setLineArtEdgeMode(LineArtEdgeMode mode)
{
    if (m_lineArtEdgeMode == mode)
        return;
    m_lineArtEdgeMode = mode;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

LineArtEdgeMode RasterWidget::lineArtEdgeMode() const
{
    return m_lineArtEdgeMode;
}

void RasterWidget::setLineArtTransparentStrokeWidth(float width)
{
    const float clampedWidth = std::clamp(width, 0.25f, 8.0f);
    if (m_lineArtTransparentStrokeWidth == clampedWidth)
        return;
    m_lineArtTransparentStrokeWidth = clampedWidth;
    if (m_isLineArtMode && regenerateLineArt()) {
        emit sceneContentChanged();
        update();
    }
}

float RasterWidget::lineArtTransparentStrokeWidth() const
{
    return m_lineArtTransparentStrokeWidth;
}

QJsonObject RasterWidget::lineArtConfigObject(bool includeRuntimeState) const
{
    QJsonObject config;
    config[QStringLiteral("version")] = 1;
    config[QStringLiteral("thresholdScale")] = m_lineArtThresholdScale;
    config[QStringLiteral("lineStrength")] = m_lineArtLineStrength;
    config[QStringLiteral("processScale")] = m_lineArtProcessScale;
    config[QStringLiteral("keepGrayBase")] = m_lineArtKeepGrayBase;
    config[QStringLiteral("comparePreview")] = m_lineArtComparePreview;
    config[QStringLiteral("thresholdCurvePreset")] = static_cast<int>(m_lineArtThresholdCurvePreset);
    config[QStringLiteral("edgeMode")] = static_cast<int>(m_lineArtEdgeMode);
    config[QStringLiteral("transparentStrokeWidth")] = m_lineArtTransparentStrokeWidth;
    config[QStringLiteral("compareSplit")] = m_lineArtCompareSplit;
    if (includeRuntimeState) {
        config[QStringLiteral("lineArtMode")] = m_isLineArtMode;
        config[QStringLiteral("sourcePath")] = m_lineArtSourcePath;
    }
    return config;
}

bool RasterWidget::applyLineArtConfigObject(const QJsonObject &config,
                                            bool allowLoadSourceImage,
                                            QString *errorMessage)
{
    m_lineArtThresholdScale = std::clamp(static_cast<float>(
        config.value(QStringLiteral("thresholdScale")).toDouble(m_lineArtThresholdScale)), 0.25f, 4.0f);
    m_lineArtLineStrength = std::clamp(static_cast<float>(
        config.value(QStringLiteral("lineStrength")).toDouble(m_lineArtLineStrength)), 0.1f, 4.0f);
    m_lineArtProcessScale = std::clamp(static_cast<float>(
        config.value(QStringLiteral("processScale")).toDouble(m_lineArtProcessScale)), 0.1f, 1.0f);
    m_lineArtKeepGrayBase = config.value(QStringLiteral("keepGrayBase")).toBool(m_lineArtKeepGrayBase);
    m_lineArtComparePreview = config.value(QStringLiteral("comparePreview")).toBool(m_lineArtComparePreview);
    m_lineArtThresholdCurvePreset = static_cast<LineArtThresholdCurvePreset>(
        config.value(QStringLiteral("thresholdCurvePreset")).toInt(static_cast<int>(m_lineArtThresholdCurvePreset)));
    m_lineArtEdgeMode = static_cast<LineArtEdgeMode>(
        config.value(QStringLiteral("edgeMode")).toInt(static_cast<int>(m_lineArtEdgeMode)));
    m_lineArtTransparentStrokeWidth = std::clamp(static_cast<float>(
        config.value(QStringLiteral("transparentStrokeWidth")).toDouble(m_lineArtTransparentStrokeWidth)), 0.25f, 8.0f);
    m_lineArtCompareSplit = std::clamp(static_cast<float>(
        config.value(QStringLiteral("compareSplit")).toDouble(m_lineArtCompareSplit)), 0.0f, 1.0f);

    if (!allowLoadSourceImage) {
        if (errorMessage != nullptr)
            errorMessage->clear();
        return true;
    }

    const bool shouldEnterLineArtMode = config.value(QStringLiteral("lineArtMode")).toBool(false);
    const QString sourcePath = config.value(QStringLiteral("sourcePath")).toString();
    if (!shouldEnterLineArtMode || sourcePath.isEmpty()) {
        if (m_isLineArtMode)
            exitLineArtMode();
        requestRender();
        if (errorMessage != nullptr)
            errorMessage->clear();
        return true;
    }

    return loadPhotoLineArt(sourcePath, errorMessage);
}

QJsonObject RasterWidget::saveLineArtConfig() const
{
    return lineArtConfigObject(true);
}

bool RasterWidget::loadLineArtConfig(const QJsonObject &config, QString *errorMessage)
{
    const bool loaded = applyLineArtConfigObject(config, true, errorMessage);
    if (loaded) {
        emit sceneContentChanged();
        if (!m_isLineArtMode)
            update();
    }
    return loaded;
}

bool RasterWidget::batchExportLineArt(const QStringList &inputPaths,
                                      const QString &outputDirectory,
                                      QString *errorMessage) const
{
    if (inputPaths.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("没有可导出的输入图片。");
        return false;
    }

    QDir directory(outputDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建导出目录：%1").arg(outputDirectory);
        return false;
    }

    int exportedCount = 0;
    QStringList failedFiles;
    for (const QString &inputPath : inputPaths) {
        QImage image(inputPath);
        if (image.isNull()) {
            failedFiles.push_back(QFileInfo(inputPath).fileName());
            continue;
        }

        const LineArtBuildResult result = makeLineArtImages(image,
                                                            m_lineArtThresholdScale,
                                                            m_lineArtLineStrength,
                                                            m_lineArtProcessScale,
                                                            m_lineArtKeepGrayBase,
                                                            m_lineArtThresholdCurvePreset,
                                                            m_lineArtEdgeMode,
                                                            m_lineArtTransparentStrokeWidth);
        if (result.opaqueImage.isNull() || result.transparentImage.isNull()) {
            failedFiles.push_back(QFileInfo(inputPath).fileName());
            continue;
        }

        const QFileInfo fileInfo(inputPath);
        const QString baseName = fileInfo.completeBaseName();
        const QString opaquePath = directory.filePath(QStringLiteral("%1_line_art.png").arg(baseName));
        const QString transparentPath = directory.filePath(QStringLiteral("%1_line_art_transparent.png").arg(baseName));
        if (!result.opaqueImage.save(opaquePath) || !result.transparentImage.save(transparentPath)) {
            failedFiles.push_back(fileInfo.fileName());
            continue;
        }

        ++exportedCount;
    }

    if (exportedCount == 0) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("批量导出失败，没有成功处理任何图片。");
        return false;
    }

    if (!failedFiles.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("已导出 %1 张，失败 %2 张：%3")
                                .arg(exportedCount)
                                .arg(failedFiles.size())
                                .arg(failedFiles.join(QStringLiteral("、")));
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

void RasterWidget::addDemoCubeObject()
{
    exitLineArtMode();

    if (m_sceneObjects.size() == 1 && m_sceneObjects.front().isDemoCube)
        m_sceneObjects.clear();

    SceneObjectEntry object;
    object.mesh = Mesh::makeCube();
    object.transform = makeDemoCubeTransform();
    object.transform.position.x += static_cast<float>(m_sceneObjects.size()) * 2.6f;
    object.materialInstance.material = Material::makeLambertTextured();
    object.materialInstance.texturePreset = DemoTexturePreset::WarmChecker;
    object.displayName = makeSceneObjectDisplayName(QString(), true, static_cast<int>(m_sceneObjects.size()));
    object.isDemoCube = true;
    m_sceneObjects.push_back(std::move(object));
    m_selectedSceneObjectIndex = static_cast<int>(m_sceneObjects.size()) - 1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::useDemoCube()
{
    exitLineArtMode();
    resetSceneToDemoCube();
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::removeSelectedSceneObject()
{
    exitLineArtMode();

    if (m_sceneObjects.empty())
        return;

    m_sceneObjects.erase(m_sceneObjects.begin() + m_selectedSceneObjectIndex);
    if (m_sceneObjects.empty()) {
        m_selectedSceneObjectIndex = 0;
    } else {
        m_selectedSceneObjectIndex = std::clamp(m_selectedSceneObjectIndex, 0, sceneObjectCount() - 1);
    }
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

bool RasterWidget::hasLoadedModel() const
{
    if (m_isLineArtMode)
        return true;

    return std::any_of(m_sceneObjects.begin(), m_sceneObjects.end(), [](const SceneObjectEntry &object) {
        return !object.isDemoCube;
    });
}

bool RasterWidget::hasSceneObjects() const
{
    return !m_sceneObjects.empty();
}

QString RasterWidget::loadedModelPath() const
{
    if (m_isLineArtMode)
        return m_lineArtSourcePath;

    for (const SceneObjectEntry &object : m_sceneObjects) {
        if (!object.sourcePath.isEmpty())
            return object.sourcePath;
    }
    return {};
}

QString RasterWidget::loadedModelName() const
{
    if (m_isLineArtMode) {
        const QString fileName = QFileInfo(m_lineArtSourcePath).fileName();
        return fileName.isEmpty() ? QStringLiteral("图片线稿") : QStringLiteral("%1 线稿").arg(fileName);
    }

    if (m_sceneObjects.empty())
        return QStringLiteral("空场景");

    if (m_sceneObjects.size() == 1)
        return m_sceneObjects.front().displayName;

    return QStringLiteral("%1 个对象").arg(m_sceneObjects.size());
}

int RasterWidget::sceneObjectCount() const
{
    return static_cast<int>(m_sceneObjects.size());
}

QString RasterWidget::sceneObjectName(int index) const
{
    if (index < 0 || index >= sceneObjectCount())
        return QStringLiteral("无效对象");
    return m_sceneObjects[static_cast<std::size_t>(index)].displayName;
}

void RasterWidget::renameSelectedSceneObject(const QString &name)
{
    SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr)
        return;

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || object->displayName == trimmedName)
        return;

    object->displayName = trimmedName;
    emit sceneContentChanged();
}

int RasterWidget::selectedSceneObjectIndex() const
{
    return m_selectedSceneObjectIndex;
}

void RasterWidget::setSelectedSceneObjectIndex(int index)
{
    if (m_sceneObjects.empty())
        return;

    const int clampedIndex = std::clamp(index, 0, sceneObjectCount() - 1);
    if (m_selectedSceneObjectIndex == clampedIndex)
        return;

    m_selectedSceneObjectIndex = clampedIndex;
    emit sceneContentChanged();
    update();
}

Transform RasterWidget::selectedSceneObjectTransform() const
{
    if (m_sceneObjects.empty())
        return {};
    return m_sceneObjects[static_cast<std::size_t>(m_selectedSceneObjectIndex)].transform;
}

void RasterWidget::setSelectedSceneObjectTransform(const Transform &transform)
{
    if (m_sceneObjects.empty())
        return;

    m_sceneObjects[static_cast<std::size_t>(m_selectedSceneObjectIndex)].transform = transform;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::setGizmoSpaceMode(GizmoSpaceMode mode)
{
    if (m_gizmoSpaceMode == mode)
        return;

    m_gizmoSpaceMode = mode;
    emit sceneContentChanged();
    update();
}

GizmoSpaceMode RasterWidget::gizmoSpaceMode() const
{
    return m_gizmoSpaceMode;
}

void RasterWidget::setGizmoTranslationSnapStep(float step)
{
    const float clampedStep = std::clamp(step, 0.01f, 100.0f);
    if (m_gizmoTranslationSnapStep == clampedStep)
        return;

    m_gizmoTranslationSnapStep = clampedStep;
    emit sceneContentChanged();
}

float RasterWidget::gizmoTranslationSnapStep() const
{
    return m_gizmoTranslationSnapStep;
}

void RasterWidget::setGizmoRotationSnapDegrees(float degrees)
{
    const float clampedDegrees = std::clamp(degrees, 1.0f, 180.0f);
    const float radians = clampedDegrees * kPi / 180.0f;
    if (m_gizmoRotationSnapRadians == radians)
        return;

    m_gizmoRotationSnapRadians = radians;
    emit sceneContentChanged();
}

float RasterWidget::gizmoRotationSnapDegrees() const
{
    return m_gizmoRotationSnapRadians * 180.0f / kPi;
}

void RasterWidget::setGizmoScaleSnapStep(float step)
{
    const float clampedStep = std::clamp(step, 0.01f, 10.0f);
    if (m_gizmoScaleSnapStep == clampedStep)
        return;

    m_gizmoScaleSnapStep = clampedStep;
    emit sceneContentChanged();
}

float RasterWidget::gizmoScaleSnapStep() const
{
    return m_gizmoScaleSnapStep;
}

void RasterWidget::setInspectorGizmoHighlight(int handleKind, int operation, int axis)
{
    const ViewportHandleKind nextKind = static_cast<ViewportHandleKind>(handleKind);
    const ViewportHandleOperation nextOperation = static_cast<ViewportHandleOperation>(operation);
    const ViewportAxis nextAxis = static_cast<ViewportAxis>(axis);
    if (m_inspectorHighlightKind == nextKind
        && m_inspectorHighlightOperation == nextOperation
        && m_inspectorHighlightAxis == nextAxis) {
        return;
    }

    m_inspectorHighlightKind = nextKind;
    m_inspectorHighlightOperation = nextOperation;
    m_inspectorHighlightAxis = nextAxis;
    update();
}

void RasterWidget::setCameraProjectionMode(CameraProjectionMode mode)
{
    Camera camera = m_renderer.camera();
    if (camera.projectionMode == mode)
        return;

    camera.projectionMode = mode;
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
}

CameraProjectionMode RasterWidget::cameraProjectionMode() const
{
    return m_renderer.camera().projectionMode;
}

void RasterWidget::setCameraVerticalFovDegrees(float degrees)
{
    Camera camera = m_renderer.camera();
    const float radians = std::clamp(degrees, 10.0f, 140.0f) * kPi / 180.0f;
    if (std::fabs(camera.verticalFovRadians - radians) <= 1e-6f)
        return;

    camera.verticalFovRadians = radians;
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
}

float RasterWidget::cameraVerticalFovDegrees() const
{
    return m_renderer.camera().verticalFovRadians * 180.0f / kPi;
}

void RasterWidget::setCameraOrthographicHeight(float height)
{
    Camera camera = m_renderer.camera();
    const float clampedHeight = std::max(0.1f, height);
    if (std::fabs(camera.orthographicHeight - clampedHeight) <= 1e-6f)
        return;

    camera.orthographicHeight = clampedHeight;
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
}

float RasterWidget::cameraOrthographicHeight() const
{
    return m_renderer.camera().orthographicHeight;
}

void RasterWidget::setCameraNearPlane(float nearPlane)
{
    Camera camera = m_renderer.camera();
    const float clampedNear = std::clamp(nearPlane, 0.001f, std::max(0.001f, camera.farPlane - 0.01f));
    if (std::fabs(camera.nearPlane - clampedNear) <= 1e-6f)
        return;

    camera.nearPlane = clampedNear;
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
}

float RasterWidget::cameraNearPlane() const
{
    return m_renderer.camera().nearPlane;
}

void RasterWidget::setCameraFarPlane(float farPlane)
{
    Camera camera = m_renderer.camera();
    const float clampedFar = std::max(camera.nearPlane + 0.01f, farPlane);
    if (std::fabs(camera.farPlane - clampedFar) <= 1e-6f)
        return;

    camera.farPlane = clampedFar;
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
}

float RasterWidget::cameraFarPlane() const
{
    return m_renderer.camera().farPlane;
}

void RasterWidget::setCameraMoveSpeed(float speed)
{
    const float clampedSpeed = std::max(0.05f, speed);
    if (std::fabs(m_cameraMoveSpeed - clampedSpeed) <= 1e-6f)
        return;

    m_cameraMoveSpeed = clampedSpeed;
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
}

float RasterWidget::cameraMoveSpeed() const
{
    return m_cameraMoveSpeed;
}

void RasterWidget::resetCameraView()
{
    if (m_isLineArtMode)
        return;

    const ScenePreset preset = m_scenePreset == ScenePreset::Custom ? ScenePreset::DefaultOrbit : m_scenePreset;
    m_renderer.setCamera(makeResetCamera(preset));
    m_scenePreset = preset;
    clearCameraMoveState();
    emit cameraChanged();
    requestRender();
}

void RasterWidget::setCameraAxisView(CameraAxisView view)
{
    if (m_isLineArtMode)
        return;

    Camera camera = makeAxisViewCamera(m_renderer.camera(), view);
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    clearCameraMoveState();
    emit cameraChanged();
    requestRender();
}

void RasterWidget::setSelectedLightSelection(SelectedLightKind kind, int index)
{
    const SelectedLightKind nextKind = index < 0 ? SelectedLightKind::None : kind;
    if (m_selectedLightKind == nextKind && m_selectedLightIndex == index)
        return;

    m_selectedLightKind = nextKind;
    m_selectedLightIndex = index;
    update();
}

QJsonObject RasterWidget::cameraPresetObject() const
{
    const Camera &camera = m_renderer.camera();
    QJsonObject cameraObject;
    cameraObject[QStringLiteral("version")] = 1;
    cameraObject[QStringLiteral("position")] = QJsonArray{camera.position.x, camera.position.y, camera.position.z};
    cameraObject[QStringLiteral("target")] = QJsonArray{camera.target.x, camera.target.y, camera.target.z};
    cameraObject[QStringLiteral("up")] = QJsonArray{camera.up.x, camera.up.y, camera.up.z};
    cameraObject[QStringLiteral("verticalFovRadians")] = camera.verticalFovRadians;
    cameraObject[QStringLiteral("nearPlane")] = camera.nearPlane;
    cameraObject[QStringLiteral("farPlane")] = camera.farPlane;
    cameraObject[QStringLiteral("projectionMode")] = static_cast<int>(camera.projectionMode);
    cameraObject[QStringLiteral("orthographicHeight")] = camera.orthographicHeight;
    cameraObject[QStringLiteral("moveSpeed")] = m_cameraMoveSpeed;
    return cameraObject;
}

bool RasterWidget::applyCameraPresetObject(const QJsonObject &preset, QString *errorMessage)
{
    if (preset.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("相机预设为空。");
        return false;
    }

    Camera camera = m_renderer.camera();
    const QJsonArray positionArray = preset.value(QStringLiteral("position")).toArray();
    const QJsonArray targetArray = preset.value(QStringLiteral("target")).toArray();
    const QJsonArray upArray = preset.value(QStringLiteral("up")).toArray();
    if (positionArray.size() == 3) {
        camera.position = {
            static_cast<float>(positionArray[0].toDouble()),
            static_cast<float>(positionArray[1].toDouble()),
            static_cast<float>(positionArray[2].toDouble())
        };
    }
    if (targetArray.size() == 3) {
        camera.target = {
            static_cast<float>(targetArray[0].toDouble()),
            static_cast<float>(targetArray[1].toDouble()),
            static_cast<float>(targetArray[2].toDouble())
        };
    }
    if (upArray.size() == 3) {
        camera.up = {
            static_cast<float>(upArray[0].toDouble()),
            static_cast<float>(upArray[1].toDouble()),
            static_cast<float>(upArray[2].toDouble())
        };
    }
    camera.verticalFovRadians = static_cast<float>(preset.value(QStringLiteral("verticalFovRadians")).toDouble(camera.verticalFovRadians));
    camera.nearPlane = static_cast<float>(preset.value(QStringLiteral("nearPlane")).toDouble(camera.nearPlane));
    camera.farPlane = static_cast<float>(preset.value(QStringLiteral("farPlane")).toDouble(camera.farPlane));
    camera.projectionMode = static_cast<CameraProjectionMode>(
        preset.value(QStringLiteral("projectionMode")).toInt(static_cast<int>(camera.projectionMode)));
    camera.orthographicHeight = std::max(0.1f,
                                         static_cast<float>(preset.value(QStringLiteral("orthographicHeight"))
                                                                .toDouble(camera.orthographicHeight)));
    camera.nearPlane = std::clamp(camera.nearPlane, 0.001f, std::max(0.001f, camera.farPlane - 0.01f));
    camera.farPlane = std::max(camera.nearPlane + 0.01f, camera.farPlane);
    m_cameraMoveSpeed = std::max(0.05f,
                                 static_cast<float>(preset.value(QStringLiteral("moveSpeed"))
                                                        .toDouble(m_cameraMoveSpeed)));
    m_renderer.setCamera(camera);
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

QJsonObject RasterWidget::saveCameraPreset() const
{
    return cameraPresetObject();
}

bool RasterWidget::loadCameraPreset(const QJsonObject &preset, QString *errorMessage)
{
    if (!applyCameraPresetObject(preset, errorMessage))
        return false;

    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::selectedObjectUsesMaterialAsset() const
{
    const SceneObjectEntry *object = selectedSceneObject();
    return object != nullptr && object->useMaterialAsset;
}

QString RasterWidget::selectedObjectMaterialDisplayName() const
{
    const SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr)
        return QStringLiteral("当前没有对象");

    if (object->useMaterialAsset) {
        const MaterialAssetEntry *asset = materialAssetById(object->materialAssetId);
        return asset != nullptr ? asset->displayName : QStringLiteral("共享材质（缺失）");
    }

    return QStringLiteral("对象独立材质");
}

int RasterWidget::materialAssetCount() const
{
    return static_cast<int>(m_materialAssets.size());
}

QString RasterWidget::materialAssetName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return QString();
    return m_materialAssets[static_cast<std::size_t>(index)].displayName;
}

int RasterWidget::materialAssetUsageCount(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return 0;

    const int assetId = m_materialAssets[static_cast<std::size_t>(index)].id;
    return static_cast<int>(std::count_if(m_sceneObjects.begin(),
                                          m_sceneObjects.end(),
                                          [assetId](const SceneObjectEntry &object) {
                                              return object.useMaterialAsset && object.materialAssetId == assetId;
                                          }));
}

int RasterWidget::textureAssetCount() const
{
    return static_cast<int>(m_textureAssets.size());
}

QString RasterWidget::textureAssetName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return QString();
    return m_textureAssets[static_cast<std::size_t>(index)].displayName;
}

int RasterWidget::textureAssetUsageCount(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return 0;

    const int assetId = m_textureAssets[static_cast<std::size_t>(index)].id;
    int usageCount = 0;
    for (const SceneObjectEntry &object : m_sceneObjects) {
        const MaterialBinding *binding = object.useMaterialAsset
            ? (materialAssetById(object.materialAssetId) != nullptr ? &materialAssetById(object.materialAssetId)->binding
                                                                    : &object.materialInstance)
            : &object.materialInstance;
        if (binding->textureAssetId == assetId)
            ++usageCount;
        if (binding->normalTextureAssetId == assetId)
            ++usageCount;
        if (binding->metallicRoughnessTextureAssetId == assetId)
            ++usageCount;
    }
    return usageCount;
}

int RasterWidget::selectedObjectMaterialAssetIndex() const
{
    const SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr || !object->useMaterialAsset)
        return -1;

    for (int i = 0; i < static_cast<int>(m_materialAssets.size()); ++i) {
        if (m_materialAssets[static_cast<std::size_t>(i)].id == object->materialAssetId)
            return i;
    }
    return -1;
}

void RasterWidget::makeSelectedObjectUseMaterialInstance()
{
    SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr || !object->useMaterialAsset)
        return;

    if (const MaterialAssetEntry *asset = materialAssetById(object->materialAssetId))
        object->materialInstance = asset->binding;
    object->useMaterialAsset = false;
    object->materialAssetId = -1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

void RasterWidget::duplicateSelectedMaterialAsAsset()
{
    SceneObjectEntry *object = selectedSceneObject();
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    if (object == nullptr || binding == nullptr)
        return;

    const QString baseName = object->displayName.trimmed().isEmpty()
        ? QStringLiteral("材质")
        : QStringLiteral("%1 材质").arg(object->displayName.trimmed());
    const int materialAssetId = createMaterialAssetFromBinding(*binding, baseName);
    object->materialInstance = *binding;
    object->useMaterialAsset = true;
    object->materialAssetId = materialAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

bool RasterWidget::duplicateMaterialAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return false;

    const MaterialAssetEntry &sourceAsset = m_materialAssets[static_cast<std::size_t>(index)];
    createMaterialAssetFromBinding(sourceAsset.binding,
                                   QStringLiteral("%1 副本").arg(sourceAsset.displayName),
                                   sourceAsset.sourcePath);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

void RasterWidget::assignSelectedObjectMaterialAsset(int index)
{
    SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr || index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return;

    const MaterialAssetEntry &asset = m_materialAssets[static_cast<std::size_t>(index)];
    if (object->useMaterialAsset && object->materialAssetId == asset.id)
        return;

    object->materialInstance = asset.binding;
    object->useMaterialAsset = true;
    object->materialAssetId = asset.id;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

int RasterWidget::assignMaterialAssetToAllObjects(int index)
{
    if (index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return 0;

    const MaterialAssetEntry &asset = m_materialAssets[static_cast<std::size_t>(index)];
    int changedCount = 0;
    for (SceneObjectEntry &object : m_sceneObjects) {
        if (object.useMaterialAsset && object.materialAssetId == asset.id)
            continue;
        object.materialInstance = asset.binding;
        object.useMaterialAsset = true;
        object.materialAssetId = asset.id;
        ++changedCount;
    }

    if (changedCount <= 0)
        return 0;

    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return changedCount;
}

bool RasterWidget::assignSelectedObjectTextureAsset(int index)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return false;

    const int textureAssetId = m_textureAssets[static_cast<std::size_t>(index)].id;
    if (binding->textureAssetId == textureAssetId)
        return false;

    applyMaterialType(binding->material, texturedMaterialVariant(binding->material.type));
    binding->textureAssetId = textureAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

bool RasterWidget::assignSelectedObjectNormalTextureAsset(int index)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return false;

    const int textureAssetId = m_textureAssets[static_cast<std::size_t>(index)].id;
    if (binding->normalTextureAssetId == textureAssetId)
        return false;

    binding->normalTextureAssetId = textureAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

bool RasterWidget::assignSelectedObjectMetallicRoughnessTextureAsset(int index)
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr || index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return false;

    const int textureAssetId = m_textureAssets[static_cast<std::size_t>(index)].id;
    if (binding->metallicRoughnessTextureAssetId == textureAssetId)
        return false;

    binding->metallicRoughnessTextureAssetId = textureAssetId;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

int RasterWidget::assignTextureAssetToAllObjectsColor(int index)
{
    if (index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return 0;

    const int textureAssetId = m_textureAssets[static_cast<std::size_t>(index)].id;
    int changedCount = 0;
    for (SceneObjectEntry &object : m_sceneObjects) {
        MaterialBinding *binding = object.useMaterialAsset
            ? (materialAssetById(object.materialAssetId) != nullptr ? &materialAssetById(object.materialAssetId)->binding
                                                                    : &object.materialInstance)
            : &object.materialInstance;
        if (binding->textureAssetId == textureAssetId)
            continue;
        applyMaterialType(binding->material, texturedMaterialVariant(binding->material.type));
        binding->textureAssetId = textureAssetId;
        ++changedCount;
    }

    if (changedCount <= 0)
        return 0;

    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return changedCount;
}

bool RasterWidget::duplicateTextureAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return false;

    TextureAssetEntry duplicateAsset = m_textureAssets[static_cast<std::size_t>(index)];
    duplicateAsset.id = m_nextTextureAssetId++;
    duplicateAsset.displayName = QStringLiteral("%1 副本").arg(duplicateAsset.displayName);
    m_textureAssets.push_back(std::move(duplicateAsset));
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

bool RasterWidget::renameTextureAsset(int index, const QString &name)
{
    if (index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return false;

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return false;

    TextureAssetEntry &asset = m_textureAssets[static_cast<std::size_t>(index)];
    if (asset.displayName == trimmedName)
        return false;

    asset.displayName = trimmedName;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

bool RasterWidget::removeTextureAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_textureAssets.size()))
        return false;

    const int removedAssetId = m_textureAssets[static_cast<std::size_t>(index)].id;
    auto clearBindingRefs = [removedAssetId](MaterialBinding &binding) {
        if (binding.textureAssetId == removedAssetId)
            binding.textureAssetId = -1;
        if (binding.normalTextureAssetId == removedAssetId)
            binding.normalTextureAssetId = -1;
        if (binding.metallicRoughnessTextureAssetId == removedAssetId)
            binding.metallicRoughnessTextureAssetId = -1;
    };

    for (SceneObjectEntry &object : m_sceneObjects)
        clearBindingRefs(object.materialInstance);
    for (MaterialAssetEntry &asset : m_materialAssets)
        clearBindingRefs(asset.binding);

    m_textureAssets.erase(m_textureAssets.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

int RasterWidget::removeUnusedTextureAssets()
{
    std::vector<int> usedTextureAssetIds;
    usedTextureAssetIds.reserve(static_cast<std::size_t>(sceneObjectCount()) * 3);
    for (const SceneObjectEntry &object : m_sceneObjects) {
        const MaterialBinding *binding = object.useMaterialAsset
            ? (materialAssetById(object.materialAssetId) != nullptr ? &materialAssetById(object.materialAssetId)->binding
                                                                    : &object.materialInstance)
            : &object.materialInstance;
        for (int textureAssetId : {binding->textureAssetId, binding->normalTextureAssetId, binding->metallicRoughnessTextureAssetId}) {
            if (textureAssetId >= 0
                && std::find(usedTextureAssetIds.begin(), usedTextureAssetIds.end(), textureAssetId) == usedTextureAssetIds.end()) {
                usedTextureAssetIds.push_back(textureAssetId);
            }
        }
    }

    std::vector<int> removedIds;
    m_textureAssets.erase(std::remove_if(m_textureAssets.begin(),
                                         m_textureAssets.end(),
                                         [&](const TextureAssetEntry &asset) {
                                             const bool used = std::find(usedTextureAssetIds.begin(),
                                                                         usedTextureAssetIds.end(),
                                                                         asset.id) != usedTextureAssetIds.end();
                                             if (!used)
                                                 removedIds.push_back(asset.id);
                                             return !used;
                                         }),
                          m_textureAssets.end());
    if (removedIds.empty())
        return 0;

    auto clearRemovedTextureRefs = [&removedIds](MaterialBinding &binding) {
        const auto wasRemoved = [&removedIds](int textureAssetId) {
            return textureAssetId >= 0
                && std::find(removedIds.begin(), removedIds.end(), textureAssetId) != removedIds.end();
        };
        if (wasRemoved(binding.textureAssetId))
            binding.textureAssetId = -1;
        if (wasRemoved(binding.normalTextureAssetId))
            binding.normalTextureAssetId = -1;
        if (wasRemoved(binding.metallicRoughnessTextureAssetId))
            binding.metallicRoughnessTextureAssetId = -1;
    };

    for (SceneObjectEntry &object : m_sceneObjects)
        clearRemovedTextureRefs(object.materialInstance);
    for (MaterialAssetEntry &asset : m_materialAssets)
        clearRemovedTextureRefs(asset.binding);

    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return static_cast<int>(removedIds.size());
}

bool RasterWidget::renameMaterialAsset(int index, const QString &name)
{
    if (index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return false;

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return false;

    MaterialAssetEntry &asset = m_materialAssets[static_cast<std::size_t>(index)];
    if (asset.displayName == trimmedName)
        return false;

    asset.displayName = trimmedName;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

bool RasterWidget::removeMaterialAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_materialAssets.size()))
        return false;

    const MaterialAssetEntry removedAsset = m_materialAssets[static_cast<std::size_t>(index)];
    for (SceneObjectEntry &object : m_sceneObjects) {
        if (!object.useMaterialAsset || object.materialAssetId != removedAsset.id)
            continue;
        object.materialInstance = removedAsset.binding;
        object.useMaterialAsset = false;
        object.materialAssetId = -1;
    }

    m_materialAssets.erase(m_materialAssets.begin() + index);
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return true;
}

int RasterWidget::removeUnusedMaterialAssets()
{
    const auto isUsed = [&](int assetId) {
        return std::any_of(m_sceneObjects.begin(), m_sceneObjects.end(), [assetId](const SceneObjectEntry &object) {
            return object.useMaterialAsset && object.materialAssetId == assetId;
        });
    };

    const int beforeCount = static_cast<int>(m_materialAssets.size());
    m_materialAssets.erase(std::remove_if(m_materialAssets.begin(),
                                          m_materialAssets.end(),
                                          [&](const MaterialAssetEntry &asset) {
                                              return !isUsed(asset.id);
                                          }),
                           m_materialAssets.end());
    const int removedCount = beforeCount - static_cast<int>(m_materialAssets.size());
    if (removedCount <= 0)
        return 0;

    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    return removedCount;
}

bool RasterWidget::saveSelectedMaterialAssetToFile(const QString &path, QString *errorMessage) const
{
    const MaterialBinding *binding = selectedEditableMaterialBinding();
    if (path.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("材质保存路径为空。");
        return false;
    }
    if (binding == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可保存的对象材质。");
        return false;
    }

    const QString materialFilePath = QFileInfo(path).absoluteFilePath();
    const QString resourceDirectoryName = m_sceneResourceDirectoryName.isEmpty()
        ? QStringLiteral("resources")
        : m_sceneResourceDirectoryName;
    const QDir materialDir(QFileInfo(materialFilePath).absolutePath());
    const QString textureDirRelative = QStringLiteral("%1/textures").arg(resourceDirectoryName);
    if ((binding->textureAssetId >= 0 || binding->normalTextureAssetId >= 0 || binding->metallicRoughnessTextureAssetId >= 0)
        && !materialDir.mkpath(textureDirRelative)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建材质资源目录：%1").arg(materialDir.filePath(textureDirRelative));
        return false;
    }

    std::vector<int> referencedTextureIds;
    for (int textureId : {binding->textureAssetId, binding->normalTextureAssetId, binding->metallicRoughnessTextureAssetId}) {
        if (textureId < 0)
            continue;
        if (std::find(referencedTextureIds.begin(), referencedTextureIds.end(), textureId) == referencedTextureIds.end())
            referencedTextureIds.push_back(textureId);
    }

    std::vector<TextureAssetEntry> exportedTextureAssets;
    exportedTextureAssets.reserve(referencedTextureIds.size());
    for (int textureId : referencedTextureIds) {
        const TextureAssetEntry *asset = textureAssetById(textureId);
        if (asset == nullptr)
            continue;

        TextureAssetEntry exportedAsset = *asset;
        if (!exportedAsset.sourcePath.isEmpty()) {
            const QFileInfo sourceInfo(exportedAsset.sourcePath);
            if (!sourceInfo.exists()) {
                if (errorMessage != nullptr)
                    *errorMessage = QStringLiteral("材质引用的纹理不存在：%1").arg(exportedAsset.sourcePath);
                return false;
            }

            const QString targetName = QStringLiteral("%1_%2").arg(exportedAsset.id).arg(sourceInfo.fileName());
            const QString targetPath = materialDir.filePath(QStringLiteral("%1/%2").arg(textureDirRelative, targetName));
            if (QFileInfo::exists(targetPath))
                QFile::remove(targetPath);
            if (!QFile::copy(exportedAsset.sourcePath, targetPath)) {
                if (errorMessage != nullptr)
                    *errorMessage = QStringLiteral("无法复制纹理到材质资源目录：%1").arg(targetPath);
                return false;
            }
            exportedAsset.relativePath = QStringLiteral("%1/%2").arg(textureDirRelative, targetName);
        } else {
            exportedAsset.relativePath.clear();
        }
        exportedTextureAssets.push_back(std::move(exportedAsset));
    }

    MaterialAssetEntry materialAsset;
    materialAsset.id = 1;
    materialAsset.displayName = selectedObjectMaterialDisplayName();
    materialAsset.sourcePath = materialFilePath;
    materialAsset.binding = *binding;

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("resourceDirectory")] = resourceDirectoryName;
    QJsonArray textureAssetsArray;
    for (const TextureAssetEntry &asset : exportedTextureAssets)
        textureAssetsArray.push_back(textureAssetObject(asset, materialFilePath));
    root[QStringLiteral("textureAssets")] = textureAssetsArray;
    root[QStringLiteral("materialAsset")] = materialAssetObject(materialAsset, materialFilePath);

    QSaveFile file(materialFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法写入材质文件：%1").arg(materialFilePath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法提交材质文件：%1").arg(materialFilePath);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::loadMaterialAssetFromFile(const QString &path, QString *errorMessage)
{
    if (m_isLineArtMode) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前处于线稿模式，不能给 3D 对象加载材质。");
        return false;
    }

    SceneObjectEntry *object = selectedSceneObject();
    if (object == nullptr) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("当前没有可绑定材质的对象。");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法读取材质文件：%1").arg(path);
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("材质文件不是有效的 JSON 对象。");
        return false;
    }

    const QString materialFilePath = QFileInfo(path).absoluteFilePath();
    const QJsonObject root = document.object();

    std::vector<TextureAssetEntry> importedTextureAssets;
    const QJsonArray textureAssetsArray = root.value(QStringLiteral("textureAssets")).toArray();
    for (const QJsonValue &value : textureAssetsArray) {
        TextureAssetEntry asset;
        if (!loadTextureAssetObject(value.toObject(), materialFilePath, asset, errorMessage))
            return false;
        importedTextureAssets.push_back(std::move(asset));
    }

    MaterialAssetEntry importedMaterialAsset;
    const QJsonObject materialAssetObjectValue = root.value(QStringLiteral("materialAsset")).toObject();
    if (!materialAssetObjectValue.isEmpty()) {
        if (!loadMaterialAssetObject(materialAssetObjectValue, materialFilePath, importedMaterialAsset, errorMessage))
            return false;
    } else if (root.contains(QStringLiteral("binding"))) {
        importedMaterialAsset.displayName = QFileInfo(materialFilePath).completeBaseName();
        importedMaterialAsset.sourcePath = materialFilePath;
        importedMaterialAsset.binding.material = Material::makeLambertTextured();
        importedMaterialAsset.binding.texturePreset = DemoTexturePreset::WarmChecker;
        if (!loadMaterialBindingObject(root, importedMaterialAsset.binding, errorMessage))
            return false;
    } else {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("材质文件缺少 materialAsset/binding 节点。");
        return false;
    }

    std::vector<std::pair<int, int>> textureIdRemap;
    textureIdRemap.reserve(importedTextureAssets.size());
    for (TextureAssetEntry &asset : importedTextureAssets) {
        const int oldId = asset.id;
        asset.id = m_nextTextureAssetId++;
        if (asset.displayName.isEmpty())
            asset.displayName = QFileInfo(asset.sourcePath).completeBaseName();
        m_textureAssets.push_back(asset);
        textureIdRemap.emplace_back(oldId, asset.id);
    }

    auto remapTextureId = [&textureIdRemap](int textureId) {
        if (textureId < 0)
            return -1;
        for (const auto &entry : textureIdRemap) {
            if (entry.first == textureId)
                return entry.second;
        }
        return -1;
    };

    importedMaterialAsset.id = m_nextMaterialAssetId++;
    importedMaterialAsset.sourcePath = materialFilePath;
    if (importedMaterialAsset.displayName.isEmpty())
        importedMaterialAsset.displayName = QFileInfo(materialFilePath).completeBaseName();
    importedMaterialAsset.binding.textureAssetId = remapTextureId(importedMaterialAsset.binding.textureAssetId);
    importedMaterialAsset.binding.normalTextureAssetId = remapTextureId(importedMaterialAsset.binding.normalTextureAssetId);
    importedMaterialAsset.binding.metallicRoughnessTextureAssetId = remapTextureId(importedMaterialAsset.binding.metallicRoughnessTextureAssetId);

    m_materialAssets.push_back(importedMaterialAsset);
    object->materialInstance = importedMaterialAsset.binding;
    object->useMaterialAsset = true;
    object->materialAssetId = importedMaterialAsset.id;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::saveSceneToFile(const QString &path, QString *errorMessage)
{
    if (path.isEmpty()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("场景保存路径为空。");
        return false;
    }

    m_sceneFilePath = QFileInfo(path).absoluteFilePath();
    if (!exportSceneResources(m_sceneFilePath, errorMessage))
        return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }

    const QJsonDocument document(saveSceneState());
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法提交场景文件：%1").arg(path);
        return false;
    }

    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::loadSceneFromFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法读取场景文件：%1").arg(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage != nullptr) {
            const QString detail = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("根节点不是 JSON 对象。")
                : QStringLiteral("%1（偏移 %2）").arg(parseError.errorString()).arg(parseError.offset);
            *errorMessage = QStringLiteral("场景文件解析失败：%1\n原因：%2")
                                .arg(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()), detail);
        }
        return false;
    }

    m_sceneFilePath = QFileInfo(path).absoluteFilePath();
    return loadSceneState(document.object(), errorMessage);
}

QJsonObject RasterWidget::textureAssetObject(const TextureAssetEntry &asset, const QString &sceneFilePath) const
{
    QJsonObject object;
    object[QStringLiteral("id")] = asset.id;
    object[QStringLiteral("name")] = asset.displayName;
    object[QStringLiteral("sourcePath")] = asset.sourcePath;
    object[QStringLiteral("relativePath")] = asset.relativePath.isEmpty()
        ? relativizePathForScene(asset.sourcePath, sceneFilePath)
        : asset.relativePath;
    return object;
}

bool RasterWidget::loadTextureAssetObject(const QJsonObject &object,
                                          const QString &sceneFilePath,
                                          TextureAssetEntry &asset,
                                          QString *errorMessage)
{
    asset.id = object.value(QStringLiteral("id")).toInt(0);
    asset.displayName = object.value(QStringLiteral("name")).toString();
    asset.relativePath = object.value(QStringLiteral("relativePath")).toString();
    const QString resolvedSourcePath = resolveSceneRelativePath(
        object.value(QStringLiteral("sourcePath")).toString(),
        sceneFilePath);
    const QString resolvedRelativePath = resolveSceneRelativePath(asset.relativePath, sceneFilePath);
    asset.sourcePath = (!resolvedSourcePath.isEmpty() && QFileInfo::exists(resolvedSourcePath))
        ? resolvedSourcePath
        : resolvedRelativePath;

    const QString resolvedPath = asset.sourcePath;
    if (!resolvedPath.isEmpty()) {
        Texture2D texture;
        if (!loadTextureFromImageFile(resolvedPath, texture, errorMessage)) {
            if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("纹理资产“%1”加载失败。\n%2")
                                    .arg(asset.displayName.isEmpty() ? QFileInfo(resolvedPath).fileName() : asset.displayName,
                                         *errorMessage);
            }
            return false;
        }
        asset.texture = std::move(texture);
    }
    if (asset.displayName.isEmpty())
        asset.displayName = QFileInfo(resolvedPath).completeBaseName();
    return true;
}

QJsonObject RasterWidget::materialBindingObject(const MaterialBinding &binding) const
{
    const Material &material = binding.material;
    QJsonObject object;
    object[QStringLiteral("materialType")] = static_cast<int>(material.type);
    object[QStringLiteral("surfaceMode")] = static_cast<int>(material.surfaceMode);
    object[QStringLiteral("opacity")] = material.opacity;
    object[QStringLiteral("depthWriteEnable")] = material.depthWriteEnable;
    object[QStringLiteral("texturePreset")] = static_cast<int>(binding.texturePreset);
    object[QStringLiteral("textureAssetId")] = binding.textureAssetId;
    object[QStringLiteral("normalTextureAssetId")] = binding.normalTextureAssetId;
    object[QStringLiteral("metallicRoughnessTextureAssetId")] = binding.metallicRoughnessTextureAssetId;
    object[QStringLiteral("textureFilter")] = static_cast<int>(material.sampler.filter);
    object[QStringLiteral("addressModeU")] = static_cast<int>(material.sampler.addressU);
    object[QStringLiteral("addressModeV")] = static_cast<int>(material.sampler.addressV);
    object[QStringLiteral("normalTextureFilter")] = static_cast<int>(material.normalSampler.filter);
    object[QStringLiteral("normalAddressModeU")] = static_cast<int>(material.normalSampler.addressU);
    object[QStringLiteral("normalAddressModeV")] = static_cast<int>(material.normalSampler.addressV);
    object[QStringLiteral("metallicRoughnessTextureFilter")] = static_cast<int>(material.metallicRoughnessSampler.filter);
    object[QStringLiteral("metallicRoughnessAddressModeU")] = static_cast<int>(material.metallicRoughnessSampler.addressU);
    object[QStringLiteral("metallicRoughnessAddressModeV")] = static_cast<int>(material.metallicRoughnessSampler.addressV);
    object[QStringLiteral("specularColor")] = QJsonArray{material.specularColor.x, material.specularColor.y, material.specularColor.z};
    object[QStringLiteral("specularStrength")] = material.specularStrength;
    object[QStringLiteral("shininess")] = material.shininess;
    object[QStringLiteral("normalStrength")] = material.normalStrength;
    object[QStringLiteral("metallic")] = material.metallic;
    object[QStringLiteral("roughness")] = material.roughness;
    object[QStringLiteral("receiveShadow")] = material.receiveShadow;
    return object;
}

bool RasterWidget::loadMaterialBindingObject(const QJsonObject &object,
                                             MaterialBinding &binding,
                                             QString *errorMessage)
{
    applyMaterialType(binding.material,
                      static_cast<MaterialType>(object.value(QStringLiteral("materialType"))
                                                    .toInt(static_cast<int>(binding.material.type))));
    binding.material.surfaceMode = static_cast<MaterialSurfaceMode>(
        object.value(QStringLiteral("surfaceMode")).toInt(static_cast<int>(binding.material.surfaceMode)));
    binding.material.opacity = static_cast<float>(object.value(QStringLiteral("opacity")).toDouble(binding.material.opacity));
    binding.material.depthWriteEnable = object.value(QStringLiteral("depthWriteEnable"))
        .toBool(binding.material.surfaceMode == MaterialSurfaceMode::Opaque ? true : binding.material.depthWriteEnable);
    binding.texturePreset = static_cast<DemoTexturePreset>(
        object.value(QStringLiteral("texturePreset")).toInt(static_cast<int>(binding.texturePreset)));
    binding.textureAssetId = object.value(QStringLiteral("textureAssetId")).toInt(binding.textureAssetId);
    binding.normalTextureAssetId = object.value(QStringLiteral("normalTextureAssetId")).toInt(binding.normalTextureAssetId);
    binding.metallicRoughnessTextureAssetId = object.value(QStringLiteral("metallicRoughnessTextureAssetId")).toInt(binding.metallicRoughnessTextureAssetId);
    binding.material.sampler.filter = static_cast<TextureFilter>(
        object.value(QStringLiteral("textureFilter")).toInt(static_cast<int>(binding.material.sampler.filter)));
    binding.material.sampler.addressU = static_cast<AddressMode>(
        object.value(QStringLiteral("addressModeU")).toInt(static_cast<int>(binding.material.sampler.addressU)));
    binding.material.sampler.addressV = static_cast<AddressMode>(
        object.value(QStringLiteral("addressModeV")).toInt(static_cast<int>(binding.material.sampler.addressV)));
    binding.material.normalSampler.filter = static_cast<TextureFilter>(
        object.value(QStringLiteral("normalTextureFilter")).toInt(static_cast<int>(binding.material.normalSampler.filter)));
    binding.material.normalSampler.addressU = static_cast<AddressMode>(
        object.value(QStringLiteral("normalAddressModeU")).toInt(static_cast<int>(binding.material.normalSampler.addressU)));
    binding.material.normalSampler.addressV = static_cast<AddressMode>(
        object.value(QStringLiteral("normalAddressModeV")).toInt(static_cast<int>(binding.material.normalSampler.addressV)));
    binding.material.metallicRoughnessSampler.filter = static_cast<TextureFilter>(
        object.value(QStringLiteral("metallicRoughnessTextureFilter")).toInt(static_cast<int>(binding.material.metallicRoughnessSampler.filter)));
    binding.material.metallicRoughnessSampler.addressU = static_cast<AddressMode>(
        object.value(QStringLiteral("metallicRoughnessAddressModeU")).toInt(static_cast<int>(binding.material.metallicRoughnessSampler.addressU)));
    binding.material.metallicRoughnessSampler.addressV = static_cast<AddressMode>(
        object.value(QStringLiteral("metallicRoughnessAddressModeV")).toInt(static_cast<int>(binding.material.metallicRoughnessSampler.addressV)));

    const QJsonArray specularColorArray = object.value(QStringLiteral("specularColor")).toArray();
    if (specularColorArray.size() == 3) {
        binding.material.specularColor = clamp01(Vec3f{
            static_cast<float>(specularColorArray[0].toDouble()),
            static_cast<float>(specularColorArray[1].toDouble()),
            static_cast<float>(specularColorArray[2].toDouble())
        });
    }
    binding.material.specularStrength = std::clamp(
        static_cast<float>(object.value(QStringLiteral("specularStrength")).toDouble(binding.material.specularStrength)),
        0.0f,
        4.0f);
    binding.material.shininess = std::clamp(
        static_cast<float>(object.value(QStringLiteral("shininess")).toDouble(binding.material.shininess)),
        1.0f,
        256.0f);
    binding.material.normalStrength = std::clamp(
        static_cast<float>(object.value(QStringLiteral("normalStrength")).toDouble(binding.material.normalStrength)),
        0.0f,
        2.0f);
    binding.material.metallic = std::clamp(
        static_cast<float>(object.value(QStringLiteral("metallic")).toDouble(binding.material.metallic)),
        0.0f,
        1.0f);
    binding.material.roughness = std::clamp(
        static_cast<float>(object.value(QStringLiteral("roughness")).toDouble(binding.material.roughness)),
        0.045f,
        1.0f);
    binding.material.receiveShadow = object.value(QStringLiteral("receiveShadow")).toBool(binding.material.receiveShadow);
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

QJsonObject RasterWidget::materialAssetObject(const MaterialAssetEntry &asset, const QString &sceneFilePath) const
{
    QJsonObject object;
    object[QStringLiteral("id")] = asset.id;
    object[QStringLiteral("name")] = asset.displayName;
    object[QStringLiteral("sourcePath")] = asset.sourcePath;
    object[QStringLiteral("binding")] = materialBindingObject(asset.binding);
    object[QStringLiteral("sourcePathRelative")] = relativizePathForScene(asset.sourcePath, sceneFilePath);
    return object;
}

bool RasterWidget::loadMaterialAssetObject(const QJsonObject &object,
                                           const QString &sceneFilePath,
                                           MaterialAssetEntry &asset,
                                           QString *errorMessage)
{
    asset.id = object.value(QStringLiteral("id")).toInt(0);
    asset.displayName = object.value(QStringLiteral("name")).toString();
    const QString resolvedSourcePath = resolveSceneRelativePath(
        object.value(QStringLiteral("sourcePath")).toString(),
        sceneFilePath);
    const QString resolvedRelativePath = resolveSceneRelativePath(
        object.value(QStringLiteral("sourcePathRelative")).toString(),
        sceneFilePath);
    asset.sourcePath = (!resolvedSourcePath.isEmpty() && QFileInfo::exists(resolvedSourcePath))
        ? resolvedSourcePath
        : resolvedRelativePath;
    if (!loadMaterialBindingObject(object.value(QStringLiteral("binding")).toObject(), asset.binding, errorMessage))
        return false;
    if (asset.displayName.isEmpty())
        asset.displayName = QStringLiteral("材质 %1").arg(asset.id);
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool RasterWidget::exportSceneResources(const QString &sceneFilePath, QString *errorMessage)
{
    const QDir sceneDir(QFileInfo(sceneFilePath).absolutePath());
    const QString resourceDirPath = sceneDir.filePath(m_sceneResourceDirectoryName);
    QDir resourceDir(resourceDirPath);
    if (!resourceDir.exists() && !sceneDir.mkpath(m_sceneResourceDirectoryName)) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建资源目录：%1").arg(resourceDirPath);
        return false;
    }

    QDir texturesDir(resourceDir.filePath(QStringLiteral("textures")));
    if (!texturesDir.exists() && !resourceDir.mkpath(QStringLiteral("textures"))) {
        if (errorMessage != nullptr)
            *errorMessage = QStringLiteral("无法创建纹理资源目录：%1").arg(texturesDir.path());
        return false;
    }

    for (TextureAssetEntry &asset : m_textureAssets) {
        asset.relativePath = relativizePathForScene(asset.sourcePath, sceneFilePath);
        if (asset.sourcePath.isEmpty())
            continue;

        const QFileInfo sourceInfo(asset.sourcePath);
        if (!sourceInfo.exists())
            continue;

        const QString relativeName = QStringLiteral("%1_%2").arg(asset.id).arg(sourceInfo.fileName());
        const QString destinationPath = texturesDir.filePath(relativeName);
        if (QFileInfo::exists(destinationPath))
            QFile::remove(destinationPath);
        if (QFile::copy(asset.sourcePath, destinationPath))
            asset.relativePath = QStringLiteral("%1/%2").arg(m_sceneResourceDirectoryName, QStringLiteral("textures/%1").arg(relativeName));
    }

    return true;
}

QJsonObject RasterWidget::saveSceneState() const
{
    const RenderState &renderState = m_renderer.renderState();

    QJsonObject state;
    state[QStringLiteral("version")] = 2;
    state[QStringLiteral("scenePreset")] = static_cast<int>(m_scenePreset);
    state[QStringLiteral("selectedObjectIndex")] = m_selectedSceneObjectIndex;
    state[QStringLiteral("resourceDirectory")] = m_sceneResourceDirectoryName;

    QJsonArray textureAssetsArray;
    for (const TextureAssetEntry &asset : m_textureAssets)
        textureAssetsArray.push_back(textureAssetObject(asset, m_sceneFilePath));
    state[QStringLiteral("textureAssets")] = textureAssetsArray;

    QJsonArray materialAssetsArray;
    for (const MaterialAssetEntry &asset : m_materialAssets)
        materialAssetsArray.push_back(materialAssetObject(asset, m_sceneFilePath));
    state[QStringLiteral("materialAssets")] = materialAssetsArray;

    QJsonArray objectsArray;
    for (const SceneObjectEntry &object : m_sceneObjects) {
        QJsonObject objectState;
        objectState[QStringLiteral("type")] = object.isDemoCube ? QStringLiteral("demoCube") : QStringLiteral("obj");
        objectState[QStringLiteral("name")] = object.displayName;
        objectState[QStringLiteral("path")] = relativizePathForScene(object.sourcePath, m_sceneFilePath);
        objectState[QStringLiteral("sourceText")] = object.sourceText;

        QJsonObject transformObject;
        transformObject[QStringLiteral("position")] = QJsonArray{
            object.transform.position.x,
            object.transform.position.y,
            object.transform.position.z
        };
        transformObject[QStringLiteral("rotationRadians")] = QJsonArray{
            object.transform.rotationRadians.x,
            object.transform.rotationRadians.y,
            object.transform.rotationRadians.z
        };
        transformObject[QStringLiteral("scale")] = QJsonArray{
            object.transform.scale.x,
            object.transform.scale.y,
            object.transform.scale.z
        };
        objectState[QStringLiteral("transform")] = transformObject;
        objectState[QStringLiteral("materialMode")] = object.useMaterialAsset ? QStringLiteral("asset") : QStringLiteral("instance");
        objectState[QStringLiteral("materialAssetId")] = object.materialAssetId;
        objectState[QStringLiteral("materialInstance")] = materialBindingObject(object.materialInstance);

        objectsArray.push_back(objectState);
    }
    state[QStringLiteral("objects")] = objectsArray;

    state[QStringLiteral("camera")] = cameraPresetObject();

    QJsonObject lightingObject;
    QJsonArray directionalLightsArray;
    for (const DirectionalLight &light : m_lightingContext.directionalLights) {
        QJsonObject lightObject;
        lightObject[QStringLiteral("name")] = QString::fromStdString(light.name);
        lightObject[QStringLiteral("direction")] = QJsonArray{light.direction.x, light.direction.y, light.direction.z};
        lightObject[QStringLiteral("color")] = QJsonArray{light.color.x, light.color.y, light.color.z};
        lightObject[QStringLiteral("ambient")] = light.ambient;
        lightObject[QStringLiteral("intensity")] = light.intensity;
        lightObject[QStringLiteral("castShadow")] = light.castShadow;
        lightObject[QStringLiteral("shadowStrength")] = light.shadowStrength;
        lightObject[QStringLiteral("shadowBias")] = light.shadowBias;
        lightObject[QStringLiteral("shadowMapSize")] = light.shadowMapSize;
        lightObject[QStringLiteral("shadowCoverage")] = light.shadowCoverage;
        lightObject[QStringLiteral("shadowFilterQuality")] = static_cast<int>(light.shadowFilterQuality);
        lightObject[QStringLiteral("enabled")] = light.enabled;
        directionalLightsArray.push_back(lightObject);
    }
    lightingObject[QStringLiteral("directionalLights")] = directionalLightsArray;
    QJsonArray pointLightsArray;
    for (const PointLight &light : m_lightingContext.pointLights) {
        QJsonObject lightObject;
        lightObject[QStringLiteral("name")] = QString::fromStdString(light.name);
        lightObject[QStringLiteral("position")] = QJsonArray{light.position.x, light.position.y, light.position.z};
        lightObject[QStringLiteral("color")] = QJsonArray{light.color.x, light.color.y, light.color.z};
        lightObject[QStringLiteral("ambient")] = light.ambient;
        lightObject[QStringLiteral("intensity")] = light.intensity;
        lightObject[QStringLiteral("range")] = light.range;
        lightObject[QStringLiteral("castShadow")] = light.castShadow;
        lightObject[QStringLiteral("shadowStrength")] = light.shadowStrength;
        lightObject[QStringLiteral("shadowBias")] = light.shadowBias;
        lightObject[QStringLiteral("shadowMapSize")] = light.shadowMapSize;
        lightObject[QStringLiteral("shadowRange")] = light.shadowRange;
        lightObject[QStringLiteral("shadowFilterQuality")] = static_cast<int>(light.shadowFilterQuality);
        lightObject[QStringLiteral("enabled")] = light.enabled;
        pointLightsArray.push_back(lightObject);
    }
    lightingObject[QStringLiteral("pointLights")] = pointLightsArray;
    QJsonArray spotLightsArray;
    for (const SpotLight &light : m_lightingContext.spotLights) {
        QJsonObject lightObject;
        lightObject[QStringLiteral("name")] = QString::fromStdString(light.name);
        lightObject[QStringLiteral("position")] = QJsonArray{light.position.x, light.position.y, light.position.z};
        lightObject[QStringLiteral("direction")] = QJsonArray{light.direction.x, light.direction.y, light.direction.z};
        lightObject[QStringLiteral("color")] = QJsonArray{light.color.x, light.color.y, light.color.z};
        lightObject[QStringLiteral("ambient")] = light.ambient;
        lightObject[QStringLiteral("intensity")] = light.intensity;
        lightObject[QStringLiteral("range")] = light.range;
        lightObject[QStringLiteral("innerConeDegrees")] = light.innerConeDegrees;
        lightObject[QStringLiteral("outerConeDegrees")] = light.outerConeDegrees;
        lightObject[QStringLiteral("castShadow")] = light.castShadow;
        lightObject[QStringLiteral("shadowStrength")] = light.shadowStrength;
        lightObject[QStringLiteral("shadowBias")] = light.shadowBias;
        lightObject[QStringLiteral("shadowMapSize")] = light.shadowMapSize;
        lightObject[QStringLiteral("shadowRange")] = light.shadowRange;
        lightObject[QStringLiteral("shadowFilterQuality")] = static_cast<int>(light.shadowFilterQuality);
        lightObject[QStringLiteral("enabled")] = light.enabled;
        spotLightsArray.push_back(lightObject);
    }
    lightingObject[QStringLiteral("spotLights")] = spotLightsArray;
    state[QStringLiteral("lighting")] = lightingObject;
    state[QStringLiteral("lineArt")] = lineArtConfigObject(true);
    QJsonObject gizmoObject;
    gizmoObject[QStringLiteral("spaceMode")] = static_cast<int>(m_gizmoSpaceMode);
    gizmoObject[QStringLiteral("translationSnapStep")] = m_gizmoTranslationSnapStep;
    gizmoObject[QStringLiteral("rotationSnapDegrees")] = gizmoRotationSnapDegrees();
    gizmoObject[QStringLiteral("scaleSnapStep")] = m_gizmoScaleSnapStep;
    state[QStringLiteral("gizmo")] = gizmoObject;

    QJsonObject renderStateObject;
    renderStateObject[QStringLiteral("depthTestEnable")] = renderState.depthTestEnable;
    renderStateObject[QStringLiteral("depthWriteEnable")] = renderState.depthWriteEnable;
    renderStateObject[QStringLiteral("depthFunc")] = static_cast<int>(renderState.depthFunc);
    renderStateObject[QStringLiteral("cullMode")] = static_cast<int>(renderState.cullMode);
    renderStateObject[QStringLiteral("fillMode")] = static_cast<int>(renderState.fillMode);
    renderStateObject[QStringLiteral("debugView")] = static_cast<int>(renderState.debugView);
    renderStateObject[QStringLiteral("antiAliasing")] = static_cast<int>(renderState.antiAliasing);
    renderStateObject[QStringLiteral("blendMode")] = static_cast<int>(renderState.blend.mode);
    state[QStringLiteral("renderState")] = renderStateObject;

    QJsonObject parallelRasterObject;
    parallelRasterObject[QStringLiteral("enabled")] = m_renderer.parallelRasterEnabled();
    parallelRasterObject[QStringLiteral("workerThreadCount")] = m_renderer.requestedWorkerThreadCount();
    parallelRasterObject[QStringLiteral("tileSize")] = m_renderer.rasterTileSize();
    parallelRasterObject[QStringLiteral("minTileCount")] = m_renderer.minParallelTileCount();
    parallelRasterObject[QStringLiteral("minPixelCount")] = m_renderer.minParallelPixelCount();
    parallelRasterObject[QStringLiteral("tilesPerTask")] = m_renderer.parallelTilesPerTask();
    state[QStringLiteral("parallelRaster")] = parallelRasterObject;

    QJsonObject postProcessObject;
    postProcessObject[QStringLiteral("enabled")] = m_postProcessSettings.enabled;
    postProcessObject[QStringLiteral("toneMapping")] = static_cast<int>(m_postProcessSettings.toneMapping);
    postProcessObject[QStringLiteral("exposure")] = m_postProcessSettings.exposure;
    postProcessObject[QStringLiteral("gamma")] = m_postProcessSettings.gamma;
    postProcessObject[QStringLiteral("contrast")] = m_postProcessSettings.contrast;
    postProcessObject[QStringLiteral("saturation")] = m_postProcessSettings.saturation;
    state[QStringLiteral("postProcess")] = postProcessObject;

    return state;
}

bool RasterWidget::loadSceneState(const QJsonObject &state, QString *errorMessage)
{
    exitLineArtMode();

    const int sceneVersion = state.contains(QStringLiteral("version"))
        ? state.value(QStringLiteral("version")).toInt(1)
        : 1;
    if (sceneVersion < kMinimumSupportedSceneStateVersion) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("不支持的场景版本：v%1。\n当前最低可读取版本：v%2。")
                                .arg(sceneVersion)
                                .arg(kMinimumSupportedSceneStateVersion);
        }
        return false;
    }
    if (sceneVersion > kCurrentSceneStateVersion) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("场景版本过新：v%1。\n当前程序最高支持读取到 v%2，请升级程序后再打开。")
                                .arg(sceneVersion)
                                .arg(kCurrentSceneStateVersion);
        }
        return false;
    }

    m_sceneResourceDirectoryName = state.value(QStringLiteral("resourceDirectory"))
        .toString(QStringLiteral("resources"));

    std::vector<TextureAssetEntry> loadedTextureAssets;
    std::vector<MaterialAssetEntry> loadedMaterialAssets;
    int nextTextureAssetId = 1;
    int nextMaterialAssetId = 1;

    auto registerLegacyTextureAsset = [&](const QString &storedPath,
                                          const QString &suggestedName,
                                          int &assetId) -> bool {
        assetId = -1;
        if (storedPath.trimmed().isEmpty())
            return true;

        const QString resolvedPath = resolveSceneRelativePath(storedPath, m_sceneFilePath);
        if (resolvedPath.isEmpty())
            return true;

        const QString absolutePath = QFileInfo(resolvedPath).absoluteFilePath();
        auto existing = std::find_if(loadedTextureAssets.begin(), loadedTextureAssets.end(), [&](const TextureAssetEntry &asset) {
            return QFileInfo(asset.sourcePath).absoluteFilePath() == absolutePath;
        });
        if (existing != loadedTextureAssets.end()) {
            assetId = existing->id;
            return true;
        }

        Texture2D texture;
        if (!loadTextureFromImageFile(absolutePath, texture, errorMessage)) {
            if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("旧版场景纹理“%1”恢复失败。\n%2")
                                    .arg(suggestedName.isEmpty() ? QFileInfo(absolutePath).fileName() : suggestedName,
                                         *errorMessage);
            }
            return false;
        }

        TextureAssetEntry asset;
        asset.id = nextTextureAssetId++;
        asset.displayName = suggestedName.trimmed().isEmpty()
            ? QFileInfo(absolutePath).completeBaseName()
            : suggestedName.trimmed();
        if (asset.displayName.isEmpty())
            asset.displayName = QStringLiteral("纹理 %1").arg(asset.id);
        asset.sourcePath = absolutePath;
        asset.relativePath = relativizePathForScene(asset.sourcePath, m_sceneFilePath);
        asset.texture = std::move(texture);
        loadedTextureAssets.push_back(std::move(asset));
        assetId = loadedTextureAssets.back().id;
        return true;
    };

    auto loadLegacyMaterialBinding = [&](const QJsonObject &materialObject,
                                         MaterialBinding &binding) -> bool {
        binding.material = Material::makeLambertTextured();
        binding.texturePreset = DemoTexturePreset::WarmChecker;
        binding.textureAssetId = -1;
        binding.normalTextureAssetId = -1;
        binding.metallicRoughnessTextureAssetId = -1;

        applyMaterialType(binding.material,
                          static_cast<MaterialType>(materialObject.value(QStringLiteral("materialType"))
                                                        .toInt(static_cast<int>(binding.material.type))));
        binding.material.surfaceMode = static_cast<MaterialSurfaceMode>(
            materialObject.value(QStringLiteral("surfaceMode")).toInt(static_cast<int>(binding.material.surfaceMode)));
        binding.material.opacity = static_cast<float>(
            materialObject.value(QStringLiteral("opacity")).toDouble(binding.material.opacity));
        binding.material.depthWriteEnable = materialObject.value(QStringLiteral("depthWriteEnable"))
            .toBool(binding.material.surfaceMode == MaterialSurfaceMode::Opaque ? true : binding.material.depthWriteEnable);
        binding.texturePreset = static_cast<DemoTexturePreset>(
            materialObject.value(QStringLiteral("texturePreset")).toInt(static_cast<int>(binding.texturePreset)));

        const QString texturePath = materialObject.value(QStringLiteral("textureSourcePath")).toString();
        const QString normalTexturePath = materialObject.value(QStringLiteral("normalTextureSourcePath")).toString();
        const QString metallicRoughnessTexturePath = materialObject.value(QStringLiteral("metallicRoughnessTextureSourcePath")).toString();
        if (!registerLegacyTextureAsset(texturePath,
                                        QFileInfo(resolveSceneRelativePath(texturePath, m_sceneFilePath)).completeBaseName(),
                                        binding.textureAssetId)
            || !registerLegacyTextureAsset(normalTexturePath,
                                           QFileInfo(resolveSceneRelativePath(normalTexturePath, m_sceneFilePath)).completeBaseName(),
                                           binding.normalTextureAssetId)
            || !registerLegacyTextureAsset(metallicRoughnessTexturePath,
                                           QFileInfo(resolveSceneRelativePath(metallicRoughnessTexturePath, m_sceneFilePath)).completeBaseName(),
                                           binding.metallicRoughnessTextureAssetId)) {
            return false;
        }

        if (binding.textureAssetId >= 0)
            applyMaterialType(binding.material, texturedMaterialVariant(binding.material.type));

        binding.material.sampler.filter = static_cast<TextureFilter>(
            materialObject.value(QStringLiteral("textureFilter")).toInt(static_cast<int>(binding.material.sampler.filter)));
        binding.material.sampler.addressU = static_cast<AddressMode>(
            materialObject.value(QStringLiteral("addressModeU")).toInt(static_cast<int>(binding.material.sampler.addressU)));
        binding.material.sampler.addressV = static_cast<AddressMode>(
            materialObject.value(QStringLiteral("addressModeV")).toInt(static_cast<int>(binding.material.sampler.addressV)));
        binding.material.normalSampler.filter = static_cast<TextureFilter>(
            materialObject.value(QStringLiteral("normalTextureFilter")).toInt(static_cast<int>(binding.material.normalSampler.filter)));
        binding.material.normalSampler.addressU = static_cast<AddressMode>(
            materialObject.value(QStringLiteral("normalAddressModeU")).toInt(static_cast<int>(binding.material.normalSampler.addressU)));
        binding.material.normalSampler.addressV = static_cast<AddressMode>(
            materialObject.value(QStringLiteral("normalAddressModeV")).toInt(static_cast<int>(binding.material.normalSampler.addressV)));
        binding.material.metallicRoughnessSampler.filter = static_cast<TextureFilter>(
            materialObject.value(QStringLiteral("metallicRoughnessTextureFilter")).toInt(static_cast<int>(binding.material.metallicRoughnessSampler.filter)));
        binding.material.metallicRoughnessSampler.addressU = static_cast<AddressMode>(
            materialObject.value(QStringLiteral("metallicRoughnessAddressModeU")).toInt(static_cast<int>(binding.material.metallicRoughnessSampler.addressU)));
        binding.material.metallicRoughnessSampler.addressV = static_cast<AddressMode>(
            materialObject.value(QStringLiteral("metallicRoughnessAddressModeV")).toInt(static_cast<int>(binding.material.metallicRoughnessSampler.addressV)));

        const QJsonArray specularColorArray = materialObject.value(QStringLiteral("specularColor")).toArray();
        if (specularColorArray.size() == 3) {
            binding.material.specularColor = clamp01(Vec3f{
                static_cast<float>(specularColorArray[0].toDouble()),
                static_cast<float>(specularColorArray[1].toDouble()),
                static_cast<float>(specularColorArray[2].toDouble())
            });
        }
        binding.material.specularStrength = std::clamp(
            static_cast<float>(materialObject.value(QStringLiteral("specularStrength")).toDouble(binding.material.specularStrength)),
            0.0f,
            4.0f);
        binding.material.shininess = std::clamp(
            static_cast<float>(materialObject.value(QStringLiteral("shininess")).toDouble(binding.material.shininess)),
            1.0f,
            256.0f);
        binding.material.normalStrength = std::clamp(
            static_cast<float>(materialObject.value(QStringLiteral("normalStrength")).toDouble(binding.material.normalStrength)),
            0.0f,
            2.0f);
        binding.material.metallic = std::clamp(
            static_cast<float>(materialObject.value(QStringLiteral("metallic")).toDouble(binding.material.metallic)),
            0.0f,
            1.0f);
        binding.material.roughness = std::clamp(
            static_cast<float>(materialObject.value(QStringLiteral("roughness")).toDouble(binding.material.roughness)),
            0.045f,
            1.0f);
        binding.material.receiveShadow = materialObject.value(QStringLiteral("receiveShadow")).toBool(binding.material.receiveShadow);
        return true;
    };

    const QJsonArray textureAssetsArray = state.value(QStringLiteral("textureAssets")).toArray();
    for (const QJsonValue &value : textureAssetsArray) {
        TextureAssetEntry asset;
        if (!loadTextureAssetObject(value.toObject(), m_sceneFilePath, asset, errorMessage))
            return false;
        nextTextureAssetId = std::max(nextTextureAssetId, asset.id + 1);
        loadedTextureAssets.push_back(std::move(asset));
    }

    const QJsonArray materialAssetsArray = state.value(QStringLiteral("materialAssets")).toArray();
    for (const QJsonValue &value : materialAssetsArray) {
        MaterialAssetEntry asset;
        if (!loadMaterialAssetObject(value.toObject(), m_sceneFilePath, asset, errorMessage))
            return false;
        nextMaterialAssetId = std::max(nextMaterialAssetId, asset.id + 1);
        loadedMaterialAssets.push_back(std::move(asset));
    }

    std::vector<SceneObjectEntry> loadedObjects;
    const bool hasObjectsField = state.contains(QStringLiteral("objects"));
    QJsonArray objectsArray = state.value(QStringLiteral("objects")).toArray();
    const QJsonObject legacyMaterialObject = state.value(QStringLiteral("material")).toObject();

    // 兼容旧版单对象场景格式。
    if (!hasObjectsField && objectsArray.isEmpty()) {
        const QJsonObject legacyModelObject = state.value(QStringLiteral("model")).toObject();
        if (!legacyModelObject.isEmpty())
            objectsArray.push_back(legacyModelObject);
    }

    for (const QJsonValue &value : objectsArray) {
        const QJsonObject objectState = value.toObject();
        const QString objectType = objectState.value(QStringLiteral("type")).toString();
        const QString storedObjectPath = objectState.value(QStringLiteral("path")).toString(
            objectState.value(QStringLiteral("sourcePath")).toString(
                objectState.value(QStringLiteral("objPath")).toString()));
        const QString objectPath = resolveSceneRelativePath(storedObjectPath, m_sceneFilePath);
        const QString objectSourceText = objectState.value(QStringLiteral("sourceText")).toString();
        const QString objectName = objectState.value(QStringLiteral("name")).toString();

        SceneObjectEntry object;
        object.sourcePath = objectPath;
        object.sourceText = objectSourceText;
        object.displayName = objectName;
        object.isDemoCube = objectType != QStringLiteral("obj");
        object.useMaterialAsset = objectState.value(QStringLiteral("materialMode")).toString() == QStringLiteral("asset");
        object.materialAssetId = objectState.value(QStringLiteral("materialAssetId")).toInt(-1);
        object.materialInstance.material = Material::makeLambertTextured();
        object.materialInstance.texturePreset = DemoTexturePreset::WarmChecker;

        if (object.isDemoCube) {
            object.mesh = Mesh::makeCube();
            object.transform = makeDemoCubeTransform();
            if (object.displayName.isEmpty())
                object.displayName = makeSceneObjectDisplayName(QString(), true, static_cast<int>(loadedObjects.size()));
        } else {
            if (!object.sourceText.isEmpty()) {
                if (!loadMeshFromObjText(object.sourceText, object.mesh, errorMessage)) {
                    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                        *errorMessage = QStringLiteral("场景对象“%1”的内嵌 OBJ 数据解析失败。\n%2")
                                            .arg(object.displayName.isEmpty() ? QStringLiteral("未命名对象") : object.displayName,
                                                 *errorMessage);
                    }
                    return false;
                }
            } else {
                if (object.sourcePath.isEmpty()) {
                    if (errorMessage != nullptr)
                        *errorMessage = QStringLiteral("场景对象缺少可用的 OBJ 路径和内嵌数据。");
                    return false;
                }

                QFile file(object.sourcePath);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    if (errorMessage != nullptr)
                        *errorMessage = QStringLiteral("无法打开场景对象文件：%1").arg(QDir::toNativeSeparators(object.sourcePath));
                    return false;
                }
                object.sourceText = QString::fromUtf8(file.readAll());
                if (!loadMeshFromObjText(object.sourceText, object.mesh, errorMessage)) {
                    if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                        *errorMessage = QStringLiteral("场景对象“%1”的 OBJ 文件解析失败：%2\n%3")
                                            .arg(object.displayName.isEmpty() ? QFileInfo(object.sourcePath).fileName()
                                                                              : object.displayName,
                                                 QDir::toNativeSeparators(object.sourcePath),
                                                 *errorMessage);
                    }
                    return false;
                }
            }

            object.transform = makeLoadedMeshTransform(object.mesh);
            if (object.displayName.isEmpty())
                object.displayName = makeSceneObjectDisplayName(object.sourcePath, false, static_cast<int>(loadedObjects.size()));
        }

        const QJsonObject transformObject = objectState.value(QStringLiteral("transform")).toObject();
        const QJsonArray positionArray = transformObject.value(QStringLiteral("position")).toArray();
        const QJsonArray rotationArray = transformObject.value(QStringLiteral("rotationRadians")).toArray();
        const QJsonArray scaleArray = transformObject.value(QStringLiteral("scale")).toArray();
        if (positionArray.size() == 3) {
            object.transform.position = {
                static_cast<float>(positionArray[0].toDouble()),
                static_cast<float>(positionArray[1].toDouble()),
                static_cast<float>(positionArray[2].toDouble())
            };
        }
        if (rotationArray.size() == 3) {
            object.transform.rotationRadians = {
                static_cast<float>(rotationArray[0].toDouble()),
                static_cast<float>(rotationArray[1].toDouble()),
                static_cast<float>(rotationArray[2].toDouble())
            };
        }
        if (scaleArray.size() == 3) {
            object.transform.scale = {
                static_cast<float>(scaleArray[0].toDouble()),
                static_cast<float>(scaleArray[1].toDouble()),
                static_cast<float>(scaleArray[2].toDouble())
            };
        }

        if (objectState.contains(QStringLiteral("materialInstance"))) {
            if (!loadMaterialBindingObject(objectState.value(QStringLiteral("materialInstance")).toObject(),
                                           object.materialInstance,
                                           errorMessage)) {
                return false;
            }
        } else {
            const QJsonObject objectLegacyMaterialObject = objectState.value(QStringLiteral("material")).toObject();
            const QJsonObject materialObject = objectLegacyMaterialObject.isEmpty()
                ? legacyMaterialObject
                : objectLegacyMaterialObject;
            if (!materialObject.isEmpty() && !loadLegacyMaterialBinding(materialObject, object.materialInstance))
                return false;
        }

        if (object.useMaterialAsset) {
            const auto assetIt = std::find_if(loadedMaterialAssets.begin(),
                                              loadedMaterialAssets.end(),
                                              [&](const MaterialAssetEntry &asset) {
                                                  return asset.id == object.materialAssetId;
                                              });
            if (assetIt == loadedMaterialAssets.end()) {
                object.useMaterialAsset = false;
                object.materialAssetId = -1;
            }
        } else {
            object.materialAssetId = -1;
        }

        loadedObjects.push_back(std::move(object));
    }

    m_textureAssets = std::move(loadedTextureAssets);
    m_materialAssets = std::move(loadedMaterialAssets);
    m_nextTextureAssetId = nextTextureAssetId;
    m_nextMaterialAssetId = nextMaterialAssetId;

    if (loadedObjects.empty()) {
        m_sceneObjects.clear();
    } else {
        m_sceneObjects = std::move(loadedObjects);
    }

    m_lightingContext = LightingContext::makeDefault();
    const QJsonObject lightingObject = state.value(QStringLiteral("lighting")).toObject();
    const QJsonArray directionalLightsArray = lightingObject.value(QStringLiteral("directionalLights")).toArray();
    const QJsonArray pointLightsArray = lightingObject.value(QStringLiteral("pointLights")).toArray();
    const QJsonArray spotLightsArray = lightingObject.value(QStringLiteral("spotLights")).toArray();
    if (!directionalLightsArray.isEmpty()) {
        m_lightingContext.directionalLights.clear();
        const DirectionalLight defaultLight = LightingContext::makeDefault().directionalLights.front();
        for (const QJsonValue &lightValue : directionalLightsArray) {
            const QJsonObject lightObject = lightValue.toObject();
            DirectionalLight light = defaultLight;
            light.name = trimmedOrFallbackLightName(lightObject.value(QStringLiteral("name")).toString(),
                                                    fallbackDirectionalLightName(static_cast<int>(m_lightingContext.directionalLights.size())))
                             .toStdString();
            const QJsonArray directionArray = lightObject.value(QStringLiteral("direction")).toArray();
            if (directionArray.size() == 3) {
                light.direction = normalize(Vec3f{
                    static_cast<float>(directionArray[0].toDouble()),
                    static_cast<float>(directionArray[1].toDouble()),
                    static_cast<float>(directionArray[2].toDouble())
                });
            }
            const QJsonArray colorArray = lightObject.value(QStringLiteral("color")).toArray();
            if (colorArray.size() == 3) {
                light.color = {
                    static_cast<float>(colorArray[0].toDouble()),
                    static_cast<float>(colorArray[1].toDouble()),
                    static_cast<float>(colorArray[2].toDouble())
                };
            }
            light.ambient = static_cast<float>(lightObject.value(QStringLiteral("ambient")).toDouble(light.ambient));
            light.intensity = static_cast<float>(lightObject.value(QStringLiteral("intensity")).toDouble(light.intensity));
            light.castShadow = lightObject.value(QStringLiteral("castShadow")).toBool(light.castShadow);
            light.shadowStrength = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("shadowStrength")).toDouble(light.shadowStrength)),
                0.0f,
                1.0f);
            light.shadowBias = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("shadowBias")).toDouble(light.shadowBias)),
                0.0f,
                0.02f);
            light.shadowMapSize = std::clamp(lightObject.value(QStringLiteral("shadowMapSize")).toInt(light.shadowMapSize), 64, 2048);
            light.shadowCoverage = std::max(0.5f, static_cast<float>(lightObject.value(QStringLiteral("shadowCoverage")).toDouble(light.shadowCoverage)));
            light.shadowFilterQuality = static_cast<ShadowFilterQuality>(
                lightObject.value(QStringLiteral("shadowFilterQuality")).toInt(static_cast<int>(light.shadowFilterQuality)));
            light.enabled = lightObject.value(QStringLiteral("enabled")).toBool(light.enabled);
            m_lightingContext.directionalLights.push_back(light);
        }
    }
    if (!pointLightsArray.isEmpty()) {
        m_lightingContext.pointLights.clear();
        for (const QJsonValue &lightValue : pointLightsArray) {
            const QJsonObject lightObject = lightValue.toObject();
            PointLight light = makeDefaultPointLight();
            light.name = trimmedOrFallbackLightName(lightObject.value(QStringLiteral("name")).toString(),
                                                    fallbackPointLightName(static_cast<int>(m_lightingContext.pointLights.size())))
                             .toStdString();
            const QJsonArray positionArray = lightObject.value(QStringLiteral("position")).toArray();
            if (positionArray.size() == 3) {
                light.position = {
                    static_cast<float>(positionArray[0].toDouble()),
                    static_cast<float>(positionArray[1].toDouble()),
                    static_cast<float>(positionArray[2].toDouble())
                };
            }
            const QJsonArray colorArray = lightObject.value(QStringLiteral("color")).toArray();
            if (colorArray.size() == 3) {
                light.color = {
                    static_cast<float>(colorArray[0].toDouble()),
                    static_cast<float>(colorArray[1].toDouble()),
                    static_cast<float>(colorArray[2].toDouble())
                };
            }
            light.ambient = static_cast<float>(lightObject.value(QStringLiteral("ambient")).toDouble(light.ambient));
            light.intensity = std::max(0.0f, static_cast<float>(lightObject.value(QStringLiteral("intensity")).toDouble(light.intensity)));
            light.range = std::max(0.1f, static_cast<float>(lightObject.value(QStringLiteral("range")).toDouble(light.range)));
            light.castShadow = lightObject.value(QStringLiteral("castShadow")).toBool(light.castShadow);
            light.shadowStrength = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("shadowStrength")).toDouble(light.shadowStrength)),
                0.0f,
                1.0f);
            light.shadowBias = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("shadowBias")).toDouble(light.shadowBias)),
                0.0f,
                0.05f);
            light.shadowMapSize = std::clamp(lightObject.value(QStringLiteral("shadowMapSize")).toInt(light.shadowMapSize), 64, 1024);
            light.shadowRange = std::max(0.1f, static_cast<float>(lightObject.value(QStringLiteral("shadowRange")).toDouble(light.shadowRange)));
            light.shadowFilterQuality = static_cast<ShadowFilterQuality>(
                lightObject.value(QStringLiteral("shadowFilterQuality")).toInt(static_cast<int>(light.shadowFilterQuality)));
            light.enabled = lightObject.value(QStringLiteral("enabled")).toBool(light.enabled);
            m_lightingContext.pointLights.push_back(light);
        }
    }
    if (!spotLightsArray.isEmpty()) {
        m_lightingContext.spotLights.clear();
        for (const QJsonValue &lightValue : spotLightsArray) {
            const QJsonObject lightObject = lightValue.toObject();
            SpotLight light = makeDefaultSpotLight();
            light.name = trimmedOrFallbackLightName(lightObject.value(QStringLiteral("name")).toString(),
                                                    fallbackSpotLightName(static_cast<int>(m_lightingContext.spotLights.size())))
                             .toStdString();
            const QJsonArray positionArray = lightObject.value(QStringLiteral("position")).toArray();
            if (positionArray.size() == 3) {
                light.position = {
                    static_cast<float>(positionArray[0].toDouble()),
                    static_cast<float>(positionArray[1].toDouble()),
                    static_cast<float>(positionArray[2].toDouble())
                };
            }
            const QJsonArray directionArray = lightObject.value(QStringLiteral("direction")).toArray();
            if (directionArray.size() == 3) {
                light.direction = normalize(Vec3f{
                    static_cast<float>(directionArray[0].toDouble()),
                    static_cast<float>(directionArray[1].toDouble()),
                    static_cast<float>(directionArray[2].toDouble())
                });
            }
            const QJsonArray colorArray = lightObject.value(QStringLiteral("color")).toArray();
            if (colorArray.size() == 3) {
                light.color = {
                    static_cast<float>(colorArray[0].toDouble()),
                    static_cast<float>(colorArray[1].toDouble()),
                    static_cast<float>(colorArray[2].toDouble())
                };
            }
            light.ambient = static_cast<float>(lightObject.value(QStringLiteral("ambient")).toDouble(light.ambient));
            light.intensity = std::max(0.0f, static_cast<float>(lightObject.value(QStringLiteral("intensity")).toDouble(light.intensity)));
            light.range = std::max(0.1f, static_cast<float>(lightObject.value(QStringLiteral("range")).toDouble(light.range)));
            light.innerConeDegrees = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("innerConeDegrees")).toDouble(light.innerConeDegrees)),
                1.0f,
                89.0f);
            light.outerConeDegrees = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("outerConeDegrees")).toDouble(light.outerConeDegrees)),
                light.innerConeDegrees,
                89.5f);
            light.castShadow = lightObject.value(QStringLiteral("castShadow")).toBool(light.castShadow);
            light.shadowStrength = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("shadowStrength")).toDouble(light.shadowStrength)),
                0.0f,
                1.0f);
            light.shadowBias = std::clamp(
                static_cast<float>(lightObject.value(QStringLiteral("shadowBias")).toDouble(light.shadowBias)),
                0.0f,
                0.02f);
            light.shadowMapSize = std::clamp(lightObject.value(QStringLiteral("shadowMapSize")).toInt(light.shadowMapSize), 64, 2048);
            light.shadowRange = std::max(0.1f, static_cast<float>(lightObject.value(QStringLiteral("shadowRange")).toDouble(light.shadowRange)));
            light.shadowFilterQuality = static_cast<ShadowFilterQuality>(
                lightObject.value(QStringLiteral("shadowFilterQuality")).toInt(static_cast<int>(light.shadowFilterQuality)));
            light.enabled = lightObject.value(QStringLiteral("enabled")).toBool(light.enabled);
            m_lightingContext.spotLights.push_back(light);
        }
    }
    if (directionalLightsArray.isEmpty() && pointLightsArray.isEmpty() && spotLightsArray.isEmpty()) {
        QJsonObject legacyMaterialObject = state.value(QStringLiteral("material")).toObject();
        if (legacyMaterialObject.isEmpty() && !objectsArray.isEmpty())
            legacyMaterialObject = objectsArray.first().toObject().value(QStringLiteral("material")).toObject();
        const QJsonArray lightDirectionArray = legacyMaterialObject.value(QStringLiteral("lightDirection")).toArray();
        const QJsonArray lightColorArray = legacyMaterialObject.value(QStringLiteral("lightColor")).toArray();
        DirectionalLight &legacyLight = primarySceneLight(m_lightingContext);
        if (lightDirectionArray.size() == 3) {
            legacyLight.direction = normalize(Vec3f{
                static_cast<float>(lightDirectionArray[0].toDouble()),
                static_cast<float>(lightDirectionArray[1].toDouble()),
                static_cast<float>(lightDirectionArray[2].toDouble())
            });
        }
        if (lightColorArray.size() == 3) {
            legacyLight.color = {
                static_cast<float>(lightColorArray[0].toDouble()),
                static_cast<float>(lightColorArray[1].toDouble()),
                static_cast<float>(lightColorArray[2].toDouble())
            };
        }
        legacyLight.ambient = static_cast<float>(legacyMaterialObject.value(QStringLiteral("ambient")).toDouble(legacyLight.ambient));
        legacyLight.intensity = static_cast<float>(legacyMaterialObject.value(QStringLiteral("intensity")).toDouble(legacyLight.intensity));
        legacyLight.castShadow = legacyMaterialObject.value(QStringLiteral("castShadow")).toBool(legacyLight.castShadow);
        legacyLight.shadowStrength = std::clamp(
            static_cast<float>(legacyMaterialObject.value(QStringLiteral("shadowStrength")).toDouble(legacyLight.shadowStrength)),
            0.0f,
            1.0f);
        legacyLight.shadowBias = std::clamp(
            static_cast<float>(legacyMaterialObject.value(QStringLiteral("shadowBias")).toDouble(legacyLight.shadowBias)),
            0.0f,
            0.02f);
    }

    const QJsonObject gizmoObject = state.value(QStringLiteral("gizmo")).toObject();
    m_gizmoSpaceMode = static_cast<GizmoSpaceMode>(
        gizmoObject.value(QStringLiteral("spaceMode")).toInt(static_cast<int>(m_gizmoSpaceMode)));
    m_gizmoTranslationSnapStep = std::clamp(
        static_cast<float>(gizmoObject.value(QStringLiteral("translationSnapStep")).toDouble(m_gizmoTranslationSnapStep)),
        0.01f,
        100.0f);
    m_gizmoRotationSnapRadians = std::clamp(
        static_cast<float>(gizmoObject.value(QStringLiteral("rotationSnapDegrees")).toDouble(gizmoRotationSnapDegrees())),
        1.0f,
        180.0f) * kPi / 180.0f;
    m_gizmoScaleSnapStep = std::clamp(
        static_cast<float>(gizmoObject.value(QStringLiteral("scaleSnapStep")).toDouble(m_gizmoScaleSnapStep)),
        0.01f,
        10.0f);

    m_selectedSceneObjectIndex = sceneObjectCount() > 0
        ? std::clamp(state.value(QStringLiteral("selectedObjectIndex")).toInt(0), 0, sceneObjectCount() - 1)
        : -1;

    const QJsonObject cameraObject = state.value(QStringLiteral("camera")).toObject();
    if (!cameraObject.isEmpty() && !applyCameraPresetObject(cameraObject, errorMessage))
        return false;

    RenderState renderState = m_renderer.renderState();
    const QJsonObject renderStateObject = state.value(QStringLiteral("renderState")).toObject();
    renderState.depthTestEnable = renderStateObject.value(QStringLiteral("depthTestEnable")).toBool(renderState.depthTestEnable);
    renderState.depthWriteEnable = renderStateObject.value(QStringLiteral("depthWriteEnable")).toBool(renderState.depthWriteEnable);
    renderState.depthFunc = static_cast<DepthFunc>(renderStateObject.value(QStringLiteral("depthFunc")).toInt(static_cast<int>(renderState.depthFunc)));
    renderState.cullMode = static_cast<CullMode>(renderStateObject.value(QStringLiteral("cullMode")).toInt(static_cast<int>(renderState.cullMode)));
    renderState.fillMode = static_cast<FillMode>(renderStateObject.value(QStringLiteral("fillMode")).toInt(static_cast<int>(renderState.fillMode)));
    renderState.debugView = static_cast<DebugView>(renderStateObject.value(QStringLiteral("debugView")).toInt(static_cast<int>(renderState.debugView)));
    renderState.antiAliasing = static_cast<AntiAliasingMode>(renderStateObject.value(QStringLiteral("antiAliasing")).toInt(static_cast<int>(renderState.antiAliasing)));
    renderState.blend.mode = static_cast<BlendMode>(renderStateObject.value(QStringLiteral("blendMode")).toInt(static_cast<int>(renderState.blend.mode)));
    m_renderer.setRenderState(renderState);

    const QJsonObject parallelRasterObject = state.value(QStringLiteral("parallelRaster")).toObject();
    if (!parallelRasterObject.isEmpty()) {
        m_renderer.setParallelRasterEnabled(
            parallelRasterObject.value(QStringLiteral("enabled")).toBool(m_renderer.parallelRasterEnabled()));
        m_renderer.setWorkerThreadCount(
            parallelRasterObject.value(QStringLiteral("workerThreadCount"))
                .toInt(m_renderer.requestedWorkerThreadCount()));
        m_renderer.setRasterTileSize(
            std::max(1,
                     parallelRasterObject.value(QStringLiteral("tileSize"))
                         .toInt(m_renderer.rasterTileSize())));
        m_renderer.setParallelThresholds(
            std::max(1,
                     parallelRasterObject.value(QStringLiteral("minTileCount"))
                         .toInt(m_renderer.minParallelTileCount())),
            std::max(1,
                     parallelRasterObject.value(QStringLiteral("minPixelCount"))
                         .toInt(m_renderer.minParallelPixelCount())));
        m_renderer.setParallelTilesPerTask(
            std::max(1,
                     parallelRasterObject.value(QStringLiteral("tilesPerTask"))
                         .toInt(m_renderer.parallelTilesPerTask())));
    }

    const QJsonObject postProcessObject = state.value(QStringLiteral("postProcess")).toObject();
    if (!postProcessObject.isEmpty()) {
        m_postProcessSettings.enabled = postProcessObject.value(QStringLiteral("enabled")).toBool(m_postProcessSettings.enabled);
        m_postProcessSettings.toneMapping = static_cast<ToneMappingMode>(
            postProcessObject.value(QStringLiteral("toneMapping")).toInt(static_cast<int>(m_postProcessSettings.toneMapping)));
        m_postProcessSettings.exposure = std::clamp(
            static_cast<float>(postProcessObject.value(QStringLiteral("exposure")).toDouble(m_postProcessSettings.exposure)),
            0.05f,
            8.0f);
        m_postProcessSettings.gamma = std::clamp(
            static_cast<float>(postProcessObject.value(QStringLiteral("gamma")).toDouble(m_postProcessSettings.gamma)),
            0.5f,
            4.0f);
        m_postProcessSettings.contrast = std::clamp(
            static_cast<float>(postProcessObject.value(QStringLiteral("contrast")).toDouble(m_postProcessSettings.contrast)),
            0.0f,
            2.5f);
        m_postProcessSettings.saturation = std::clamp(
            static_cast<float>(postProcessObject.value(QStringLiteral("saturation")).toDouble(m_postProcessSettings.saturation)),
            0.0f,
            2.5f);
    }

    m_scenePreset = static_cast<ScenePreset>(state.value(QStringLiteral("scenePreset")).toInt(static_cast<int>(ScenePreset::Custom)));
    const QJsonObject lineArtObject = state.value(QStringLiteral("lineArt")).toObject();
    if (!lineArtObject.isEmpty()) {
        if (!applyLineArtConfigObject(lineArtObject, true, errorMessage))
            return false;
    }
    emit cameraChanged();
    emit sceneContentChanged();
    requestRender();
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

void RasterWidget::resetSceneToDemoCube()
{
    exitLineArtMode();
    m_textureAssets.clear();
    m_materialAssets.clear();
    m_nextTextureAssetId = 1;
    m_nextMaterialAssetId = 1;
    m_sceneFilePath.clear();
    m_sceneResourceDirectoryName = QStringLiteral("resources");
    m_lightingContext = LightingContext::makeDefault();
    DirectionalLight &light = primarySceneLight(m_lightingContext);
    light.direction = normalize(Vec3f{-0.45f, -0.65f, -0.75f});
    light.color = {1.0f, 0.98f, 0.92f};
    light.ambient = 0.18f;

    SceneObjectEntry object;
    object.mesh = Mesh::makeCube();
    object.transform = makeDemoCubeTransform();
    object.materialInstance.material = Material::makeLambertTextured();
    object.materialInstance.texturePreset = DemoTexturePreset::WarmChecker;
    object.displayName = makeSceneObjectDisplayName(QString(), true, 0);
    object.isDemoCube = true;

    m_sceneObjects.clear();
    m_sceneObjects.push_back(std::move(object));
    m_selectedSceneObjectIndex = 0;
}

void RasterWidget::resetDemoMaterial()
{
    MaterialBinding *binding = selectedEditableMaterialBinding();
    if (binding == nullptr)
        return;

    binding->material = Material::makeLambertTextured();
    binding->texturePreset = DemoTexturePreset::WarmChecker;
    binding->textureAssetId = -1;
    binding->normalTextureAssetId = -1;
    binding->metallicRoughnessTextureAssetId = -1;
    m_scenePreset = ScenePreset::Custom;
    emit sceneContentChanged();
    requestRender();
}

Vec3f RasterWidget::gizmoAxisDirection(const Transform *transform, ViewportAxis axis) const
{
    const Vec3f localAxis = [&]() {
        switch (axis) {
        case ViewportAxis::X:
            return Vec3f{1.0f, 0.0f, 0.0f};
        case ViewportAxis::Y:
            return Vec3f{0.0f, 1.0f, 0.0f};
        case ViewportAxis::Z:
            return Vec3f{0.0f, 0.0f, 1.0f};
        case ViewportAxis::None:
            return Vec3f{0.0f, 0.0f, 0.0f};
        }
        return Vec3f{0.0f, 0.0f, 0.0f};
    }();

    if (transform == nullptr || m_gizmoSpaceMode == GizmoSpaceMode::World)
        return localAxis;

    return rotateByEulerRadians(localAxis, transform->rotationRadians);
}

bool RasterWidget::projectViewportHandle(const Vec3f &worldPosition, QPointF &screenPosition, float &viewDepth) const
{
    const Camera &camera = m_renderer.camera();
    if (!projectWorldToViewport(camera, width(), height(), worldPosition, screenPosition))
        return false;

    viewDepth = std::max(0.05f, dot(worldPosition - camera.position, cameraForward(camera)));
    return true;
}

RasterWidget::ViewportHandleHit RasterWidget::pickViewportHandle(const QPoint &mousePosition) const
{
    ViewportHandleHit bestHit;
    if (m_isLineArtMode)
        return bestHit;

    const QPointF mousePoint(mousePosition);
    float bestDistanceSquared = std::numeric_limits<float>::max();
    const auto acceptHit = [&](ViewportHandleKind kind,
                               int index,
                               ViewportAxis axis,
                               ViewportHandleOperation operation,
                               const QPointF &screenPosition,
                               float viewDepth,
                               float distanceSquared) {
        if (distanceSquared >= bestDistanceSquared)
            return;

        bestDistanceSquared = distanceSquared;
        bestHit.kind = kind;
        bestHit.index = index;
        bestHit.axis = axis;
        bestHit.operation = operation;
        bestHit.screenPosition = screenPosition;
        bestHit.viewDepth = viewDepth;
    };

    const auto considerObjectOrPointAxes = [&](ViewportHandleKind kind, int index, const Vec3f &worldPosition, const Transform *transform) {
        QPointF baseScreenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(worldPosition, baseScreenPosition, viewDepth))
            return;

        const float axisLengthWorld = worldUnitsPerPixelAtViewDepth(m_renderer.camera(), viewDepth, height()) * 60.0f;
        for (const ViewportAxis axis : {ViewportAxis::X, ViewportAxis::Y, ViewportAxis::Z}) {
            const Vec3f axisDirection = gizmoAxisDirection(transform, axis);
            QPointF translateEndScreenPosition;
            float translateEndDepth = 0.0f;
            QPointF scaleEndScreenPosition;
            float scaleEndDepth = 0.0f;
            if (!projectViewportHandle(worldPosition + axisDirection * axisLengthWorld,
                                       translateEndScreenPosition,
                                       translateEndDepth)
                || !projectViewportHandle(worldPosition + axisDirection * (axisLengthWorld * 1.35f),
                                          scaleEndScreenPosition,
                                          scaleEndDepth)) {
                continue;
            }

            Q_UNUSED(translateEndDepth);
            Q_UNUSED(scaleEndDepth);
            const float translateLineDistanceSquared = pointSegmentDistanceSquared(mousePoint, baseScreenPosition, translateEndScreenPosition);
            if (translateLineDistanceSquared <= 49.0f) {
                acceptHit(kind,
                          index,
                          axis,
                          ViewportHandleOperation::Translate,
                          translateEndScreenPosition,
                          viewDepth,
                          translateLineDistanceSquared);
            }

            if (kind == ViewportHandleKind::SceneObject) {
                const qreal scaleDx = scaleEndScreenPosition.x() - mousePoint.x();
                const qreal scaleDy = scaleEndScreenPosition.y() - mousePoint.y();
                const float scaleDistanceSquared = static_cast<float>(scaleDx * scaleDx + scaleDy * scaleDy);
                if (scaleDistanceSquared <= 100.0f) {
                    acceptHit(kind,
                              index,
                              axis,
                              ViewportHandleOperation::Scale,
                              scaleEndScreenPosition,
                              viewDepth,
                              scaleDistanceSquared);
                }
            }
        }
    };

    if (m_selectedSceneObjectIndex >= 0 && m_selectedSceneObjectIndex < sceneObjectCount()) {
        const Transform &transform = m_sceneObjects[static_cast<std::size_t>(m_selectedSceneObjectIndex)].transform;
        const Vec3f objectPosition = transform.position;
        QPointF centerScreenPosition;
        float viewDepth = 0.0f;
        if (projectViewportHandle(objectPosition, centerScreenPosition, viewDepth)) {
            considerObjectOrPointAxes(ViewportHandleKind::SceneObject,
                                      m_selectedSceneObjectIndex,
                                      objectPosition,
                                      &transform);

            const qreal distanceToCenter = std::sqrt(std::max(0.0,
                (mousePoint.x() - centerScreenPosition.x()) * (mousePoint.x() - centerScreenPosition.x())
                + (mousePoint.y() - centerScreenPosition.y()) * (mousePoint.y() - centerScreenPosition.y())));
            const qreal ringRadii[3] = {32.0, 42.0, 52.0};
            const ViewportAxis ringAxes[3] = {ViewportAxis::X, ViewportAxis::Y, ViewportAxis::Z};
            for (int ringIndex = 0; ringIndex < 3; ++ringIndex) {
                const float bandDistance = static_cast<float>(std::fabs(distanceToCenter - ringRadii[ringIndex]));
                if (bandDistance <= 6.0f) {
                    acceptHit(ViewportHandleKind::SceneObject,
                              m_selectedSceneObjectIndex,
                              ringAxes[ringIndex],
                              ViewportHandleOperation::Rotate,
                              centerScreenPosition,
                              viewDepth,
                              bandDistance * bandDistance);
                }
            }
        }
    }
    if (m_selectedLightKind == SelectedLightKind::Point
        && m_selectedLightIndex >= 0
        && m_selectedLightIndex < pointLightCount()) {
        considerObjectOrPointAxes(ViewportHandleKind::PointLight,
                                  m_selectedLightIndex,
                                  m_lightingContext.pointLights[static_cast<std::size_t>(m_selectedLightIndex)].position,
                                  nullptr);
    }
    if (m_selectedLightKind == SelectedLightKind::Spot
        && m_selectedLightIndex >= 0
        && m_selectedLightIndex < spotLightCount()) {
        const SpotLight &light = m_lightingContext.spotLights[static_cast<std::size_t>(m_selectedLightIndex)];
        considerObjectOrPointAxes(ViewportHandleKind::SpotLight,
                                  m_selectedLightIndex,
                                  light.position,
                                  nullptr);

        QPointF baseScreenPosition;
        float viewDepth = 0.0f;
        if (projectViewportHandle(light.position, baseScreenPosition, viewDepth)) {
            const float axisLengthWorld = worldUnitsPerPixelAtViewDepth(m_renderer.camera(), viewDepth, height()) * 72.0f;
            QPointF directionTipScreenPosition;
            float directionTipDepth = 0.0f;
            if (projectViewportHandle(light.position + normalize(light.direction) * axisLengthWorld,
                                      directionTipScreenPosition,
                                      directionTipDepth)) {
                Q_UNUSED(directionTipDepth);
                const float lineDistanceSquared = pointSegmentDistanceSquared(mousePoint,
                                                                              baseScreenPosition,
                                                                              directionTipScreenPosition);
                if (lineDistanceSquared <= 49.0f) {
                    acceptHit(ViewportHandleKind::SpotLight,
                              m_selectedLightIndex,
                              ViewportAxis::None,
                              ViewportHandleOperation::Direction,
                              directionTipScreenPosition,
                              viewDepth,
                              lineDistanceSquared);
                }

                const qreal dx = directionTipScreenPosition.x() - mousePoint.x();
                const qreal dy = directionTipScreenPosition.y() - mousePoint.y();
                const float tipDistanceSquared = static_cast<float>(dx * dx + dy * dy);
                if (tipDistanceSquared <= 100.0f) {
                    acceptHit(ViewportHandleKind::SpotLight,
                              m_selectedLightIndex,
                              ViewportAxis::None,
                              ViewportHandleOperation::Direction,
                              directionTipScreenPosition,
                              viewDepth,
                              tipDistanceSquared);
                }
            }
        }
    }
    if (m_selectedLightKind == SelectedLightKind::Directional
        && m_selectedLightIndex >= 0
        && m_selectedLightIndex < directionalLightCount()) {
        const QRectF sphereRect = directionalLightSphereRect();
        const QPointF sphereCenter = sphereRect.center();
        const qreal radius = sphereRect.width() * 0.5;
        const qreal dx = mousePoint.x() - sphereCenter.x();
        const qreal dy = mousePoint.y() - sphereCenter.y();
        const float distanceSquared = static_cast<float>(dx * dx + dy * dy);
        if (distanceSquared <= static_cast<float>(radius * radius)) {
            acceptHit(ViewportHandleKind::DirectionalLight,
                      m_selectedLightIndex,
                      ViewportAxis::None,
                      ViewportHandleOperation::Direction,
                      sphereCenter,
                      0.0f,
                      distanceSquared);
        }
    }
    if (bestHit.kind != ViewportHandleKind::None)
        return bestHit;

    for (int index = 0; index < directionalLightCount(); ++index) {
        if (directionalLightCardRect(index).contains(mousePoint)) {
            bestHit.kind = ViewportHandleKind::DirectionalLight;
            bestHit.index = index;
            bestHit.axis = ViewportAxis::None;
            bestHit.operation = ViewportHandleOperation::None;
            bestHit.screenPosition = directionalLightCardRect(index).center();
            bestHit.viewDepth = 0.0f;
            return bestHit;
        }
    }

    const auto considerHandle = [&](ViewportHandleKind kind,
                                    int index,
                                    ViewportHandleOperation operation,
                                    const QPointF &screenPosition,
                                    float viewDepth,
                                    qreal radius) {
        const qreal dx = screenPosition.x() - mousePoint.x();
        const qreal dy = screenPosition.y() - mousePoint.y();
        const float distanceSquared = static_cast<float>(dx * dx + dy * dy);
        if (distanceSquared > static_cast<float>(radius * radius))
            return;
        acceptHit(kind,
                  index,
                  ViewportAxis::None,
                  operation,
                  screenPosition,
                  viewDepth,
                  distanceSquared);
    };

    for (int index = 0; index < pointLightCount(); ++index) {
        QPointF screenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(m_lightingContext.pointLights[static_cast<std::size_t>(index)].position,
                                   screenPosition,
                                   viewDepth)) {
            continue;
        }

        const bool selected = m_selectedLightKind == SelectedLightKind::Point && m_selectedLightIndex == index;
        considerHandle(ViewportHandleKind::PointLight,
                       index,
                       ViewportHandleOperation::Translate,
                       screenPosition,
                       viewDepth,
                       selected ? 17.0 : 14.0);
    }

    for (int index = 0; index < spotLightCount(); ++index) {
        QPointF screenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(m_lightingContext.spotLights[static_cast<std::size_t>(index)].position,
                                   screenPosition,
                                   viewDepth)) {
            continue;
        }

        const bool selected = m_selectedLightKind == SelectedLightKind::Spot && m_selectedLightIndex == index;
        considerHandle(ViewportHandleKind::SpotLight,
                       index,
                       ViewportHandleOperation::Translate,
                       screenPosition,
                       viewDepth,
                       selected ? 17.0 : 14.0);
    }

    for (int index = 0; index < sceneObjectCount(); ++index) {
        QPointF screenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(m_sceneObjects[static_cast<std::size_t>(index)].transform.position,
                                   screenPosition,
                                   viewDepth)) {
            continue;
        }

        const bool selected = m_selectedSceneObjectIndex == index;
        considerHandle(ViewportHandleKind::SceneObject,
                       index,
                       ViewportHandleOperation::Translate,
                       screenPosition,
                       viewDepth,
                       selected ? 16.0 : 13.0);
    }

    return bestHit;
}

bool RasterWidget::beginViewportHandleDrag(const ViewportHandleHit &hit, const QPoint &mousePosition)
{
    if (hit.kind == ViewportHandleKind::None || hit.index < 0)
        return false;

    m_isDraggingViewportHandle = true;
    m_dragHandleKind = hit.kind;
    m_dragHandleIndex = hit.index;
    m_dragHandleAxis = hit.axis;
    m_dragHandleOperation = hit.operation;
    m_dragStartMousePosition = mousePosition;
    m_dragStartViewDepth = hit.viewDepth;
    m_dragStartWorldPosition = {0.0f, 0.0f, 0.0f};
    m_dragAxisLengthWorld = 0.0f;
    m_dragStartDirection = {0.0f, -1.0f, 0.0f};
    m_dragStartRotationRadians = {0.0f, 0.0f, 0.0f};
    m_dragStartScale = {1.0f, 1.0f, 1.0f};

    if (hit.kind == ViewportHandleKind::SceneObject) {
        const Transform &transform = m_sceneObjects[static_cast<std::size_t>(hit.index)].transform;
        m_dragStartWorldPosition = transform.position;
        m_dragStartRotationRadians = transform.rotationRadians;
        m_dragStartScale = transform.scale;
    } else if (hit.kind == ViewportHandleKind::PointLight) {
        m_dragStartWorldPosition = m_lightingContext.pointLights[static_cast<std::size_t>(hit.index)].position;
    } else if (hit.kind == ViewportHandleKind::SpotLight) {
        const SpotLight &light = m_lightingContext.spotLights[static_cast<std::size_t>(hit.index)];
        m_dragStartWorldPosition = light.position;
        m_dragStartDirection = light.direction;
    } else if (hit.kind == ViewportHandleKind::DirectionalLight) {
        m_dragStartDirection = m_lightingContext.directionalLights[static_cast<std::size_t>(hit.index)].direction;
    }
    if (m_dragHandleOperation == ViewportHandleOperation::Translate
        || m_dragHandleOperation == ViewportHandleOperation::Scale
        || (m_dragHandleKind == ViewportHandleKind::SpotLight
            && m_dragHandleOperation == ViewportHandleOperation::Direction)) {
        m_dragAxisLengthWorld = worldUnitsPerPixelAtViewDepth(m_renderer.camera(), m_dragStartViewDepth, height()) * 60.0f;
    }
    emit gizmoInteractionChanged(true,
                                 static_cast<int>(m_dragHandleKind),
                                 static_cast<int>(m_dragHandleOperation),
                                 static_cast<int>(m_dragHandleAxis));

    return true;
}

bool RasterWidget::updateViewportHandleDrag(const QPoint &mousePosition)
{
    if (!m_isDraggingViewportHandle || m_dragHandleIndex < 0)
        return false;

    const qreal fineTuneFactor = m_gizmoFineTune ? static_cast<qreal>(m_gizmoFineTuneFactor) : 1.0;
    const QPointF effectiveMousePosition(static_cast<qreal>(m_dragStartMousePosition.x())
                                             + static_cast<qreal>(mousePosition.x() - m_dragStartMousePosition.x()) * fineTuneFactor,
                                         static_cast<qreal>(m_dragStartMousePosition.y())
                                             + static_cast<qreal>(mousePosition.y() - m_dragStartMousePosition.y()) * fineTuneFactor);
    const QPointF effectiveDelta = effectiveMousePosition - QPointF(m_dragStartMousePosition);
    if (std::fabs(effectiveDelta.x()) <= 1e-4 && std::fabs(effectiveDelta.y()) <= 1e-4)
        return false;

    const bool snapEnabled = m_moveFast;

    if (m_dragHandleOperation == ViewportHandleOperation::Direction
        && m_dragHandleKind == ViewportHandleKind::DirectionalLight) {
        const QRectF sphereRect = directionalLightSphereRect();
        const Vec3f localSphereVector = clampSphereVector(effectiveMousePosition, sphereRect);
        const Camera &camera = m_renderer.camera();
        const Vec3f worldLightVector = normalize(cameraRight(camera) * localSphereVector.x
                                                 + cameraOrthoUp(camera) * localSphereVector.y
                                                 + cameraForward(camera) * localSphereVector.z);
        setDirectionalLightDirection(m_dragHandleIndex, worldLightVector * -1.0f);
        return true;
    }

    if (m_dragHandleOperation == ViewportHandleOperation::Direction
        && m_dragHandleKind == ViewportHandleKind::SpotLight) {
        QPointF baseScreenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(m_dragStartWorldPosition, baseScreenPosition, viewDepth))
            return false;

        const Camera &camera = m_renderer.camera();
        const QPointF screenDelta = effectiveMousePosition - baseScreenPosition;
        const float pixelToWorld = worldUnitsPerPixelAtViewDepth(camera, m_dragStartViewDepth, height());
        const Vec3f worldTarget = m_dragStartWorldPosition
            + cameraRight(camera) * static_cast<float>(screenDelta.x()) * pixelToWorld
            + cameraOrthoUp(camera) * static_cast<float>(-screenDelta.y()) * pixelToWorld
            + cameraForward(camera) * std::max(m_dragAxisLengthWorld, pixelToWorld * 56.0f);
        const Vec3f newDirection = normalize(worldTarget - m_dragStartWorldPosition);
        if (dot(newDirection, newDirection) <= 1e-6f)
            return false;

        setSpotLightDirection(m_dragHandleIndex, newDirection);
        return true;
    }

    if (m_dragHandleOperation == ViewportHandleOperation::Rotate
        && m_dragHandleKind == ViewportHandleKind::SceneObject
        && m_dragHandleAxis != ViewportAxis::None) {
        QPointF centerScreenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(m_dragStartWorldPosition, centerScreenPosition, viewDepth))
            return false;

        Q_UNUSED(viewDepth);
        const qreal startAngle = std::atan2(static_cast<qreal>(m_dragStartMousePosition.y()) - centerScreenPosition.y(),
                                            static_cast<qreal>(m_dragStartMousePosition.x()) - centerScreenPosition.x());
        const qreal currentAngle = std::atan2(effectiveMousePosition.y() - centerScreenPosition.y(),
                                              effectiveMousePosition.x() - centerScreenPosition.x());
        float deltaAngle = static_cast<float>(currentAngle - startAngle);
        if (snapEnabled)
            deltaAngle = snapToStep(deltaAngle, m_gizmoRotationSnapRadians);
        const int axisIndex = viewportAxisIndex(m_dragHandleAxis);
        if (axisIndex < 0)
            return false;

        Transform transform = m_sceneObjects[static_cast<std::size_t>(m_dragHandleIndex)].transform;
        if (axisIndex == 0)
            transform.rotationRadians.x = m_dragStartRotationRadians.x + deltaAngle;
        else if (axisIndex == 1)
            transform.rotationRadians.y = m_dragStartRotationRadians.y + deltaAngle;
        else
            transform.rotationRadians.z = m_dragStartRotationRadians.z + deltaAngle;
        setSelectedSceneObjectTransform(transform);
        return true;
    }

    if (m_dragHandleOperation == ViewportHandleOperation::Scale
        && m_dragHandleKind == ViewportHandleKind::SceneObject
        && m_dragHandleAxis != ViewportAxis::None) {
        QPointF baseScreenPosition;
        float baseDepth = 0.0f;
        QPointF endScreenPosition;
        float endDepth = 0.0f;
        if (!projectViewportHandle(m_dragStartWorldPosition, baseScreenPosition, baseDepth)
            || !projectViewportHandle(m_dragStartWorldPosition + viewportAxisVector(m_dragHandleAxis) * (m_dragAxisLengthWorld * 1.35f),
                                      endScreenPosition,
                                      endDepth)) {
            return false;
        }

        Q_UNUSED(baseDepth);
        Q_UNUSED(endDepth);
        const QPointF screenAxis = endScreenPosition - baseScreenPosition;
        const qreal axisScreenLength = std::sqrt(screenAxis.x() * screenAxis.x() + screenAxis.y() * screenAxis.y());
        if (axisScreenLength <= 1e-4)
            return false;

        const QPointF normalizedScreenAxis(screenAxis.x() / axisScreenLength, screenAxis.y() / axisScreenLength);
        const qreal signedPixels = effectiveDelta.x() * normalizedScreenAxis.x()
                                   + effectiveDelta.y() * normalizedScreenAxis.y();
        const float rawScaleFactor = std::max(0.05f, 1.0f + static_cast<float>(signedPixels) / 80.0f);
        const int axisIndex = viewportAxisIndex(m_dragHandleAxis);
        if (axisIndex < 0)
            return false;

        Transform transform = m_sceneObjects[static_cast<std::size_t>(m_dragHandleIndex)].transform;
        if (axisIndex == 0) {
            float nextScale = std::max(0.05f, m_dragStartScale.x * rawScaleFactor);
            if (snapEnabled)
                nextScale = std::max(0.05f, snapToStep(nextScale, m_gizmoScaleSnapStep));
            transform.scale.x = nextScale;
        } else if (axisIndex == 1) {
            float nextScale = std::max(0.05f, m_dragStartScale.y * rawScaleFactor);
            if (snapEnabled)
                nextScale = std::max(0.05f, snapToStep(nextScale, m_gizmoScaleSnapStep));
            transform.scale.y = nextScale;
        } else {
            float nextScale = std::max(0.05f, m_dragStartScale.z * rawScaleFactor);
            if (snapEnabled)
                nextScale = std::max(0.05f, snapToStep(nextScale, m_gizmoScaleSnapStep));
            transform.scale.z = nextScale;
        }
        setSelectedSceneObjectTransform(transform);
        return true;
    }

    Vec3f worldOffset{0.0f, 0.0f, 0.0f};
    if (m_dragHandleOperation == ViewportHandleOperation::Translate && m_dragHandleAxis != ViewportAxis::None) {
        QPointF baseScreenPosition;
        float baseDepth = 0.0f;
        QPointF endScreenPosition;
        float endDepth = 0.0f;
        if (!projectViewportHandle(m_dragStartWorldPosition, baseScreenPosition, baseDepth)
            || !projectViewportHandle(m_dragStartWorldPosition + viewportAxisVector(m_dragHandleAxis) * m_dragAxisLengthWorld,
                                      endScreenPosition,
                                      endDepth)) {
            return false;
        }

        Q_UNUSED(baseDepth);
        Q_UNUSED(endDepth);
        const QPointF screenAxis = endScreenPosition - baseScreenPosition;
        const qreal axisScreenLength = std::sqrt(screenAxis.x() * screenAxis.x() + screenAxis.y() * screenAxis.y());
        if (axisScreenLength <= 1e-4)
            return false;

        const QPointF normalizedScreenAxis(screenAxis.x() / axisScreenLength, screenAxis.y() / axisScreenLength);
        const qreal signedPixels = effectiveDelta.x() * normalizedScreenAxis.x()
                                   + effectiveDelta.y() * normalizedScreenAxis.y();
        const float unitsPerPixel = m_dragAxisLengthWorld / static_cast<float>(axisScreenLength);
        const Transform *transform = m_dragHandleKind == ViewportHandleKind::SceneObject
            ? &m_sceneObjects[static_cast<std::size_t>(m_dragHandleIndex)].transform
            : nullptr;
        worldOffset = gizmoAxisDirection(transform, m_dragHandleAxis) * (static_cast<float>(signedPixels) * unitsPerPixel);
    } else {
        const Camera &camera = m_renderer.camera();
        const float pixelToWorld = worldUnitsPerPixelAtViewDepth(camera, m_dragStartViewDepth, height());
        worldOffset = cameraRight(camera) * (static_cast<float>(effectiveDelta.x()) * pixelToWorld)
                      + cameraOrthoUp(camera) * (static_cast<float>(-effectiveDelta.y()) * pixelToWorld);
    }

    const Vec3f worldPosition = m_dragStartWorldPosition + worldOffset;
    Vec3f snappedWorldPosition = worldPosition;
    if (snapEnabled) {
        snappedWorldPosition.x = snapToStep(snappedWorldPosition.x, m_gizmoTranslationSnapStep);
        snappedWorldPosition.y = snapToStep(snappedWorldPosition.y, m_gizmoTranslationSnapStep);
        snappedWorldPosition.z = snapToStep(snappedWorldPosition.z, m_gizmoTranslationSnapStep);
    }

    if (m_dragHandleKind == ViewportHandleKind::SceneObject) {
        Transform transform = m_sceneObjects[static_cast<std::size_t>(m_dragHandleIndex)].transform;
        transform.position = snappedWorldPosition;
        setSelectedSceneObjectTransform(transform);
        return true;
    }

    if (m_dragHandleKind == ViewportHandleKind::PointLight) {
        setPointLightPosition(m_dragHandleIndex, snappedWorldPosition);
        return true;
    }

    if (m_dragHandleKind == ViewportHandleKind::SpotLight) {
        setSpotLightPosition(m_dragHandleIndex, snappedWorldPosition);
        return true;
    }

    return false;
}

void RasterWidget::endViewportHandleDrag()
{
    const ViewportHandleKind previousKind = m_dragHandleKind;
    const ViewportHandleOperation previousOperation = m_dragHandleOperation;
    const ViewportAxis previousAxis = m_dragHandleAxis;
    m_isDraggingViewportHandle = false;
    m_dragHandleKind = ViewportHandleKind::None;
    m_dragHandleIndex = -1;
    m_dragHandleAxis = ViewportAxis::None;
    m_dragHandleOperation = ViewportHandleOperation::None;
    m_dragAxisLengthWorld = 0.0f;
    emit gizmoInteractionChanged(false,
                                 static_cast<int>(previousKind),
                                 static_cast<int>(previousOperation),
                                 static_cast<int>(previousAxis));
}

void RasterWidget::drawSceneObjectGizmos(QPainter &painter) const
{
    if (m_sceneObjects.empty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor selectedColor(72, 196, 255);
    const QColor idleColor(114, 224, 188);
    const QColor fillColor(18, 28, 34, 210);

    for (int index = 0; index < sceneObjectCount(); ++index) {
        QPointF screenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(m_sceneObjects[static_cast<std::size_t>(index)].transform.position,
                                   screenPosition,
                                   viewDepth)) {
            continue;
        }

        Q_UNUSED(viewDepth);
        const bool selected = m_selectedSceneObjectIndex == index;
        const QColor accent = selected ? selectedColor : idleColor;
        const qreal radius = selected ? 8.0 : 5.5;

        if (selected) {
            const float axisLengthWorld = worldUnitsPerPixelAtViewDepth(m_renderer.camera(), viewDepth, height()) * 60.0f;
            for (const ViewportAxis axis : {ViewportAxis::X, ViewportAxis::Y, ViewportAxis::Z}) {
                const Vec3f axisDirection = gizmoAxisDirection(&m_sceneObjects[static_cast<std::size_t>(index)].transform, axis);
                QPointF translateEndScreenPosition;
                float translateEndDepth = 0.0f;
                QPointF scaleEndScreenPosition;
                float scaleEndDepth = 0.0f;
                if (!projectViewportHandle(m_sceneObjects[static_cast<std::size_t>(index)].transform.position
                                               + axisDirection * axisLengthWorld,
                                           translateEndScreenPosition,
                                           translateEndDepth)
                    || !projectViewportHandle(m_sceneObjects[static_cast<std::size_t>(index)].transform.position
                                                  + axisDirection * (axisLengthWorld * 1.35f),
                                              scaleEndScreenPosition,
                                              scaleEndDepth)) {
                    continue;
                }

                Q_UNUSED(translateEndDepth);
                Q_UNUSED(scaleEndDepth);
                const QColor axisColor = viewportAxisColor(axis);
                const bool translatingAxis = m_isDraggingViewportHandle
                                             && m_dragHandleKind == ViewportHandleKind::SceneObject
                                             && m_dragHandleIndex == index
                                             && m_dragHandleAxis == axis
                                             && m_dragHandleOperation == ViewportHandleOperation::Translate;
                const bool inspectorTranslateAxis = m_inspectorHighlightKind == ViewportHandleKind::SceneObject
                                                    && m_inspectorHighlightOperation == ViewportHandleOperation::Translate
                                                    && m_inspectorHighlightAxis == axis;
                const bool scalingAxis = m_isDraggingViewportHandle
                                         && m_dragHandleKind == ViewportHandleKind::SceneObject
                                         && m_dragHandleIndex == index
                                         && m_dragHandleAxis == axis
                                         && m_dragHandleOperation == ViewportHandleOperation::Scale;
                const bool inspectorScaleAxis = m_inspectorHighlightKind == ViewportHandleKind::SceneObject
                                                && m_inspectorHighlightOperation == ViewportHandleOperation::Scale
                                                && m_inspectorHighlightAxis == axis;
                painter.setPen(QPen(axisColor, (translatingAxis || inspectorTranslateAxis) ? 3.0 : 2.0));
                painter.drawLine(screenPosition, translateEndScreenPosition);
                painter.setBrush(axisColor);
                painter.drawEllipse(translateEndScreenPosition,
                                    (translatingAxis || inspectorTranslateAxis) ? 6.5 : 5.0,
                                    (translatingAxis || inspectorTranslateAxis) ? 6.5 : 5.0);

                painter.setPen(QPen(axisColor, (scalingAxis || inspectorScaleAxis) ? 3.0 : 1.6, Qt::DashLine));
                painter.drawLine(translateEndScreenPosition, scaleEndScreenPosition);
                painter.setBrush(axisColor);
                const qreal squareRadius = (scalingAxis || inspectorScaleAxis) ? 7.0 : 5.5;
                painter.drawRect(QRectF(scaleEndScreenPosition.x() - squareRadius,
                                        scaleEndScreenPosition.y() - squareRadius,
                                        squareRadius * 2.0,
                                        squareRadius * 2.0));
            }

            const qreal ringRadii[3] = {32.0, 42.0, 52.0};
            const ViewportAxis ringAxes[3] = {ViewportAxis::X, ViewportAxis::Y, ViewportAxis::Z};
            for (int ringIndex = 0; ringIndex < 3; ++ringIndex) {
                const bool rotatingAxis = m_isDraggingViewportHandle
                                          && m_dragHandleKind == ViewportHandleKind::SceneObject
                                          && m_dragHandleIndex == index
                                          && m_dragHandleAxis == ringAxes[ringIndex]
                                          && m_dragHandleOperation == ViewportHandleOperation::Rotate;
                const bool inspectorRotateAxis = m_inspectorHighlightKind == ViewportHandleKind::SceneObject
                                                 && m_inspectorHighlightOperation == ViewportHandleOperation::Rotate
                                                 && m_inspectorHighlightAxis == ringAxes[ringIndex];
                painter.setPen(QPen(viewportAxisColor(ringAxes[ringIndex]), (rotatingAxis || inspectorRotateAxis) ? 3.0 : 1.6));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(screenPosition, ringRadii[ringIndex], ringRadii[ringIndex]);
            }
        }

        painter.setPen(QPen(accent, selected ? 2.2 : 1.4));
        painter.setBrush(fillColor);
        painter.drawEllipse(screenPosition, radius, radius);
        painter.drawLine(QPointF(screenPosition.x() - radius - 5.0, screenPosition.y()),
                         QPointF(screenPosition.x() + radius + 5.0, screenPosition.y()));
        painter.drawLine(QPointF(screenPosition.x(), screenPosition.y() - radius - 5.0),
                         QPointF(screenPosition.x(), screenPosition.y() + radius + 5.0));

        if (selected) {
            painter.setPen(QPen(selectedColor, 1.4));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screenPosition, radius + 5.0, radius + 5.0);
        }

        painter.setPen(QColor(244, 247, 250));
        painter.drawText(QRectF(screenPosition.x() + 12.0, screenPosition.y() - 10.0, 168.0, 20.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         sceneObjectName(index));
    }

    painter.restore();
}

void RasterWidget::drawLightGizmos(QPainter &painter) const
{
    if (m_lightingContext.directionalLights.empty()
        && m_lightingContext.pointLights.empty()
        && m_lightingContext.spotLights.empty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const Camera camera = m_renderer.camera();
    const Vec3f viewRight = cameraRight(camera);
    const Vec3f viewUp = cameraOrthoUp(camera);
    const QColor selectedColor(72, 196, 255);
    const QColor idleColor(255, 210, 102);
    const QColor disabledColor(132, 132, 132);

    for (int index = 0; index < directionalLightCount(); ++index) {
        const DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(index)];
        const bool selected = m_selectedLightKind == SelectedLightKind::Directional && m_selectedLightIndex == index;
        const QColor accent = !light.enabled ? disabledColor : (selected ? selectedColor : idleColor);
        const QRectF cardRect = directionalLightCardRect(index);

        painter.setPen(QPen(accent, selected ? 2.5 : 1.5));
        painter.setBrush(QColor(20, 28, 38, light.enabled ? 188 : 116));
        painter.drawRoundedRect(cardRect, 10.0, 10.0);

        const Vec3f lightVector = normalize(light.direction * -1.0f);
        Vec2f arrow2d{dot(lightVector, viewRight), dot(lightVector, viewUp)};
        const float arrowLength = std::sqrt(arrow2d.x * arrow2d.x + arrow2d.y * arrow2d.y);
        if (arrowLength <= 1e-5f)
            arrow2d = {0.0f, 1.0f};
        else
            arrow2d = {arrow2d.x / arrowLength, arrow2d.y / arrowLength};

        const QPointF center(cardRect.left() + 38.0, cardRect.center().y() + 4.0);
        const QPointF tip(center.x() + static_cast<qreal>(arrow2d.x) * 18.0,
                          center.y() - static_cast<qreal>(arrow2d.y) * 18.0);
        const QPointF leftWing(tip.x() - static_cast<qreal>(arrow2d.x) * 7.0 - static_cast<qreal>(arrow2d.y) * 5.0,
                               tip.y() + static_cast<qreal>(arrow2d.y) * 7.0 - static_cast<qreal>(arrow2d.x) * 5.0);
        const QPointF rightWing(tip.x() - static_cast<qreal>(arrow2d.x) * 7.0 + static_cast<qreal>(arrow2d.y) * 5.0,
                                tip.y() + static_cast<qreal>(arrow2d.y) * 7.0 + static_cast<qreal>(arrow2d.x) * 5.0);

        painter.drawLine(center, tip);
        painter.drawLine(tip, leftWing);
        painter.drawLine(tip, rightWing);

        painter.setPen(QColor(244, 247, 250));
        painter.drawText(QRectF(cardRect.left() + 58.0, cardRect.top() + 10.0, 96.0, 20.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         directionalLightName(index));
        painter.setPen(QColor(190, 198, 208));
        painter.drawText(QRectF(cardRect.left() + 58.0, cardRect.top() + 32.0, 96.0, 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         light.enabled ? QStringLiteral("方向光") : QStringLiteral("方向光（禁用）"));
    }

    if (m_selectedLightKind == SelectedLightKind::Directional
        && m_selectedLightIndex >= 0
        && m_selectedLightIndex < directionalLightCount()) {
        const DirectionalLight &light = m_lightingContext.directionalLights[static_cast<std::size_t>(m_selectedLightIndex)];
        const QRectF sphereRect = directionalLightSphereRect();
        const QPointF sphereCenter = sphereRect.center();
        const qreal sphereRadius = sphereRect.width() * 0.5;
        const Vec3f viewForward = cameraForward(camera);
        const Vec3f lightVector = normalize(light.direction * -1.0f);
        const Vec3f localVector{
            dot(lightVector, viewRight),
            dot(lightVector, viewUp),
            dot(lightVector, viewForward)
        };
        const QPointF handlePoint(sphereCenter.x() + static_cast<qreal>(localVector.x) * sphereRadius,
                                  sphereCenter.y() - static_cast<qreal>(localVector.y) * sphereRadius);
        const bool draggingDirection = m_isDraggingViewportHandle
                                       && m_dragHandleKind == ViewportHandleKind::DirectionalLight
                                       && m_dragHandleIndex == m_selectedLightIndex
                                       && m_dragHandleOperation == ViewportHandleOperation::Direction;
        const bool inspectorDirection = m_inspectorHighlightKind == ViewportHandleKind::DirectionalLight
                                        && m_inspectorHighlightOperation == ViewportHandleOperation::Direction;

        painter.setPen(QPen(selectedColor, (draggingDirection || inspectorDirection) ? 3.0 : 2.0));
        painter.setBrush(QColor(20, 28, 38, 196));
        painter.drawEllipse(sphereRect);
        painter.setPen(QPen(QColor(76, 86, 98), 1.0));
        painter.drawLine(QPointF(sphereRect.left(), sphereCenter.y()), QPointF(sphereRect.right(), sphereCenter.y()));
        painter.drawLine(QPointF(sphereCenter.x(), sphereRect.top()), QPointF(sphereCenter.x(), sphereRect.bottom()));
        painter.setPen(QPen(QColor(132, 144, 158), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(sphereRect.left() + 12.0, sphereRect.top() + 12.0, sphereRect.width() - 24.0, sphereRect.height() - 24.0));
        painter.setPen(QPen(idleColor, 2.0));
        painter.drawLine(sphereCenter, handlePoint);
        painter.setBrush(idleColor);
        painter.drawEllipse(handlePoint,
                            (draggingDirection || inspectorDirection) ? 7.0 : 5.5,
                            (draggingDirection || inspectorDirection) ? 7.0 : 5.5);
        painter.setPen(QColor(244, 247, 250));
        painter.drawText(QRectF(sphereRect.left(), sphereRect.bottom() + 6.0, sphereRect.width(), 18.0),
                         Qt::AlignCenter,
                         QStringLiteral("方向球面"));
    }

    for (int index = 0; index < pointLightCount(); ++index) {
        const PointLight &light = m_lightingContext.pointLights[static_cast<std::size_t>(index)];
        QPointF screenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(light.position, screenPosition, viewDepth))
            continue;

        const bool selected = m_selectedLightKind == SelectedLightKind::Point && m_selectedLightIndex == index;
        const QColor accent = !light.enabled ? disabledColor : (selected ? selectedColor : idleColor);
        const QColor fill(static_cast<int>(std::clamp(light.color.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                          static_cast<int>(std::clamp(light.color.y, 0.0f, 1.0f) * 255.0f + 0.5f),
                          static_cast<int>(std::clamp(light.color.z, 0.0f, 1.0f) * 255.0f + 0.5f),
                          light.enabled ? 220 : 120);
        const qreal radius = selected ? 9.0 : 6.5;

        if (selected) {
            const float axisLengthWorld = worldUnitsPerPixelAtViewDepth(m_renderer.camera(), viewDepth, height()) * 60.0f;
            for (const ViewportAxis axis : {ViewportAxis::X, ViewportAxis::Y, ViewportAxis::Z}) {
                const Vec3f axisDirection = gizmoAxisDirection(nullptr, axis);
                QPointF axisEndScreenPosition;
                float axisEndDepth = 0.0f;
                if (!projectViewportHandle(light.position + axisDirection * axisLengthWorld,
                                           axisEndScreenPosition,
                                           axisEndDepth)) {
                    continue;
                }

                Q_UNUSED(axisEndDepth);
                const bool axisDragging = m_isDraggingViewportHandle
                                          && m_dragHandleKind == ViewportHandleKind::PointLight
                                          && m_dragHandleIndex == index
                                          && m_dragHandleAxis == axis
                                          && m_dragHandleOperation == ViewportHandleOperation::Translate;
                const bool inspectorAxis = m_inspectorHighlightKind == ViewportHandleKind::PointLight
                                           && m_inspectorHighlightOperation == ViewportHandleOperation::Translate
                                           && m_inspectorHighlightAxis == axis;
                const QColor axisColor = viewportAxisColor(axis);
                painter.setPen(QPen(axisColor, (axisDragging || inspectorAxis) ? 3.0 : 2.0));
                painter.drawLine(screenPosition, axisEndScreenPosition);
                painter.setBrush(axisColor);
                painter.drawEllipse(axisEndScreenPosition,
                                    (axisDragging || inspectorAxis) ? 6.5 : 5.0,
                                    (axisDragging || inspectorAxis) ? 6.5 : 5.0);
            }
        }

        painter.setPen(QPen(accent, selected ? 2.5 : 1.5));
        painter.setBrush(fill);
        painter.drawEllipse(screenPosition, radius, radius);
        painter.drawLine(QPointF(screenPosition.x() - radius - 4.0, screenPosition.y()),
                         QPointF(screenPosition.x() + radius + 4.0, screenPosition.y()));
        painter.drawLine(QPointF(screenPosition.x(), screenPosition.y() - radius - 4.0),
                         QPointF(screenPosition.x(), screenPosition.y() + radius + 4.0));

        if (selected) {
            painter.setPen(QPen(selectedColor, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screenPosition, radius + 5.0, radius + 5.0);
        }

        painter.setPen(QColor(244, 247, 250));
        painter.drawText(QRectF(screenPosition.x() + 12.0, screenPosition.y() - 10.0, 140.0, 20.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         pointLightName(index));
    }

    for (int index = 0; index < spotLightCount(); ++index) {
        const SpotLight &light = m_lightingContext.spotLights[static_cast<std::size_t>(index)];
        QPointF screenPosition;
        float viewDepth = 0.0f;
        if (!projectViewportHandle(light.position, screenPosition, viewDepth))
            continue;

        const bool selected = m_selectedLightKind == SelectedLightKind::Spot && m_selectedLightIndex == index;
        const QColor accent = !light.enabled ? disabledColor : (selected ? selectedColor : idleColor);
        const QColor fill(static_cast<int>(std::clamp(light.color.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                          static_cast<int>(std::clamp(light.color.y, 0.0f, 1.0f) * 255.0f + 0.5f),
                          static_cast<int>(std::clamp(light.color.z, 0.0f, 1.0f) * 255.0f + 0.5f),
                          light.enabled ? 220 : 120);
        const qreal radius = selected ? 9.0 : 6.5;
        const float axisLengthWorld = worldUnitsPerPixelAtViewDepth(m_renderer.camera(), viewDepth, height()) * 60.0f;

        if (selected) {
            for (const ViewportAxis axis : {ViewportAxis::X, ViewportAxis::Y, ViewportAxis::Z}) {
                const Vec3f axisDirection = gizmoAxisDirection(nullptr, axis);
                QPointF axisEndScreenPosition;
                float axisEndDepth = 0.0f;
                if (!projectViewportHandle(light.position + axisDirection * axisLengthWorld,
                                           axisEndScreenPosition,
                                           axisEndDepth)) {
                    continue;
                }

                Q_UNUSED(axisEndDepth);
                const bool axisDragging = m_isDraggingViewportHandle
                                          && m_dragHandleKind == ViewportHandleKind::SpotLight
                                          && m_dragHandleIndex == index
                                          && m_dragHandleAxis == axis
                                          && m_dragHandleOperation == ViewportHandleOperation::Translate;
                const bool inspectorAxis = m_inspectorHighlightKind == ViewportHandleKind::SpotLight
                                           && m_inspectorHighlightOperation == ViewportHandleOperation::Translate
                                           && m_inspectorHighlightAxis == axis;
                const QColor axisColor = viewportAxisColor(axis);
                painter.setPen(QPen(axisColor, (axisDragging || inspectorAxis) ? 3.0 : 2.0));
                painter.drawLine(screenPosition, axisEndScreenPosition);
                painter.setBrush(axisColor);
                painter.drawEllipse(axisEndScreenPosition,
                                    (axisDragging || inspectorAxis) ? 6.5 : 5.0,
                                    (axisDragging || inspectorAxis) ? 6.5 : 5.0);
            }
        }

        QPointF directionTipScreenPosition;
        float directionTipDepth = 0.0f;
        const bool hasDirectionTip = projectViewportHandle(light.position + normalize(light.direction) * (axisLengthWorld * 1.2f),
                                                           directionTipScreenPosition,
                                                           directionTipDepth);
        const bool draggingDirection = m_isDraggingViewportHandle
                                       && m_dragHandleKind == ViewportHandleKind::SpotLight
                                       && m_dragHandleIndex == index
                                       && m_dragHandleOperation == ViewportHandleOperation::Direction;
        const bool inspectorDirection = m_inspectorHighlightKind == ViewportHandleKind::SpotLight
                                        && m_inspectorHighlightOperation == ViewportHandleOperation::Direction;

        if (hasDirectionTip) {
            Q_UNUSED(directionTipDepth);
            const QPointF screenDirection = directionTipScreenPosition - screenPosition;
            const qreal screenDirectionLength = std::sqrt(screenDirection.x() * screenDirection.x()
                                                          + screenDirection.y() * screenDirection.y());
            if (screenDirectionLength > 1e-4) {
                const QPointF directionUnit(screenDirection.x() / screenDirectionLength,
                                            screenDirection.y() / screenDirectionLength);
                const QPointF perpendicular(-directionUnit.y(), directionUnit.x());
                const qreal coneLength = std::min<qreal>(screenDirectionLength, 22.0);
                const qreal coneSpread = std::clamp(std::tan(std::clamp(light.outerConeDegrees, 1.0f, 89.5f)
                                                             * (kPi / 180.0f))
                                                        * coneLength * 0.32,
                                                    6.0,
                                                    26.0);
                const QPointF coneBaseCenter(screenPosition.x() + directionUnit.x() * coneLength,
                                             screenPosition.y() + directionUnit.y() * coneLength);
                const QPointF coneLeft(coneBaseCenter.x() + perpendicular.x() * coneSpread,
                                       coneBaseCenter.y() + perpendicular.y() * coneSpread);
                const QPointF coneRight(coneBaseCenter.x() - perpendicular.x() * coneSpread,
                                        coneBaseCenter.y() - perpendicular.y() * coneSpread);

                painter.setPen(QPen(accent, (draggingDirection || inspectorDirection) ? 3.0 : 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(screenPosition, directionTipScreenPosition);
                painter.drawLine(screenPosition, coneLeft);
                painter.drawLine(screenPosition, coneRight);
                painter.drawLine(coneLeft, directionTipScreenPosition);
                painter.drawLine(coneRight, directionTipScreenPosition);
                painter.setBrush(accent);
                painter.drawEllipse(directionTipScreenPosition,
                                    (draggingDirection || inspectorDirection) ? 7.0 : 5.5,
                                    (draggingDirection || inspectorDirection) ? 7.0 : 5.5);
            }
        }

        painter.setPen(QPen(accent, selected ? 2.5 : 1.5));
        painter.setBrush(fill);
        painter.drawEllipse(screenPosition, radius, radius);
        painter.drawLine(QPointF(screenPosition.x() - radius - 4.0, screenPosition.y()),
                         QPointF(screenPosition.x() + radius + 4.0, screenPosition.y()));
        painter.drawLine(QPointF(screenPosition.x(), screenPosition.y() - radius - 4.0),
                         QPointF(screenPosition.x(), screenPosition.y() + radius + 4.0));

        if (selected) {
            painter.setPen(QPen(selectedColor, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(screenPosition, radius + 5.0, radius + 5.0);
        }

        painter.setPen(QColor(244, 247, 250));
        painter.drawText(QRectF(screenPosition.x() + 12.0, screenPosition.y() - 10.0, 160.0, 20.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         spotLightName(index));
    }

    painter.restore();
}

void RasterWidget::drawCameraOverlay(QPainter &painter) const
{
    if (m_isLineArtMode)
        return;

    const Camera &camera = m_renderer.camera();
    const Vec3f forward = cameraForward(camera);
    const QString projectionSummary = camera.projectionMode == CameraProjectionMode::Orthographic
        ? QStringLiteral("%1  高度 %2").arg(cameraProjectionModeName(camera.projectionMode)).arg(camera.orthographicHeight, 0, 'f', 2)
        : QStringLiteral("%1  FOV %2°").arg(cameraProjectionModeName(camera.projectionMode)).arg(camera.verticalFovRadians * 180.0f / kPi, 0, 'f', 1);
    const QString clipSummary = QStringLiteral("Near %1  Far %2  速度 %3")
        .arg(camera.nearPlane, 0, 'f', 3)
        .arg(camera.farPlane, 0, 'f', 2)
        .arg(m_cameraMoveSpeed, 0, 'f', 2);
    const QString positionSummary = QStringLiteral("Pos %1, %2, %3")
        .arg(camera.position.x, 0, 'f', 2)
        .arg(camera.position.y, 0, 'f', 2)
        .arg(camera.position.z, 0, 'f', 2);
    const QString directionSummary = QStringLiteral("Dir %1, %2, %3")
        .arg(forward.x, 0, 'f', 2)
        .arg(forward.y, 0, 'f', 2)
        .arg(forward.z, 0, 'f', 2);

    const QStringList stateLines{
        QStringLiteral("相机状态"),
        projectionSummary,
        clipSummary,
        positionSummary,
        directionSummary
    };
    const QStringList shortcutLines{
        QStringLiteral("快捷键"),
        QStringLiteral("左键 轨道旋转"),
        QStringLiteral("中键 平移"),
        QStringLiteral("右键 看向"),
        QStringLiteral("滚轮 缩放"),
        QStringLiteral("WASD/QE 自由飞行"),
        QStringLiteral("Shift 吸附  Ctrl 细调"),
        QStringLiteral("飞行模式 Shift 加速")
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QFontMetrics metrics(painter.font());
    const auto blockRectForLines = [&metrics](const QStringList &lines) {
        int maxWidth = 0;
        for (const QString &line : lines)
            maxWidth = std::max(maxWidth, metrics.horizontalAdvance(line));
        return QSize(maxWidth + 28, metrics.height() * lines.size() + 22);
    };

    const QSize stateSize = blockRectForLines(stateLines);
    const QSize shortcutSize = blockRectForLines(shortcutLines);
    const QRect stateRect(16, height() - stateSize.height() - 16, stateSize.width(), stateSize.height());
    const QRect shortcutRect(width() - shortcutSize.width() - 16,
                             height() - shortcutSize.height() - 16,
                             shortcutSize.width(),
                             shortcutSize.height());

    const auto drawBlock = [&painter, &metrics](const QRect &rect, const QStringList &lines) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(16, 22, 30, 184));
        painter.drawRoundedRect(rect, 10.0, 10.0);

        for (int i = 0; i < lines.size(); ++i) {
            painter.setPen(i == 0 ? QColor(244, 247, 250) : QColor(196, 204, 214));
            painter.drawText(QRect(rect.left() + 14,
                                   rect.top() + 12 + i * metrics.height(),
                                   rect.width() - 28,
                                   metrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             lines[i]);
        }
    };

    drawBlock(stateRect, stateLines);
    drawBlock(shortcutRect, shortcutLines);
    painter.restore();
}

void RasterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QImage image = buildViewportPresentationImage(false);
    if (!image.isNull())
        painter.drawImage(rect(), image);
    if (!m_isLineArtMode) {
        drawSceneObjectGizmos(painter);
        drawLightGizmos(painter);
        drawCameraOverlay(painter);
    }
}

void RasterWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // framebuffer 和窗口保持同尺寸，避免额外缩放逻辑。
    m_renderer.resize(std::max(1, width()), std::max(1, height()));
    requestRender();
}

void RasterWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_isLineArtMode) {
        if (m_lineArtComparePreview
            && !m_lineArtSourceImage.isNull()
            && event->button() == Qt::LeftButton
            && !m_lineArtImage.isNull()) {
            const QRect targetRect = fitImageRectForArea(m_lineArtImage.size(), rect().adjusted(20, 20, -20, -20));
            if (targetRect.contains(event->pos())) {
                m_isDraggingLineArtSplit = true;
                const float split = static_cast<float>(event->pos().x() - targetRect.left())
                                    / static_cast<float>(std::max(1, targetRect.width() - 1));
                m_lineArtCompareSplit = std::clamp(split, 0.0f, 1.0f);
                update();
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus(Qt::MouseFocusReason);

    if (event->button() == Qt::LeftButton) {
        const ViewportHandleHit hit = pickViewportHandle(event->pos());
        if (hit.kind == ViewportHandleKind::SceneObject) {
            setSelectedSceneObjectIndex(hit.index);
            if (hit.operation != ViewportHandleOperation::None && beginViewportHandleDrag(hit, event->pos()))
                setCursor(Qt::SizeAllCursor);
            event->accept();
            return;
        }
        if (hit.kind == ViewportHandleKind::PointLight) {
            setSelectedLightSelection(SelectedLightKind::Point, hit.index);
            emit lightPicked(static_cast<int>(SelectedLightKind::Point), hit.index);
            if (hit.operation != ViewportHandleOperation::None && beginViewportHandleDrag(hit, event->pos()))
                setCursor(Qt::SizeAllCursor);
            event->accept();
            return;
        }
        if (hit.kind == ViewportHandleKind::SpotLight) {
            setSelectedLightSelection(SelectedLightKind::Spot, hit.index);
            emit lightPicked(static_cast<int>(SelectedLightKind::Spot), hit.index);
            if (hit.operation != ViewportHandleOperation::None && beginViewportHandleDrag(hit, event->pos()))
                setCursor(Qt::SizeAllCursor);
            event->accept();
            return;
        }
        if (hit.kind == ViewportHandleKind::DirectionalLight) {
            setSelectedLightSelection(SelectedLightKind::Directional, hit.index);
            emit lightPicked(static_cast<int>(SelectedLightKind::Directional), hit.index);
            if (hit.operation == ViewportHandleOperation::Direction && beginViewportHandleDrag(hit, event->pos()))
                setCursor(Qt::SizeAllCursor);
            event->accept();
            return;
        }

        m_isOrbiting = true;
        m_lastMousePosition = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        m_isFreeLooking = true;
        m_lastMousePosition = event->pos();
        setCursor(Qt::BlankCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastMousePosition = event->pos();
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void RasterWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isLineArtMode) {
        if (m_isDraggingLineArtSplit && !m_lineArtImage.isNull()) {
            const QRect targetRect = fitImageRectForArea(m_lineArtImage.size(), rect().adjusted(20, 20, -20, -20));
            const float split = static_cast<float>(event->pos().x() - targetRect.left())
                                / static_cast<float>(std::max(1, targetRect.width() - 1));
            m_lineArtCompareSplit = std::clamp(split, 0.0f, 1.0f);
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_isDraggingViewportHandle) {
        if (updateViewportHandleDrag(event->pos())) {
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_isOrbiting && !m_isPanning && !m_isFreeLooking) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint currentPosition = event->pos();
    const QPoint delta = currentPosition - m_lastMousePosition;
    m_lastMousePosition = currentPosition;

    Camera camera = m_renderer.camera();
    bool didChangeCamera = false;
    if (m_isOrbiting && (delta.x() != 0 || delta.y() != 0)) {
        orbitCamera(camera,
                    static_cast<float>(-delta.x()) * 0.01f,
                    static_cast<float>(-delta.y()) * 0.01f);
        didChangeCamera = true;
    }
    if (m_isPanning && (delta.x() != 0 || delta.y() != 0)) {
        panCamera(camera,
                  static_cast<float>(delta.x()),
                  static_cast<float>(delta.y()),
                  height());
        didChangeCamera = true;
    }
    if (m_isFreeLooking && (delta.x() != 0 || delta.y() != 0)) {
        freeLookCamera(camera,
                       static_cast<float>(-delta.x()) * 0.0085f,
                       static_cast<float>(-delta.y()) * 0.0085f);
        didChangeCamera = true;
    }

    if (didChangeCamera) {
        m_renderer.setCamera(camera);
        m_scenePreset = ScenePreset::Custom;
        emit cameraChanged();
        requestRender();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void RasterWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_isLineArtMode) {
        if (event->button() == Qt::LeftButton)
            m_isDraggingLineArtSplit = false;
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton && m_isDraggingViewportHandle)
        endViewportHandleDrag();
    if (event->button() == Qt::LeftButton)
        m_isOrbiting = false;
    if (event->button() == Qt::RightButton)
        m_isFreeLooking = false;
    if (event->button() == Qt::MiddleButton)
        m_isPanning = false;
    if (!m_isOrbiting && !m_isPanning && !m_isFreeLooking)
        unsetCursor();

    QWidget::mouseReleaseEvent(event);
}

void RasterWidget::wheelEvent(QWheelEvent *event)
{
    if (m_isLineArtMode) {
        QWidget::wheelEvent(event);
        return;
    }

    const QPoint angleDelta = event->angleDelta();
    if (angleDelta.y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    Camera camera = m_renderer.camera();
    dollyCamera(camera, static_cast<float>(angleDelta.y()) / 120.0f);
    m_renderer.setCamera(camera);
    m_scenePreset = ScenePreset::Custom;
    emit cameraChanged();
    requestRender();
    event->accept();
}

void RasterWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_isLineArtMode) {
        QWidget::keyPressEvent(event);
        return;
    }

    bool handled = true;
    switch (event->key()) {
    case Qt::Key_W:
        m_moveForward = true;
        break;
    case Qt::Key_S:
        m_moveBackward = true;
        break;
    case Qt::Key_A:
        m_moveLeft = true;
        break;
    case Qt::Key_D:
        m_moveRight = true;
        break;
    case Qt::Key_Q:
        m_moveDown = true;
        break;
    case Qt::Key_E:
        m_moveUp = true;
        break;
    case Qt::Key_Shift:
        m_moveFast = true;
        break;
    case Qt::Key_Control:
        m_gizmoFineTune = true;
        break;
    default:
        handled = false;
        break;
    }

    if (handled) {
        updateRenderLoopState();
        requestRender();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RasterWidget::keyReleaseEvent(QKeyEvent *event)
{
    bool handled = true;
    switch (event->key()) {
    case Qt::Key_W:
        m_moveForward = false;
        break;
    case Qt::Key_S:
        m_moveBackward = false;
        break;
    case Qt::Key_A:
        m_moveLeft = false;
        break;
    case Qt::Key_D:
        m_moveRight = false;
        break;
    case Qt::Key_Q:
        m_moveDown = false;
        break;
    case Qt::Key_E:
        m_moveUp = false;
        break;
    case Qt::Key_Shift:
        m_moveFast = false;
        break;
    case Qt::Key_Control:
        m_gizmoFineTune = false;
        break;
    default:
        handled = false;
        break;
    }

    if (handled) {
        updateRenderLoopState();
        requestRender();
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}

void RasterWidget::focusOutEvent(QFocusEvent *event)
{
    clearCameraMoveState();
    QWidget::focusOutEvent(event);
}

void RasterWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateRenderLoopState();
    requestRender();
}

void RasterWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateRenderLoopState();
}

void RasterWidget::renderFrame()
{
    if (m_isLineArtMode || !isVisible())
        return;

    if (!renderSceneNow(false))
        return;

    emit frameReady(m_renderer.stats());
    updateRenderLoopState();
    update();
}
