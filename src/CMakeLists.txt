add_definitions(
    #-ggdb3
    -std=gnu99
    )

include_directories(
    ${GLIB2_INCLUDE_DIRS}
    ${LCMTYPES_INCLUDE_DIRS}
    )

add_executable(camera-stream main.c)

target_link_libraries (camera-stream ${LCMTYPES_LIBS})

pods_use_pkg_config_packages(camera-stream
    glib-2.0
    gobject-2.0
    gthread-2.0
    lcm
    bot2-core
    bot2-param-client
    camunits
    path-utils)

pods_install_executables(camera-stream)
