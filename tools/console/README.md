# console - Per-Device Shell Launcher

A small DMOD application module started once per tty device by `libsystemd`
(see [`configs/console@.ini`](configs/console@.ini) and
[`configs/console.rules`](configs/console.rules)). It is **not** a terminal
itself - `dmell` already is one - it only:

1. Reads the module to run from the `DMOD_SHELL` environment variable.
2. Binds `stdin`/`stdout`/`stderr`/`stdlog` for that module to the device
   path given as its own argument.
3. Starts it via `Dmod_SpawnModule()`.

## How it gets started

`dmtty` reports every tty node it exposes (the main `/dev/tty` node, plus any
attached via `dmtty_attach()` or a `dmhaman` hot-plug event) to `libsystemd`
as soon as the node's absolute path is known, via its `dmdrvi_path_ready` DIF
calling `libsystemd_notify_device_added("tty", <node_name>, <absolute_path>)`
- see [`../../docs/configuration.md`](../../docs/configuration.md).

[`configs/console.rules`](configs/console.rules) tells `libsystemd` that
every device reported under class `tty` should start a `console@<node_name>`
instance:

```ini
[class=tty]
start=console@%name
```

That instance is resolved on demand from
[`configs/console@.ini`](configs/console@.ini), with `%v` expanding to the
node's absolute path (the `user_value` from the report above) and passed
through as both `console`'s own argument and its stream redirections:

```ini
exec=console
args=%v
stdin=%v
stdout=%v
stderr=%v
stdlog=%v
type=oneshot
```

Drop both files into `libsystemd`'s units/rules directories (see
[`dmsystem`'s configuration docs](https://github.com/choco-technologies/dmsystem/blob/main/app/libsystemd/docs/configuration.md))
to enable a console/shell per tty device.

`type=oneshot` and no `restart=` (defaults to `no`) are deliberate: `console`
exits as soon as it has started `$DMOD_SHELL` - it does not wait for the
shell to finish - so `restart=always` would race a fresh console/shell
against the one still running on the same device. Re-attaching the device
(or `service start console@<name>` by hand) starts a new session instead.

## Building

Built together with `dmtty` itself (added via `add_subdirectory()` from the
top-level `CMakeLists.txt`):

```bash
cmake -B build
cmake --build build
```

The resulting module file is produced at `build/dmf/console.dmf`, alongside
`build/dmf/dmtty.dmf` and `build/dmf/tty.dmf`.

## Usage

```
console <device_path>
```

Not meant to be run by hand - see [How it gets started](#how-it-gets-started)
above.
