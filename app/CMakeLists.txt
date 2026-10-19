cmake_minimum_required(VERSION 3.13)
project(cewm C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Upstream libCerfEngine needs CMake 3.25 (Aurora SDK has 3.19), so engine/ builds its sources itself; hw/ is CERF's board/SoC code.
# Baseline ARMv8.0 + CRC32 so one binary runs on every device (the MT8788 tablet is A53/A73: no LSE atomics, no fp16); tuned for the big cores.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  add_compile_options(-march=armv8-a+crc -mtune=cortex-a76)
endif()
add_subdirectory(engine)
add_subdirectory(hw)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_library(imgui STATIC
  third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_sdl2.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends ${SDL2_INCLUDE_DIRS})
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES2)

add_executable(cewm src/main.cpp src/launcher/theme.cpp host/haptic.cpp)
pkg_check_modules(DBUS dbus-1)
if(DBUS_FOUND)
  target_sources(cewm PRIVATE host/device_stats.cpp)
  target_compile_definitions(cewm PRIVATE CEWM_HAVE_DBUS)
  target_include_directories(cewm PRIVATE ${DBUS_INCLUDE_DIRS})
  target_link_directories(cewm PRIVATE ${DBUS_LIBRARY_DIRS})
  target_link_libraries(cewm PRIVATE ${DBUS_LIBRARIES})
else()
  target_sources(cewm PRIVATE host/device_stats.cpp)
endif()
# wl_surface_set_buffer_transform for the screen rotation hint (a no-op unless SDL runs on Wayland)
pkg_check_modules(WAYLAND wayland-client)
if(WAYLAND_FOUND)
  target_compile_definitions(cewm PRIVATE CEWM_WAYLAND)
  target_include_directories(cewm PRIVATE ${WAYLAND_INCLUDE_DIRS})
  target_link_directories(cewm PRIVATE ${WAYLAND_LIBRARY_DIRS})
  target_link_libraries(cewm PRIVATE ${WAYLAND_LIBRARIES})
endif()
target_include_directories(cewm PRIVATE src/launcher)
target_link_directories(cewm PRIVATE ${SDL2_LIBRARY_DIRS})
target_include_directories(cewm PRIVATE host)
# Whole-archive: CERF services register themselves from static constructors.
target_link_libraries(cewm PRIVATE -Wl,--whole-archive cerfhw cerfengine -Wl,--no-whole-archive imgui ${SDL2_LIBRARIES} GLESv2 EGL dl pthread)
