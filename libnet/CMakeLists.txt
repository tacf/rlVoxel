add_library(net STATIC
    src/protocol.c
    src/transport.c
    src/net.c
)

target_include_directories(net
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${enet_SOURCE_DIR}/include
)

target_link_libraries(net
    PUBLIC
        ${ENET_TARGET}
        Threads::Threads
)
