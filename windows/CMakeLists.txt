cmake_minimum_required(VERSION 3.28)
project(pokopia_native_windows LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# The static C runtime (/MT) everywhere, as LLVM's libraries already require of nxlift: the tools the installer ships run on a PC without the
# Visual C++ Redistributable (docs/M7-REPORT.md). -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL gives the old /MD tree.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  # The build folder's paths stay out of what is compiled (__FILE__, the semantics bitcode nxlift embeds): the shipped tools name no build PC's folders
  set(NXW_PATH_MAP "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=" "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=" "-fdebug-compilation-dir=.")
  add_compile_options(${NXW_PATH_MAP})
endif()
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
# Linux (docs/LINUX-PLAN.md): glibc's full API; the tools carry their own C++ runtime, so the kit runs on older and newer distributions alike
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(NXW_LINUX ON)
  add_compile_definitions(_GNU_SOURCE)
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

enable_testing()

# One folder per component; each owns its CMakeLists.txt
add_subdirectory(src/common)
add_subdirectory(src/abi)
add_subdirectory(src/nxload)
add_subdirectory(src/a64)
add_subdirectory(src/nxanalyze)
add_subdirectory(src/sem)
add_subdirectory(src/nxlift)
add_subdirectory(src/interp)
if(WIN32 OR NXW_LINUX)
  add_subdirectory(src/nxrt)
endif()
if(APPLE)
  add_subdirectory(src/swapin)   # the Mac swap-in harness (docs/m4-swapin.md): the tools above plus the arm64 runtime
endif()
add_subdirectory(src/s2tex)
add_subdirectory(src/nxshader)   # the shader tool's tests (docs/m5-record-shaders.md)
add_subdirectory(src/coop)       # Link Play between our builds (docs/coop-step1.md)
