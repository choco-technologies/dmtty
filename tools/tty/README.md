# tty - Manual dmtty Attach Tool

A small DMOD application module that lets you attach or detach a `dmtty`
terminal node by hand, without writing code or editing `dmdevfs`
configuration.

It is a thin CLI wrapper around
[`dmtty_attach()` / `dmtty_detach()`](../../include/dmtty.h) - see
[docs/configuration.md](../../docs/configuration.md) for background on
`dmtty` itself.

## Building

Built together with dmtty itself (added via `add_subdirectory()` from the
top-level `CMakeLists.txt`):

```bash
cmake -B build
cmake --build build
```

The resulting module file is produced at `build/dmf/tty.dmf`, alongside
`build/dmf/dmtty.dmf`.

## Usage

From `dmell` (or any DMOD shell that can run modules), once `/dev/tty` has
already been configured by `dmdevfs`:

```
tty attach <backing_path> [node_name] [options]
tty detach <node_name>
```

### attach

- `backing_path` - full path to the file to forward to/from, e.g. `/dev/dmuart1`
- `node_name` - optional name for the new node under `/dev` (default: auto-numbered `ttyN`)
- `--echo on|off` - echo bytes back to the backing file (default: `on`)
- `--canonical on|off` - line-buffered input (default: `on`)

Example:

```
tty attach /dev/dmuart1 ttyUSB0
```

creates `/dev/ttyUSB0`, backed by `/dev/dmuart1`, with the default
echo + canonical line discipline (`DMTTY_FLAGS_DEFAULT`).

```
tty attach /dev/dmuart1 ttyUSB0 --echo off --canonical off
```

creates the same node in raw mode (no echo, no line buffering).

### detach

```
tty detach ttyUSB0
```

Removes a node previously created with `attach` (or the dmhaman hot-plug
event). The main `tty` node (created from `dmdevfs` configuration) cannot
be detached.
