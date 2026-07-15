#include "dmod.h"
#include "dmtty.h"
#include <string.h>
#include <stdbool.h>
#include <errno.h>

static void print_usage(const char *prog)
{
    Dmod_Printf("Usage: %s attach <backing_path> [node_name] [options]\n", prog);
    Dmod_Printf("       %s detach <node_name>\n", prog);
    Dmod_Printf("\n");
    Dmod_Printf("attach - turns <backing_path> (e.g. /dev/dmuart1) into a new dmtty\n");
    Dmod_Printf("         terminal node, the same way dmtty_attach() would from code.\n");
    Dmod_Printf("         If node_name is omitted, dmtty picks one automatically (ttyN).\n");
    Dmod_Printf("\n");
    Dmod_Printf("         Options:\n");
    Dmod_Printf("           --echo on|off        Echo bytes back to the backing file (default: on)\n");
    Dmod_Printf("           --canonical on|off   Line-buffered input (default: on)\n");
    Dmod_Printf("\n");
    Dmod_Printf("detach - removes a node previously created with attach (or the\n");
    Dmod_Printf("         dmhaman hot-plug event). The main 'tty' node cannot be detached.\n");
    Dmod_Printf("\n");
    Dmod_Printf("Requires /dev/tty to already be configured by dmdevfs, see\n");
    Dmod_Printf("docs/configuration.md.\n");
}

/**
 * @brief Parses an on/off-style flag value
 *
 * Mirrors dmtty's own [dmtty] config parsing (on/off, 1/0, true/false), so
 * --echo/--canonical accept the same values as dmdevfs's config.ini.
 *
 * @return 0 on success (value written to *out), -EINVAL on an unrecognized value
 */
static int parse_bool(const char *value, bool *out)
{
    if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0 || strcmp(value, "true") == 0)
    {
        *out = true;
        return 0;
    }
    if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0 || strcmp(value, "false") == 0)
    {
        *out = false;
        return 0;
    }
    return -EINVAL;
}

static int run_attach(int argc, char *argv[])
{
    if (argc < 1)
    {
        Dmod_Printf("tty: attach requires <backing_path>\n");
        return -EINVAL;
    }

    const char *backing_path = argv[0];
    const char *node_name = NULL;
    bool echo = true;
    bool canonical = true;

    int i = 1;
    if (i < argc && strncmp(argv[i], "--", 2) != 0)
    {
        node_name = argv[i];
        i++;
    }

    for (; i < argc; i++)
    {
        if (strcmp(argv[i], "--echo") == 0 && i + 1 < argc)
        {
            if (parse_bool(argv[++i], &echo) != 0)
            {
                Dmod_Printf("tty: invalid value for --echo: '%s'\n", argv[i]);
                return -EINVAL;
            }
        }
        else if (strcmp(argv[i], "--canonical") == 0 && i + 1 < argc)
        {
            if (parse_bool(argv[++i], &canonical) != 0)
            {
                Dmod_Printf("tty: invalid value for --canonical: '%s'\n", argv[i]);
                return -EINVAL;
            }
        }
        else
        {
            Dmod_Printf("tty: unknown option '%s'\n", argv[i]);
            return -EINVAL;
        }
    }

    uint32_t flags = (echo ? dmtty_flag_echo : 0) | (canonical ? dmtty_flag_canonical : 0);

    int ret = dmtty_attach(backing_path, node_name, flags);
    if (ret != 0)
    {
        Dmod_Printf("tty: failed to attach '%s': error %d\n", backing_path, ret);
        return ret;
    }

    if (node_name != NULL)
    {
        Dmod_Printf("tty: attached '%s' as '%s' (echo=%s, canonical=%s)\n",
                    backing_path, node_name, echo ? "on" : "off", canonical ? "on" : "off");
    }
    else
    {
        Dmod_Printf("tty: attached '%s' (echo=%s, canonical=%s)\n",
                    backing_path, echo ? "on" : "off", canonical ? "on" : "off");
    }

    return 0;
}

static int run_detach(int argc, char *argv[])
{
    if (argc < 1)
    {
        Dmod_Printf("tty: detach requires <node_name>\n");
        return -EINVAL;
    }

    const char *node_name = argv[0];

    int ret = dmtty_detach(node_name);
    if (ret != 0)
    {
        Dmod_Printf("tty: failed to detach '%s': error %d\n", node_name, ret);
        return ret;
    }

    Dmod_Printf("tty: detached '%s'\n", node_name);
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
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        print_usage(argv[0]);
        return (argc < 2) ? 1 : 0;
    }

    if (strcmp(argv[1], "attach") == 0)
    {
        return (run_attach(argc - 2, argv + 2) == 0) ? 0 : 1;
    }

    if (strcmp(argv[1], "detach") == 0)
    {
        return (run_detach(argc - 2, argv + 2) == 0) ? 0 : 1;
    }

    Dmod_Printf("tty: unknown command '%s'\n", argv[1]);
    print_usage(argv[0]);
    return 1;
}
