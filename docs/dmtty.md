# DMTTY - DMOD TTY Line Discipline Driver Module

## Overview

DMTTY is a DMOD module that adds terminal ("tty") behavior - echo and
canonical (line-buffered) input - on top of *any* other stream-like device
that dmdevfs can already open, without that device's own driver having to
implement it.

It follows the standard DMOD driver interface (DMDRVI) pattern, exactly like
`dmuart` or `dmfmc`, so it shows up under `/dev` and can be used from `dmell`
(redirecting `stdin`/`stdout` to it) or any other DMOD program like any other
device file.

## Motivation

Every DMOD driver is exposed as a stream file, which is convenient: in
`dmell` you can redirect `stdin`/`stdout` to, say, a UART device node and use
it as a new terminal. The problem is that IO-flag handling (echo, canonical
input) is currently only implemented inside `dmlog`'s own console - a plain
`dmuart` (or any other) device node just forwards raw bytes, so redirecting
to it gives you a terminal with no echo and no line editing.

Rather than duplicating termios-like logic in every driver, `dmtty` implements
it once and lets it be layered on top of *any* backing file:

```
┌───────────────────────────┐
│         dmell (shell)      │
│  stdin/stdout -> /dev/tty1 │
└──────────────┬─────────────┘
               │  DMDRVI (open/read/write/ioctl)
               ▼
┌───────────────────────────┐
│           dmtty             │   <- echo + canonical line discipline
│  (line discipline layer)   │
└──────────────┬─────────────┘
               │  generic dmod SAL file calls (Dmod_FileOpen/Read/Write)
               ▼
┌───────────────────────────┐
│   /dev/dmuart1 (or any     │
│   other stream device)     │
└─────────────────────────────┘
```

## Architecture

A single `dmtty` context (created once by `dmdevfs` from configuration, see
[configuration.md](configuration.md)) exposes the main `/dev/tty` node plus
any number of additional nodes ("slots"), each pairing:

- a device number the node is exposed under (always an alt-name, e.g. `tty`,
  `tty1`, or a caller-chosen name),
- the path of the backing file forwarded to/from, and
- its own IO flags (echo / canonical), so different nodes can behave
  differently.

This mirrors the dynamic-device pattern documented by `dmdrvi` itself (see
`dmdrvi(3)`, "Dynamic Device Notifications"): all nodes share the one context
created by `dmtty_dmdrvi_create()`, and additional ones are announced to
`dmdevfs` via `dmdrvi_device_available()` instead of creating a new context
per node.

### Adding new nodes

There are two ways to attach a backing file to a new `/dev` node at runtime:

1. **Direct API** - call `dmtty_attach()` (declared in `dmtty.h`) from any
   module, e.g. a startup script that knows exactly which device it wants to
   turn into a terminal.
2. **dmhaman event** - a driver that does not want a compile-time dependency
   on `dmtty` can instead broadcast a `DMTTY_HANDLER_NAME_DEVICE_AVAILABLE`
   event via `dmhaman_call_handler()` once it knows its own full device path.
   `dmtty` listens for this event (registered during
   `dmtty_dmdrvi_create()`) and reacts exactly as `dmtty_attach()` would.

Both paths go through the same internal `attach_internal()` logic and result
in a new device file appearing under `/dev` (numbered `tty1`, `tty2`, ... if
no name is given, or under the requested name otherwise).

### Line discipline

Each open handle forwards data directly to/from the backing file
(`Dmod_FileRead`/`Dmod_FileWrite`), or, for a node with no backing file
attached yet, straight to/from the raw kernel stdin/stdout
(`Dmod_ReadKernel`/`Dmod_WriteKernel`) - with two independent behaviors
controlled by the node's IO flags (`dmtty_flags_t`):

- **`dmtty_flag_echo`** - every byte read back is also written back to the
  backing file, so whoever is on the other end (e.g. a terminal emulator
  connected to the physical UART) sees what they typed.
- **`dmtty_flag_canonical`** - input is assembled into complete lines
  (terminated by `\n`, or capped at an internal buffer size) before a
  `read()` call returns any of it, so callers reading with a small buffer get
  data a line at a time instead of byte by byte.

Both flags can be read/written per node via `ioctl()` (`dmtty_ioctl_cmd_get_flags`
/ `dmtty_ioctl_cmd_set_flags`), and the backing file itself can be
re-pointed at runtime via `dmtty_ioctl_cmd_get_backing_path` /
`dmtty_ioctl_cmd_set_backing_path`.

## Features

- Standard DMDRVI device interface (read/write/ioctl)
- Echo and canonical (line-buffered) IO flags, configurable per node via ioctl
- One main `/dev/tty` node created from configuration, plus any number of
  additional nodes created at runtime
- Two ways to attach a backing file: a direct API (`dmtty_attach()`) and a
  `dmhaman` hot-plug event (`DMTTY_HANDLER_NAME_DEVICE_AVAILABLE`)
- No MCU/architecture-specific code: works identically on every target, since
  it only forwards to an already-open backing file

## Device Paths

- The node created from configuration is always `/dev/tty`.
- Nodes created via `dmtty_attach()` (or the dmhaman event) are exposed as
  `/dev/<name>` if a name was given, or auto-numbered as `/dev/tty1`,
  `/dev/tty2`, ... otherwise.

## Dependencies

- `dmdrvi` - DMOD Driver Interface
- `dmini` - INI configuration parser (main node configuration)
- `dmhaman` - Handler registry (hot-plug announcements)
- `dmlist` - Linked list (node bookkeeping)
- `dmosi` - Mutex used to guard node bookkeeping
