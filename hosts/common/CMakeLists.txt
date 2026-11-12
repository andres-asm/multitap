# host_common: code shared by every core host (host-libretro, later host-jg,
# host-mame, ...): dynamic library loading, logging, later the IPC transport.
# Must not depend on SDL or any frontend code.

add_library(host_common STATIC
    host_log.cpp
    dynamic_library.cpp
    host_fs.cpp
)

target_include_directories(host_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(host_common PUBLIC ${CMAKE_DL_LIBS})

multitap_set_project_flags(host_common)
