#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include "SoftwareRenderer.h"

namespace {

Texture2D makeSolidTexture(std::uint32_t color)
{
    Texture2D texture;
    texture.width = 1;
    texture.height = 1;
    texture.texels = {color};
    texture.rebuildMipChain();
    return texture;
}

DirectionalLight makeHeadOnWhiteLight(float ambient = 0.0f)
{
    return {normalize(Vec3f{0.0f, 0.0f, -1.0f}), {1.0f, 1.0f, 1.0f}, 1.0f, ambient};
}

Scene makeStressScene(int gridWidth, int gridHeight)
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
    scene.camera = {{0.0f, 3.8f, 12.0f},
                    {0.0f, 0.0f, -7.0f},
                    {0.0f, 1.0f, 0.0f},
                    55.0f * 3.14159265358979323846f / 180.0f,
                    0.1f,
                    96.0f};
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

std::uint64_t hashColorBuffer(SoftwareRenderer &renderer)
{
    const std::uint8_t *bytes = reinterpret_cast<const std::uint8_t *>(renderer.colorBufferData());
    const std::size_t byteCount = static_cast<std::size_t>(renderer.width())
        * static_cast<std::size_t>(renderer.height())
        * sizeof(std::uint32_t);

    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < byteCount; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

struct BenchmarkOptions {
    int width = 1280;
    int height = 720;
    int frames = 24;
    int warmupFrames = 4;
    int gridWidth = 8;
    int gridHeight = 8;
    int workerThreads = -1;
    int tileSize = 16;
    int minParallelTiles = 4;
    int minParallelPixels = 1024;
    int tilesPerTask = 1;
    bool runSingleThread = true;
    bool runMultiThread = true;
    bool sweepThresholds = false;
};

struct BenchmarkResult {
    double averageMilliseconds = 0.0;
    double framesPerSecond = 0.0;
    std::uint64_t colorHash = 0;
    RenderStats renderStats;
    ParallelRasterStats parallelStats;
};

bool parseIntValue(const std::string &text, int &value)
{
    std::stringstream stream(text);
    stream >> value;
    return !stream.fail() && stream.eof();
}

BenchmarkOptions parseArguments(int argc, char **argv)
{
    BenchmarkOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        const auto readInt = [&](int &target) {
            if (index + 1 >= argc)
                return false;
            ++index;
            return parseIntValue(argv[index], target);
        };

        if (arg == "--width") {
            readInt(options.width);
        } else if (arg == "--height") {
            readInt(options.height);
        } else if (arg == "--frames") {
            readInt(options.frames);
        } else if (arg == "--warmup") {
            readInt(options.warmupFrames);
        } else if (arg == "--grid-width") {
            readInt(options.gridWidth);
        } else if (arg == "--grid-height") {
            readInt(options.gridHeight);
        } else if (arg == "--threads") {
            readInt(options.workerThreads);
        } else if (arg == "--tile-size") {
            readInt(options.tileSize);
        } else if (arg == "--min-parallel-tiles") {
            readInt(options.minParallelTiles);
        } else if (arg == "--min-parallel-pixels") {
            readInt(options.minParallelPixels);
        } else if (arg == "--tiles-per-task") {
            readInt(options.tilesPerTask);
        } else if (arg == "--single-only") {
            options.runMultiThread = false;
        } else if (arg == "--multi-only") {
            options.runSingleThread = false;
        } else if (arg == "--sweep") {
            options.sweepThresholds = true;
        }
    }

    options.width = std::max(1, options.width);
    options.height = std::max(1, options.height);
    options.frames = std::max(1, options.frames);
    options.warmupFrames = std::max(0, options.warmupFrames);
    options.gridWidth = std::max(1, options.gridWidth);
    options.gridHeight = std::max(1, options.gridHeight);
    options.tileSize = std::max(1, options.tileSize);
    options.minParallelTiles = std::max(1, options.minParallelTiles);
    options.minParallelPixels = std::max(1, options.minParallelPixels);
    options.tilesPerTask = std::max(1, options.tilesPerTask);
    return options;
}

BenchmarkResult runBenchmark(const BenchmarkOptions &options,
                             const Scene &scene,
                             bool parallelEnabled,
                             int tileSize,
                             int minParallelTiles,
                             int minParallelPixels,
                             int tilesPerTask)
{
    SoftwareRenderer renderer;
    renderer.resize(options.width, options.height);
    renderer.setParallelRasterEnabled(parallelEnabled);
    renderer.setWorkerThreadCount(parallelEnabled ? options.workerThreads : 0);
    renderer.setRasterTileSize(tileSize);
    renderer.setParallelThresholds(minParallelTiles, minParallelPixels);
    renderer.setParallelTilesPerTask(tilesPerTask);

    for (int warmupIndex = 0; warmupIndex < options.warmupFrames; ++warmupIndex)
        renderer.renderScene(scene);

    const auto start = std::chrono::steady_clock::now();
    for (int frameIndex = 0; frameIndex < options.frames; ++frameIndex)
        renderer.renderScene(scene);
    const auto end = std::chrono::steady_clock::now();

    BenchmarkResult result;
    const double totalMilliseconds =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
    result.averageMilliseconds = totalMilliseconds / static_cast<double>(options.frames);
    result.framesPerSecond = result.averageMilliseconds > 0.0 ? 1000.0 / result.averageMilliseconds : 0.0;
    result.colorHash = hashColorBuffer(renderer);
    result.renderStats = renderer.stats();
    result.parallelStats = renderer.parallelStats();
    return result;
}

void printResult(const std::string &label, const BenchmarkResult &result)
{
    std::cout << label << '\n';
    std::cout << "  avg: " << std::fixed << std::setprecision(3) << result.averageMilliseconds << " ms";
    std::cout << " | fps: " << std::fixed << std::setprecision(2) << result.framesPerSecond;
    std::cout << " | hash: 0x" << std::hex << result.colorHash << std::dec << '\n';
    std::cout << "  tiles: " << result.parallelStats.tileCount
              << " | tileSize: " << result.parallelStats.tileSize
              << " | workers: " << result.parallelStats.workerThreadCount
              << " | tasks: " << result.parallelStats.taskCount << '\n';
    std::cout << "  dispatch(serial/parallel): "
              << result.parallelStats.serialTaskCount << '/'
              << result.parallelStats.parallelTaskCount
              << " | tiles(serial/parallel): "
              << result.parallelStats.serialTileCount << '/'
              << result.parallelStats.parallelTileCount << '\n';
    std::cout << "  timings(us) build/dispatch/wait: "
              << result.parallelStats.tileBuildMicroseconds << '/'
              << result.parallelStats.dispatchMicroseconds << '/'
              << result.parallelStats.waitMicroseconds << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    const BenchmarkOptions options = parseArguments(argc, argv);
    const Scene scene = makeStressScene(options.gridWidth, options.gridHeight);

    std::cout << "renderer_benchmark\n";
    std::cout << "resolution: " << options.width << 'x' << options.height
              << " | frames: " << options.frames
              << " | warmup: " << options.warmupFrames
              << " | scene grid: " << options.gridWidth << 'x' << options.gridHeight << "\n\n";

    BenchmarkResult singleThreadResult;
    BenchmarkResult multiThreadResult;
    bool haveSingleThreadResult = false;
    bool haveMultiThreadResult = false;

    if (options.runSingleThread) {
        singleThreadResult = runBenchmark(options,
                                          scene,
                                          false,
                                          options.tileSize,
                                          options.minParallelTiles,
                                          options.minParallelPixels,
                                          options.tilesPerTask);
        haveSingleThreadResult = true;
        printResult("single-thread", singleThreadResult);
        std::cout << '\n';
    }

    if (options.runMultiThread) {
        multiThreadResult = runBenchmark(options,
                                         scene,
                                         true,
                                         options.tileSize,
                                         options.minParallelTiles,
                                         options.minParallelPixels,
                                         options.tilesPerTask);
        haveMultiThreadResult = true;
        printResult("multi-thread", multiThreadResult);
        std::cout << '\n';
    }

    if (haveSingleThreadResult && haveMultiThreadResult) {
        const double speedup = multiThreadResult.averageMilliseconds > 0.0
            ? singleThreadResult.averageMilliseconds / multiThreadResult.averageMilliseconds
            : 0.0;
        std::cout << "speedup: " << std::fixed << std::setprecision(2) << speedup << "x\n";
        std::cout << "output match: " << (singleThreadResult.colorHash == multiThreadResult.colorHash ? "yes" : "no") << "\n\n";
    }

    if (options.sweepThresholds && options.runMultiThread) {
        std::cout << "threshold sweep\n";
        BenchmarkResult bestResult;
        int bestTileSize = options.tileSize;
        int bestMinTiles = options.minParallelTiles;
        int bestMinPixels = options.minParallelPixels;
        int bestTilesPerTask = options.tilesPerTask;
        double bestMilliseconds = std::numeric_limits<double>::max();

        for (int tileSize : {8, 16, 32}) {
            for (int minTiles : {1, 4, 8}) {
                for (int minPixels : {1024, 4096, 16384}) {
                    for (int tilesPerTask : {1, 2, 4, 8}) {
                        BenchmarkResult result = runBenchmark(options,
                                                              scene,
                                                              true,
                                                              tileSize,
                                                              minTiles,
                                                              minPixels,
                                                              tilesPerTask);
                        if (result.averageMilliseconds < bestMilliseconds) {
                            bestMilliseconds = result.averageMilliseconds;
                            bestTileSize = tileSize;
                            bestMinTiles = minTiles;
                            bestMinPixels = minPixels;
                            bestTilesPerTask = tilesPerTask;
                            bestResult = result;
                        }
                    }
                }
            }
        }

        std::cout << "best config"
                  << " | tileSize=" << bestTileSize
                  << " | minTiles=" << bestMinTiles
                  << " | minPixels=" << bestMinPixels
                  << " | tilesPerTask=" << bestTilesPerTask << '\n';
        printResult("best multi-thread", bestResult);
    }

    return 0;
}
