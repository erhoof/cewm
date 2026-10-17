# CERF hardware/emulation code (upstream cerf/cerf, MIT) + our POSIX forks in ../port and minimal host services in ../host.
# Source list = files that passed a host syntax check (hw_sources.txt); regenerate when the shim grows.
set(CERF ${CMAKE_CURRENT_SOURCE_DIR}/../../cerf/cerf)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../hw_sources.txt HW_REL)
set(HW_SRC "")
foreach(f ${HW_REL})
  list(APPEND HW_SRC ${CERF}/${f})
endforeach()
add_library(cerfhw STATIC ${HW_SRC}
  ../port/emulated_memory.cpp ../port/virtual_clock.cpp ../port/guest_cycle_clock.cpp ../port/log.cpp
  ../port/paced_wave_out_sdl.cpp ../port/ga_stubs.cpp ../port/slirp_backend.cpp ../port/slirp_icmp_posix.cpp ../port/folder_share_posix.cpp ../port/cerf_virt_framebuffer.cpp ../port/state_stream.cpp ../port/hibernation.cpp ../port/state_boot_gate.cpp
  ../port/storage_posix.cpp ../port/host_file_bytes.cpp ../port/service_stubs.cpp ../port/s3c2410_adc_touch_input.cpp ../port/s3c2410_iis.cpp ../port/guest_clock_touch_input.cpp ../port/compaq_ipaq_h3000_audio_player.cpp ../port/p_sa1111_sac.cpp ../port/p_sa1111_sac_dma.cpp ../port/p_sa1111_sac_rx_dma.cpp ../port/sa1111_sac_host_input.cpp ../port/sa1111_sac_host_output.cpp ../port/sa11xx_dma_audio_player.cpp ../port/siemens_simpad_audio_player.cpp ../port/arm_engine_bridge.cpp ../port/trace_manager.cpp ../host/host_services.cpp ../host/emu_runner.cpp ../host/http_proxy.cpp ../host/rom_download.cpp)
target_include_directories(cerfhw PUBLIC ${CERF} ${CMAKE_CURRENT_SOURCE_DIR}/../engine/shim ${CMAKE_CURRENT_SOURCE_DIR}/../host)
target_compile_definitions(cerfhw PUBLIC CERF_DEV_MODE=0)
target_compile_options(cerfhw PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/../engine/shim/cerf_compat.h)
target_link_libraries(cerfhw PUBLIC cerfengine)

# NAT for the Guest Additions NIC: libslirp 4.9.0 (BSD-3, vendored) on glib; CERF's slirp backend runs on the winsock shim.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
file(GLOB SLIRP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libslirp/src/*.c)
add_library(slirp STATIC ${SLIRP_SRC})
target_include_directories(slirp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libslirp/include ${GLIB_INCLUDE_DIRS})
target_include_directories(slirp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libslirp/src)
target_compile_definitions(slirp PRIVATE _GNU_SOURCE G_LOG_DOMAIN="Slirp")
target_compile_options(slirp PRIVATE -w)
target_link_directories(cerfhw PUBLIC ${GLIB_LIBRARY_DIRS})
target_link_libraries(cerfhw PUBLIC slirp ${GLIB_LIBRARIES})

# Guest audio goes out through SDL.
pkg_check_modules(SDL2 REQUIRED sdl2)
target_include_directories(cerfhw PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_directories(cerfhw PUBLIC ${SDL2_LIBRARY_DIRS})
target_link_libraries(cerfhw PUBLIC ${SDL2_LIBRARIES})

# Host HTTP proxy (https upgrade for the guest). Headers are vendored (laptop lacks curl-devel); the Aurora target ships libcurl.so.4.
find_library(CURL_LIB NAMES curl libcurl.so.4 REQUIRED)
target_include_directories(cerfhw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/curl)
target_link_libraries(cerfhw PUBLIC ${CURL_LIB})
