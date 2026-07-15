# DMTTY Configuration Files

Unlike `dmuart`/`dmfmc`, `dmtty` has no MCU- or board-specific code, so there
are no `board/` or `mcu/` subdirectories here - just a single default
configuration for the driver itself.

## Files

- **`dmtty.ini`** - Default `dmdevfs` configuration that creates the main
  `/dev/tty` node. Drop it into `dmdevfs`'s config directory (see
  `dmod-boot`) so it is picked up at boot.

## Configuration Format

```ini
[dmtty]
backing=/dev/dmuart1   ; optional: bind /dev/tty to a device at boot
echo=on                ; optional: default "on"
canonical=on           ; optional: default "on"
```

See [`../docs/configuration.md`](../docs/configuration.md) for the full list
of keys and how additional nodes (beyond the main `/dev/tty`) are created at
runtime.
