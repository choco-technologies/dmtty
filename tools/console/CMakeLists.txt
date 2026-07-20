# =====================================================================
#               console - Per-Device Shell Launcher
# =====================================================================
# Added via add_subdirectory() from the top-level dmtty CMakeLists.txt, so it
# shares dmtty's own dmod fetch/configuration (DMOD_MODULE_NAME, DMOD_DIR,
# DMOD_MODULE_VERSION, ...) instead of setting up a second, separate DMOD
# project - see tools/tty/CMakeLists.txt for the same pattern.

# Path to the DMR file for this module - setting this before dmod_add_executable
# makes the build automatically produce a ready-to-publish release package
# (via mkdmrpkg) under build/packages/console.zip, right alongside dmtty.zip
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/console.dmr)

dmod_add_executable(console ${DMOD_MODULE_VERSION}
    main.c
)
