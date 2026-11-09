# =====================================================================
#               console - Per-Device Shell Launcher
# =====================================================================
set(DMOD_MODULE_NAME        console)
# console loads and enables the whole shell module on its own stack
# (Dmod_RunModuleDetached), peaking at ~1.5 KiB on Cortex-M7 with an exception
# frame on top; 1024 (+512 dmosi overhead) overflowed. On the Xtensa T-Deck Pro (ESP32-S3) the peak measured with dmosi_thread_get_info()
# over about 25 starts is 2384..2592 bytes (more than 2560 in about a quarter of them): 2048 (+512 = 2560) overflows then (a hang in
# vApplicationStackOverflowHook);
# 3072 (+512 = 3584) is 38 % over the highest peak
set(DMOD_STACK_SIZE         3072)
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/console.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    main.c
)

dmod_link_modules(${DMOD_MODULE_NAME} 
    dmosi
    libsystemd
)
