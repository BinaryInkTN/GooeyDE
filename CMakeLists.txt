cmake_minimum_required(VERSION 3.10)
project(gooeyde_desktop VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XCURSOR REQUIRED xcursor)
pkg_check_modules(XRANDR REQUIRED xrandr)

find_library(GOOEYGUI_LIB NAMES GooeyGUI-1 PATHS /usr/local/lib)
find_library(GLPS_LIB NAMES GLPS PATHS /usr/local/lib)
include_directories(
    ${DBUS_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${XCURSOR_INCLUDE_DIRS}
    ${XRANDR_INCLUDE_DIRS}
    /usr/local/include/GLPS
     /usr/local/include/Gooey
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/components
)

set(COMMON_LIBS
    ${DBUS_LIBRARIES}
    ${GOOEYGUI_LIB}
    ${GLPS_LIB}
    ${X11_LIBRARIES}
    ${XRANDR_LIBRARIES}
    ${XCURSOR_LIBRARIES}
    m
    pthread
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-rpath,/usr/local/lib")

add_executable(gooey_shell main.c components/gooey_shell.c)
target_link_libraries(gooey_shell ${COMMON_LIBS})

add_executable(gooeyde_appmenu components/gooeyde_appmenu.c)
target_link_libraries(gooeyde_appmenu ${COMMON_LIBS})

add_executable(gooeyde_systemsettings components/gooeyde_systemsettings.c)
target_link_libraries(gooeyde_systemsettings ${COMMON_LIBS})

add_executable(gooeyde_desktop components/gooeyde_desktop.c)
target_link_libraries(gooeyde_desktop ${COMMON_LIBS})

add_custom_command(TARGET gooey_shell POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/roboto.ttf
        ${CMAKE_BINARY_DIR}/roboto.ttf
    COMMENT "Copying assets and font"
)

add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/roboto.ttf
        ${CMAKE_BINARY_DIR}/roboto.ttf
)
add_dependencies(gooeyde_appmenu copy_assets)
add_dependencies(gooeyde_systemsettings copy_assets)
add_dependencies(gooeyde_desktop copy_assets)
