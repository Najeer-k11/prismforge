cmake_minimum_required(VERSION 3.24)

project(PrismForge
    VERSION 0.2.0
    DESCRIPTION "Native GPU shader development environment"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PRISMFORGE_BUILD_TESTS "Build automated tests" ON)
set(PRISMFORGE_APP_ID "io.github.Najeer_k11.PrismForge" CACHE STRING
    "Reverse-DNS application id used for the desktop file, icon and AppStream metadata")
option(PRISMFORGE_WITH_GLSLANG "Validate shaders with glslang before driver compilation" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)
qt_standard_project_setup()

set(PRISMFORGE_HAVE_GLSLANG OFF)
if(PRISMFORGE_WITH_GLSLANG)
    find_package(glslang CONFIG QUIET)
    if(glslang_FOUND)
        set(PRISMFORGE_HAVE_GLSLANG ON)
        message(STATUS "glslang ${glslang_VERSION} found: front-end validation enabled")
    else()
        message(WARNING "glslang not found: shaders will be compiled by the GL driver only")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src)

include(packaging/Packaging.cmake)

if(PRISMFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
