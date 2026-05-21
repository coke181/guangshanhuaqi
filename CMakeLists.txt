cmake_minimum_required(VERSION 3.21)

# 这是整个工程的顶层构建入口。
# 工程被拆成纯 C++ 渲染核心和一个薄 Qt UI 外壳。
project(soft_rasterizer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)

qt_standard_project_setup(REQUIRES 6.10)
include(CTest)

# 纯 CPU 软光栅化核心，不依赖 Widgets。
add_library(rastercore STATIC
    RasterMath.h
    SoftwareRenderer.h
    SoftwareRenderer.cpp
)

target_include_directories(rastercore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(rastercore
    PUBLIC Qt6::Core
)

# Qt 程序入口和显示层。
qt_add_executable(software_rasterizer
    main.cpp
    MainWindow.h
    MainWindow.cpp
    RasterWidget.h
    RasterWidget.cpp
)

set_target_properties(software_rasterizer PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(software_rasterizer
    PRIVATE Qt6::Widgets
    PRIVATE rastercore
)

if(BUILD_TESTING)
    # 最小渲染器回归测试，覆盖基本出图和深度覆盖行为。
    qt_add_executable(tst_renderer
        tests/tst_renderer.cpp
    )

    target_link_libraries(tst_renderer
        PRIVATE Qt6::Test
        PRIVATE rastercore
    )

    add_test(NAME renderer-tests COMMAND $<TARGET_FILE:tst_renderer>)
endif()

# 独立 benchmark，可用于对比单线程 / 多线程、tile size 和调度阈值。
add_executable(renderer_benchmark
    tests/renderer_benchmark.cpp
)

target_link_libraries(renderer_benchmark
    PRIVATE rastercore
)

# 安装时只需要发主程序即可，核心静态库会被链接进可执行文件。
install(TARGETS software_rasterizer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
