# ibus-engine-vikey (+ later vikey-settings). Version = project.yml MARKETING_VERSION, like the other platforms.
find_package(PkgConfig REQUIRED)
pkg_check_modules(IBUS REQUIRED IMPORTED_TARGET ibus-1.0 glib-2.0 gio-2.0 gstreamer-1.0)
pkg_check_modules(X11 IMPORTED_TARGET x11)
include(GNUInstallDirs)

add_executable(ibus-engine-vikey Engine.cpp Core.cpp Prefs.cpp Crash.cpp)
target_include_directories(ibus-engine-vikey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ibus-engine-vikey PRIVATE VIKEY_VERSION="${VIKEY_VERSION}" VIKEY_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/vikey")
target_link_libraries(ibus-engine-vikey PRIVATE vikey_engine PkgConfig::IBUS)
if(X11_FOUND)
  target_compile_definitions(ibus-engine-vikey PRIVATE VIKEY_HAVE_X11)
  target_link_libraries(ibus-engine-vikey PRIVATE PkgConfig::X11)
endif()
configure_file(vikey.xml.in ${CMAKE_CURRENT_BINARY_DIR}/vikey.xml @ONLY)
install(TARGETS ibus-engine-vikey DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vikey.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/ibus/component)
# shared data: the switch sounds (the Mac's .caf files as .wav) and the emoji list of the picker
install(FILES ../sounds/SwitchToVietnamese.wav ../sounds/SwitchToEnglish.wav DESTINATION ${CMAKE_INSTALL_DATADIR}/vikey/sounds)
install(FILES ${ROOT}/App/Resources/Emoji.json DESTINATION ${CMAKE_INSTALL_DATADIR}/vikey)

add_subdirectory(../settings ${CMAKE_CURRENT_BINARY_DIR}/settings)
