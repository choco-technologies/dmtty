# =====================================================================
#               console - Per-Device Shell Launcher
# =====================================================================
set(DMOD_MODULE_NAME        console)
# console loads and enables the whole shell module on its own stack
# (Dmod_RunModuleDetached), peaking at ~1.5 KiB on Cortex-M7 with an exception
# frame on top; 1024 (+512 dmosi overhead) overflowed
set(DMOD_STACK_SIZE         2048)
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/console.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    main.c
)

dmod_link_modules(${DMOD_MODULE_NAME} 
    dmosi
    libsystemd
)
