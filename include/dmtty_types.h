#ifndef DMTTY_TYPES_H
#define DMTTY_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Maximum length of a backing file path (excluding null terminator)
 */
#define DMTTY_MAX_PATH_LEN     63

/**
 * @brief IO flags controlling the line discipline applied to a dmtty device node
 *
 * These mirror the DMOD_STDIN_FLAG_* flags used by dmod's own stdin handling
 * (see dmod_sal.h) and dmlog's DMLOG_FLAG_INPUT_ECHO_OFF / _LINE_MODE, but are
 * scoped per dmtty device node instead of being a single process-wide setting,
 * so every attached backing file (UART, pipe, ...) can have its own terminal
 * behavior.
 */
typedef enum
{
    dmtty_flag_none      = 0,
    dmtty_flag_echo      = (1 << 0),  /**< Echo bytes read from the backing file back to it */
    dmtty_flag_canonical = (1 << 1),  /**< Canonical (line-buffered) mode: assemble a full line before a read() returns any of it */
} dmtty_flags_t;

/**
 * @brief Convenience default flags: echo + canonical, matching a typical interactive terminal
 */
#define DMTTY_FLAGS_DEFAULT   ((uint32_t)(dmtty_flag_echo | dmtty_flag_canonical))

/**
 * @brief IOCTL commands supported on dmtty device nodes
 *
 * dmtty_ioctl_cmd_claim_foreground / dmtty_ioctl_cmd_is_foreground relate to
 * the foreground-reader concept: each device node tracks exactly one "foreground"
 * open handle (the first one to open it, by default). Reads through any other,
 * background, handle on the same node quietly block instead of racing the
 * foreground handle for incoming bytes - the dmtty equivalent of POSIX
 * SIGTTIN/job control, without an actual signal. Ownership is released when the
 * foreground handle is closed, at which point the node is unclaimed again and
 * the next handle to attempt a read (foreground or not) claims it.
 */
typedef enum
{
    dmtty_ioctl_cmd_get_flags = 1,       /**< Get current IO flags; arg = uint32_t* (dmtty_flags_t bitmask) */
    dmtty_ioctl_cmd_set_flags,           /**< Set IO flags; arg = uint32_t* (dmtty_flags_t bitmask) */
    dmtty_ioctl_cmd_get_backing_path,    /**< Get backing file path; arg = char[DMTTY_MAX_PATH_LEN + 1] */
    dmtty_ioctl_cmd_set_backing_path,    /**< Re-point this node to another backing file; arg = const char* */
    dmtty_ioctl_cmd_claim_foreground,    /**< Make the calling handle the foreground reader of this node; arg unused (pass NULL) */
    dmtty_ioctl_cmd_is_foreground,       /**< Check foreground status; arg = bool* (true if the calling handle is currently foreground) */

    dmtty_ioctl_cmd_max
} dmtty_ioctl_cmd_t;

/**
 * @brief Name of the dmhaman handler dmtty listens on for hot-plug announcements
 *
 * A driver that wants a terminal device node created on top of itself (e.g. a
 * UART that just finished configuring itself and knows it is TTY-compatible)
 * calls:
 *
 *   dmtty_device_available_params_t params = { .path = self_path, .name = NULL, .flags = 0 };
 *   dmhaman_call_handler(DMTTY_HANDLER_NAME_DEVICE_AVAILABLE, &params);
 *
 * dmtty reacts by attaching a new /dev node backed by `params.path`, exactly
 * as dmtty_attach() would.
 */
#define DMTTY_HANDLER_NAME_DEVICE_AVAILABLE   "dmtty_device_available"

/**
 * @brief Parameters passed through the DMTTY_HANDLER_NAME_DEVICE_AVAILABLE event
 */
typedef struct
{
    const char *path;   /**< Full path to the backing device (e.g. "/dev/dmuart1") - required */
    const char *name;   /**< Suggested node name under /dev; NULL = auto-numbered "ttyN" */
    uint32_t    flags;  /**< Initial IO flags (dmtty_flags_t bitmask); 0 = DMTTY_FLAGS_DEFAULT */
} dmtty_device_available_params_t;

/**
 * @brief Name of the dmhaman handler dmtty listens on for hot-unplug announcements
 *
 * The counterpart of DMTTY_HANDLER_NAME_DEVICE_AVAILABLE: a driver that
 * previously announced itself as TTY-compatible and is now going away (e.g. a
 * UART being torn down) calls:
 *
 *   dmtty_device_unavailable_params_t params = { .path = self_path };
 *   dmhaman_call_handler(DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE, &params);
 *
 * dmtty reacts by detaching the /dev node(s) backed by `params.path`, exactly
 * as dmtty_detach() would. The main "/dev/tty" node created from
 * configuration is never detached this way, matching dmtty_detach()'s own
 * restriction.
 */
#define DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE   "dmtty_device_unavailable"

/**
 * @brief Parameters passed through the DMTTY_HANDLER_NAME_DEVICE_UNAVAILABLE event
 */
typedef struct
{
    const char *path;   /**< Full path to the backing device previously passed as
                              dmtty_device_available_params_t.path - required */
} dmtty_device_unavailable_params_t;

#endif // DMTTY_TYPES_H
