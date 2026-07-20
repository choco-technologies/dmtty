#include "dmod.h"
#include "dmosi.h"
#include <errno.h>

/**
 * @brief Environment variable naming the module to run as the actual shell
 *
 * console is not a terminal itself (dmell already is one) - it only wires
 * up stdin/stdout/stderr/stdlog for the module named here and starts it.
 */
#define CONSOLE_SHELL_ENV_VAR "DMOD_SHELL"

static void print_usage(const char *prog)
{
    Dmod_Printf("Usage: %s <device_path>\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("Not meant to be run by hand - started once per tty device by\n");
    Dmod_Printf("libsystemd from console@.ini, with <device_path> supplied as the\n");
    Dmod_Printf("unit's %%v/user_parameter (see console.rules and dmtty's\n");
    Dmod_Printf("dmdrvi_path_ready -> libsystemd_notify_device_added() report).\n");
    Dmod_Printf("\n");
    Dmod_Printf("Reads the module to run from the %s environment variable,\n", CONSOLE_SHELL_ENV_VAR);
    Dmod_Printf("binds its stdin/stdout/stderr/stdlog to <device_path>, and starts it.\n");
}

/**
 * @brief Main function of the application
 *
 * @param argc Number of arguments
 * @param argv Array of arguments
 *
 * @return 0 if success, error code otherwise
 */
int main(int argc, char *argv[])
{
    if (argc < 2 || argv[1][0] == '\0')
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *device_path = argv[1];

    const char *shell_module = Dmod_GetEnv(CONSOLE_SHELL_ENV_VAR);
    if (shell_module == NULL || shell_module[0] == '\0')
    {
        Dmod_Printf("console: %s is not set, nothing to start on '%s'\n", CONSOLE_SHELL_ENV_VAR, device_path);
        return 1;
    }

    const Dmod_StreamRedirection_t entries[] =
    {
        { DMOD_STDIN,  device_path },
        { DMOD_STDOUT, device_path },
        { DMOD_STDERR, device_path },
        { DMOD_STDLOG, device_path },
    };
    const Dmod_StreamRedirections_t streams = { entries, sizeof(entries) / sizeof(entries[0]) };

    int ret = Dmod_SpawnModule(shell_module, 0, NULL, &streams);
    if (ret < 0)
    {
        Dmod_Printf("console: failed to start '%s' on '%s': error %d\n", shell_module, device_path, ret);
        return 1;
    }

    // Deliberately not returning here: libsystemd only ever tracks the PID this unit's
    // own process was spawned with (see console@.ini/libsystemd_start_service_internal()),
    // never the shell's - so as far as service-stop/device-removal handling is concerned,
    // this process *is* the console@<device> unit for as long as the shell runs. Waiting
    // here (rather than exiting right after the spawn, as this used to do) keeps that PID
    // alive and findable for the whole session, so dmosi_process_kill() on it - which
    // recursively kills every process parented under it, i.e. the shell and anything it
    // spawned - actually reaches the shell instead of finding nothing left to kill.
    //
    // The shell still gets its own freshly-sized stack from Dmod_SpawnModule above (per
    // its own module's declared stack size) - it is not run in-place on this thread's own
    // stack, which was only ever sized for console itself.
    dmosi_process_t shell_process = dmosi_process_find_by_id((dmosi_process_id_t)ret);
    if (shell_process != NULL)
    {
        dmosi_process_wait(shell_process, -1);
    }

    return 0;
}
