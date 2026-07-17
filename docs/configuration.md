# DMTTY Configuration Guide

## Configuration File Format

DMTTY uses INI-format configuration files parsed by the DMINI module, like
every other DMOD driver configured through `dmdevfs`. Unlike `dmuart`/`dmfmc`
there is no board- or MCU-specific configuration: a single `[dmtty]` section
configures the one context dmtty creates (the main `/dev/tty` node).

## Configuration Parameters

All parameters are in the `[dmtty]` section:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `backing` | string | (unset) | Full path of the file to forward `/dev/tty` to/from at boot, e.g. `/dev/dmuart1`. Left unset, `/dev/tty` exists but is unbound until something attaches a backing file to it - reads/writes fall back to the raw kernel stdin/stdout (`Dmod_ReadKernel`/`Dmod_WriteKernel`) in the meantime. |
| `echo` | bool (`on`/`off`/`1`/`0`/`true`/`false`) | `on` | Echo bytes read back to the backing file (or to raw kernel stdout, while unbound) |
| `canonical` | bool (`on`/`off`/`1`/`0`/`true`/`false`) | `on` | Assemble input into complete lines before a read returns any of it |

Since `config` may also be `NULL` (dmtty is not given a config file at all),
the same defaults (`echo=on`, `canonical=on`, unbound) apply in that case
too.

## Examples

### Minimal - just create /dev/tty, bind it later

```ini
[dmtty]
```

### Bind /dev/tty to a UART at boot

```ini
[dmtty]
backing=/dev/dmuart1
echo=on
canonical=on
```

### Raw mode (no echo, byte-at-a-time reads) - e.g. for a binary protocol

```ini
[dmtty]
backing=/dev/dmuart2
echo=off
canonical=off
```

## Additional Nodes

Only the main `/dev/tty` node is created from this configuration file. Every
other node - e.g. `/dev/tty1` for a second UART, or `/dev/ttyUSB0` for some
USB-serial adapter - is created at *runtime*, either by calling
`dmtty_attach()` directly or by another driver broadcasting the
`DMTTY_HANDLER_NAME_DEVICE_AVAILABLE` dmhaman event once it knows its own
device path. See [api-reference.md](api-reference.md) for both.

```c
#include "dmtty.h"

/* Turn /dev/dmuart1 into a new terminal node, e.g. for dmell to redirect to */
int ret = dmtty_attach("/dev/dmuart1", NULL, DMTTY_FLAGS_DEFAULT);
/* -> creates /dev/tty1 (first auto-numbered node) */
```

## Using in dmell

Once a node exists and is bound to a backing file, redirect `dmell`'s
`stdin`/`stdout` to it exactly like any other device file to get a fully
interactive terminal - including echo and line editing - over that backing
device:

```
dmell < /dev/tty1 > /dev/tty1
```
