# Vikey engine + test tools, cross-platform (MSVC on Windows, clang on macOS for validation).
# Configure:  cmake -S Windows -B Windows/build -G Ninja -DCMAKE_BUILD_TYPE=Release
# Build:      cmake --build Windows/build
# The test binaries land where Tools/autoswitch/check.sh and the Python scripts expect them.
cmake_minimum_required(VERSION 3.21)
# Static C++ runtime (/MT): Vikey.exe runs without the Visual C++ Redistributable, which the installer and zip don't ship (#79)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(Vikey LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ENGINE ${ROOT}/Engine)
set(AS ${ROOT}/Tools/autoswitch)

set(ENGINE_SRC ${ENGINE}/Engine.cpp ${ENGINE}/Vietnamese.cpp ${ENGINE}/SmartSwitchKey.cpp)

add_library(vikey_engine STATIC ${ENGINE_SRC} ${ENGINE}/Driver.cpp ${ENGINE}/TouchModel.cpp)
target_include_directories(vikey_engine PUBLIC ${ENGINE} ${ENGINE}/platforms)
if(MSVC)
  target_compile_options(vikey_engine PUBLIC /utf-8 /W0 /EHsc /O2 /bigobj)
  target_compile_definitions(vikey_engine PUBLIC _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(vikey_engine PUBLIC -w -O2)
endif()

# A separate engine build with AS_TESTING for the simulator (check.sh compiles sim with -DAS_TESTING).
add_library(vikey_engine_testing STATIC ${ENGINE_SRC})
target_include_directories(vikey_engine_testing PUBLIC ${ENGINE} ${ENGINE}/platforms)
target_compile_definitions(vikey_engine_testing PUBLIC AS_TESTING)
if(MSVC)
  target_compile_options(vikey_engine_testing PUBLIC /utf-8 /W0 /EHsc /O2 /bigobj)
  target_compile_definitions(vikey_engine_testing PUBLIC _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(vikey_engine_testing PUBLIC -w -O2)
endif()

function(vikey_tool name src outdir lib)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE ${lib})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${outdir}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${outdir} RUNTIME_OUTPUT_DIRECTORY_DEBUG ${outdir})
endfunction()

vikey_tool(harness  ${AS}/test/harness.cpp   ${AS}/test   vikey_engine)
vikey_tool(sim      ${AS}/sim/sim.cpp         ${AS}/sim    vikey_engine_testing)
vikey_tool(drvlazy  ${AS}/driver/drvlazy.cpp  ${AS}/driver vikey_engine)
vikey_tool(bench    ${AS}/driver/bench.cpp    ${AS}/driver vikey_engine)
vikey_tool(touchsim ${AS}/touch/touchsim.cpp  ${AS}/touch  vikey_engine)

# Windows app (added in Phase 2)
if(WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/App/CMakeLists.txt)
  add_subdirectory(App)
endif()
