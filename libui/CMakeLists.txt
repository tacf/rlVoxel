add_library(ui STATIC
    src/ui.c
)

target_include_directories(ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ui
    PUBLIC
        raylib
        m
)
