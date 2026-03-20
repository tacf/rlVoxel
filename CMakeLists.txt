cmake_minimum_required(VERSION 3.20)
project(rlvoxel C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(raylib stb fastnoiselite)

add_subdirectory(libvoxel)
add_subdirectory(raylib-cimgui)

set(SOURCES
    src/app.c
    src/gfx/clouds.c
    src/gfx/renderer.c
    src/gfx/selection_highlight.c
    src/game/game.c
    src/game/game_input.c
    src/game/player.c
    src/game/raycast.c
    src/physics/physics.c
    src/world/blocks.c
    src/world/chunk.c
    src/world/world.c
    src/world/worldgen.c
    src/world/lighting.c
    src/world/mesher.c
    src/math/random.c
    src/math/noise.c
    src/profiling/profiler.c
    src/profiling/profiler_renderer.c
    src/diagnostics/telemetry.c
    src/diagnostics/process_stats.c
)

add_executable(rlVoxel ${SOURCES})
target_include_directories(rlVoxel PRIVATE
    src
    ${stb_SOURCE_DIR}
    ${fastnoiselite_SOURCE_DIR}/C
    ${CMAKE_SOURCE_DIR}/libvoxel/include
)

target_link_libraries(rlVoxel PRIVATE raylib m voxel raylib-cimgui)
