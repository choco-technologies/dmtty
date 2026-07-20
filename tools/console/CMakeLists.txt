# =====================================================================
#               console - Per-Device Shell Launcher
# =====================================================================
set(DMOD_MODULE_NAME        console)
set(DMOD_STACK_SIZE         1024)
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/console.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    main.c
)

dmod_link_modules(${DMOD_MODULE_NAME} 
    dmosi
)
