# =====================================================================
#               tty - Manual dmtty Attach Tool
# =====================================================================
# Added via add_subdirectory() from the top-level CMakeLists.txt, so it
# shares dmtty's own dmod fetch/configuration (DMOD_MODULE_NAME, DMOD_DIR,
# DMOD_MODULE_VERSION, ...) instead of setting up a second, separate DMOD
# project - see examples/module/CMakeLists.txt (library + application
# together) and dmffs's make_dmffs app for the same pattern elsewhere in
# the DMOD ecosystem.

# Path to the DMR file for this module - setting this before dmod_add_executable
# makes the build automatically produce a ready-to-publish release package
# (via mkdmrpkg) under build/packages/tty.zip, right alongside dmtty.zip
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/tty.dmr)

dmod_add_executable(tty ${DMOD_MODULE_VERSION}
    main.c
)

# dmtty.h also pulls in dmtty_defs.h, generated a moment ago by
# dmod_add_library(dmtty ...) in the parent CMakeLists.txt under the
# top-level build directory. The real dmtty_attach() implementation is
# resolved at runtime against the system's own dmtty instance, so this
# just adds include directories (no library linkage).
target_include_directories(tty PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)
