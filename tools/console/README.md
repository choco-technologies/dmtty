# console - Per-Device Shell Launcher

A small DMOD application module started once per tty device by `libsystemd`
(see [`configs/console@.ini`](configs/console@.ini) and
[`configs/console.rules`](configs/console.rules)). It is **not** a terminal
itself - `dmell` already is one - it only:

1. Reads the module to run from the `DMOD_SHELL` environment variable.
2. Binds `stdin`/`stdout`/`stderr`/`stdlog` for that module to the device
   path given as its own argument.
3. Starts it **detached** via `Dmod_RunModuleDetached()`.
4. Hands this unit's main PID over to the shell via
   `libsystemd_notify_main_pid()`, releases its own handles on the device,
   and exits.

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

## Why detached, and why the PID handover

Both halves of step 3-4 are load-bearing:

* **Detached, not spawned.** `Dmod_SpawnModule()` parents the new process
  under its caller, and a process exiting takes its whole parented subtree
  down with it (`Dmod_Exit()` -> `dmosi_process_kill()` -> `kill_process_tree()`).
  A `console` that spawned the shell as a child would therefore kill it the
  instant `main()` returned.
* **PID handover.** `libsystemd` tracks whichever PID it spawned for a unit -
  `console` itself. Without `libsystemd_notify_main_pid()`, `service stop
  console@<name>` and device-removal teardown would aim at a process that
  exited seconds after boot and never reach the shell. After the handover the
  unit tracks the shell, so stopping the unit stops the session and the
  shell's exit is what the unit's `restart` policy (if any) reacts to.

`console` also releases its own `stdin`/`stdout`/`stderr`/`stdlog` before
returning. A process that exits does not get them closed for it (`dmosi_process_kill()`
never calls `dmosi_process_destroy()`), and a leaked `stdin` on a tty is not
just untidy: `dmtty` gives the "foreground" reader role to the first handle
opened on a node and releases it only on close, so a leaked handle leaves the
shell's own reads spinning forever - writes work, input is dead.

`type=oneshot` and no `restart=` (defaults to `no`) are deliberate: with the
handover in place a restart policy would react to the *shell* exiting, i.e.
respawn a session every time the user typed `exit`. Re-attaching the device
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
