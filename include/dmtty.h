#ifndef DMTTY_H
#define DMTTY_H

#include "dmtty_defs.h"
#include "dmtty_types.h"

/**
 * @brief Attach a backing file to a new terminal device node
 *
 * Binds an arbitrary stream file - typically another driver's device node,
 * e.g. "/dev/dmuart1" - as the backing store for a new dmtty device node.
 * Once attached, dmtty forwards reads and writes to/from that file and
 * applies the line discipline described by `flags` (echo / canonical mode)
 * on top of it, so the backing file gains terminal-like behavior without
 * having to implement it itself.
 *
 * This is the direct counterpart of broadcasting a
 * DMTTY_HANDLER_NAME_DEVICE_AVAILABLE dmhaman event: use this function when
 * the caller can reach dmtty directly (e.g. a startup script or another
 * module configuring things explicitly); use the dmhaman event when a driver
 * wants to announce itself without a compile-time dependency on dmtty.
 *
 * Requires the main /dev/tty context to already exist, i.e. dmtty must have
 * already been configured once by dmdevfs (see docs/configuration.md).
 *
 * @param backing_path Full path to the file to forward to/from (required)
 * @param name         Node name under /dev, e.g. "ttyUSB0" (NULL = auto-numbered "ttyN")
 * @param flags        Initial IO flags (dmtty_flags_t bitmask); 0 = DMTTY_FLAGS_DEFAULT
 *
 * @return 0 on success, negative errno on failure:
 *         -ENODEV if dmtty has not been configured yet,
 *         -EEXIST if `name` is already in use,
 *         -ENAMETOOLONG if `name` is too long
 */
dmod_dmtty_api(1.0, int, _attach, (const char *backing_path, const char *name, uint32_t flags));

/**
 * @brief Remove a previously attached device node
 *
 * The main "/dev/tty" node created from configuration cannot be detached.
 *
 * @param name Node name as passed to dmtty_attach() (or the auto-generated one)
 *
 * @return 0 on success, negative errno on failure:
 *         -ENOENT if no such node exists,
 *         -EBUSY if the node is currently open,
 *         -EPERM if `name` refers to the main "/dev/tty" node
 */
dmod_dmtty_api(1.0, int, _detach, (const char *name));

#endif // DMTTY_H
