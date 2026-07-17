# DMTTY API Reference

## DMDRVI Interface Functions

### dmtty_dmdrvi_create

```c
dmdrvi_context_t dmtty_dmdrvi_create(dmini_context_t config, dmdrvi_dev_num_t* dev_num);
```

Creates the main `/dev/tty` context from INI configuration. Only one context
may exist at a time - a second call fails and returns `NULL`. `config` may be
`NULL`, in which case `/dev/tty` is created unbound with echo and canonical
mode both enabled.

**Parameters:**
- `config` - DMINI context with the `[dmtty]` section (see [configuration.md](configuration.md)), or `NULL` for defaults
- `dev_num` - Output pointer for the main node's device numbering (always `DMDRVI_NUM_ALT_NAME` with `alt_name = "tty"`)

**Returns:** DMDRVI context on success, `NULL` on failure.

---

### dmtty_dmdrvi_free

```c
void dmtty_dmdrvi_free(dmdrvi_context_t context);
```

Frees the context and every node still attached to it (any node that was
never explicitly detached).

---

### dmtty_dmdrvi_open

```c
void* dmtty_dmdrvi_open(dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num);
```

Opens a handle to one of the context's nodes, identified by `dev_num`
(the main node's, or one returned by a prior `dmtty_attach()` call /
`DMTTY_HANDLER_NAME_DEVICE_AVAILABLE` event). Fails only if the backing file
cannot be opened; a node with no backing file configured yet opens fine and
forwards to/from the raw kernel stdin/stdout (`Dmod_ReadKernel`/
`Dmod_WriteKernel`) instead.

The backing file is always opened read-write regardless of `flags`, since
echo needs to write back to it even on a read-only open.

**Returns:** Device handle on success, `NULL` on failure.

---

### dmtty_dmdrvi_close

```c
void dmtty_dmdrvi_close(dmdrvi_context_t context, void* handle);
```

Closes the backing file and releases the handle.

---

### dmtty_dmdrvi_read

```c
size_t dmtty_dmdrvi_read(dmdrvi_context_t context, void* handle, void* buffer, size_t size, uint32_t offset);
```

Reads from the backing file through the node's line discipline:

- In raw mode (`dmtty_flag_canonical` not set), forwards whatever is
  available from the backing file directly, echoing it back if
  `dmtty_flag_echo` is set.
- In canonical mode, buffers bytes internally (echoing each one back if
  enabled) until a complete line (`\n`-terminated, or capped at an internal
  128-byte buffer) is assembled, then hands it back to the caller - possibly
  across multiple calls if `size` is smaller than the line.

`offset` is ignored: dmtty nodes are streams, not seekable files.

**Returns:** Number of bytes copied into `buffer` (`0` if nothing is available yet).

---

### dmtty_dmdrvi_write

```c
size_t dmtty_dmdrvi_write(dmdrvi_context_t context, void* handle, const void* buffer, size_t size, uint32_t offset);
```

Forwards `buffer` directly to the backing file. `offset` is ignored.

**Returns:** Number of bytes actually written.

---

### dmtty_dmdrvi_ioctl

```c
int dmtty_dmdrvi_ioctl(dmdrvi_context_t context, void* handle, int command, void* arg);
```

See [`dmtty_ioctl_cmd_t`](#ioctl-commands) below.

**Returns:** `0` on success, negative errno on failure.

---

### dmtty_dmdrvi_flush

```c
int dmtty_dmdrvi_flush(dmdrvi_context_t context, void* handle);
```

No-op: dmtty forwards synchronously and keeps no internal output buffer.
Always returns `0` for a valid handle.

---

### dmtty_dmdrvi_stat

```c
int dmtty_dmdrvi_stat(dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat);
```

Reports a generic stream device (`size = 0`, `mode = 0666`) regardless of
`path`.

---

## IOCTL Commands

All commands operate on the node the given `handle` was opened from.

| Command | `arg` type | Description |
|---|---|---|
| `dmtty_ioctl_cmd_get_flags` | `uint32_t*` | Reads the current IO flags (`dmtty_flags_t` bitmask) |
| `dmtty_ioctl_cmd_set_flags` | `uint32_t*` | Replaces the current IO flags |
| `dmtty_ioctl_cmd_get_backing_path` | `char[DMTTY_MAX_PATH_LEN + 1]` | Copies the backing file path into the buffer |
| `dmtty_ioctl_cmd_set_backing_path` | `const char*` | Re-points *this handle* (and future opens of the same node) at a different backing file, closing the old one |

```c
uint32_t flags;
dmtty_dmdrvi_ioctl(ctx, handle, dmtty_ioctl_cmd_get_flags, &flags);
flags &= ~dmtty_flag_echo;   /* disable local echo */
dmtty_dmdrvi_ioctl(ctx, handle, dmtty_ioctl_cmd_set_flags, &flags);
```

## Direct API (`dmtty.h`)

### dmtty_attach

```c
int dmtty_attach(const char *backing_path, const char *name, uint32_t flags);
```

Attaches `backing_path` as a new `/dev` node without going through the
`dmhaman` event. Requires `/dev/tty` to already be configured (see
`dmtty_dmdrvi_create`).

**Parameters:**
- `backing_path` - full path to forward to/from, e.g. `"/dev/dmuart1"` (required)
- `name` - node name under `/dev`, e.g. `"ttyUSB0"`; `NULL` auto-numbers it as `"ttyN"`
- `flags` - initial `dmtty_flags_t` bitmask; `0` = `DMTTY_FLAGS_DEFAULT` (echo + canonical)

**Returns:** `0` on success. `-ENODEV` if dmtty is not configured yet,
`-EEXIST` if `name` is taken, `-ENAMETOOLONG` if a path/name is too long.

---

### dmtty_detach

```c
int dmtty_detach(const char *name);
```

Removes a node previously created with `dmtty_attach()` or the dmhaman event.
The main `/dev/tty` node (`name == "tty"`) cannot be detached this way.

**Returns:** `0` on success. `-ENOENT` if not found, `-EBUSY` if currently
open, `-EPERM` for `"tty"`.

## dmhaman Event

### DMTTY_HANDLER_NAME_DEVICE_AVAILABLE

```c
#define DMTTY_HANDLER_NAME_DEVICE_AVAILABLE   "dmtty_device_available"

typedef struct
{
    const char *path;   /* full path to the backing device, e.g. "/dev/dmuart1" - required */
    const char *name;   /* suggested node name under /dev; NULL = auto-numbered "ttyN" */
    uint32_t    flags;  /* initial IO flags; 0 = DMTTY_FLAGS_DEFAULT */
} dmtty_device_available_params_t;
```

Any driver that knows its own device path can announce itself as
TTY-compatible without depending on `dmtty` at compile time:

```c
dmtty_device_available_params_t params = {
    .path  = self_device_path,   /* e.g. "/dev/dmuart1" */
    .name  = NULL,
    .flags = 0,
};
dmhaman_call_handler(DMTTY_HANDLER_NAME_DEVICE_AVAILABLE, &params);
```

`dmtty` reacts exactly as `dmtty_attach(params.path, params.name, params.flags)`
would.
