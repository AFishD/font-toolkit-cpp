cmake_minimum_required(VERSION 3.16)
project(ftk VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ------------------------------------------------------- third_party (brotli)
# Vendored Brotli decoder (MIT, see third_party/brotli/LICENSE) — required for
# WOFF2 compression. Only the decoder + common parts are compiled.
file(GLOB BROTLI_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/c/common/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/c/dec/*.c)
add_library(brotlidec STATIC ${BROTLI_SRC})
target_include_directories(brotlidec
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli/c/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/brotli)
set_target_properties(brotlidec PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------ library
file(GLOB FTK_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/support/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/sfnt/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tables/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/glyf/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cff/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/var/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/layout/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/color/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dump/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/subset/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/api/*.cpp
)

add_library(ftk SHARED ${FTK_SRC}
  ${CMAKE_CURRENT_SOURCE_DIR}/include/ftk/ftk.h)
set_target_properties(ftk PROPERTIES
  DEFINE_SYMBOL FTK_BUILDING_DLL)
if(WIN32)
  # Ensure C++ symbols are exported alongside the dllexport'ed C ABI
  # (binutils disables auto-export once any dllexport is present).
  target_link_options(ftk PRIVATE -Wl,--export-all-symbols)
endif()
target_link_libraries(ftk PRIVATE brotlidec)

# ------------------------------------------------------------------ CLI
file(GLOB FTK_CLI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.cpp)
add_executable(ftk_cli ${FTK_CLI_SRC})
set_target_properties(ftk_cli PROPERTIES OUTPUT_NAME ftk)
target_link_libraries(ftk_cli PRIVATE ftk)

# ------------------------------------------------------------------ tests
enable_testing()
file(GLOB FTK_TEST_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(ftk_tests ${FTK_TEST_SRC})
target_link_libraries(ftk_tests PRIVATE ftk)
add_test(NAME ftk_tests COMMAND ftk_tests)

# ------------------------------------------------------- diagnostic probes
option(FTK_BUILD_PROBES "Build diagnostic probe executables" ON)
if(FTK_BUILD_PROBES)
  foreach(probe cffprobe probe varprobe subsetprobe)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/probes/${probe}.cpp)
      add_executable(ftk_${probe} ${CMAKE_CURRENT_SOURCE_DIR}/tools/probes/${probe}.cpp)
      target_link_libraries(ftk_${probe} PRIVATE ftk)
    endif()
  endforeach()
  # external C consumer of the plugin ABI (built as C to prove the ABI works
  # from a plain C host, not just from C++)
  add_executable(ftk_plugindemo ${CMAKE_CURRENT_SOURCE_DIR}/tools/probes/plugindemo.c)
  target_link_libraries(ftk_plugindemo PRIVATE ftk)
endif()

install(TARGETS ftk ftk_cli RUNTIME DESTINATION bin
                    LIBRARY DESTINATION lib
                    ARCHIVE DESTINATION lib)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/ftk/ftk.h DESTINATION include/ftk)
