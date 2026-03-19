cmake_minimum_required(VERSION 3.20)

# Fetch cimgui (includes Dear ImGui)
include(FetchContent)

FetchContent_Declare(
    cimgui
    GIT_REPOSITORY https://github.com/cimgui/cimgui.git
    GIT_TAG 1.90.4dock
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(cimgui)

# Create raylib-cimgui static library
add_library(raylib-cimgui STATIC
    rlcimgui.c
)

# Include directories
target_include_directories(raylib-cimgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${cimgui_SOURCE_DIR}
    ${cimgui_SOURCE_DIR}/imgui
)

# Link against raylib and cimgui
target_link_libraries(raylib-cimgui PUBLIC raylib cimgui)

# Set C standard
set_target_properties(raylib-cimgui PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
