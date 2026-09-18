#include "dmod.h"
#include "dmosi.h"
#include "libsystemd.h"
#include <errno.h>

/**
 * @brief Environment variable naming the module to run as the actual shell
 *
 * console is not a terminal itself (dmell already is one) - it only wires
 * up stdin/stdout/stderr/stdlog for the module named here and starts it.
 */
#define CONSOLE_SHELL_ENV_VAR "DMOD_SHELL"

/**
 * @brief Close every stream this process holds on its tty, before exiting
 *
 * console's own stdin/stdout/stderr/stdlog are bound to the same device as the
 * shell's (see console@.ini), and a process that exits does *not* get them
 * closed for it: dmosi_process_kill() marks the process terminated and kills
 * its threads, but never calls dmosi_process_destroy(), which is what would
 * close the handles. They would stay open for the lifetime of the system.
 *
 * That is not merely untidy for a tty: dmtty hands the "foreground" reader
 * role to the first handle opened on a node and only releases it when that
 * handle is closed, so a leaked stdin here means the shell's own reads spin
 * forever waiting for a turn that never comes - writes work, input is dead.
 *
 * Deliberately done last, after everything this process still wants to print:
 * once stdout is released, Dmod_Printf() falls back to the raw kernel log
 * rather than the tty.
 *
 * A NULL current process is the only case worth handling: a thread dmosi never
 * registered has no streams to release in the first place.
 */
static void release_own_streams(void)
{
    dmosi_process_t self = dmosi_process_current();
    if (self == NULL)
    {
        return;
    }

    for (dmosi_stream_index_t index = 0; index < DMOSI_STREAM_COUNT; index++)
    {
        dmosi_process_set_stream(self, index, NULL);
    }
}

static void print_usage(const char *prog)
{
    Dmod_Printf("Usage: %s <device_path> [unit_name]\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("Not meant to be run by hand - started once per tty device by\n");
    Dmod_Printf("libsystemd from console@.ini, with <device_path> supplied as the\n");
    Dmod_Printf("unit's %%v/user_parameter (see console.rules and dmtty's\n");
    Dmod_Printf("dmdrvi_path_ready -> libsystemd_notify_device_added() report).\n");
    Dmod_Printf("\n");
    Dmod_Printf("Reads the module to run from the %s environment variable,\n", CONSOLE_SHELL_ENV_VAR);
    Dmod_Printf("binds its stdin/stdout/stderr/stdlog to <device_path>, and starts it.\n");
    Dmod_Printf("\n");
    Dmod_Printf("[unit_name] is this console's own libsystemd unit (%%n in console@.ini),\n");
    Dmod_Printf("handed to libsystemd_notify_main_pid() so the unit follows the shell.\n");
}

/**
 * @brief Start the shell on the device named by argv[1] and hand the unit over to it
 *
 * Split out of main() so that every exit path - including the early argument/
 * environment failures - runs through release_own_streams() exactly once.
 *
 * @param argc Number of arguments
 * @param argv Array of arguments
 *
 * @return 0 if success, error code otherwise
 */
static int run_console(int argc, char *argv[])
{
    if (argc < 2 || argv[1][0] == '\0')
    {
        print_usage(argv[0]);
        return 1;
    }

    const char *device_path = argv[1];
    // Own unit name, passed as %n by console@.ini. Optional: without it the
    // handover below falls back to libsystemd resolving the unit from this
    // process's own PID, which only works once libsystemd has recorded the PID
    // it spawned - a console that gets to run before that assignment (or one
    // started by hand) would find nothing to re-point.
    const char *unit_name = (argc > 2 && argv[2][0] != '\0') ? argv[2] : NULL;

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

    // Dmod_RunModuleDetached(), not Dmod_SpawnModule(): the latter parents the shell
    // under this process, and a process exiting takes its whole parented subtree with
    // it (Dmod_Exit -> dmosi_process_kill -> kill_process_tree), so console would kill
    // the very shell it just started the moment main() returned. Detached, the shell's
    // lifetime is its own and console is free to exit immediately.
    int ret = Dmod_RunModuleDetached(shell_module, 0, NULL, &streams);
    if (ret < 0)
    {
        Dmod_Printf("console: failed to start '%s' on '%s': error %d\n", shell_module, device_path, ret);
        return 1;
    }

    // Hand the unit over to the shell. libsystemd tracks whichever PID it spawned -
    // this process - so without this it would keep aiming service-stop and
    // device-removal teardown at a console that exits a few lines below, and never
    // reach the shell. No connectivity check around this call: libsystemd is a hard
    // import (see dmod_link_modules in CMakeLists.txt), and a module whose imports
    // are unresolved never gets enabled at all - DMOD_MANUAL_LOAD is OFF here, so
    // Dmod_Enable() rejects it (see IsAllApiConnected() in dmod_system.c).
    // A failure here is therefore a real one (no such unit, dead PID), not a
    // missing integration, so it is reported rather than silently skipped.
    int notify_ret = libsystemd_notify_main_pid(unit_name, (Dmod_Pid_t)ret);
    if (notify_ret != 0)
    {
        Dmod_Printf("console: could not hand '%s' (pid %d) over to unit '%s': error %d\n",
                    shell_module, ret, (unit_name != NULL) ? unit_name : "<self>", notify_ret);
    }

    return 0;
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
    int result = run_console(argc, argv);

    // Last thing this process does - see release_own_streams(). Also on the
    // failure paths: a console that could not start a shell must not leave the
    // device's foreground handle claimed by a process that is about to exit,
    // or the next `service start console@<name>` would come up mute.
    release_own_streams();

    return result;
}
