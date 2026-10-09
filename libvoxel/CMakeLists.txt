add_library(voxel STATIC
    src/block.c
    src/chunk.c
    src/mesher.c
)

target_include_directories(voxel PUBLIC include)
target_include_directories(voxel PRIVATE src ${stb_SOURCE_DIR})
