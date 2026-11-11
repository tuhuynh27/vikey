# Vikey on Linux: the engine, the test tools (for Tools/autoswitch/check.sh) and the IBus engine.
# Configure:  cmake -S Linux -B Linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
# Build:      cmake --build Linux/build
cmake_minimum_required(VERSION 3.21)
project(Vikey LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Install to /usr like the packages do: the IBus component file bakes the libexec path in, so a /usr/local
# default would make the .deb/.rpm point at a binary that is not there.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "install prefix" FORCE)
endif()
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(ENGINE ${ROOT}/Engine)
set(AS ${ROOT}/Tools/autoswitch)
set(ENGINE_SRC ${ENGINE}/Engine.cpp ${ENGINE}/Vietnamese.cpp ${ENGINE}/SmartSwitchKey.cpp)
# One version for every platform: project.yml's MARKETING_VERSION (also the package version below).
if(NOT VIKEY_VERSION)
  file(STRINGS "${ROOT}/project.yml" _mv REGEX "MARKETING_VERSION: \"")
  string(REGEX REPLACE ".*MARKETING_VERSION: \"([^\"]+)\".*" "\\1" VIKEY_VERSION "${_mv}")
endif()
message(STATUS "Vikey version ${VIKEY_VERSION}")

add_library(vikey_engine STATIC ${ENGINE_SRC} ${ENGINE}/Driver.cpp ${ENGINE}/TouchModel.cpp)
target_include_directories(vikey_engine PUBLIC ${ENGINE} ${ENGINE}/platforms)
target_compile_options(vikey_engine PUBLIC -w -O2 -fPIC)

add_library(vikey_engine_testing STATIC ${ENGINE_SRC})
target_include_directories(vikey_engine_testing PUBLIC ${ENGINE} ${ENGINE}/platforms)
target_compile_definitions(vikey_engine_testing PUBLIC AS_TESTING)
target_compile_options(vikey_engine_testing PUBLIC -w -O2)

function(vikey_tool name src outdir lib)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE ${lib})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${outdir})
endfunction()
vikey_tool(harness   ${AS}/test/harness.cpp     ${AS}/test   vikey_engine)
vikey_tool(sim       ${AS}/sim/sim.cpp           ${AS}/sim    vikey_engine_testing)
vikey_tool(drvlazy   ${AS}/driver/drvlazy.cpp    ${AS}/driver vikey_engine)
vikey_tool(bench     ${AS}/driver/bench.cpp      ${AS}/driver vikey_engine)
vikey_tool(undo_test ${AS}/driver/undo_test.cpp  ${AS}/driver vikey_engine)
vikey_tool(touchsim  ${AS}/touch/touchsim.cpp    ${AS}/touch  vikey_engine)
add_executable(surrounding_test Tests/surrounding_test.cpp)   # the IBus engine's delete limit (#32), no IBus needed
target_include_directories(surrounding_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ibus)

option(VIKEY_IBUS "Build the IBus engine (needs libibus-1.0 + gtk4/libadwaita)" ON)
if(VIKEY_IBUS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ibus/CMakeLists.txt)
  add_subdirectory(ibus)
endif()

# ---- packages: .deb (Ubuntu/Debian) and .rpm (Fedora) via CPack -----------------------------------------
# The Arch .pkg.tar.zst is repacked from the .deb in CI (Linux/packaging/make-pacman.sh), not a CPack generator.
set(CPACK_PACKAGE_NAME "vikey")
set(CPACK_PACKAGE_VERSION "${VIKEY_VERSION}")
set(CPACK_PACKAGE_CONTACT "Tony Dinh <patent@tdinh.me>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vikey - Vietnamese input (Telex) with automatic English/Vietnamese switching")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://vikey.app")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "ibus (>= 1.5), libgtk-4-1, libadwaita-1-0, libgstreamer1.0-0, gstreamer1.0-plugins-base, gstreamer1.0-plugins-good, libnotify-bin, fonts-noto-color-emoji, curl")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)   # adds the minimum libc6, libstdc++6, GTK and libadwaita versions the binaries need, so the .deb no longer installs where it can't start (Ubuntu 22.04, Debian 12)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst")
set(CPACK_RPM_PACKAGE_REQUIRES "ibus >= 1.5, gtk4, libadwaita, gstreamer1, gstreamer1-plugins-base, gstreamer1-plugins-good, libnotify, google-noto-emoji-color-fonts, curl")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst")
set(CPACK_GENERATOR "DEB;RPM")
include(CPack)
