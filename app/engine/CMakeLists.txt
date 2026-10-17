# libCerfEngine without its x86 JIT: upstream sources + our Linux platform layer + ArmInterp.
# Host build (x86_64 Fedora) compiles everything; aarch64 also drops VFP/NEON (x86 SSE csr) -- see plan.
set(UP ${CMAKE_CURRENT_SOURCE_DIR}/../../cerf/libCerfEngine)
file(GLOB UP_SRC CONFIGURE_DEPENDS ${UP}/src/*.cpp ${UP}/src/arm/*.cpp ${UP}/src/arm/place/*.cpp)
list(FILTER UP_SRC EXCLUDE REGEX "/(engine|host_platform_win32|arm_jit|emit_jump_cache_probe|arm_tlb_ops)\\.cpp$")
# Patched copy of upstream's include tree: ArmTlbEntry::va_addend (host - va) must be pointer-sized on a 64-bit host, and the
# 16-byte/64-byte-set layout asserts only serve the x86 emitter. Done at configure time so upstream stays untouched.
set(PINC ${CMAKE_CURRENT_BINARY_DIR}/include)
file(COPY ${UP}/include/ DESTINATION ${PINC})
file(READ ${PINC}/cerfengine/arm/arm_mmu_state.h MMU_H)
string(REPLACE "uint32_t va_addend;" "uintptr_t va_addend;" MMU_H "${MMU_H}")
string(REGEX REPLACE "static_assert\\(sizeof\\(ArmTlbEntry\\) == 16,[^;]*;" "" MMU_H "${MMU_H}")
string(REGEX REPLACE "static_assert\\(\\(kArmTlbWays \\* sizeof\\(ArmTlbEntry\\)\\)[^;]*;" "" MMU_H "${MMU_H}")
file(WRITE ${PINC}/cerfengine/arm/arm_mmu_state.h "${MMU_H}")
# gcc 12 (Aurora SDK) rejects `return {.action = ...}` for ArmCoprocDecision because of this array's `= {}` initializer.
file(READ ${PINC}/cerfengine/arm/arm_system_host.h SYS_H)
string(REPLACE "cases[kArmCoprocMaxCases] = {};" "cases[kArmCoprocMaxCases];" SYS_H "${SYS_H}")
file(WRITE ${PINC}/cerfengine/arm/arm_system_host.h "${SYS_H}")
add_library(cerfengine STATIC ${UP_SRC} engine_linux.cpp host_platform_linux.cpp arm_jit.cpp arm_interp.cpp arm_tlb_ops.cpp jit_stubs.cpp)
target_include_directories(cerfengine PUBLIC ${PINC} ${CMAKE_CURRENT_SOURCE_DIR}/../../cerf/libservicelocator/include
  PRIVATE ${UP}/src ${UP}/src/arm shim)
# Dynarmic JIT (A32 frontend only, bundled externals incl. the vendored Boost headers in third_party/boost). Off on aarch64 for now: the
# phone build is interpreter-only until dynarmic's arm64 backend is wired into the Aurora build and the JIT is stable.
option(CEWM_DYNARMIC "Build the dynarmic JIT" ON)
if(CEWM_DYNARMIC)
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
  set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
  set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
  set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
  set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
  set(Boost_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/boost CACHE PATH "" FORCE)
  set(Boost_NO_BOOST_CMAKE ON CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../dynarmic dynarmic EXCLUDE_FROM_ALL)
  target_link_libraries(cerfengine PUBLIC dynarmic)
  # Our JIT glue includes dynarmic's frontend headers (a32_ir_emitter.h), which need its bundled externals.
  set(DYN ${CMAKE_CURRENT_SOURCE_DIR}/../../dynarmic)
  target_include_directories(cerfengine PRIVATE ${DYN}/src ${DYN}/externals/mcl/include ${DYN}/externals/fmt/include ${DYN}/externals/robin-map/include ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/boost)
  target_compile_definitions(cerfengine PUBLIC CEWM_HAVE_DYNARMIC)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|^arm")
  target_include_directories(cerfengine PRIVATE shim_a64)
endif()
target_compile_features(cerfengine PUBLIC cxx_std_20)
