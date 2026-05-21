#include "SoftwareRenderer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

// 只在 demo 相机设置里用到的圆周率常量。
constexpr float kPi = 3.14159265358979323846f;

// 裁剪平面容差，避免数值误差导致边界顶点来回抖动。
constexpr float kClipEpsilon = 1e-5f;

// 固定 4x MSAA 样本布局，均匀分布在像素四个象限中心。
constexpr std::array<Vec2f, 4> kMsaaSampleOffsets = {{
    {0.25f, 0.25f},
    {0.75f, 0.25f},
    {0.25f, 0.75f},
    {0.75f, 0.75f}
}};

float wrapUnit(float value)
{
    const float wrapped = value - std::floor(value);
    return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
}

float clampUnit(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float resolveAddress(float value, AddressMode mode)//解析寻址模式
{
    return mode == AddressMode::Wrap ? wrapUnit(value) : clampUnit(value);
}

int wrapIndex(int value, int size)//平铺下标
{
    const int mod = value % size;
    return mod < 0 ? mod + size : mod;
}

int clampIndex(int value, int size)//钳位下标
{
    return std::clamp(value, 0, std::max(0, size - 1));
}

int resolveIndex(int value, int size, AddressMode mode)//解析下标
{
    return mode == AddressMode::Wrap ? wrapIndex(value, size) : clampIndex(value, size);
}

Vec3f unpackColor(std::uint32_t packedColor)//解包颜色
{
    const float r = static_cast<float>((packedColor >> 16) & 0xffu) / 255.0f;
    const float g = static_cast<float>((packedColor >> 8) & 0xffu) / 255.0f;
    const float b = static_cast<float>(packedColor & 0xffu) / 255.0f;
    return {r, g, b};
}

Vec4f unpackColorAlpha(std::uint32_t packedColor)//解包颜色透明度
{
    const float a = static_cast<float>((packedColor >> 24) & 0xffu) / 255.0f;
    const Vec3f rgb = unpackColor(packedColor);
    return {rgb.x, rgb.y, rgb.z, a};
}

std::uint32_t packColorUnorm(const Vec4f &color)
{
    const Vec3f rgb = clamp01({color.x, color.y, color.z});
    const float alpha = std::clamp(color.w, 0.0f, 1.0f);
    const std::uint32_t a = static_cast<std::uint32_t>(alpha * 255.0f + 0.5f);
    const std::uint32_t r = static_cast<std::uint32_t>(rgb.x * 255.0f + 0.5f);
    const std::uint32_t g = static_cast<std::uint32_t>(rgb.y * 255.0f + 0.5f);
    const std::uint32_t b = static_cast<std::uint32_t>(rgb.z * 255.0f + 0.5f);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

std::uint64_t hashFloatBits(float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return static_cast<std::uint64_t>(bits);
}

float squaredDistance(const Vec3f &lhs, const Vec3f &rhs)
{
    const Vec3f delta = lhs - rhs;
    return dot(delta, delta);
}

float squaredDistancePointToSegment(const Vec2f &point, const Vec2f &segmentStart, const Vec2f &segmentEnd, float &segmentT)
{
    const Vec2f segment = segmentEnd - segmentStart;
    const float segmentLengthSquared = segment.x * segment.x + segment.y * segment.y;
    if (segmentLengthSquared <= 1e-8f) {
        segmentT = 0.0f;
        const Vec2f delta = point - segmentStart;
        return delta.x * delta.x + delta.y * delta.y;
    }

    const Vec2f pointDelta = point - segmentStart;
    segmentT = std::clamp((pointDelta.x * segment.x + pointDelta.y * segment.y) / segmentLengthSquared, 0.0f, 1.0f);
    const Vec2f closestPoint = segmentStart + segment * segmentT;
    const Vec2f delta = point - closestPoint;
    return delta.x * delta.x + delta.y * delta.y;
}

Vec3f modulate(const Vec3f &lhs, const Vec3f &rhs)//调制：纹理颜色* 光源颜色
{
    return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

Vec4f blendSourceAlpha(const Vec4f &source, const Vec4f &destination)//源 Alpha 混合。
{
    const float sourceAlpha = std::clamp(source.w, 0.0f, 1.0f);
    const float destinationAlpha = std::clamp(destination.w, 0.0f, 1.0f);
    const float outAlpha = sourceAlpha + destinationAlpha * (1.0f - sourceAlpha);

    return {
        source.x * sourceAlpha + destination.x * (1.0f - sourceAlpha),
        source.y * sourceAlpha + destination.y * (1.0f - sourceAlpha),
        source.z * sourceAlpha + destination.z * (1.0f - sourceAlpha),
        outAlpha
    };
}

Vec4f lerp4(const Vec4f &lhs, const Vec4f &rhs, float t)
{
    return {
        lhs.x * (1.0f - t) + rhs.x * t,
        lhs.y * (1.0f - t) + rhs.y * t,
        lhs.z * (1.0f - t) + rhs.z * t,
        lhs.w * (1.0f - t) + rhs.w * t
    };
}

int nearestWrapIndex(float uv, int size)
{
    const float wrapped = wrapUnit(uv);
    return std::min(size - 1, static_cast<int>(wrapped * static_cast<float>(size)));
}

int nearestClampIndex(float uv, int size)
{
    const float clamped = clampUnit(uv);
    return std::min(size - 1, static_cast<int>(clamped * static_cast<float>(size)));
}

const TextureMipLevel *textureLevelOrNull(const Texture2D &texture, int level)
{
    if (!texture.mipLevels.empty()) {
        const int clampedLevel = std::clamp(level, 0, static_cast<int>(texture.mipLevels.size()) - 1);
        return &texture.mipLevels[static_cast<std::size_t>(clampedLevel)];
    }

    if (!texture.isValid())
        return nullptr;

    return nullptr;
}

TextureMipLevel makeBaseMipLevel(const Texture2D &texture)
{
    TextureMipLevel base;
    base.width = texture.width;
    base.height = texture.height;
    base.texels = texture.texels;
    return base;
}

const TextureMipLevel &effectiveTextureLevel(const Texture2D &texture, int level, TextureMipLevel &temporaryBase)
{
    const TextureMipLevel *mipLevel = textureLevelOrNull(texture, level);
    if (mipLevel != nullptr)
        return *mipLevel;

    temporaryBase = makeBaseMipLevel(texture);
    return temporaryBase;
}

std::uint32_t fetchTexelWrapWrap(const TextureMipLevel &texture, int x, int y)
{
    const int wrappedX = wrapIndex(x, texture.width);
    const int wrappedY = wrapIndex(y, texture.height);
    return texture.texels[static_cast<std::size_t>(wrappedY) * static_cast<std::size_t>(texture.width)
                          + static_cast<std::size_t>(wrappedX)];
}

std::uint32_t fetchTexelClampClamp(const TextureMipLevel &texture, int x, int y)
{
    const int clampedX = clampIndex(x, texture.width);
    const int clampedY = clampIndex(y, texture.height);
    return texture.texels[static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(texture.width)
                          + static_cast<std::size_t>(clampedX)];
}

std::uint32_t fetchTexelAddress(const TextureMipLevel &texture, int x, int y, AddressMode addressU, AddressMode addressV)
{
    const int resolvedX = resolveIndex(x, texture.width, addressU);
    const int resolvedY = resolveIndex(y, texture.height, addressV);
    return texture.texels[static_cast<std::size_t>(resolvedY) * static_cast<std::size_t>(texture.width)
                          + static_cast<std::size_t>(resolvedX)];
}

Vec4f sampleColorNearestFast(const TextureMipLevel &texture, const Vec2f &uv, const SamplerState &sampler)
{
    const int x = sampler.addressU == AddressMode::Wrap
        ? nearestWrapIndex(uv.x, texture.width)
        : nearestClampIndex(uv.x, texture.width);
    const int y = sampler.addressV == AddressMode::Wrap
        ? nearestWrapIndex(uv.y, texture.height)
        : nearestClampIndex(uv.y, texture.height);
    return unpackColorAlpha(texture.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width)
                                           + static_cast<std::size_t>(x)]);
}

Vec4f sampleColorBilinearFast(const TextureMipLevel &texture, const Vec2f &uv, const SamplerState &sampler)
{
    const float resolvedU = sampler.addressU == AddressMode::Wrap ? wrapUnit(uv.x) : clampUnit(uv.x);
    const float resolvedV = sampler.addressV == AddressMode::Wrap ? wrapUnit(uv.y) : clampUnit(uv.y);
    const float u = resolvedU * static_cast<float>(texture.width) - 0.5f;
    const float v = resolvedV * static_cast<float>(texture.height) - 0.5f;

    const float floorU = std::floor(u);
    const float floorV = std::floor(v);
    const int x0 = static_cast<int>(floorU);
    const int y0 = static_cast<int>(floorV);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = u - floorU;
    const float ty = v - floorV;

    Vec4f c00;
    Vec4f c10;
    Vec4f c01;
    Vec4f c11;
    if (sampler.addressU == AddressMode::Wrap && sampler.addressV == AddressMode::Wrap) {
        c00 = unpackColorAlpha(fetchTexelWrapWrap(texture, x0, y0));
        c10 = unpackColorAlpha(fetchTexelWrapWrap(texture, x1, y0));
        c01 = unpackColorAlpha(fetchTexelWrapWrap(texture, x0, y1));
        c11 = unpackColorAlpha(fetchTexelWrapWrap(texture, x1, y1));
    } else if (sampler.addressU == AddressMode::Clamp && sampler.addressV == AddressMode::Clamp) {
        c00 = unpackColorAlpha(fetchTexelClampClamp(texture, x0, y0));
        c10 = unpackColorAlpha(fetchTexelClampClamp(texture, x1, y0));
        c01 = unpackColorAlpha(fetchTexelClampClamp(texture, x0, y1));
        c11 = unpackColorAlpha(fetchTexelClampClamp(texture, x1, y1));
    } else {
        c00 = unpackColorAlpha(fetchTexelAddress(texture, x0, y0, sampler.addressU, sampler.addressV));
        c10 = unpackColorAlpha(fetchTexelAddress(texture, x1, y0, sampler.addressU, sampler.addressV));
        c01 = unpackColorAlpha(fetchTexelAddress(texture, x0, y1, sampler.addressU, sampler.addressV));
        c11 = unpackColorAlpha(fetchTexelAddress(texture, x1, y1, sampler.addressU, sampler.addressV));
    }

    const Vec4f top = lerp4(c00, c10, tx);
    const Vec4f bottom = lerp4(c01, c11, tx);
    return lerp4(top, bottom, ty);
}

Vec4f sampleColorAtMipLevel(const Texture2D &texture, const Vec2f &uv, const SamplerState &sampler, int level)
{
    TextureMipLevel temporaryBase;
    const TextureMipLevel &mipLevel = effectiveTextureLevel(texture, level, temporaryBase);
    if (sampler.filter == TextureFilter::Nearest)
        return sampleColorNearestFast(mipLevel, uv, sampler);
    return sampleColorBilinearFast(mipLevel, uv, sampler);
}

VertexOutput defaultVertexShaderImpl(const VertexInput &vertex,
                                     const VertexShaderContext &context,
                                     const Material &material)//默认顶点着色器
{
    (void)material;
    const Vec3f worldPosition = transformPoint(context.modelTransform, vertex.position);
    return {
        context.transform * Vec4f{vertex.position.x, vertex.position.y, vertex.position.z, 1.0f},
        vertex.color,
        transformNormal(context.modelTransform, vertex.normal),
        vertex.uv,
        worldPosition,
        normalize(context.cameraWorldPosition - worldPosition)
    };
}

struct SurfaceShadingData {
    Vec3f baseColor;
    float alpha = 1.0f;
    Vec3f worldNormal;
    Vec3f worldPos;
    Vec3f viewDir;
    float metallic = 0.0f;
    float roughness = 0.55f;
};

bool materialUsesTexture(MaterialType type)
{
    return type == MaterialType::LambertTextured
        || type == MaterialType::UnlitTextured
        || type == MaterialType::BlinnPhongTextured
        || type == MaterialType::PbrTextured;
}

bool materialUsesVertexColor(MaterialType type)
{
    return type == MaterialType::LambertVertexColor
        || type == MaterialType::UnlitVertexColor
        || type == MaterialType::BlinnPhongVertexColor
        || type == MaterialType::PbrVertexColor;
}

SurfaceShadingData buildSurfaceShadingData(const FragmentInput &fragment,
                                           const Material &material,
                                           const Vec4f &textureColor,
                                           const Vec4f &metallicRoughnessSample)
{
    SurfaceShadingData surface;
    surface.baseColor = fragment.color;
    if (!materialUsesVertexColor(material.type))
        surface.baseColor = clamp01(surface.baseColor);
    if (materialUsesTexture(material.type)) {
        surface.baseColor = modulate(surface.baseColor, {textureColor.x, textureColor.y, textureColor.z});
        surface.alpha = textureColor.w;
    }
    surface.worldNormal = normalize(fragment.normal);
    surface.worldPos = fragment.worldPos;
    surface.viewDir = normalize(fragment.viewDir);
    surface.metallic = std::clamp(material.metallic * metallicRoughnessSample.z, 0.0f, 1.0f);
    surface.roughness = std::clamp(material.roughness * metallicRoughnessSample.y, 0.045f, 1.0f);
    return surface;
}

Vec3f buildFallbackTangent(const Vec3f &normal)
{
    const Vec3f reference = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{0.0f, 1.0f, 0.0f};
    const Vec3f tangent = cross(reference, normal);
    return length(tangent) > 1e-6f ? normalize(tangent) : Vec3f{1.0f, 0.0f, 0.0f};
}

Vec3f applyNormalMap(const FragmentInput &fragment, const Material &material, const Vec3f &geometricNormal)
{
    if (!material.normalTexture.isValid())
        return geometricNormal;

    const Vec4f normalSample = material.normalTexture.sampleColor(fragment.uv, material.normalSampler, fragment.textureLod);
    const Vec3f tangentNormal = normalize(Vec3f{
        normalSample.x * 2.0f - 1.0f,
        normalSample.y * 2.0f - 1.0f,
        normalSample.z * 2.0f - 1.0f
    });

    Vec3f tangent = fragment.tangent;
    if (length(tangent) <= 1e-6f)
        tangent = buildFallbackTangent(geometricNormal);
    else
        tangent = normalize(tangent - geometricNormal * dot(tangent, geometricNormal));

    Vec3f bitangent = fragment.bitangent;
    if (length(bitangent) <= 1e-6f)
        bitangent = normalize(cross(geometricNormal, tangent));
    else
        bitangent = normalize(bitangent);

    const Vec3f mappedNormal = normalize(
        tangent * tangentNormal.x
        + bitangent * tangentNormal.y
        + geometricNormal * tangentNormal.z);
    const float strength = std::clamp(material.normalStrength, 0.0f, 2.0f);
    return normalize(geometricNormal * (1.0f - strength) + mappedNormal * strength);
}

std::shared_ptr<const ShadowMap> directionalShadowMapAt(const LightingContext &lighting, std::size_t index)
{
    if (index >= lighting.directionalShadowMaps.size())
        return nullptr;
    return lighting.directionalShadowMaps[index];
}

std::shared_ptr<const PointShadowMap> pointShadowMapAt(const LightingContext &lighting, std::size_t index)
{
    if (index >= lighting.pointShadowMaps.size())
        return nullptr;
    return lighting.pointShadowMaps[index];
}

std::shared_ptr<const ShadowMap> spotShadowMapAt(const LightingContext &lighting, std::size_t index)
{
    if (index >= lighting.spotShadowMaps.size())
        return nullptr;
    return lighting.spotShadowMaps[index];
}

int shadowKernelRadius(ShadowFilterQuality quality)
{
    switch (quality) {
    case ShadowFilterQuality::Hard:
        return 0;
    case ShadowFilterQuality::Pcf5x5:
        return 2;
    case ShadowFilterQuality::Pcf3x3:
    default:
        return 1;
    }
}

float computeShadowVisibility(const ShadowMap &shadowMap,
                              const Vec2f &shadowUv,
                              float comparisonDepth,
                              ShadowFilterQuality quality,
                              float shadowStrength)
{
    if (!shadowMap.isValid())
        return 1.0f;

    const int kernelRadius = shadowKernelRadius(quality);
    float occlusion = 0.0f;
    int sampleCount = 0;
    for (int offsetY = -kernelRadius; offsetY <= kernelRadius; ++offsetY) {
        for (int offsetX = -kernelRadius; offsetX <= kernelRadius; ++offsetX) {
            const Vec2f offsetUv{
                shadowUv.x + static_cast<float>(offsetX) / static_cast<float>(std::max(1, shadowMap.width)),
                shadowUv.y + static_cast<float>(offsetY) / static_cast<float>(std::max(1, shadowMap.height))
            };
            const float storedDepth = shadowMap.sampleDepth(offsetUv);
            occlusion += comparisonDepth > storedDepth ? 1.0f : 0.0f;
            ++sampleCount;
        }
    }

    const float shadowAmount = sampleCount > 0 ? occlusion / static_cast<float>(sampleCount) : 0.0f;
    return 1.0f - shadowAmount * std::clamp(shadowStrength, 0.0f, 1.0f);
}

bool projectShadowSample(const ShadowMap &shadowMap, const Vec3f &worldPos, Vec2f &shadowUv, float &receiverDepth)
{
    const Vec4f lightClip = shadowMap.lightTransform * Vec4f{worldPos.x, worldPos.y, worldPos.z, 1.0f};
    if (std::fabs(lightClip.w) <= 1e-6f)
        return false;

    const float inverseW = 1.0f / lightClip.w;
    const Vec3f lightNdc{lightClip.x * inverseW, lightClip.y * inverseW, lightClip.z * inverseW};
    if (lightNdc.x < -1.0f || lightNdc.x > 1.0f || lightNdc.y < -1.0f || lightNdc.y > 1.0f || lightNdc.z < -1.0f || lightNdc.z > 1.0f)
        return false;

    shadowUv = {lightNdc.x * 0.5f + 0.5f, lightNdc.y * 0.5f + 0.5f};
    receiverDepth = lightNdc.z * 0.5f + 0.5f;
    return true;
}

bool projectPointShadowSample(const PointShadowMap &shadowMap,
                              const Vec3f &worldPos,
                              const ShadowMap *&faceMap,
                              Vec2f &shadowUv,
                              float &receiverDepth)
{
    const Vec3f offset = worldPos - shadowMap.lightPosition;
    const float absX = std::fabs(offset.x);
    const float absY = std::fabs(offset.y);
    const float absZ = std::fabs(offset.z);
    int faceIndex = 0;
    if (absX >= absY && absX >= absZ)
        faceIndex = offset.x >= 0.0f ? 0 : 1;
    else if (absY >= absX && absY >= absZ)
        faceIndex = offset.y >= 0.0f ? 2 : 3;
    else
        faceIndex = offset.z >= 0.0f ? 4 : 5;

    faceMap = &shadowMap.faces[static_cast<std::size_t>(faceIndex)];
    return projectShadowSample(*faceMap, worldPos, shadowUv, receiverDepth);
}

float computeDirectionalShadowFactor(const FragmentInput &fragment,
                                     const Vec3f &normal,
                                     const DirectionalLight &light,
                                     const std::shared_ptr<const ShadowMap> &shadowMap,
                                     bool receiveShadow)
{
    if (!receiveShadow || !light.castShadow || shadowMap == nullptr || !shadowMap->isValid())
        return 1.0f;

    Vec2f shadowUv;
    float receiverDepth = 1.0f;
    if (!projectShadowSample(*shadowMap, fragment.worldPos, shadowUv, receiverDepth))
        return 1.0f;

    const Vec3f lightDirection = normalize(light.direction * -1.0f);
    const float angleBias = (1.0f - std::max(0.0f, dot(normal, lightDirection))) * light.shadowBias;
    const float comparisonDepth = receiverDepth - std::max(light.shadowBias, angleBias);
    return computeShadowVisibility(*shadowMap,
                                   shadowUv,
                                   comparisonDepth,
                                   light.shadowFilterQuality,
                                   light.shadowStrength);
}

float computePointShadowFactor(const FragmentInput &fragment,
                               const Vec3f &normal,
                               const PointLight &light,
                               const std::shared_ptr<const PointShadowMap> &shadowMap,
                               bool receiveShadow)
{
    if (!receiveShadow || !light.castShadow || shadowMap == nullptr || !shadowMap->isValid())
        return 1.0f;

    const ShadowMap *faceMap = nullptr;
    Vec2f shadowUv;
    float receiverDepth = 1.0f;
    if (!projectPointShadowSample(*shadowMap, fragment.worldPos, faceMap, shadowUv, receiverDepth) || faceMap == nullptr)
        return 1.0f;

    const Vec3f lightDirection = normalize(light.position - fragment.worldPos);
    const float angleBias = (1.0f - std::max(0.0f, dot(normal, lightDirection))) * light.shadowBias;
    const float comparisonDepth = receiverDepth - std::max(light.shadowBias, angleBias);
    return computeShadowVisibility(*faceMap,
                                   shadowUv,
                                   comparisonDepth,
                                   light.shadowFilterQuality,
                                   light.shadowStrength);
}

float computeSpotShadowFactor(const FragmentInput &fragment,
                              const Vec3f &normal,
                              const SpotLight &light,
                              const std::shared_ptr<const ShadowMap> &shadowMap,
                              bool receiveShadow)
{
    if (!receiveShadow || !light.castShadow || shadowMap == nullptr || !shadowMap->isValid())
        return 1.0f;

    Vec2f shadowUv;
    float receiverDepth = 1.0f;
    if (!projectShadowSample(*shadowMap, fragment.worldPos, shadowUv, receiverDepth))
        return 1.0f;

    const Vec3f lightDirection = normalize(light.position - fragment.worldPos);
    const float angleBias = (1.0f - std::max(0.0f, dot(normal, lightDirection))) * light.shadowBias;
    const float comparisonDepth = receiverDepth - std::max(light.shadowBias, angleBias);
    return computeShadowVisibility(*shadowMap,
                                   shadowUv,
                                   comparisonDepth,
                                   light.shadowFilterQuality,
                                   light.shadowStrength);
}

int materialDebugIdForItem(const std::vector<const Material *> &materials, const Material *material)
{
    for (std::size_t index = 0; index < materials.size(); ++index) {
        if (materials[index] == material)
            return static_cast<int>(index);
    }
    return 0;
}

Vec3f computeCookTorranceSpecular(const Vec3f &normal,
                                  const Vec3f &lightDirection,
                                  const Vec3f &viewDirection,
                                  const Vec3f &f0,
                                  float roughness)
{
    const float nDotV = std::max(0.0001f, dot(normal, viewDirection));
    const float nDotL = std::max(0.0001f, dot(normal, lightDirection));
    const Vec3f halfVector = normalize(lightDirection + viewDirection);
    const float nDotH = std::max(0.0001f, dot(normal, halfVector));
    const float vDotH = std::max(0.0001f, dot(viewDirection, halfVector));
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominatorBase = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    const float distribution = alphaSquared / std::max(kPi * denominatorBase * denominatorBase, 0.0001f);
    const float visibilityK = ((roughness + 1.0f) * (roughness + 1.0f)) * 0.125f;
    const float geometryView = nDotV / (nDotV * (1.0f - visibilityK) + visibilityK);
    const float geometryLight = nDotL / (nDotL * (1.0f - visibilityK) + visibilityK);
    const float geometry = geometryView * geometryLight;
    const float fresnelFactor = std::pow(1.0f - vDotH, 5.0f);
    const Vec3f fresnel = f0 + (Vec3f{1.0f, 1.0f, 1.0f} - f0) * fresnelFactor;
    return fresnel * (distribution * geometry / std::max(4.0f * nDotV * nDotL, 0.0001f));
}

Vec3f computeBlinnPhongSpecular(const Vec3f &normal,
                                const Vec3f &lightDirection,
                                const Vec3f &viewDirection,
                                const Vec3f &lightColor,
                                float lightIntensity,
                                const Material &material)
{
    const Vec3f halfVector = normalize(lightDirection + viewDirection);
    const float specularFactor = std::pow(std::max(0.0f, dot(normal, halfVector)),
                                          std::max(1.0f, material.shininess));
    return modulate(material.specularColor, lightColor)
        * (lightIntensity * material.specularStrength * specularFactor);
}

float computePointLightAttenuation(const PointLight &light, const Vec3f &worldPos)
{
    const Vec3f toLight = light.position - worldPos;
    const float distanceSquared = std::max(dot(toLight, toLight), 1e-4f);
    const float distance = std::sqrt(distanceSquared);
    if (light.range <= 1e-4f || distance >= light.range)
        return 0.0f;

    const float normalizedDistance = distance / light.range;
    const float rangeFade = std::pow(std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f), 2.0f);
    return (light.intensity * rangeFade) / distanceSquared;
}

float computeSpotLightAttenuation(const SpotLight &light, const Vec3f &worldPos)
{
    const Vec3f toPoint = worldPos - light.position;
    const float distanceSquared = std::max(dot(toPoint, toPoint), 1e-4f);
    const float distance = std::sqrt(distanceSquared);
    if (light.range <= 1e-4f || distance >= light.range)
        return 0.0f;

    const Vec3f lightForward = normalize(light.direction);
    const Vec3f lightToPoint = toPoint * (1.0f / distance);
    const float cosTheta = dot(lightForward, lightToPoint);
    const float innerRadians = std::clamp(light.innerConeDegrees, 0.1f, 89.0f) * (kPi / 180.0f);
    const float outerRadians = std::clamp(std::max(light.innerConeDegrees, light.outerConeDegrees), 0.1f, 89.5f) * (kPi / 180.0f);
    const float outerCos = std::cos(outerRadians);
    const float innerCos = std::cos(innerRadians);
    if (cosTheta <= outerCos)
        return 0.0f;

    const float coneFade = innerCos >= outerCos
        ? std::clamp((cosTheta - outerCos) / std::max(1e-4f, innerCos - outerCos), 0.0f, 1.0f)
        : 1.0f;
    const float normalizedDistance = distance / light.range;
    const float rangeFade = std::pow(std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f), 2.0f);
    return (light.intensity * rangeFade * coneFade) / distanceSquared;
}

struct LightingEvaluation {
    Vec3f ambient{0.0f, 0.0f, 0.0f};
    Vec3f diffuse{0.0f, 0.0f, 0.0f};
    Vec3f specular{0.0f, 0.0f, 0.0f};
    Vec3f pbr{0.0f, 0.0f, 0.0f};
    float primaryShadowFactor = 1.0f;
    bool hasShadowFactor = false;
};

LightingEvaluation evaluateLightingContributions(const FragmentInput &fragment,
                                                 const Material &material,
                                                 const LightingContext &lighting,
                                                 const SurfaceShadingData &surface,
                                                 const Vec3f &normal)
{
    LightingEvaluation evaluation;

    const auto accumulatePbr = [&](const Vec3f &lightDirection,
                                   const Vec3f &radiance,
                                   const Vec3f &lightColor,
                                   float ambient,
                                   float shadowFactor) {
        const float nDotL = std::max(0.0f, dot(normal, lightDirection));
        if (nDotL <= 0.0f)
            return;

        const Vec3f dielectricF0{0.04f, 0.04f, 0.04f};
        const Vec3f f0 = dielectricF0 * (1.0f - surface.metallic) + surface.baseColor * surface.metallic;
        const Vec3f specular = computeCookTorranceSpecular(normal, lightDirection, surface.viewDir, f0, surface.roughness);
        const Vec3f halfVector = normalize(lightDirection + surface.viewDir);
        const float fresnelFactor = std::pow(1.0f - std::max(0.0f, dot(surface.viewDir, halfVector)), 5.0f);
        const Vec3f fresnel = f0 + (Vec3f{1.0f, 1.0f, 1.0f} - f0) * fresnelFactor;
        const Vec3f diffuseWeight = (Vec3f{1.0f, 1.0f, 1.0f} - fresnel) * (1.0f - surface.metallic);
        const Vec3f diffuse = modulate(diffuseWeight, surface.baseColor) * (1.0f / kPi);
        const Vec3f ambientTerm = modulate(surface.baseColor, lightColor) * (ambient * (1.0f - 0.5f * surface.metallic));
        evaluation.pbr = evaluation.pbr + ambientTerm + modulate(diffuse + specular, radiance * shadowFactor) * nDotL;
    };

    for (std::size_t lightIndex = 0; lightIndex < lighting.directionalLights.size(); ++lightIndex) {
        const DirectionalLight &light = lighting.directionalLights[lightIndex];
        if (!light.enabled)
            continue;

        const Vec3f lightDirection = normalize(light.direction * -1.0f);
        const float lambert = std::max(0.0f, dot(normal, lightDirection));
        const float shadowFactor = computeDirectionalShadowFactor(fragment,
                                                                  normal,
                                                                  light,
                                                                  directionalShadowMapAt(lighting, lightIndex),
                                                                  material.receiveShadow);
        if (!evaluation.hasShadowFactor && light.castShadow) {
            evaluation.primaryShadowFactor = shadowFactor;
            evaluation.hasShadowFactor = true;
        }

        evaluation.ambient = evaluation.ambient + light.color * light.ambient;
        evaluation.diffuse = evaluation.diffuse + light.color * (lambert * light.intensity * shadowFactor);

        if (material.type == MaterialType::BlinnPhongTextured || material.type == MaterialType::BlinnPhongVertexColor) {
            evaluation.specular = evaluation.specular
                + computeBlinnPhongSpecular(normal, lightDirection, surface.viewDir, light.color, light.intensity, material) * shadowFactor;
        } else if (material.type == MaterialType::PbrTextured || material.type == MaterialType::PbrVertexColor) {
            accumulatePbr(lightDirection, light.color * light.intensity, light.color, light.ambient, shadowFactor);
        }
    }

    for (std::size_t lightIndex = 0; lightIndex < lighting.pointLights.size(); ++lightIndex) {
        const PointLight &light = lighting.pointLights[lightIndex];
        if (!light.enabled)
            continue;

        evaluation.ambient = evaluation.ambient + light.color * light.ambient;

        const float attenuation = computePointLightAttenuation(light, surface.worldPos);
        if (attenuation <= 0.0f)
            continue;

        const Vec3f lightDirection = normalize(light.position - surface.worldPos);
        const float shadowFactor = computePointShadowFactor(fragment,
                                                            normal,
                                                            light,
                                                            pointShadowMapAt(lighting, lightIndex),
                                                            material.receiveShadow);
        if (!evaluation.hasShadowFactor && light.castShadow) {
            evaluation.primaryShadowFactor = shadowFactor;
            evaluation.hasShadowFactor = true;
        }
        const float lambert = std::max(0.0f, dot(normal, lightDirection));
        const Vec3f radiance = light.color * attenuation * shadowFactor;
        evaluation.diffuse = evaluation.diffuse + radiance * lambert;

        if (material.type == MaterialType::BlinnPhongTextured || material.type == MaterialType::BlinnPhongVertexColor) {
            evaluation.specular = evaluation.specular
                + computeBlinnPhongSpecular(normal, lightDirection, surface.viewDir, light.color, attenuation, material) * shadowFactor;
        } else if (material.type == MaterialType::PbrTextured || material.type == MaterialType::PbrVertexColor) {
            accumulatePbr(lightDirection, light.color * attenuation, light.color, light.ambient, shadowFactor);
        }
    }

    for (std::size_t lightIndex = 0; lightIndex < lighting.spotLights.size(); ++lightIndex) {
        const SpotLight &light = lighting.spotLights[lightIndex];
        if (!light.enabled)
            continue;

        evaluation.ambient = evaluation.ambient + light.color * light.ambient;

        const float attenuation = computeSpotLightAttenuation(light, surface.worldPos);
        if (attenuation <= 0.0f)
            continue;

        const Vec3f lightDirection = normalize(light.position - surface.worldPos);
        const float shadowFactor = computeSpotShadowFactor(fragment,
                                                           normal,
                                                           light,
                                                           spotShadowMapAt(lighting, lightIndex),
                                                           material.receiveShadow);
        if (!evaluation.hasShadowFactor && light.castShadow) {
            evaluation.primaryShadowFactor = shadowFactor;
            evaluation.hasShadowFactor = true;
        }

        const float lambert = std::max(0.0f, dot(normal, lightDirection));
        const Vec3f radiance = light.color * attenuation * shadowFactor;
        evaluation.diffuse = evaluation.diffuse + radiance * lambert;

        if (material.type == MaterialType::BlinnPhongTextured || material.type == MaterialType::BlinnPhongVertexColor) {
            evaluation.specular = evaluation.specular
                + computeBlinnPhongSpecular(normal, lightDirection, surface.viewDir, light.color, attenuation, material) * shadowFactor;
        } else if (material.type == MaterialType::PbrTextured || material.type == MaterialType::PbrVertexColor) {
            accumulatePbr(lightDirection, light.color * attenuation, light.color, light.ambient, shadowFactor);
        }
    }

    return evaluation;
}

FragmentOutput defaultFragmentShaderImpl(const FragmentInput &fragment,
                                         const Material &material,
                                         const LightingContext &lighting)//默认片元着色器
{
    const Vec4f textureColor = material.texture.sampleColor(fragment.uv, material.sampler, fragment.textureLod);
    const Vec4f metallicRoughnessSample = material.metallicRoughnessTexture.isValid()
        ? material.metallicRoughnessTexture.sampleColor(fragment.uv, material.metallicRoughnessSampler, fragment.textureLod)
        : Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
    const SurfaceShadingData surface = buildSurfaceShadingData(fragment, material, textureColor, metallicRoughnessSample);
    const Vec3f normal = applyNormalMap(fragment, material, surface.worldNormal);

    if (material.type == MaterialType::UnlitTextured || material.type == MaterialType::UnlitVertexColor)
        return {clamp01(surface.baseColor), surface.alpha, false};

    if (!lighting.hasLights())
        return {clamp01(surface.baseColor), surface.alpha, false};

    const LightingEvaluation lightingResult = evaluateLightingContributions(fragment, material, lighting, surface, normal);

    switch (material.type) {
    case MaterialType::LambertTextured: {
        return {clamp01(modulate(surface.baseColor, lightingResult.ambient + lightingResult.diffuse)), surface.alpha, false};
    }
    case MaterialType::LambertVertexColor: {
        return {clamp01(modulate(surface.baseColor, lightingResult.ambient + lightingResult.diffuse)), surface.alpha, false};
    }
    case MaterialType::BlinnPhongTextured: {
        const Vec3f diffuseColor = modulate(surface.baseColor, lightingResult.ambient + lightingResult.diffuse);
        return {clamp01(diffuseColor + lightingResult.specular), surface.alpha, false};
    }
    case MaterialType::BlinnPhongVertexColor: {
        const Vec3f diffuseColor = modulate(surface.baseColor, lightingResult.ambient + lightingResult.diffuse);
        return {clamp01(diffuseColor + lightingResult.specular), surface.alpha, false};
    }
    case MaterialType::PbrTextured:
        return {clamp01(lightingResult.pbr), surface.alpha, false};
    case MaterialType::PbrVertexColor:
        return {clamp01(lightingResult.pbr), surface.alpha, false};
    case MaterialType::UnlitTextured:
    case MaterialType::UnlitVertexColor:
        return {clamp01(surface.baseColor), surface.alpha, false};
    }

    return {clamp01(fragment.color), 1.0f, false};
}

// 近平面在 OpenGL 风格裁剪空间里满足 z + w >= 0。
float nearPlaneDistance(const Vec4f &clipPosition)//近裁面距离
{
    return clipPosition.z + clipPosition.w;
}

struct ObjVertexReference {
    int positionIndex = -1;
    int texcoordIndex = -1;
    int normalIndex = -1;
};

bool parseObjVertexReferenceToken(const std::string &token,
                                  ObjVertexReference &reference,
                                  std::string &errorMessage)//解析 OBJ 顶点引用标记。
{
    if (token.empty()) {
        errorMessage = "OBJ 面片顶点为空。";
        return false;
    }

    std::array<std::string, 3> parts;
    std::size_t start = 0;
    std::size_t partIndex = 0;
    while (start <= token.size() && partIndex < parts.size()) {
        const std::size_t slash = token.find('/', start);
        parts[partIndex++] = token.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }

    const auto parseIndex = [&errorMessage](const std::string &value, int &index) {
        if (value.empty()) {
            index = -1;
            return true;
        }

        try {
            index = std::stoi(value);
            return true;
        } catch (...) {
            errorMessage = "OBJ 面片索引不是有效整数。";
            return false;
        }
    };

    if (!parseIndex(parts[0], reference.positionIndex))
        return false;
    if (reference.positionIndex == -1 || reference.positionIndex == 0) {
        errorMessage = "OBJ 顶点位置索引无效。";
        return false;
    }

    if (!parseIndex(parts[1], reference.texcoordIndex))
        return false;
    if (!parseIndex(parts[2], reference.normalIndex))
        return false;
    return true;
}

bool resolveObjIndex(int rawIndex, int count, int &resolvedIndex)
{
    if (rawIndex > 0) {
        resolvedIndex = rawIndex - 1;
        return resolvedIndex >= 0 && resolvedIndex < count;
    }

    if (rawIndex < 0) {
        resolvedIndex = count + rawIndex;
        return resolvedIndex >= 0 && resolvedIndex < count;
    }

    resolvedIndex = -1;
    return false;
}

Vec3f computeFaceNormal(const Vec3f &a, const Vec3f &b, const Vec3f &c)//计算面法线
{
    const Vec3f normal = cross(b - a, c - a);
    const float lengthSquared = dot(normal, normal);
    if (lengthSquared <= 1e-8f)
        return {0.0f, 0.0f, 1.0f};
    return normalize(normal);
}

void buildTangentBasis(const Vec3f &worldA,
                       const Vec3f &worldB,
                       const Vec3f &worldC,
                       const Vec2f &uvA,
                       const Vec2f &uvB,
                       const Vec2f &uvC,
                       const Vec3f &fallbackNormal,
                       Vec3f &tangent,
                       Vec3f &bitangent)
{
    const Vec3f edge1 = worldB - worldA;
    const Vec3f edge2 = worldC - worldA;
    const Vec2f uvEdge1 = uvB - uvA;
    const Vec2f uvEdge2 = uvC - uvA;
    const float determinant = uvEdge1.x * uvEdge2.y - uvEdge1.y * uvEdge2.x;
    if (std::fabs(determinant) <= 1e-8f) {
        tangent = buildFallbackTangent(fallbackNormal);
        bitangent = normalize(cross(fallbackNormal, tangent));
        return;
    }

    const float reciprocal = 1.0f / determinant;
    tangent = normalize((edge1 * uvEdge2.y - edge2 * uvEdge1.y) * reciprocal);
    bitangent = normalize((edge2 * uvEdge1.x - edge1 * uvEdge2.x) * reciprocal);
}

float computeTextureLod(const ScreenVertex &a,
                        const ScreenVertex &b,
                        const ScreenVertex &c,
                        const Texture2D &texture)
{
    if (texture.mipLevelCount() <= 1)
        return 0.0f;

    const float determinant = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (std::fabs(determinant) <= 1e-6f)
        return 0.0f;

    const Vec2f duv1 = b.uv - a.uv;
    const Vec2f duv2 = c.uv - a.uv;
    const float inverseDeterminant = 1.0f / determinant;
    const float dudx = (duv1.x * (c.y - a.y) - duv2.x * (b.y - a.y)) * inverseDeterminant;
    const float dvdx = (duv1.y * (c.y - a.y) - duv2.y * (b.y - a.y)) * inverseDeterminant;
    const float dudy = (duv2.x * (b.x - a.x) - duv1.x * (c.x - a.x)) * inverseDeterminant;
    const float dvdy = (duv2.y * (b.x - a.x) - duv1.y * (c.x - a.x)) * inverseDeterminant;

    const float footprintX = std::sqrt(dudx * dudx + dvdx * dvdx) * static_cast<float>(texture.width);
    const float footprintY = std::sqrt(dudy * dudy + dvdy * dvdy) * static_cast<float>(texture.height);
    const float texelFootprint = std::max(1.0f, std::max(footprintX, footprintY));
    return std::clamp(std::log2(texelFootprint), 0.0f, static_cast<float>(texture.mipLevelCount() - 1));
}

} // namespace

bool Texture2D::isValid() const//判断是否为空
{
    return width > 0
        && height > 0
        && texels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

int Texture2D::mipLevelCount() const
{
    if (!mipLevels.empty())
        return static_cast<int>(mipLevels.size());
    return isValid() ? 1 : 0;
}

void Texture2D::rebuildMipChain()
{
    mipLevels.clear();
    if (!isValid())
        return;

    mipLevels.push_back({width, height, texels});
    while (mipLevels.back().width > 1 || mipLevels.back().height > 1) {
        const TextureMipLevel &previous = mipLevels.back();
        TextureMipLevel next;
        next.width = std::max(1, previous.width / 2);
        next.height = std::max(1, previous.height / 2);
        next.texels.resize(static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height), 0xff000000u);

        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                Vec4f accumulated{0.0f, 0.0f, 0.0f, 0.0f};
                int sampleCount = 0;
                for (int offsetY = 0; offsetY < 2; ++offsetY) {
                    const int sourceY = std::min(previous.height - 1, y * 2 + offsetY);
                    for (int offsetX = 0; offsetX < 2; ++offsetX) {
                        const int sourceX = std::min(previous.width - 1, x * 2 + offsetX);
                        const std::uint32_t texel =
                            previous.texels[static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(previous.width)
                                            + static_cast<std::size_t>(sourceX)];
                        const Vec4f color = unpackColorAlpha(texel);
                        accumulated.x += color.x;
                        accumulated.y += color.y;
                        accumulated.z += color.z;
                        accumulated.w += color.w;
                        ++sampleCount;
                    }
                }

                const float inverseSampleCount = sampleCount > 0 ? 1.0f / static_cast<float>(sampleCount) : 1.0f;
                next.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width)
                            + static_cast<std::size_t>(x)] = packColorUnorm({
                    accumulated.x * inverseSampleCount,
                    accumulated.y * inverseSampleCount,
                    accumulated.z * inverseSampleCount,
                    accumulated.w * inverseSampleCount
                });
            }
        }

        mipLevels.push_back(std::move(next));
    }
}

Vec4f Texture2D::sampleColor(const Vec2f &uv, const SamplerState &sampler, float lod) const
{
    if (!isValid())
        return {1.0f, 1.0f, 1.0f, 1.0f};

    if (sampler.filter == TextureFilter::Nearest)
        return sampleColorAtMipLevel(*this, uv, sampler, 0);

    if (sampler.filter == TextureFilter::Bilinear || mipLevelCount() <= 1)
        return sampleColorAtMipLevel(*this, uv, sampler, 0);

    const float clampedLod = std::clamp(lod, 0.0f, static_cast<float>(std::max(0, mipLevelCount() - 1)));
    const int baseLevel = static_cast<int>(std::floor(clampedLod));
    const int nextLevel = std::min(baseLevel + 1, mipLevelCount() - 1);
    const float levelBlend = clampedLod - static_cast<float>(baseLevel);
    return lerp4(sampleColorAtMipLevel(*this, uv, sampler, baseLevel),
                 sampleColorAtMipLevel(*this, uv, sampler, nextLevel),
                 levelBlend);
}

Vec3f Texture2D::sample(const Vec2f &uv, const SamplerState &sampler, float lod) const//采样
{
    const Vec4f color = sampleColor(uv, sampler, lod);
    return {color.x, color.y, color.z};
}

Vec3f Texture2D::sampleNearest(const Vec2f &uv, AddressMode addressU, AddressMode addressV) const
{
    if (!isValid())
        return {1.0f, 1.0f, 1.0f};

    const Vec4f color = sampleColorAtMipLevel(*this, uv, {TextureFilter::Nearest, addressU, addressV}, 0);
    return {color.x, color.y, color.z};
}

Vec3f Texture2D::sampleBilinear(const Vec2f &uv, AddressMode addressU, AddressMode addressV) const
{
    if (!isValid())
        return {1.0f, 1.0f, 1.0f};

    const Vec4f color = sampleColorAtMipLevel(*this, uv, {TextureFilter::Bilinear, addressU, addressV}, 0);
    return {color.x, color.y, color.z};
}

Texture2D Texture2D::makeCheckerboard(int textureWidth,
                                      int textureHeight,
                                      int tileSize,
                                      std::uint32_t colorA,
                                      std::uint32_t colorB)
{
    Texture2D texture;
    texture.width = std::max(1, textureWidth);
    texture.height = std::max(1, textureHeight);
    texture.texels.resize(static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height));

    const int safeTileSize = std::max(1, tileSize);
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const bool useA = ((x / safeTileSize) + (y / safeTileSize)) % 2 == 0;
            texture.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width)
                           + static_cast<std::size_t>(x)] = useA ? colorA : colorB;
        }
    }

    texture.rebuildMipChain();
    return texture;
}

bool ShadowMap::isValid() const
{
    return width > 0
        && height > 0
        && depthValues.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

float ShadowMap::sampleDepth(const Vec2f &uv) const
{
    if (!isValid())
        return 1.0f;

    const float clampedU = clampUnit(uv.x);
    const float clampedV = clampUnit(uv.y);
    const int x = std::min(width - 1, static_cast<int>(clampedU * static_cast<float>(width)));
    const int y = std::min(height - 1, static_cast<int>(clampedV * static_cast<float>(height)));
    return depthValues[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
}

bool PointShadowMap::isValid() const
{
    if (faceSize <= 0 || farPlane <= 0.0f)
        return false;
    for (const ShadowMap &face : faces) {
        if (!face.isValid())
            return false;
    }
    return true;
}

float PointShadowMap::sampleDepth(const Vec3f &worldPosition, float bias) const
{
    if (!isValid())
        return 1.0f;

    const Vec3f offset = worldPosition - lightPosition;
    const float absX = std::fabs(offset.x);
    const float absY = std::fabs(offset.y);
    const float absZ = std::fabs(offset.z);
    int faceIndex = 0;
    if (absX >= absY && absX >= absZ)
        faceIndex = offset.x >= 0.0f ? 0 : 1;
    else if (absY >= absX && absY >= absZ)
        faceIndex = offset.y >= 0.0f ? 2 : 3;
    else
        faceIndex = offset.z >= 0.0f ? 4 : 5;

    const ShadowMap &face = faces[static_cast<std::size_t>(faceIndex)];
    const Vec4f lightClip = face.lightTransform * Vec4f{worldPosition.x, worldPosition.y, worldPosition.z, 1.0f};
    if (std::fabs(lightClip.w) <= 1e-6f)
        return 1.0f;

    const float inverseW = 1.0f / lightClip.w;
    const Vec3f lightNdc{lightClip.x * inverseW, lightClip.y * inverseW, lightClip.z * inverseW};
    if (lightNdc.x < -1.0f || lightNdc.x > 1.0f || lightNdc.y < -1.0f || lightNdc.y > 1.0f || lightNdc.z < -1.0f || lightNdc.z > 1.0f)
        return 1.0f;

    const Vec2f shadowUv{lightNdc.x * 0.5f + 0.5f, lightNdc.y * 0.5f + 0.5f};
    const float receiverDepth = lightNdc.z * 0.5f + 0.5f;
    return receiverDepth - bias > face.sampleDepth(shadowUv) ? 0.0f : 1.0f;
}

std::vector<const RenderItem *> buildOrderedRenderItems(const std::vector<RenderItem> &items, const Camera &camera)
{
    std::vector<const RenderItem *> opaqueItems;
    std::vector<const RenderItem *> transparentItems;
    opaqueItems.reserve(items.size());
    transparentItems.reserve(items.size());

    for (const RenderItem &item : items) {
        if (item.mesh == nullptr || item.material == nullptr)
            continue;
        if (item.material->surfaceMode == MaterialSurfaceMode::AlphaBlend)
            transparentItems.push_back(&item);
        else
            opaqueItems.push_back(&item);
    }

    std::stable_sort(transparentItems.begin(), transparentItems.end(), [&camera](const RenderItem *lhs, const RenderItem *rhs) {
        return squaredDistance(lhs->transform.position, camera.position)
               > squaredDistance(rhs->transform.position, camera.position);
    });

    std::vector<const RenderItem *> orderedItems;
    orderedItems.reserve(opaqueItems.size() + transparentItems.size());
    orderedItems.insert(orderedItems.end(), opaqueItems.begin(), opaqueItems.end());
    orderedItems.insert(orderedItems.end(), transparentItems.begin(), transparentItems.end());
    return orderedItems;
}

Material Material::makeLambertTextured()
{
    Material material;
    material.type = MaterialType::LambertTextured;
    material.texture = Texture2D::makeCheckerboard(64, 64, 8, 0xfff4f0e8u, 0xff2d6a4fu);
    material.sampler = {};
    material.normalSampler = material.sampler;
    material.metallicRoughnessSampler = material.sampler;
    return material;
}

Material Material::makeLambertVertexColor()
{
    Material material = makeLambertTextured();
    material.type = MaterialType::LambertVertexColor;
    return material;
}

Material Material::makeUnlitTextured()
{
    Material material = makeLambertTextured();
    material.type = MaterialType::UnlitTextured;
    return material;
}

Material Material::makeUnlitVertexColor()
{
    Material material = makeLambertTextured();
    material.type = MaterialType::UnlitVertexColor;
    return material;
}

Material Material::makeBlinnPhongTextured()
{
    Material material = makeLambertTextured();
    material.type = MaterialType::BlinnPhongTextured;
    material.specularColor = {1.0f, 1.0f, 1.0f};
    material.specularStrength = 0.45f;
    material.shininess = 48.0f;
    return material;
}

Material Material::makeBlinnPhongVertexColor()
{
    Material material = makeBlinnPhongTextured();
    material.type = MaterialType::BlinnPhongVertexColor;
    return material;
}

Material Material::makePbrTextured()
{
    Material material = makeLambertTextured();
    material.type = MaterialType::PbrTextured;
    material.metallic = 0.0f;
    material.roughness = 0.55f;
    material.specularStrength = 1.0f;
    material.shininess = 64.0f;
    return material;
}

Material Material::makePbrVertexColor()
{
    Material material = makePbrTextured();
    material.type = MaterialType::PbrVertexColor;
    return material;
}

LightingContext LightingContext::makeDefault()
{
    LightingContext lighting;
    lighting.directionalLights.push_back({
        normalize(Vec3f{0.0f, 0.0f, -1.0f}),
        {1.0f, 1.0f, 1.0f},
        1.0f,
        0.0f,
        false,
        0.65f,
        0.0025f,
        512,
        12.0f,
        ShadowFilterQuality::Pcf3x3
    });
    lighting.directionalLights.back().name = "主方向光";
    return lighting;
}

bool Mesh::isValid() const
{
    return vertices.size() >= 3 && indices.size() >= 3;
}

Mesh Mesh::makeCube()
{
    Mesh mesh;
    mesh.vertices = {
        {{-1.0f, -1.0f, -1.0f}, {0.95f, 0.25f, 0.20f}, normalize(Vec3f{-1.0f, -1.0f, -1.0f}), {0.0f, 0.0f}},
        {{1.0f, -1.0f, -1.0f}, {0.95f, 0.55f, 0.20f}, normalize(Vec3f{1.0f, -1.0f, -1.0f}), {1.0f, 0.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.95f, 0.85f, 0.20f}, normalize(Vec3f{1.0f, 1.0f, -1.0f}), {1.0f, 1.0f}},
        {{-1.0f, 1.0f, -1.0f}, {0.95f, 0.25f, 0.60f}, normalize(Vec3f{-1.0f, 1.0f, -1.0f}), {0.0f, 1.0f}},
        {{-1.0f, -1.0f, 1.0f}, {0.15f, 0.55f, 0.95f}, normalize(Vec3f{-1.0f, -1.0f, 1.0f}), {0.0f, 0.0f}},
        {{1.0f, -1.0f, 1.0f}, {0.25f, 0.85f, 0.95f}, normalize(Vec3f{1.0f, -1.0f, 1.0f}), {1.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}, {0.55f, 0.95f, 0.45f}, normalize(Vec3f{1.0f, 1.0f, 1.0f}), {1.0f, 1.0f}},
        {{-1.0f, 1.0f, 1.0f}, {0.15f, 0.85f, 0.55f}, normalize(Vec3f{-1.0f, 1.0f, 1.0f}), {0.0f, 1.0f}},
    };
    mesh.indices = {
        0, 1, 2, 0, 2, 3,
        4, 6, 5, 4, 7, 6,
        0, 4, 5, 0, 5, 1,
        3, 2, 6, 3, 6, 7,
        1, 5, 6, 1, 6, 2,
        0, 3, 7, 0, 7, 4
    };
    return mesh;
}

bool Mesh::loadObjFromText(const std::string &text, Mesh &mesh, std::string *errorMessage)
{
    std::vector<Vec3f> positions;
    std::vector<Vec2f> texcoords;
    std::vector<Vec3f> normals;
    Mesh parsedMesh;

    std::istringstream stream(text);
    std::string line;
    int lineNumber = 0;

    const auto fail = [errorMessage](const std::string &message) {
        if (errorMessage != nullptr)
            *errorMessage = message;
        return false;
    };

    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);

        std::istringstream lineStream(line);
        std::string keyword;
        if (!(lineStream >> keyword))
            continue;

        if (keyword == "v") {
            Vec3f position;
            if (!(lineStream >> position.x >> position.y >> position.z))
                return fail("OBJ 顶点位置解析失败，行号: " + std::to_string(lineNumber));
            positions.push_back(position);
            continue;
        }

        if (keyword == "vt") {
            Vec2f uv;
            if (!(lineStream >> uv.x >> uv.y))
                return fail("OBJ 纹理坐标解析失败，行号: " + std::to_string(lineNumber));
            texcoords.push_back(uv);
            continue;
        }

        if (keyword == "vn") {
            Vec3f normal;
            if (!(lineStream >> normal.x >> normal.y >> normal.z))
                return fail("OBJ 法线解析失败，行号: " + std::to_string(lineNumber));
            normals.push_back(normalize(normal));
            continue;
        }

        if (keyword != "f")
            continue;

        std::vector<ObjVertexReference> polygon;
        std::string token;
        while (lineStream >> token) {
            ObjVertexReference reference;
            std::string parseError;
            if (!parseObjVertexReferenceToken(token, reference, parseError))
                return fail(parseError + " 行号: " + std::to_string(lineNumber));
            polygon.push_back(reference);
        }

        if (polygon.size() < 3)
            return fail("OBJ 面片顶点数不足 3，行号: " + std::to_string(lineNumber));

        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            const ObjVertexReference triangleReferences[3] = {
                polygon[0],
                polygon[i],
                polygon[i + 1]
            };

            Vec3f trianglePositions[3];
            for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
                int resolvedPosition = -1;
                if (!resolveObjIndex(triangleReferences[vertexIndex].positionIndex,
                                     static_cast<int>(positions.size()),
                                     resolvedPosition)) {
                    return fail("OBJ 面片引用了不存在的位置索引，行号: " + std::to_string(lineNumber));
                }
                trianglePositions[vertexIndex] = positions[static_cast<std::size_t>(resolvedPosition)];
            }

            const Vec3f generatedNormal = computeFaceNormal(trianglePositions[0], trianglePositions[1], trianglePositions[2]);

            for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
                VertexInput vertex;
                vertex.position = trianglePositions[vertexIndex];
                vertex.color = {1.0f, 1.0f, 1.0f};
                vertex.uv = {0.0f, 0.0f};
                vertex.normal = generatedNormal;

                if (triangleReferences[vertexIndex].texcoordIndex != -1) {
                    int resolvedTexcoord = -1;
                    if (!resolveObjIndex(triangleReferences[vertexIndex].texcoordIndex,
                                         static_cast<int>(texcoords.size()),
                                         resolvedTexcoord)) {
                        return fail("OBJ 面片引用了不存在的纹理坐标索引，行号: " + std::to_string(lineNumber));
                    }
                    vertex.uv = texcoords[static_cast<std::size_t>(resolvedTexcoord)];
                }

                if (triangleReferences[vertexIndex].normalIndex != -1) {
                    int resolvedNormal = -1;
                    if (!resolveObjIndex(triangleReferences[vertexIndex].normalIndex,
                                         static_cast<int>(normals.size()),
                                         resolvedNormal)) {
                        return fail("OBJ 面片引用了不存在的法线索引，行号: " + std::to_string(lineNumber));
                    }
                    vertex.normal = normals[static_cast<std::size_t>(resolvedNormal)];
                }

                parsedMesh.indices.push_back(static_cast<std::uint32_t>(parsedMesh.vertices.size()));
                parsedMesh.vertices.push_back(vertex);
            }
        }
    }

    if (!parsedMesh.isValid())
        return fail("OBJ 没有解析出有效三角形。");

    mesh = std::move(parsedMesh);
    if (errorMessage != nullptr)
        errorMessage->clear();
    return true;
}

bool Mesh::loadObjFromFile(const std::string &path, Mesh &mesh, std::string *errorMessage)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        if (errorMessage != nullptr)
            *errorMessage = "无法打开 OBJ 文件: " + path;
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadObjFromText(buffer.str(), mesh, errorMessage);
}

Mat4f Transform::modelMatrix() const
{
    // 当前采用 T * Rz * Ry * Rx * S，保持对象层语义直接清晰。
    return Mat4f::translation(position.x, position.y, position.z)
        * Mat4f::rotationZ(rotationRadians.z)
        * Mat4f::rotationY(rotationRadians.y)
        * Mat4f::rotationX(rotationRadians.x)
        * Mat4f::scale(scale.x, scale.y, scale.z);
}

Mat4f Camera::viewMatrix() const//矩阵视图
{
    return Mat4f::lookAt(position, target, up);
}

Mat4f Camera::projectionMatrix(float aspect) const//矩阵投影
{
    if (projectionMode == CameraProjectionMode::Orthographic) {
        const float clampedHeight = std::max(0.01f, orthographicHeight);
        const float halfHeight = clampedHeight * 0.5f;
        const float halfWidth = halfHeight * std::max(0.01f, aspect);
        return Mat4f::orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }

    return Mat4f::perspective(verticalFovRadians, aspect, nearPlane, farPlane);
}

SoftwareRenderer::SoftwareRenderer()//构造函数
    : m_width(1),
      m_height(1),
      m_camera{{1.6f, 1.1f, 2.25f}, {0.0f, 0.0f, -4.5f}, {0.0f, 1.0f, 0.0f}, 55.0f * kPi / 180.0f, 0.1f, 100.0f},
      m_defaultLightingContext(LightingContext::makeDefault()),
      m_activeLightingContext(nullptr),
      m_defaultRenderState{},
      m_activeRenderState(nullptr),
      m_colorBuffer(1, 0xff101722u),
      m_depthBuffer(1, 1.0f),
      m_sampleColorBuffer(static_cast<std::size_t>(kMsaaSampleCount), 0xff101722u),
      m_sampleDepthBuffer(static_cast<std::size_t>(kMsaaSampleCount), 1.0f),
      m_overdrawBuffer(1, 0u),
      m_dirtyPixelFlags(1, 0u)
{
    // 初始时就保证 buffer 可用，这样 Qt 侧可以随时读图像数据。
    m_activeLightingContext = &m_defaultLightingContext;
    resetStats();
    startWorkerThreads();
    m_parallelStats.workerThreadCount = static_cast<int>(m_workerThreads.size());
}

SoftwareRenderer::~SoftwareRenderer()
{
    stopWorkerThreads();
}

void SoftwareRenderer::resize(int width, int height)
{
    // 尺寸变化时整块重建 framebuffer，逻辑简单直接。
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    const std::size_t pixelCount = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    m_colorBuffer.assign(pixelCount, 0xff101722u);
    m_depthBuffer.assign(pixelCount, 1.0f);
    m_sampleColorBuffer.assign(pixelCount * static_cast<std::size_t>(kMsaaSampleCount), 0xff101722u);
    m_sampleDepthBuffer.assign(pixelCount * static_cast<std::size_t>(kMsaaSampleCount), 1.0f);
    m_overdrawBuffer.assign(pixelCount, 0u);
    m_dirtyPixelFlags.assign(pixelCount, 0u);
    m_dirtyPixelIndices.clear();
    resetStats();
}

void SoftwareRenderer::clear(std::uint32_t color)
{
    // 每帧开始先把颜色和深度恢复到初始状态。
    std::fill(m_colorBuffer.begin(), m_colorBuffer.end(), color);
    std::fill(m_sampleColorBuffer.begin(), m_sampleColorBuffer.end(), color);
    clearDepth(1.0f);
    clearOverdraw();
    std::fill(m_dirtyPixelFlags.begin(), m_dirtyPixelFlags.end(), 0u);
    m_dirtyPixelIndices.clear();
    resetStats();
}

void SoftwareRenderer::drawMesh(const Mesh &mesh, const Mat4f &transform, const Material &material)
{
    const VertexShaderContext context{
        transform,
        Mat4f::identity(),
        Mat4f::identity(),
        Mat4f::identity(),
        m_camera.position
    };
    drawMeshWithContext(mesh, context, material, {});
}

void SoftwareRenderer::drawMeshWithContext(const Mesh &mesh,
                                           const VertexShaderContext &context,
                                           const Material &material,
                                           const DrawDebugInfo &debugInfo)
{
    if (!mesh.isValid())
        return;

    // 几何复用之后，真正的绘制循环直接消费 Mesh。
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t ia = mesh.indices[i];
        const std::uint32_t ib = mesh.indices[i + 1];
        const std::uint32_t ic = mesh.indices[i + 2];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size())
            continue;
        drawTriangle(mesh.vertices[ia],
                     mesh.vertices[ib],
                     mesh.vertices[ic],
                     context,
                     material,
                     debugInfo,
                     static_cast<int>(i / 3));
    }
}

void SoftwareRenderer::drawMesh(const Mesh &mesh, const Transform &transform, const Material &material)
{
    const Mat4f modelTransform = transform.modelMatrix();
    const VertexShaderContext context{
        modelTransform,
        modelTransform,
        Mat4f::identity(),
        Mat4f::identity(),
        m_camera.position
    };
    drawMeshWithContext(mesh, context, material, {});
}

void SoftwareRenderer::drawRenderItem(const RenderItem &item)
{
    if (item.mesh == nullptr || item.material == nullptr)
        return;
    drawMesh(*item.mesh, item.transform, *item.material);
}

void SoftwareRenderer::drawRenderItems(const std::vector<RenderItem> &items)
{
    const std::vector<const RenderItem *> orderedItems = buildOrderedRenderItems(items, m_camera);
    std::vector<const Material *> uniqueMaterials;
    int triangleBase = 0;
    for (std::size_t objectIndex = 0; objectIndex < orderedItems.size(); ++objectIndex) {
        const RenderItem *item = orderedItems[objectIndex];
        if (item == nullptr || item->mesh == nullptr || item->material == nullptr)
            continue;
        if (std::find(uniqueMaterials.begin(), uniqueMaterials.end(), item->material) == uniqueMaterials.end())
            uniqueMaterials.push_back(item->material);

        const Mat4f modelTransform = item->transform.modelMatrix();
        const VertexShaderContext context{
            modelTransform,
            modelTransform,
            Mat4f::identity(),
            Mat4f::identity(),
            m_camera.position
        };
        const DrawDebugInfo debugInfo{
            static_cast<int>(objectIndex),
            materialDebugIdForItem(uniqueMaterials, item->material),
            triangleBase
        };
        drawMeshWithContext(*item->mesh, context, *item->material, debugInfo);
        triangleBase += static_cast<int>(item->mesh->indices.size() / 3);
    }
}

void SoftwareRenderer::renderPass(const RenderPass &pass)//渲染通道
{
    // pass 级别入口统一管理清理动作、相机和本次临时状态覆盖。
    resolveDirtyPixels();
    resetStats();
    if (pass.clearColorEnabled) {
        std::fill(m_colorBuffer.begin(), m_colorBuffer.end(), pass.scene.clearColor);
        std::fill(m_sampleColorBuffer.begin(), m_sampleColorBuffer.end(), pass.scene.clearColor);
    }
    if (pass.clearDepthEnabled)
        clearDepth(pass.clearDepthValue);
    clearOverdraw();

    m_camera = pass.scene.camera;

    const float aspect = static_cast<float>(m_width) / static_cast<float>(std::max(1, m_height));
    const Mat4f view = pass.scene.camera.viewMatrix();
    const Mat4f projection = pass.scene.camera.projectionMatrix(aspect);

    const RenderState *previousState = m_activeRenderState;
    const LightingContext *previousLighting = m_activeLightingContext;
    m_activeRenderState = &pass.state;

    LightingContext frameLighting = pass.scene.lighting.hasLights()
        ? pass.scene.lighting
        : m_defaultLightingContext;
    frameLighting.directionalShadowMaps.assign(frameLighting.directionalLights.size(), nullptr);
    frameLighting.pointShadowMaps.assign(frameLighting.pointLights.size(), nullptr);
    frameLighting.spotShadowMaps.assign(frameLighting.spotLights.size(), nullptr);
    m_activeLightingContext = &frameLighting;

    std::vector<const RenderItem *> shadowItems;
    shadowItems.reserve(pass.scene.items.size());
    for (const RenderItem &item : pass.scene.items)
        shadowItems.push_back(&item);

    m_directionalShadowMapCache.resize(frameLighting.directionalLights.size());
    const std::vector<const RenderItem *> orderedItems = buildOrderedRenderItems(pass.scene.items, pass.scene.camera);
    for (std::size_t lightIndex = 0; lightIndex < frameLighting.directionalLights.size(); ++lightIndex) {
        const DirectionalLight &light = frameLighting.directionalLights[lightIndex];
        ShadowMapCacheEntry &cacheEntry = m_directionalShadowMapCache[lightIndex];
        if (!light.enabled || !light.castShadow) {
            cacheEntry.shadowMap.reset();
            cacheEntry.signature = 0;
            cacheEntry.valid = false;
            continue;
        }

        const std::uint64_t cacheSignature = buildShadowMapCacheSignature(shadowItems, light);
        if (cacheSignature != 0
            && cacheEntry.valid
            && cacheEntry.signature == cacheSignature
            && cacheEntry.shadowMap != nullptr
            && cacheEntry.shadowMap->isValid()) {
            frameLighting.directionalShadowMaps[lightIndex] = cacheEntry.shadowMap;
            continue;
        }

        std::shared_ptr<ShadowMap> shadowMap = buildShadowMapForItems(shadowItems, light);
        frameLighting.directionalShadowMaps[lightIndex] = shadowMap;
        if (cacheSignature != 0 && shadowMap != nullptr && shadowMap->isValid()) {
            cacheEntry.shadowMap = shadowMap;
            cacheEntry.signature = cacheSignature;
            cacheEntry.valid = true;
        } else {
            cacheEntry.shadowMap.reset();
            cacheEntry.signature = 0;
            cacheEntry.valid = false;
        }
    }

    for (std::size_t lightIndex = 0; lightIndex < frameLighting.pointLights.size(); ++lightIndex) {
        const PointLight &light = frameLighting.pointLights[lightIndex];
        if (!light.enabled || !light.castShadow)
            continue;
        frameLighting.pointShadowMaps[lightIndex] = buildPointShadowMapForItems(shadowItems, light);
    }

    for (std::size_t lightIndex = 0; lightIndex < frameLighting.spotLights.size(); ++lightIndex) {
        const SpotLight &light = frameLighting.spotLights[lightIndex];
        if (!light.enabled || !light.castShadow)
            continue;
        frameLighting.spotShadowMaps[lightIndex] = buildShadowMapForItems(shadowItems, light);
    }

    std::vector<const Material *> uniqueMaterials;
    for (const RenderItem *item : orderedItems) {
        if (item != nullptr && item->material != nullptr
            && std::find(uniqueMaterials.begin(), uniqueMaterials.end(), item->material) == uniqueMaterials.end()) {
            uniqueMaterials.push_back(item->material);
        }
    }

    int triangleBase = 0;
    for (std::size_t objectIndex = 0; objectIndex < orderedItems.size(); ++objectIndex) {
        const RenderItem *item = orderedItems[objectIndex];
        if (item == nullptr || item->mesh == nullptr || item->material == nullptr)
            continue;
        const Mat4f modelTransform = item->transform.modelMatrix();
        const VertexShaderContext context{
            projection * view * modelTransform,
            modelTransform,
            view,
            projection,
            pass.scene.camera.position
        };
        const DrawDebugInfo debugInfo{
            static_cast<int>(objectIndex),
            materialDebugIdForItem(uniqueMaterials, item->material),
            triangleBase
        };
        drawMeshWithContext(*item->mesh, context, *item->material, debugInfo);
        triangleBase += static_cast<int>(item->mesh->indices.size() / 3);
    }

    m_activeRenderState = previousState;
    m_activeLightingContext = previousLighting;
}

void SoftwareRenderer::renderScene(const Scene &scene)
{
    RenderPass pass;
    pass.scene = scene;
    pass.state = m_defaultRenderState;
    renderPass(pass);
}

void SoftwareRenderer::drawIndexedTriangles(const std::vector<VertexInput> &vertices,
                                            const std::vector<std::uint32_t> &indices,
                                            const Mat4f &transform,
                                            const Material &material)
{
    Mesh mesh;
    mesh.vertices = vertices;
    mesh.indices = indices;
    drawMesh(mesh, transform, material);
}

void SoftwareRenderer::setCamera(const Camera &camera)
{
    m_camera = camera;
}

void SoftwareRenderer::setLightingContext(const LightingContext &lighting)
{
    m_defaultLightingContext = lighting.hasLights() ? lighting : LightingContext::makeDefault();
    m_defaultLightingContext.directionalShadowMaps.clear();
    m_defaultLightingContext.pointShadowMaps.clear();
    m_defaultLightingContext.spotShadowMaps.clear();
    invalidateShadowMapCache();
    if (m_activeLightingContext == nullptr || m_activeLightingContext == &m_defaultLightingContext)
        m_activeLightingContext = &m_defaultLightingContext;
}

void SoftwareRenderer::setRenderState(const RenderState &state)
{
    resolveDirtyPixels();
    m_defaultRenderState = state;
}

void SoftwareRenderer::setParallelRasterEnabled(bool enabled)
{
    m_parallelConfig.enabled = enabled;
}

void SoftwareRenderer::setWorkerThreadCount(int count)
{
    if (m_parallelConfig.requestedWorkerCount == count)
        return;

    m_parallelConfig.requestedWorkerCount = count;
    rebuildWorkerThreads();
}

void SoftwareRenderer::setRasterTileSize(int size)
{
    m_parallelConfig.tileSize = std::max(1, size);
}

void SoftwareRenderer::setParallelThresholds(int minTileCount, int minPixelCount)
{
    m_parallelConfig.minParallelTileCount = std::max(1, minTileCount);
    m_parallelConfig.minParallelPixelCount = std::max(1, minPixelCount);
}

void SoftwareRenderer::setParallelTilesPerTask(int tilesPerTask)
{
    m_parallelConfig.tilesPerTask = std::max(1, tilesPerTask);
}

void SoftwareRenderer::setCullMode(CullMode mode)
{
    m_defaultRenderState.cullMode = mode;
}

void SoftwareRenderer::setAntiAliasingMode(AntiAliasingMode mode)
{
    resolveDirtyPixels();
    m_defaultRenderState.antiAliasing = mode;
}

void SoftwareRenderer::renderDemo(float elapsedSeconds)
{
    static const Mesh cubeMesh = Mesh::makeCube();
    static const Material cubeMaterial = Material::makeLambertTextured();

    // demo 现在走 Scene -> RenderItem 的高层入口，和后续场景化接口保持一致。
    Scene scene;
    scene.camera = m_camera;
    scene.clearColor = 0xff101722u;

    RenderItem cube;
    cube.mesh = &cubeMesh;
    cube.material = &cubeMaterial;
    cube.transform.position = {0.0f, 0.0f, -4.5f};
    cube.transform.rotationRadians = {elapsedSeconds * 0.55f, elapsedSeconds * 0.9f, 0.0f};
    scene.items.push_back(cube);

    renderScene(scene);
}

const std::uint32_t *SoftwareRenderer::colorBufferData()
{
    resolveDirtyPixels();
    return m_colorBuffer.data();
}

std::uint32_t SoftwareRenderer::colorAt(int x, int y)
{
    resolveDirtyPixels();
    // 测试接口不抛异常，越界时返回 0。
    if (x < 0 || y < 0 || x >= m_width || y >= m_height)
        return 0u;
    return m_colorBuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x)];
}

void SoftwareRenderer::resetStats()
{
    // stats 跟着 clear/resize 一起重置，表示“当前帧”的结果。
    m_stats.width = m_width;
    m_stats.height = m_height;
    m_stats.trianglesSubmitted = 0;
    m_stats.trianglesCulled = 0;
    m_stats.trianglesRasterized = 0;
    m_stats.pixelsDrawn = 0;

    m_parallelStats = {};
    m_parallelStats.tileSize = m_parallelConfig.tileSize;
    m_parallelStats.workerThreadCount = static_cast<int>(m_workerThreads.size());
    m_parallelStats.minParallelTileCount = m_parallelConfig.minParallelTileCount;
    m_parallelStats.minParallelPixelCount = m_parallelConfig.minParallelPixelCount;
    m_parallelStats.tilesPerTask = m_parallelConfig.tilesPerTask;
}

void SoftwareRenderer::rebuildWorkerThreads()
{
    stopWorkerThreads();
    startWorkerThreads();
    m_parallelStats.workerThreadCount = static_cast<int>(m_workerThreads.size());
}

void SoftwareRenderer::clearDepth(float depth)
{
    std::fill(m_depthBuffer.begin(), m_depthBuffer.end(), depth);
    std::fill(m_sampleDepthBuffer.begin(), m_sampleDepthBuffer.end(), depth);
    std::fill(m_dirtyPixelFlags.begin(), m_dirtyPixelFlags.end(), 0u);
    m_dirtyPixelIndices.clear();
}

void SoftwareRenderer::clearOverdraw()
{
    std::fill(m_overdrawBuffer.begin(), m_overdrawBuffer.end(), 0u);
}

std::vector<SoftwareRenderer::RasterTileBounds> SoftwareRenderer::buildTilesForBounds(int startX,
                                                                                      int endX,
                                                                                      int startY,
                                                                                      int endY) const
{
    std::vector<RasterTileBounds> tiles;
    if (startX > endX || startY > endY)
        return tiles;

    const int tileSize = std::max(1, m_parallelConfig.tileSize);
    const int tileCountX = (endX - startX + tileSize) / tileSize;
    const int tileCountY = (endY - startY + tileSize) / tileSize;
    tiles.reserve(static_cast<std::size_t>(tileCountX) * static_cast<std::size_t>(tileCountY));
    for (int tileStartY = startY; tileStartY <= endY; tileStartY += tileSize) {
        const int tileEndY = std::min(tileStartY + tileSize - 1, endY);
        for (int tileStartX = startX; tileStartX <= endX; tileStartX += tileSize) {
            tiles.push_back({tileStartX,
                             std::min(tileStartX + tileSize - 1, endX),
                             tileStartY,
                             tileEndY});
        }
    }
    return tiles;
}

void SoftwareRenderer::mergeTileTaskResult(TileTaskResult &&result)
{
    m_stats.pixelsDrawn += result.pixelsDrawn;
    for (std::uint32_t pixelIndexValue : result.dirtyPixelIndices) {
        const std::size_t pixelIndex = static_cast<std::size_t>(pixelIndexValue);
        if (m_dirtyPixelFlags[pixelIndex] != 0u)
            continue;
        m_dirtyPixelFlags[pixelIndex] = 1u;
        m_dirtyPixelIndices.push_back(pixelIndexValue);
    }
}

bool SoftwareRenderer::shouldUseParallelRaster(std::size_t tileCount, int estimatedPixelCount) const
{
    return m_parallelConfig.enabled
           && m_workerThreads.size() >= 2
           && tileCount >= static_cast<std::size_t>(m_parallelConfig.minParallelTileCount)
           && estimatedPixelCount >= m_parallelConfig.minParallelPixelCount;
}

void SoftwareRenderer::startWorkerThreads()
{
    m_stopWorkers = false;
    const int requestedCount = m_parallelConfig.requestedWorkerCount;
    const unsigned int hardwareThreadCount = std::thread::hardware_concurrency();
    const std::size_t desiredWorkerCount = requestedCount == 0
        ? 0u
        : (requestedCount > 0
               ? static_cast<std::size_t>(requestedCount)
               : (hardwareThreadCount > 1u ? static_cast<std::size_t>(hardwareThreadCount - 1u) : 0u));
    if (desiredWorkerCount == 0)
        return;

    m_workerThreads.reserve(desiredWorkerCount);
    for (std::size_t workerIndex = 0; workerIndex < desiredWorkerCount; ++workerIndex)
        m_workerThreads.emplace_back(&SoftwareRenderer::workerThreadMain, this);
}

void SoftwareRenderer::stopWorkerThreads()
{
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_stopWorkers = true;
    }
    m_workerCondition.notify_all();
    for (std::thread &worker : m_workerThreads) {
        if (worker.joinable())
            worker.join();
    }
    m_workerThreads.clear();
    m_workerTasks.clear();
    m_stopWorkers = false;
}

void SoftwareRenderer::workerThreadMain()
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            m_workerCondition.wait(lock, [this]() {
                return m_stopWorkers || !m_workerTasks.empty();
            });

            if (m_stopWorkers && m_workerTasks.empty())
                return;

            task = std::move(m_workerTasks.front());
            m_workerTasks.pop_front();
        }

        task();
    }
}

std::future<SoftwareRenderer::TileTaskResult> SoftwareRenderer::enqueueTileTask(std::function<TileTaskResult()> task)
{
    auto packagedTask = std::make_shared<std::packaged_task<TileTaskResult()>>(std::move(task));
    std::future<TileTaskResult> future = packagedTask->get_future();
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerTasks.emplace_back([packagedTask]() {
            (*packagedTask)();
        });
    }
    m_workerCondition.notify_one();
    return future;
}

void SoftwareRenderer::executeTileTasks(const std::vector<RasterTileBounds> &tiles,
                                        int estimatedPixelCount,
                                        const std::function<TileTaskResult(const RasterTileBounds &)> &workFn)
{
    using Clock = std::chrono::steady_clock;
    m_parallelStats.tileCount += static_cast<int>(tiles.size());

    if (!shouldUseParallelRaster(tiles.size(), estimatedPixelCount)) {
        ++m_parallelStats.serialTaskCount;
        m_parallelStats.serialTileCount += static_cast<int>(tiles.size());
        if (m_parallelConfig.enabled && !m_workerThreads.empty())
            ++m_parallelStats.skippedParallelDispatchCount;
        for (const RasterTileBounds &tile : tiles)
            mergeTileTaskResult(workFn(tile));
        return;
    }

    ++m_parallelStats.parallelTaskCount;
    const std::size_t suggestedTaskCount =
        (tiles.size() + static_cast<std::size_t>(std::max(1, m_parallelConfig.tilesPerTask)) - 1u)
        / static_cast<std::size_t>(std::max(1, m_parallelConfig.tilesPerTask));
    const std::size_t taskCount = std::max<std::size_t>(1u,
        std::min<std::size_t>(m_workerThreads.size(),
                              std::min<std::size_t>(tiles.size(), suggestedTaskCount)));
    m_parallelStats.taskCount += static_cast<int>(taskCount);
    m_parallelStats.parallelTileCount += static_cast<int>(tiles.size());

    std::vector<std::future<TileTaskResult>> futures;
    futures.reserve(taskCount);

    const Clock::time_point dispatchStart = Clock::now();
    for (std::size_t workerIndex = 0; workerIndex < taskCount; ++workerIndex) {
        const std::size_t begin = workerIndex * tiles.size() / taskCount;
        const std::size_t end = (workerIndex + 1u) * tiles.size() / taskCount;
        futures.push_back(enqueueTileTask([&tiles, &workFn, begin, end]() {
            TileTaskResult result;
            for (std::size_t tileIndex = begin; tileIndex < end; ++tileIndex) {
                TileTaskResult tileResult = workFn(tiles[tileIndex]);
                result.pixelsDrawn += tileResult.pixelsDrawn;
                result.dirtyPixelIndices.insert(result.dirtyPixelIndices.end(),
                                                tileResult.dirtyPixelIndices.begin(),
                                                tileResult.dirtyPixelIndices.end());
            }
            return result;
        }));
    }
    const Clock::time_point dispatchEnd = Clock::now();
    m_parallelStats.dispatchMicroseconds +=
        std::chrono::duration_cast<std::chrono::microseconds>(dispatchEnd - dispatchStart).count();

    const Clock::time_point waitStart = Clock::now();
    for (std::future<TileTaskResult> &future : futures)
        mergeTileTaskResult(future.get());
    const Clock::time_point waitEnd = Clock::now();
    m_parallelStats.waitMicroseconds +=
        std::chrono::duration_cast<std::chrono::microseconds>(waitEnd - waitStart).count();
}

std::uint64_t SoftwareRenderer::buildShadowMapCacheSignature(const std::vector<const RenderItem *> &items,
                                                             const DirectionalLight &light) const
{
    if (!light.castShadow)
        return 0;

    std::uint64_t signature = 1469598103934665603ull;
    signature = hashCombine(signature, hashFloatBits(light.direction.x));
    signature = hashCombine(signature, hashFloatBits(light.direction.y));
    signature = hashCombine(signature, hashFloatBits(light.direction.z));
    signature = hashCombine(signature, static_cast<std::uint64_t>(std::clamp(light.shadowMapSize, 64, 2048)));
    signature = hashCombine(signature, hashFloatBits(std::max(0.5f, light.shadowCoverage)));
    signature = hashCombine(signature, static_cast<std::uint64_t>(light.shadowFilterQuality));

    bool hasCacheableGeometry = false;
    for (const RenderItem *item : items) {
        if (item == nullptr || item->mesh == nullptr || item->material == nullptr || !item->mesh->isValid())
            continue;
        if (item->material->vertexShader)
            return 0;

        hasCacheableGeometry = true;
        signature = hashCombine(signature, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(item->mesh)));
        signature = hashCombine(signature, static_cast<std::uint64_t>(item->mesh->vertices.size()));
        signature = hashCombine(signature, static_cast<std::uint64_t>(item->mesh->indices.size()));
        signature = hashCombine(signature, hashFloatBits(item->transform.position.x));
        signature = hashCombine(signature, hashFloatBits(item->transform.position.y));
        signature = hashCombine(signature, hashFloatBits(item->transform.position.z));
        signature = hashCombine(signature, hashFloatBits(item->transform.rotationRadians.x));
        signature = hashCombine(signature, hashFloatBits(item->transform.rotationRadians.y));
        signature = hashCombine(signature, hashFloatBits(item->transform.rotationRadians.z));
        signature = hashCombine(signature, hashFloatBits(item->transform.scale.x));
        signature = hashCombine(signature, hashFloatBits(item->transform.scale.y));
        signature = hashCombine(signature, hashFloatBits(item->transform.scale.z));
    }

    return hasCacheableGeometry ? signature : 0;
}

void SoftwareRenderer::invalidateShadowMapCache()
{
    m_directionalShadowMapCache.clear();
}

std::shared_ptr<ShadowMap> SoftwareRenderer::buildShadowMapForItems(const std::vector<const RenderItem *> &items,
                                                                    const DirectionalLight &light) const
{
    if (!light.castShadow || items.empty())
        return nullptr;

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
    bool hasGeometry = false;
    for (const RenderItem *item : items) {
        if (item == nullptr || item->mesh == nullptr)
            continue;
        const Mat4f modelTransform = item->transform.modelMatrix();
        for (const VertexInput &vertex : item->mesh->vertices) {
            const Vec3f worldPos = transformPoint(modelTransform, vertex.position);
            minPoint.x = std::min(minPoint.x, worldPos.x);
            minPoint.y = std::min(minPoint.y, worldPos.y);
            minPoint.z = std::min(minPoint.z, worldPos.z);
            maxPoint.x = std::max(maxPoint.x, worldPos.x);
            maxPoint.y = std::max(maxPoint.y, worldPos.y);
            maxPoint.z = std::max(maxPoint.z, worldPos.z);
            hasGeometry = true;
        }
    }

    if (!hasGeometry)
        return nullptr;

    const Vec3f center = (minPoint + maxPoint) * 0.5f;
    const float radius = std::max(0.5f, length(maxPoint - minPoint) * 0.5f);
    const Vec3f lightForward = normalize(light.direction);
    const Vec3f lightUp = std::fabs(lightForward.y) > 0.98f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{0.0f, 1.0f, 0.0f};
    const Vec3f lightPosition = center - lightForward * (radius * 2.5f + 4.0f);
    const Mat4f lightView = Mat4f::lookAt(lightPosition, center, lightUp);

    Vec3f lightMin{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    Vec3f lightMax{
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()
    };
    for (const RenderItem *item : items) {
        if (item == nullptr || item->mesh == nullptr)
            continue;
        const Mat4f modelTransform = item->transform.modelMatrix();
        for (const VertexInput &vertex : item->mesh->vertices) {
            const Vec3f worldPos = transformPoint(modelTransform, vertex.position);
            const Vec3f lightSpacePos = transformPoint(lightView, worldPos);
            lightMin.x = std::min(lightMin.x, lightSpacePos.x);
            lightMin.y = std::min(lightMin.y, lightSpacePos.y);
            lightMin.z = std::min(lightMin.z, lightSpacePos.z);
            lightMax.x = std::max(lightMax.x, lightSpacePos.x);
            lightMax.y = std::max(lightMax.y, lightSpacePos.y);
            lightMax.z = std::max(lightMax.z, lightSpacePos.z);
        }
    }

    const float coverage = std::max(0.5f, light.shadowCoverage);
    const float margin = std::max(0.25f, coverage * 0.05f);
    const float left = -coverage;
    const float right = coverage;
    const float bottom = -coverage;
    const float top = coverage;
    const float nearPlane = std::max(0.1f, -lightMax.z);
    const float farPlane = std::max(nearPlane + 0.1f, -lightMin.z + margin * 2.0f);
    const Mat4f lightProjection = Mat4f::orthographic(left, right, bottom, top, nearPlane, farPlane);

    std::shared_ptr<ShadowMap> shadowMap = std::make_shared<ShadowMap>();
    shadowMap->width = std::clamp(light.shadowMapSize, 64, 2048);
    shadowMap->height = shadowMap->width;
    shadowMap->lightTransform = lightProjection * lightView;
    shadowMap->depthValues.assign(static_cast<std::size_t>(shadowMap->width) * static_cast<std::size_t>(shadowMap->height), 1.0f);

    for (const RenderItem *item : items) {
        if (item == nullptr || item->mesh == nullptr || item->material == nullptr || !item->mesh->isValid())
            continue;

        const Mat4f modelTransform = item->transform.modelMatrix();
        const VertexShaderContext context{
            shadowMap->lightTransform * modelTransform,
            modelTransform,
            lightView,
            lightProjection,
            lightPosition
        };

        for (std::size_t i = 0; i + 2 < item->mesh->indices.size(); i += 3) {
            const std::uint32_t ia = item->mesh->indices[i];
            const std::uint32_t ib = item->mesh->indices[i + 1];
            const std::uint32_t ic = item->mesh->indices[i + 2];
            if (ia >= item->mesh->vertices.size() || ib >= item->mesh->vertices.size() || ic >= item->mesh->vertices.size())
                continue;

            const VertexOutput a = runVertexShader(item->mesh->vertices[ia], context, *item->material);
            const VertexOutput b = runVertexShader(item->mesh->vertices[ib], context, *item->material);
            const VertexOutput c = runVertexShader(item->mesh->vertices[ic], context, *item->material);
            rasterizeShadowTriangle(a, b, c, *shadowMap);
        }
    }

    return shadowMap;
}

std::shared_ptr<ShadowMap> SoftwareRenderer::buildShadowMapForItems(const std::vector<const RenderItem *> &items,
                                                                    const SpotLight &light) const
{
    if (!light.castShadow || items.empty())
        return nullptr;

    const Vec3f lightForward = normalize(light.direction);
    const Vec3f lightUp = std::fabs(lightForward.y) > 0.98f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{0.0f, 1.0f, 0.0f};
    const Vec3f lightPosition = light.position;
    const Mat4f lightView = Mat4f::lookAt(lightPosition, lightPosition + lightForward, lightUp);
    const float farPlane = std::max(0.2f, light.shadowRange);
    const float nearPlane = 0.05f;
    const float outerConeDegrees = std::clamp(std::max(light.innerConeDegrees, light.outerConeDegrees), 1.0f, 89.5f);
    const Mat4f lightProjection = Mat4f::perspective(outerConeDegrees * 2.0f * (kPi / 180.0f), 1.0f, nearPlane, farPlane);

    std::shared_ptr<ShadowMap> shadowMap = std::make_shared<ShadowMap>();
    shadowMap->width = std::clamp(light.shadowMapSize, 64, 2048);
    shadowMap->height = shadowMap->width;
    shadowMap->lightTransform = lightProjection * lightView;
    shadowMap->depthValues.assign(static_cast<std::size_t>(shadowMap->width) * static_cast<std::size_t>(shadowMap->height), 1.0f);

    for (const RenderItem *item : items) {
        if (item == nullptr || item->mesh == nullptr || item->material == nullptr || !item->mesh->isValid())
            continue;

        const Mat4f modelTransform = item->transform.modelMatrix();
        const VertexShaderContext context{
            shadowMap->lightTransform * modelTransform,
            modelTransform,
            lightView,
            lightProjection,
            lightPosition
        };

        for (std::size_t i = 0; i + 2 < item->mesh->indices.size(); i += 3) {
            const std::uint32_t ia = item->mesh->indices[i];
            const std::uint32_t ib = item->mesh->indices[i + 1];
            const std::uint32_t ic = item->mesh->indices[i + 2];
            if (ia >= item->mesh->vertices.size() || ib >= item->mesh->vertices.size() || ic >= item->mesh->vertices.size())
                continue;

            const VertexOutput a = runVertexShader(item->mesh->vertices[ia], context, *item->material);
            const VertexOutput b = runVertexShader(item->mesh->vertices[ib], context, *item->material);
            const VertexOutput c = runVertexShader(item->mesh->vertices[ic], context, *item->material);
            rasterizeShadowTriangle(a, b, c, *shadowMap);
        }
    }

    return shadowMap;
}

std::shared_ptr<PointShadowMap> SoftwareRenderer::buildPointShadowMapForItems(const std::vector<const RenderItem *> &items,
                                                                              const PointLight &light) const
{
    if (!light.castShadow || items.empty())
        return nullptr;

    static const Vec3f faceDirections[PointShadowMap::kFaceCount] = {
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f}
    };
    static const Vec3f faceUps[PointShadowMap::kFaceCount] = {
        {0.0f, -1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}
    };

    std::shared_ptr<PointShadowMap> pointShadowMap = std::make_shared<PointShadowMap>();
    pointShadowMap->faceSize = std::clamp(light.shadowMapSize, 64, 1024);
    pointShadowMap->farPlane = std::max(0.2f, light.shadowRange);
    pointShadowMap->lightPosition = light.position;
    const Mat4f projection = Mat4f::perspective(kPi * 0.5f, 1.0f, 0.05f, pointShadowMap->farPlane);
    for (int faceIndex = 0; faceIndex < PointShadowMap::kFaceCount; ++faceIndex) {
        ShadowMap &face = pointShadowMap->faces[static_cast<std::size_t>(faceIndex)];
        const Mat4f view = Mat4f::lookAt(light.position, light.position + faceDirections[faceIndex], faceUps[faceIndex]);
        face.width = pointShadowMap->faceSize;
        face.height = pointShadowMap->faceSize;
        face.lightTransform = projection * view;
        face.depthValues.assign(static_cast<std::size_t>(face.width) * static_cast<std::size_t>(face.height), 1.0f);
    }

    for (const RenderItem *item : items) {
        if (item == nullptr || item->mesh == nullptr || item->material == nullptr || !item->mesh->isValid())
            continue;

        const Mat4f modelTransform = item->transform.modelMatrix();
        for (int faceIndex = 0; faceIndex < PointShadowMap::kFaceCount; ++faceIndex) {
            const ShadowMap &face = pointShadowMap->faces[static_cast<std::size_t>(faceIndex)];
            const Mat4f view = Mat4f::lookAt(light.position, light.position + faceDirections[faceIndex], faceUps[faceIndex]);
            const VertexShaderContext context{
                face.lightTransform * modelTransform,
                modelTransform,
                view,
                projection,
                light.position
            };

            for (std::size_t i = 0; i + 2 < item->mesh->indices.size(); i += 3) {
                const std::uint32_t ia = item->mesh->indices[i];
                const std::uint32_t ib = item->mesh->indices[i + 1];
                const std::uint32_t ic = item->mesh->indices[i + 2];
                if (ia >= item->mesh->vertices.size() || ib >= item->mesh->vertices.size() || ic >= item->mesh->vertices.size())
                    continue;

                const VertexOutput a = runVertexShader(item->mesh->vertices[ia], context, *item->material);
                const VertexOutput b = runVertexShader(item->mesh->vertices[ib], context, *item->material);
                const VertexOutput c = runVertexShader(item->mesh->vertices[ic], context, *item->material);
                rasterizeShadowTriangle(a,
                                        b,
                                        c,
                                        pointShadowMap->faces[static_cast<std::size_t>(faceIndex)]);
            }
        }
    }

    return pointShadowMap->isValid() ? pointShadowMap : nullptr;
}

void SoftwareRenderer::rasterizeShadowTriangle(const VertexOutput &a,
                                               const VertexOutput &b,
                                               const VertexOutput &c,
                                               ShadowMap &shadowMap) const
{
    const std::vector<VertexOutput> clippedPolygon = clipTriangleAgainstNearPlane(a, b, c);
    if (clippedPolygon.size() < 3)
        return;

    const auto projectToShadow = [&shadowMap](const VertexOutput &vertex, ScreenVertex &screenVertex) {
        if (std::fabs(vertex.clipPosition.w) <= 1e-6f)
            return false;

        const float inverseW = 1.0f / vertex.clipPosition.w;
        const float ndcX = vertex.clipPosition.x * inverseW;
        const float ndcY = vertex.clipPosition.y * inverseW;
        const float ndcZ = vertex.clipPosition.z * inverseW;
        screenVertex.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(shadowMap.width - 1);
        screenVertex.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(shadowMap.height - 1);
        screenVertex.depth = ndcZ * 0.5f + 0.5f;
        screenVertex.inverseW = inverseW;
        return true;
    };

    for (std::size_t i = 1; i + 1 < clippedPolygon.size(); ++i) {
        ScreenVertex va;
        ScreenVertex vb;
        ScreenVertex vc;
        if (!projectToShadow(clippedPolygon[0], va)
            || !projectToShadow(clippedPolygon[i], vb)
            || !projectToShadow(clippedPolygon[i + 1], vc)) {
            continue;
        }

        float area = edgeFunction(va.x, va.y, vb.x, vb.y, vc.x, vc.y);
        if (std::fabs(area) <= 1e-6f)
            continue;
        if (area < 0.0f) {
            std::swap(vb, vc);
            area = -area;
        }

        const int startX = std::max(0, static_cast<int>(std::floor(std::min({va.x, vb.x, vc.x}))));
        const int endX = std::min(shadowMap.width - 1, static_cast<int>(std::ceil(std::max({va.x, vb.x, vc.x}))));
        const int startY = std::max(0, static_cast<int>(std::floor(std::min({va.y, vb.y, vc.y}))));
        const int endY = std::min(shadowMap.height - 1, static_cast<int>(std::ceil(std::max({va.y, vb.y, vc.y}))));
        const float inverseArea = 1.0f / area;

        for (int y = startY; y <= endY; ++y) {
            for (int x = startX; x <= endX; ++x) {
                const float sampleX = static_cast<float>(x) + 0.5f;
                const float sampleY = static_cast<float>(y) + 0.5f;
                const float w0 = edgeFunction(vb.x, vb.y, vc.x, vc.y, sampleX, sampleY);
                const float w1 = edgeFunction(vc.x, vc.y, va.x, va.y, sampleX, sampleY);
                const float w2 = edgeFunction(va.x, va.y, vb.x, vb.y, sampleX, sampleY);
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                    continue;

                const float barycentric0 = w0 * inverseArea;
                const float barycentric1 = w1 * inverseArea;
                const float barycentric2 = w2 * inverseArea;
                const float depth = barycentric0 * va.depth + barycentric1 * vb.depth + barycentric2 * vc.depth;
                const std::size_t pixelIndex =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(shadowMap.width) + static_cast<std::size_t>(x);
                shadowMap.depthValues[pixelIndex] = std::min(shadowMap.depthValues[pixelIndex], depth);
            }
        }
    }
}

const RenderState &SoftwareRenderer::activeRenderState() const
{
    return m_activeRenderState != nullptr ? *m_activeRenderState : m_defaultRenderState;
}

void SoftwareRenderer::drawTriangle(const VertexInput &a,
                                    const VertexInput &b,
                                    const VertexInput &c,
                                    const VertexShaderContext &context,
                                    const Material &material,
                                    const DrawDebugInfo &debugInfo,
                                    int triangleIndex)
{
    ++m_stats.trianglesSubmitted;

    const VertexOutput outputA = runVertexShader(a, context, material);
    const VertexOutput outputB = runVertexShader(b, context, material);
    const VertexOutput outputC = runVertexShader(c, context, material);

    // 先在 VertexOutput 层做近平面裁剪，保留跨近平面的可见部分。
    const std::vector<VertexOutput> clippedPolygon = clipTriangleAgainstNearPlane(outputA, outputB, outputC);
    if (clippedPolygon.size() < 3)
        return;

    // 单平面裁剪后最多得到一个四边形，按三角扇重新拆回三角形。
    for (std::size_t i = 1; i + 1 < clippedPolygon.size(); ++i) {
        ScreenVertex sa;
        ScreenVertex sb;
        ScreenVertex sc;
        if (!projectVertexOutput(clippedPolygon[0], sa)
            || !projectVertexOutput(clippedPolygon[i], sb)
            || !projectVertexOutput(clippedPolygon[i + 1], sc)) {
            continue;
        }

        // 在视口映射之后根据屏幕空间绕序做正反面判断。
        if (shouldCullTriangle(sa, sb, sc)) {
            ++m_stats.trianglesCulled;
            continue;
        }

        rasterizeTriangle(sa,
                          sb,
                          sc,
                          material,
                          debugInfo,
                          debugInfo.triangleBase + triangleIndex);
    }
}

VertexOutput SoftwareRenderer::runVertexShader(const VertexInput &vertex,
                                               const VertexShaderContext &context,
                                               const Material &material) const
{
    // 默认材质不再预装 std::function，直接走内建路径，只有显式自定义时才付出分发成本。
    if (!material.vertexShader)
        return defaultVertexShader(vertex, context, material);
    return material.vertexShader(vertex, context, material);
}

FragmentOutput SoftwareRenderer::runFragmentShader(const FragmentInput &fragment,
                                                   const Material &material) const
{
    const LightingContext &lighting = m_activeLightingContext != nullptr ? *m_activeLightingContext : m_defaultLightingContext;
    if (!material.fragmentShader)
        return defaultFragmentShader(fragment, material, lighting);
    return material.fragmentShader(fragment, material, lighting);
}

std::vector<VertexOutput> SoftwareRenderer::clipTriangleAgainstNearPlane(const VertexOutput &a,
                                                                         const VertexOutput &b,
                                                                         const VertexOutput &c) const
{
    const auto interpolateVertexOutput = [](const VertexOutput &from, const VertexOutput &to, float t) {
        return VertexOutput{
            {
                from.clipPosition.x + (to.clipPosition.x - from.clipPosition.x) * t,
                from.clipPosition.y + (to.clipPosition.y - from.clipPosition.y) * t,
                from.clipPosition.z + (to.clipPosition.z - from.clipPosition.z) * t,
                from.clipPosition.w + (to.clipPosition.w - from.clipPosition.w) * t
            },
            from.color + (to.color - from.color) * t,
            from.normal + (to.normal - from.normal) * t,
            from.uv + (to.uv - from.uv) * t,
            from.worldPos + (to.worldPos - from.worldPos) * t,
            from.viewDir + (to.viewDir - from.viewDir) * t
        };
    };

    std::vector<VertexOutput> input = {a, b, c};
    std::vector<VertexOutput> output;
    output.reserve(4);

    for (std::size_t i = 0; i < input.size(); ++i) {
        const VertexOutput &current = input[i];
        const VertexOutput &next = input[(i + 1) % input.size()];

        const float currentDistance = nearPlaneDistance(current.clipPosition);
        const float nextDistance = nearPlaneDistance(next.clipPosition);
        const bool currentInside = currentDistance >= -kClipEpsilon;
        const bool nextInside = nextDistance >= -kClipEpsilon;

        if (currentInside && nextInside) {
            output.push_back(next);
            continue;
        }

        if (currentInside != nextInside) {
            const float denominator = currentDistance - nextDistance;
            if (std::fabs(denominator) > kClipEpsilon) {
                const float t = currentDistance / denominator;
                output.push_back(interpolateVertexOutput(current, next, t));
            }
        }

        if (!currentInside && nextInside)
            output.push_back(next);
    }

    return output;
}

bool SoftwareRenderer::projectVertexOutput(const VertexOutput &vertex, ScreenVertex &screenVertex) const
{
    // 裁剪后的点仍然需要通过 w 检查，避免在相机后方做透视除法。
    const Vec4f &clip = vertex.clipPosition;
    if (clip.w <= 0.0001f)
        return false;

    // 先做透视除法得到 NDC。
    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;
    const float ndcZ = clip.z * inverseW;

    // 把 NDC [-1, 1] 映射到屏幕像素坐标和深度范围 [0, 1]。
    screenVertex.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(m_width - 1);
    screenVertex.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(m_height - 1);
    screenVertex.depth = ndcZ * 0.5f + 0.5f;
    screenVertex.inverseW = inverseW;
    screenVertex.color = vertex.color;
    screenVertex.normal = vertex.normal;
    screenVertex.uv = vertex.uv;
    screenVertex.worldPos = vertex.worldPos;
    screenVertex.viewDir = vertex.viewDir;
    return true;
}

bool SoftwareRenderer::shouldCullTriangle(const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c) const
{
    const RenderState &state = activeRenderState();
    if (state.cullMode == CullMode::None)
        return false;

    // 当前约定屏幕空间有正面积的三角形是正面。
    const float signedArea = edgeFunction(a.x, a.y, b.x, b.y, c.x, c.y);
    if (std::fabs(signedArea) <= std::numeric_limits<float>::epsilon())
        return false;

    const bool isFrontFacing = signedArea > 0.0f;
    if (state.cullMode == CullMode::Back)
        return !isFrontFacing;
    return isFrontFacing;
}

void SoftwareRenderer::rasterizeTriangle(const ScreenVertex &a,
                                         const ScreenVertex &b,
                                         const ScreenVertex &c,
                                         const Material &material,
                                         const DrawDebugInfo &debugInfo,
                                         int triangleId)
{
    const RenderState &state = activeRenderState();

    ScreenVertex va = a;
    ScreenVertex vb = b;
    ScreenVertex vc = c;

    // 边函数的值同时充当带符号面积，零面积三角形直接忽略。
    float area = edgeFunction(va.x, va.y, vb.x, vb.y, vc.x, vc.y);
    if (std::fabs(area) <= std::numeric_limits<float>::epsilon())
        return;
    const bool frontFacing = area > 0.0f;

    // 规范成统一绕序，避免 top-left rule 因提交顺序不同而产生不同覆盖。
    if (area < 0.0f) {
        std::swap(vb, vc);
        area = -area;
    }

    if (state.fillMode == FillMode::Wireframe) {
        ++m_stats.trianglesRasterized;
        rasterizeWireframeTriangle(va, vb, vc, material, debugInfo, triangleId, frontFacing);
        return;
    }

    // 先算包围盒，把遍历范围限制到最小矩形区域。
    const float minX = std::floor(std::min({va.x, vb.x, vc.x}));
    const float maxX = std::ceil(std::max({va.x, vb.x, vc.x}));
    const float minY = std::floor(std::min({va.y, vb.y, vc.y}));
    const float maxY = std::ceil(std::max({va.y, vb.y, vc.y}));

    const int startX = std::max(0, static_cast<int>(minX));
    const int endX = std::min(m_width - 1, static_cast<int>(maxX));
    const int startY = std::max(0, static_cast<int>(minY));
    const int endY = std::min(m_height - 1, static_cast<int>(maxY));

    ++m_stats.trianglesRasterized;

    TriangleRasterWork work;
    work.vertices[0] = va;
    work.vertices[1] = vb;
    work.vertices[2] = vc;
    work.edgeTopLeft[0] = isTopLeftEdge(vb.x, vb.y, vc.x, vc.y);
    work.edgeTopLeft[1] = isTopLeftEdge(vc.x, vc.y, va.x, va.y);
    work.edgeTopLeft[2] = isTopLeftEdge(va.x, va.y, vb.x, vb.y);
    work.useMsaa = state.antiAliasing == AntiAliasingMode::Coverage4x;
    work.overdrawDebug = state.debugView == DebugView::Overdraw;
    work.frontFacing = frontFacing;
    work.edgeStepX[0] = vc.y - vb.y;
    work.edgeStepY[0] = vb.x - vc.x;
    work.edgeStepX[1] = va.y - vc.y;
    work.edgeStepY[1] = vc.x - va.x;
    work.edgeStepX[2] = vb.y - va.y;
    work.edgeStepY[2] = va.x - vb.x;

    const float inverseArea = 1.0f / area;
    for (int i = 0; i < 3; ++i) {
        const ScreenVertex &vertex = work.vertices[i];
        work.weightedInverseW[i] = vertex.inverseW * inverseArea;
        work.weightedDepth[i] = vertex.depth * work.weightedInverseW[i];

        work.weightedColor.x[i] = vertex.color.x * work.weightedInverseW[i];
        work.weightedColor.y[i] = vertex.color.y * work.weightedInverseW[i];
        work.weightedColor.z[i] = vertex.color.z * work.weightedInverseW[i];

        work.weightedNormal.x[i] = vertex.normal.x * work.weightedInverseW[i];
        work.weightedNormal.y[i] = vertex.normal.y * work.weightedInverseW[i];
        work.weightedNormal.z[i] = vertex.normal.z * work.weightedInverseW[i];

        work.weightedUv.x[i] = vertex.uv.x * work.weightedInverseW[i];
        work.weightedUv.y[i] = vertex.uv.y * work.weightedInverseW[i];

        work.weightedWorldPos.x[i] = vertex.worldPos.x * work.weightedInverseW[i];
        work.weightedWorldPos.y[i] = vertex.worldPos.y * work.weightedInverseW[i];
        work.weightedWorldPos.z[i] = vertex.worldPos.z * work.weightedInverseW[i];

        work.weightedViewDir.x[i] = vertex.viewDir.x * work.weightedInverseW[i];
        work.weightedViewDir.y[i] = vertex.viewDir.y * work.weightedInverseW[i];
        work.weightedViewDir.z[i] = vertex.viewDir.z * work.weightedInverseW[i];
    }
    buildTangentBasis(va.worldPos,
                      vb.worldPos,
                      vc.worldPos,
                      va.uv,
                      vb.uv,
                      vc.uv,
                      normalize(va.normal + vb.normal + vc.normal),
                      work.tangent,
                      work.bitangent);
    work.textureLod = computeTextureLod(va, vb, vc, material.texture);
    work.inverseArea = inverseArea;
    work.cameraWorldPosition = m_camera.position;
    work.objectId = debugInfo.objectId;
    work.materialId = debugInfo.materialId;
    work.triangleId = triangleId;

    if (!work.useMsaa && !m_dirtyPixelIndices.empty())
        resolveDirtyPixels();

    const auto tileBuildStart = std::chrono::steady_clock::now();
    const std::vector<RasterTileBounds> tiles = buildTilesForBounds(startX, endX, startY, endY);
    const auto tileBuildEnd = std::chrono::steady_clock::now();
    m_parallelStats.tileBuildMicroseconds +=
        std::chrono::duration_cast<std::chrono::microseconds>(tileBuildEnd - tileBuildStart).count();
    const int estimatedPixelCount = (endX - startX + 1) * (endY - startY + 1);
    executeTileTasks(tiles,
                     estimatedPixelCount,
                     [this, &work, &material](const RasterTileBounds &tile) {
                         return rasterizeTriangleTile(work, tile, material);
                     });
}

void SoftwareRenderer::rasterizeWireframeTriangle(const ScreenVertex &a,
                                                  const ScreenVertex &b,
                                                  const ScreenVertex &c,
                                                  const Material &material,
                                                  const DrawDebugInfo &debugInfo,
                                                  int triangleId,
                                                  bool frontFacing)
{
    rasterizeLineSegment(a, b, material, debugInfo, triangleId, frontFacing);
    rasterizeLineSegment(b, c, material, debugInfo, triangleId, frontFacing);
    rasterizeLineSegment(c, a, material, debugInfo, triangleId, frontFacing);
}

SoftwareRenderer::TileTaskResult SoftwareRenderer::rasterizeTriangleTile(const TriangleRasterWork &work,
                                                                         const RasterTileBounds &tile,
                                                                         const Material &material)
{
    TileTaskResult result;
    const auto isInsideSample = [&work](double w0, double w1, double w2) {
        return (w0 > 0.0 || (std::fabs(w0) <= static_cast<double>(std::numeric_limits<float>::epsilon()) && work.edgeTopLeft[0]))
               && (w1 > 0.0 || (std::fabs(w1) <= static_cast<double>(std::numeric_limits<float>::epsilon()) && work.edgeTopLeft[1]))
               && (w2 > 0.0 || (std::fabs(w2) <= static_cast<double>(std::numeric_limits<float>::epsilon()) && work.edgeTopLeft[2]));
    };

    const auto buildFragment = [&work](float sampleX,
                                       float sampleY,
                                       float w0,
                                       float w1,
                                       float w2,
                                       FragmentInput &fragment) {
        const float weights[3] = {w0, w1, w2};
        const float reciprocal = weights[0] * work.weightedInverseW[0]
                               + weights[1] * work.weightedInverseW[1]
                               + weights[2] * work.weightedInverseW[2];
        if (reciprocal <= 0.0f)
            return false;

        const float reciprocalInverse = 1.0f / reciprocal;
        fragment.x = sampleX;
        fragment.y = sampleY;
        fragment.depth = (weights[0] * work.weightedDepth[0]
                          + weights[1] * work.weightedDepth[1]
                          + weights[2] * work.weightedDepth[2]) * reciprocalInverse;
        fragment.color = clamp01({
            (weights[0] * work.weightedColor.x[0] + weights[1] * work.weightedColor.x[1] + weights[2] * work.weightedColor.x[2]) * reciprocalInverse,
            (weights[0] * work.weightedColor.y[0] + weights[1] * work.weightedColor.y[1] + weights[2] * work.weightedColor.y[2]) * reciprocalInverse,
            (weights[0] * work.weightedColor.z[0] + weights[1] * work.weightedColor.z[1] + weights[2] * work.weightedColor.z[2]) * reciprocalInverse
        });
        fragment.normal = normalize({
            (weights[0] * work.weightedNormal.x[0] + weights[1] * work.weightedNormal.x[1] + weights[2] * work.weightedNormal.x[2]) * reciprocalInverse,
            (weights[0] * work.weightedNormal.y[0] + weights[1] * work.weightedNormal.y[1] + weights[2] * work.weightedNormal.y[2]) * reciprocalInverse,
            (weights[0] * work.weightedNormal.z[0] + weights[1] * work.weightedNormal.z[1] + weights[2] * work.weightedNormal.z[2]) * reciprocalInverse
        });
        fragment.uv = {
            (weights[0] * work.weightedUv.x[0] + weights[1] * work.weightedUv.x[1] + weights[2] * work.weightedUv.x[2]) * reciprocalInverse,
            (weights[0] * work.weightedUv.y[0] + weights[1] * work.weightedUv.y[1] + weights[2] * work.weightedUv.y[2]) * reciprocalInverse
        };
        fragment.worldPos = {
            (weights[0] * work.weightedWorldPos.x[0] + weights[1] * work.weightedWorldPos.x[1] + weights[2] * work.weightedWorldPos.x[2]) * reciprocalInverse,
            (weights[0] * work.weightedWorldPos.y[0] + weights[1] * work.weightedWorldPos.y[1] + weights[2] * work.weightedWorldPos.y[2]) * reciprocalInverse,
            (weights[0] * work.weightedWorldPos.z[0] + weights[1] * work.weightedWorldPos.z[1] + weights[2] * work.weightedWorldPos.z[2]) * reciprocalInverse
        };
        const Vec3f interpolatedViewDir{
            (weights[0] * work.weightedViewDir.x[0] + weights[1] * work.weightedViewDir.x[1] + weights[2] * work.weightedViewDir.x[2]) * reciprocalInverse,
            (weights[0] * work.weightedViewDir.y[0] + weights[1] * work.weightedViewDir.y[1] + weights[2] * work.weightedViewDir.y[2]) * reciprocalInverse,
            (weights[0] * work.weightedViewDir.z[0] + weights[1] * work.weightedViewDir.z[1] + weights[2] * work.weightedViewDir.z[2]) * reciprocalInverse
        };
        const Vec3f derivedViewDir = work.cameraWorldPosition - fragment.worldPos;
        fragment.viewDir = length(derivedViewDir) > 1e-6f
            ? normalize(derivedViewDir)
            : normalize(interpolatedViewDir);
        fragment.tangent = work.tangent;
        fragment.bitangent = work.bitangent;
        fragment.textureLod = work.textureLod;
        fragment.barycentric = {w0 * work.inverseArea, w1 * work.inverseArea, w2 * work.inverseArea};
        fragment.objectId = work.objectId;
        fragment.materialId = work.materialId;
        fragment.triangleId = work.triangleId;
        fragment.frontFacing = work.frontFacing;
        return true;
    };

    std::array<double, kMsaaSampleCount> rowEdge0Values{};
    std::array<double, kMsaaSampleCount> rowEdge1Values{};
    std::array<double, kMsaaSampleCount> rowEdge2Values{};
    const ScreenVertex &v0 = work.vertices[0];
    const ScreenVertex &v1 = work.vertices[1];
    const ScreenVertex &v2 = work.vertices[2];
    for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
        const Vec2f &offset = kMsaaSampleOffsets[static_cast<std::size_t>(sampleIndex)];
        const float sampleX = static_cast<float>(tile.startX) + offset.x;
        const float sampleY = static_cast<float>(tile.startY) + offset.y;
        rowEdge0Values[static_cast<std::size_t>(sampleIndex)] = edgeFunction(v1.x, v1.y, v2.x, v2.y, sampleX, sampleY);
        rowEdge1Values[static_cast<std::size_t>(sampleIndex)] = edgeFunction(v2.x, v2.y, v0.x, v0.y, sampleX, sampleY);
        rowEdge2Values[static_cast<std::size_t>(sampleIndex)] = edgeFunction(v0.x, v0.y, v1.x, v1.y, sampleX, sampleY);
    }

    double centerRowEdge0 = edgeFunction(v1.x, v1.y, v2.x, v2.y,
                                         static_cast<float>(tile.startX) + 0.5f,
                                         static_cast<float>(tile.startY) + 0.5f);
    double centerRowEdge1 = edgeFunction(v2.x, v2.y, v0.x, v0.y,
                                         static_cast<float>(tile.startX) + 0.5f,
                                         static_cast<float>(tile.startY) + 0.5f);
    double centerRowEdge2 = edgeFunction(v0.x, v0.y, v1.x, v1.y,
                                         static_cast<float>(tile.startX) + 0.5f,
                                         static_cast<float>(tile.startY) + 0.5f);

    for (int y = tile.startY; y <= tile.endY; ++y) {
        std::array<double, kMsaaSampleCount> edge0Values = rowEdge0Values;
        std::array<double, kMsaaSampleCount> edge1Values = rowEdge1Values;
        std::array<double, kMsaaSampleCount> edge2Values = rowEdge2Values;
        double centerEdge0 = centerRowEdge0;
        double centerEdge1 = centerRowEdge1;
        double centerEdge2 = centerRowEdge2;

        for (int x = tile.startX; x <= tile.endX; ++x) {
            if (work.useMsaa) {
                int coveredSamples = 0;
                bool anySampleWritten = false;

                for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
                    const std::size_t sampleOffsetIndex = static_cast<std::size_t>(sampleIndex);
                    const double w0 = edge0Values[sampleOffsetIndex];
                    const double w1 = edge1Values[sampleOffsetIndex];
                    const double w2 = edge2Values[sampleOffsetIndex];
                    if (!isInsideSample(w0, w1, w2))
                        continue;

                    const Vec2f &offset = kMsaaSampleOffsets[sampleOffsetIndex];
                    const float sampleX = static_cast<float>(x) + offset.x;
                    const float sampleY = static_cast<float>(y) + offset.y;
                    ++coveredSamples;
                    if (work.overdrawDebug)
                        continue;

                    FragmentInput fragment;
                    if (!buildFragment(sampleX,
                                       sampleY,
                                       static_cast<float>(w0),
                                       static_cast<float>(w1),
                                       static_cast<float>(w2),
                                       fragment))
                        continue;

                    const bool wroteSample = shadeAndWriteSample(x, y, sampleIndex, fragment, material);
                    anySampleWritten = anySampleWritten || wroteSample;
                }

                if (coveredSamples > 0) {
                    if (work.overdrawDebug) {
                        const std::size_t pixelIndex = pixelBufferIndex(x, y);
                        m_overdrawBuffer[pixelIndex] += static_cast<std::uint32_t>(coveredSamples);
                        const std::uint32_t packedColor = packColor(overdrawDebugColor(m_overdrawBuffer[pixelIndex]));
                        const std::size_t sampleBase = sampleBufferIndex(x, y, 0);
                        for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex)
                            m_sampleColorBuffer[sampleBase + static_cast<std::size_t>(sampleIndex)] = packedColor;
                        m_colorBuffer[pixelIndex] = packedColor;
                        ++result.pixelsDrawn;
                    } else if (anySampleWritten) {
                        result.dirtyPixelIndices.push_back(static_cast<std::uint32_t>(pixelBufferIndex(x, y)));
                        ++result.pixelsDrawn;
                    }
                }
            } else if (isInsideSample(centerEdge0, centerEdge1, centerEdge2)) {
                FragmentInput fragment;
                if (buildFragment(static_cast<float>(x) + 0.5f,
                                  static_cast<float>(y) + 0.5f,
                                  static_cast<float>(centerEdge0),
                                  static_cast<float>(centerEdge1),
                                  static_cast<float>(centerEdge2),
                                  fragment)) {
                    shadeAndWritePixelDeferred(x, y, fragment, material, result);
                }
            }

            for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
                edge0Values[static_cast<std::size_t>(sampleIndex)] += work.edgeStepX[0];
                edge1Values[static_cast<std::size_t>(sampleIndex)] += work.edgeStepX[1];
                edge2Values[static_cast<std::size_t>(sampleIndex)] += work.edgeStepX[2];
            }
            centerEdge0 += work.edgeStepX[0];
            centerEdge1 += work.edgeStepX[1];
            centerEdge2 += work.edgeStepX[2];
        }

        for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
            rowEdge0Values[static_cast<std::size_t>(sampleIndex)] += work.edgeStepY[0];
            rowEdge1Values[static_cast<std::size_t>(sampleIndex)] += work.edgeStepY[1];
            rowEdge2Values[static_cast<std::size_t>(sampleIndex)] += work.edgeStepY[2];
        }
        centerRowEdge0 += work.edgeStepY[0];
        centerRowEdge1 += work.edgeStepY[1];
        centerRowEdge2 += work.edgeStepY[2];
    }

    return result;
}

SoftwareRenderer::TileTaskResult SoftwareRenderer::rasterizeLineSegmentTile(const LineRasterWork &work,
                                                                            const RasterTileBounds &tile,
                                                                            const Material &material)
{
    TileTaskResult result;

    for (int y = tile.startY; y <= tile.endY; ++y) {
        for (int x = tile.startX; x <= tile.endX; ++x) {
            int coveredSamples = 0;
            bool anySampleWritten = false;

            for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
                const Vec2f &offset = kMsaaSampleOffsets[static_cast<std::size_t>(sampleIndex)];
                const Vec2f samplePoint = {static_cast<float>(x) + offset.x, static_cast<float>(y) + offset.y};
                float t = 0.0f;
                if (squaredDistancePointToSegment(samplePoint, work.start, work.end, t) > work.lineRadiusSquared)
                    continue;

                ++coveredSamples;
                if (work.overdrawDebug)
                    continue;

                FragmentInput fragment;
                fragment.x = samplePoint.x;
                fragment.y = samplePoint.y;
                fragment.depth = work.from.depth + (work.to.depth - work.from.depth) * t;
                fragment.color = clamp01(work.from.color + (work.to.color - work.from.color) * t);
                fragment.normal = normalize(work.from.normal + (work.to.normal - work.from.normal) * t);
                fragment.uv = work.from.uv + (work.to.uv - work.from.uv) * t;
                fragment.worldPos = work.from.worldPos + (work.to.worldPos - work.from.worldPos) * t;
                const Vec3f interpolatedViewDir = work.from.viewDir + (work.to.viewDir - work.from.viewDir) * t;
                const Vec3f derivedViewDir = work.cameraWorldPosition - fragment.worldPos;
                fragment.viewDir = length(derivedViewDir) > 1e-6f
                    ? normalize(derivedViewDir)
                    : normalize(interpolatedViewDir);
                fragment.tangent = work.tangent;
                fragment.bitangent = work.bitangent;
                fragment.textureLod = work.textureLod;
                fragment.barycentric = {1.0f - t, t, 0.0f};
                fragment.objectId = work.objectId;
                fragment.materialId = work.materialId;
                fragment.triangleId = work.triangleId;
                fragment.frontFacing = work.frontFacing;

                const bool wroteSample = shadeAndWriteSample(x, y, sampleIndex, fragment, material);
                anySampleWritten = anySampleWritten || wroteSample;
            }

            if (coveredSamples == 0)
                continue;

            if (work.overdrawDebug) {
                const std::size_t pixelIndex = pixelBufferIndex(x, y);
                m_overdrawBuffer[pixelIndex] += static_cast<std::uint32_t>(coveredSamples);
                const std::uint32_t packedColor = packColor(overdrawDebugColor(m_overdrawBuffer[pixelIndex]));
                const std::size_t sampleBase = sampleBufferIndex(x, y, 0);
                for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex)
                    m_sampleColorBuffer[sampleBase + static_cast<std::size_t>(sampleIndex)] = packedColor;
                m_colorBuffer[pixelIndex] = packedColor;
                ++result.pixelsDrawn;
                continue;
            }

            if (!anySampleWritten)
                continue;

            result.dirtyPixelIndices.push_back(static_cast<std::uint32_t>(pixelBufferIndex(x, y)));
            ++result.pixelsDrawn;
        }
    }

    return result;
}

void SoftwareRenderer::rasterizeLineSegment(const ScreenVertex &from,
                                            const ScreenVertex &to,
                                            const Material &material,
                                            const DrawDebugInfo &debugInfo,
                                            int triangleId,
                                            bool frontFacing)
{
    const RenderState &state = activeRenderState();
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;

    if (state.antiAliasing == AntiAliasingMode::Coverage4x) {
        // 线框在 MSAA 模式下改成样本级命中测试，避免整像素硬台阶。
        const float minX = std::floor(std::min(from.x, to.x) - 1.0f);
        const float maxX = std::ceil(std::max(from.x, to.x) + 1.0f);
        const float minY = std::floor(std::min(from.y, to.y) - 1.0f);
        const float maxY = std::ceil(std::max(from.y, to.y) + 1.0f);

        const int startX = std::max(0, static_cast<int>(minX));
        const int endX = std::min(m_width - 1, static_cast<int>(maxX));
        const int startY = std::max(0, static_cast<int>(minY));
        const int endY = std::min(m_height - 1, static_cast<int>(maxY));

        static constexpr float kLineRadiusSquared = 0.25f;
        const Vec2f start = {from.x, from.y};
        const Vec2f end = {to.x, to.y};

        LineRasterWork work;
        work.from = from;
        work.to = to;
        work.start = start;
        work.end = end;
        work.tangent = buildFallbackTangent(normalize(from.normal + to.normal));
        work.bitangent = normalize(cross(normalize(from.normal + to.normal), work.tangent));
        work.textureLod = 0.0f;
        work.cameraWorldPosition = m_camera.position;
        work.overdrawDebug = state.debugView == DebugView::Overdraw;
        work.lineRadiusSquared = kLineRadiusSquared;
        work.frontFacing = frontFacing;
        work.objectId = debugInfo.objectId;
        work.materialId = debugInfo.materialId;
        work.triangleId = triangleId;

        const auto tileBuildStart = std::chrono::steady_clock::now();
        const std::vector<RasterTileBounds> tiles = buildTilesForBounds(startX, endX, startY, endY);
        const auto tileBuildEnd = std::chrono::steady_clock::now();
        m_parallelStats.tileBuildMicroseconds +=
            std::chrono::duration_cast<std::chrono::microseconds>(tileBuildEnd - tileBuildStart).count();
        const int estimatedPixelCount = (endX - startX + 1) * (endY - startY + 1);
        executeTileTasks(tiles,
                         estimatedPixelCount,
                         [this, &work, &material](const RasterTileBounds &tile) {
                             return rasterizeLineSegmentTile(work, tile, material);
                         });
        return;
    }

    const float steps = std::max(std::fabs(dx), std::fabs(dy));
    const int iterations = std::max(1, static_cast<int>(std::ceil(steps)));

    for (int i = 0; i <= iterations; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(iterations);
        const float px = from.x + dx * t;
        const float py = from.y + dy * t;
        const int x = static_cast<int>(std::round(px));
        const int y = static_cast<int>(std::round(py));
        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
            continue;

        FragmentInput fragment;
        fragment.x = static_cast<float>(x) + 0.5f;
        fragment.y = static_cast<float>(y) + 0.5f;
        fragment.depth = from.depth + (to.depth - from.depth) * t;
        fragment.color = clamp01(from.color + (to.color - from.color) * t);
        fragment.normal = normalize(from.normal + (to.normal - from.normal) * t);
        fragment.uv = from.uv + (to.uv - from.uv) * t;
        fragment.worldPos = from.worldPos + (to.worldPos - from.worldPos) * t;
        const Vec3f interpolatedViewDir = from.viewDir + (to.viewDir - from.viewDir) * t;
        const Vec3f derivedViewDir = m_camera.position - fragment.worldPos;
        fragment.viewDir = length(derivedViewDir) > 1e-6f
            ? normalize(derivedViewDir)
            : normalize(interpolatedViewDir);
        fragment.tangent = buildFallbackTangent(fragment.normal);
        fragment.bitangent = normalize(cross(fragment.normal, fragment.tangent));
        fragment.textureLod = 0.0f;
        fragment.barycentric = {1.0f - t, t, 0.0f};
        fragment.objectId = debugInfo.objectId;
        fragment.materialId = debugInfo.materialId;
        fragment.triangleId = triangleId;
        fragment.frontFacing = frontFacing;
        shadeAndWritePixel(x, y, fragment, material);
    }
}

void SoftwareRenderer::shadeAndWritePixel(int x,
                                          int y,
                                          const FragmentInput &fragment,
                                          const Material &material)
{
    if (!m_dirtyPixelIndices.empty())
        resolveDirtyPixels();

    TileTaskResult result;
    if (shadeAndWritePixelDeferred(x, y, fragment, material, result))
        mergeTileTaskResult(std::move(result));
}

bool SoftwareRenderer::shadeAndWritePixelDeferred(int x,
                                                  int y,
                                                  const FragmentInput &fragment,
                                                  const Material &material,
                                                  TileTaskResult &result)
{
    const RenderState &state = activeRenderState();
    if (fragment.depth < 0.0f || fragment.depth > 1.0f)
        return false;

    const std::size_t pixelIndex = pixelBufferIndex(x, y);
    ++m_overdrawBuffer[pixelIndex];

    if (state.debugView == DebugView::Overdraw) {
        const std::uint32_t packedColor = packColor(overdrawDebugColor(m_overdrawBuffer[pixelIndex]));
        const std::size_t sampleBase = sampleBufferIndex(x, y, 0);
        for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex)
            m_sampleColorBuffer[sampleBase + static_cast<std::size_t>(sampleIndex)] = packedColor;
        m_colorBuffer[pixelIndex] = packedColor;
        ++result.pixelsDrawn;
        return true;
    }

    if (state.depthTestEnable && !depthTestPasses(fragment.depth, m_depthBuffer[pixelIndex]))
        return false;

    Vec4f shadedColor;
    if (!shadeFragment(fragment, material, shadedColor))
        return false;

    const bool depthWriteEnabled = state.depthWriteEnable
                                   && (material.surfaceMode == MaterialSurfaceMode::Opaque || material.depthWriteEnable);
    const BlendMode effectiveBlendMode = material.surfaceMode == MaterialSurfaceMode::AlphaBlend
        ? BlendMode::Alpha
        : state.blend.mode;
    const std::size_t sampleBase = sampleBufferIndex(x, y, 0);

    for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
        Vec4f finalColor = shadedColor;
        const std::size_t resolvedSampleIndex = sampleBase + static_cast<std::size_t>(sampleIndex);
        if (effectiveBlendMode == BlendMode::Alpha || shadedColor.w < 0.999f)
            finalColor = blendSourceAlpha(shadedColor, unpackColorAlpha(m_sampleColorBuffer[resolvedSampleIndex]));
        m_sampleColorBuffer[resolvedSampleIndex] = packColor(finalColor);
        if (depthWriteEnabled)
            m_sampleDepthBuffer[resolvedSampleIndex] = fragment.depth;
    }

    if (depthWriteEnabled)
        m_depthBuffer[pixelIndex] = fragment.depth;
    result.dirtyPixelIndices.push_back(static_cast<std::uint32_t>(pixelIndex));
    ++result.pixelsDrawn;
    return true;
}

bool SoftwareRenderer::shadeAndWriteSample(int x,
                                           int y,
                                           int sampleIndex,
                                           const FragmentInput &fragment,
                                           const Material &material)
{
    const RenderState &state = activeRenderState();
    if (fragment.depth < 0.0f || fragment.depth > 1.0f)
        return false;

    const std::size_t sampleBufferOffset = sampleBufferIndex(x, y, sampleIndex);
    if (state.depthTestEnable && !depthTestPasses(fragment.depth, m_sampleDepthBuffer[sampleBufferOffset]))
        return false;

    Vec4f shadedColor;
    if (!shadeFragment(fragment, material, shadedColor))
        return false;

    const bool depthWriteEnabled = state.depthWriteEnable
                                   && (material.surfaceMode == MaterialSurfaceMode::Opaque || material.depthWriteEnable);
    const BlendMode effectiveBlendMode = material.surfaceMode == MaterialSurfaceMode::AlphaBlend
        ? BlendMode::Alpha
        : state.blend.mode;

    Vec4f finalColor = shadedColor;
    if (effectiveBlendMode == BlendMode::Alpha || shadedColor.w < 0.999f)
        finalColor = blendSourceAlpha(shadedColor, unpackColorAlpha(m_sampleColorBuffer[sampleBufferOffset]));

    m_sampleColorBuffer[sampleBufferOffset] = packColor(finalColor);
    if (depthWriteEnabled)
        m_sampleDepthBuffer[sampleBufferOffset] = fragment.depth;
    return true;
}

bool SoftwareRenderer::shadeFragment(const FragmentInput &fragment,
                                     const Material &material,
                                     Vec4f &shadedColor) const
{
    const RenderState &state = activeRenderState();
    if (state.debugView == DebugView::Depth) {
        const float depthValue = 1.0f - std::clamp(fragment.depth, 0.0f, 1.0f);
        shadedColor = {depthValue, depthValue, depthValue, 1.0f};
        return true;
    }

    if (state.debugView == DebugView::Normal) {
        const Vec3f normal = normalize(fragment.normal);
        shadedColor = {
            normal.x * 0.5f + 0.5f,
            normal.y * 0.5f + 0.5f,
            normal.z * 0.5f + 0.5f,
            1.0f
        };
        return true;
    }

    if (state.debugView == DebugView::UV) {
        shadedColor = {
            wrapUnit(fragment.uv.x),
            wrapUnit(fragment.uv.y),
            0.0f,
            1.0f
        };
        return true;
    }

    if (state.debugView == DebugView::ObjectId) {
        shadedColor = debugIdColor(fragment.objectId);
        return true;
    }

    if (state.debugView == DebugView::MaterialId) {
        shadedColor = debugIdColor(fragment.materialId);
        return true;
    }

    if (state.debugView == DebugView::TriangleId) {
        shadedColor = debugIdColor(fragment.triangleId);
        return true;
    }

    if (state.debugView == DebugView::FaceOrientation) {
        shadedColor = fragment.frontFacing
            ? Vec4f{0.1f, 0.9f, 0.2f, 1.0f}
            : Vec4f{0.95f, 0.2f, 0.15f, 1.0f};
        return true;
    }

    if (state.debugView == DebugView::Barycentric) {
        shadedColor = {
            std::clamp(fragment.barycentric.x, 0.0f, 1.0f),
            std::clamp(fragment.barycentric.y, 0.0f, 1.0f),
            std::clamp(fragment.barycentric.z, 0.0f, 1.0f),
            1.0f
        };
        return true;
    }

    if (state.debugView == DebugView::Shadow || state.debugView == DebugView::Lighting) {
        const LightingContext &lighting = m_activeLightingContext != nullptr
            ? *m_activeLightingContext
            : m_defaultLightingContext;
        const Vec4f textureColor = material.texture.sampleColor(fragment.uv, material.sampler, fragment.textureLod);
        const Vec4f metallicRoughnessSample = material.metallicRoughnessTexture.isValid()
            ? material.metallicRoughnessTexture.sampleColor(fragment.uv, material.metallicRoughnessSampler, fragment.textureLod)
            : Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
        const SurfaceShadingData surface = buildSurfaceShadingData(fragment, material, textureColor, metallicRoughnessSample);
        const Vec3f normal = applyNormalMap(fragment, material, surface.worldNormal);
        const LightingEvaluation lightingResult = evaluateLightingContributions(fragment, material, lighting, surface, normal);

        if (state.debugView == DebugView::Shadow) {
            const float shadow = lightingResult.hasShadowFactor ? lightingResult.primaryShadowFactor : 1.0f;
            shadedColor = {shadow, shadow, shadow, 1.0f};
            return true;
        }

        Vec3f debugLighting{0.0f, 0.0f, 0.0f};
        switch (material.type) {
        case MaterialType::LambertTextured:
        case MaterialType::LambertVertexColor:
            debugLighting = lightingResult.ambient + lightingResult.diffuse;
            break;
        case MaterialType::BlinnPhongTextured:
        case MaterialType::BlinnPhongVertexColor:
            debugLighting = lightingResult.ambient + lightingResult.diffuse + lightingResult.specular;
            break;
        case MaterialType::PbrTextured:
        case MaterialType::PbrVertexColor:
            debugLighting = lightingResult.pbr;
            break;
        case MaterialType::UnlitTextured:
        case MaterialType::UnlitVertexColor:
            debugLighting = {0.0f, 0.0f, 0.0f};
            break;
        }

        const Vec3f clampedLighting = clamp01(debugLighting);
        shadedColor = {clampedLighting.x, clampedLighting.y, clampedLighting.z, 1.0f};
        return true;
    }

    const FragmentOutput shaded = runFragmentShader(fragment, material);
    if (shaded.discard)
        return false;

    shadedColor = {
        shaded.color.x,
        shaded.color.y,
        shaded.color.z,
        std::clamp(shaded.alpha * std::clamp(material.opacity, 0.0f, 1.0f), 0.0f, 1.0f)
    };
    return true;
}

void SoftwareRenderer::resolvePixelColor(int x, int y)
{
    Vec4f accumulated = {0.0f, 0.0f, 0.0f, 0.0f};
    const std::size_t pixelIndex = pixelBufferIndex(x, y);
    const std::size_t sampleBase = sampleBufferIndex(x, y, 0);
    for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex) {
        const Vec4f color = unpackColorAlpha(m_sampleColorBuffer[sampleBase + static_cast<std::size_t>(sampleIndex)]);
        accumulated.x += color.x;
        accumulated.y += color.y;
        accumulated.z += color.z;
        accumulated.w += color.w;
    }

    const float inverseSampleCount = 1.0f / static_cast<float>(kMsaaSampleCount);
    m_colorBuffer[pixelIndex] = packColor({
        accumulated.x * inverseSampleCount,
        accumulated.y * inverseSampleCount,
        accumulated.z * inverseSampleCount,
        accumulated.w * inverseSampleCount
    });
}

void SoftwareRenderer::resolvePixelDepth(int x, int y)
{
    float minDepth = 1.0f;
    const std::size_t pixelIndex = pixelBufferIndex(x, y);
    const std::size_t sampleBase = sampleBufferIndex(x, y, 0);
    for (int sampleIndex = 0; sampleIndex < kMsaaSampleCount; ++sampleIndex)
        minDepth = std::min(minDepth, m_sampleDepthBuffer[sampleBase + static_cast<std::size_t>(sampleIndex)]);
    m_depthBuffer[pixelIndex] = minDepth;
}

void SoftwareRenderer::markPixelDirty(int x, int y)
{
    const std::size_t pixelIndex = pixelBufferIndex(x, y);
    if (m_dirtyPixelFlags[pixelIndex] != 0u)
        return;

    m_dirtyPixelFlags[pixelIndex] = 1u;
    m_dirtyPixelIndices.push_back(static_cast<std::uint32_t>(pixelIndex));
}

void SoftwareRenderer::resolveDirtyPixels()
{
    if (m_dirtyPixelIndices.empty())
        return;

    for (std::uint32_t pixelIndexValue : m_dirtyPixelIndices) {
        const std::size_t pixelIndex = static_cast<std::size_t>(pixelIndexValue);
        m_dirtyPixelFlags[pixelIndex] = 0u;

        const int x = static_cast<int>(pixelIndex % static_cast<std::size_t>(m_width));
        const int y = static_cast<int>(pixelIndex / static_cast<std::size_t>(m_width));
        resolvePixelDepth(x, y);
        resolvePixelColor(x, y);
    }

    m_dirtyPixelIndices.clear();
}

std::size_t SoftwareRenderer::pixelBufferIndex(int x, int y) const
{
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
}

std::size_t SoftwareRenderer::sampleBufferIndex(int x, int y, int sampleIndex) const
{
    return pixelBufferIndex(x, y) * static_cast<std::size_t>(kMsaaSampleCount) + static_cast<std::size_t>(sampleIndex);
}

bool SoftwareRenderer::depthTestPasses(float incomingDepth, float storedDepth) const
{
    switch (activeRenderState().depthFunc) {
    case DepthFunc::Never:
        return false;
    case DepthFunc::Less:
        return incomingDepth < storedDepth;
    case DepthFunc::LessEqual:
        return incomingDepth <= storedDepth;
    case DepthFunc::Equal:
        return std::fabs(incomingDepth - storedDepth) <= 1e-6f;
    case DepthFunc::NotEqual:
        return std::fabs(incomingDepth - storedDepth) > 1e-6f;
    case DepthFunc::Greater:
        return incomingDepth > storedDepth;
    case DepthFunc::GreaterEqual:
        return incomingDepth >= storedDepth;
    case DepthFunc::Always:
        return true;
    }

    return incomingDepth < storedDepth;
}

bool SoftwareRenderer::isTopLeftEdge(float ax, float ay, float bx, float by)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    return dy > 0.0f || (std::fabs(dy) <= std::numeric_limits<float>::epsilon() && dx < 0.0f);
}

Vec4f SoftwareRenderer::overdrawDebugColor(std::uint32_t count)
{
    if (count == 0u)
        return {0.0f, 0.0f, 0.0f, 1.0f};
    if (count == 1u)
        return {0.0f, 0.75f, 0.0f, 1.0f};
    if (count == 2u)
        return {1.0f, 1.0f, 0.0f, 1.0f};
    if (count == 3u)
        return {1.0f, 0.5f, 0.0f, 1.0f};
    return {1.0f, 0.0f, 0.0f, 1.0f};
}

Vec4f SoftwareRenderer::debugIdColor(int id)
{
    const std::uint32_t value = static_cast<std::uint32_t>(std::max(0, id) + 1) * 2654435761u;
    const float r = static_cast<float>((value >> 16) & 0xffu) / 255.0f;
    const float g = static_cast<float>((value >> 8) & 0xffu) / 255.0f;
    const float b = static_cast<float>(value & 0xffu) / 255.0f;
    return {r, g, b, 1.0f};
}

VertexOutput SoftwareRenderer::defaultVertexShader(const VertexInput &vertex,
                                                   const VertexShaderContext &context,
                                                   const Material &material)
{
    return defaultVertexShaderImpl(vertex, context, material);
}

FragmentOutput SoftwareRenderer::defaultFragmentShader(const FragmentInput &fragment,
                                                       const Material &material,
                                                       const LightingContext &lighting)
{
    return defaultFragmentShaderImpl(fragment, material, lighting);
}

float SoftwareRenderer::edgeFunction(float ax, float ay, float bx, float by, float px, float py)
{
    // 本质上是二维叉积，可判断点位于边的哪一侧。
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

std::uint32_t SoftwareRenderer::packColor(const Vec4f &color)
{
    // 内部颜色统一以浮点表示，输出前再压成 8-bit 通道。
    const Vec3f rgb = clamp01({color.x, color.y, color.z});
    const float alpha = std::clamp(color.w, 0.0f, 1.0f);
    const std::uint32_t a = static_cast<std::uint32_t>(alpha * 255.0f + 0.5f);
    const std::uint32_t r = static_cast<std::uint32_t>(rgb.x * 255.0f + 0.5f);
    const std::uint32_t g = static_cast<std::uint32_t>(rgb.y * 255.0f + 0.5f);
    const std::uint32_t b = static_cast<std::uint32_t>(rgb.z * 255.0f + 0.5f);
    return (a << 24) | (r << 16) | (g << 8) | b;
}
