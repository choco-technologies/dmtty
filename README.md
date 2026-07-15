# DMTTY - DMOD TTY Line Discipline Driver Module

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A DMOD (Dynamic Modular System) module that adds terminal behavior - echo and
canonical (line-buffered) input - on top of any other stream device dmdevfs
can open, without that device's own driver having to implement it.

## Why

Every DMOD driver shows up as a stream file under `/dev`, which is handy: in
`dmell` you can redirect `stdin`/`stdout` to a UART device node and use it as
a new terminal. The catch is that IO-flag handling (echo, line editing) only
exists inside `dmlog`'s own console today - a plain `dmuart` node just
forwards raw bytes, so redirecting to it gives you a terminal with no echo.
`dmtty` implements that line discipline once and lets it be layered on top of
*any* backing file instead of duplicating it in every driver.

## Features

- **Standard DMDRVI Interface**: Read/write/ioctl device access pattern, like any other DMOD driver
- **Echo & Canonical Mode**: Per-node IO flags, configurable via ioctl at runtime
- **One Main Node, Many Extra Nodes**: Creates `/dev/tty` from configuration, then exposes an API to attach any number of additional nodes to other backing files
- **Two Attach Paths**: A direct API (`dmtty_attach()`) and a `dmhaman` hot-plug event (`DMTTY_HANDLER_NAME_DEVICE_AVAILABLE`) for drivers that don't want a compile-time dependency on dmtty
- **Architecture-Independent**: No MCU/board-specific code - it only forwards to an already-open backing file, so it builds identically everywhere

## Quick Start

### Installation

Using `dmf-get` from the DMOD release package:

```bash
dmf-get install dmtty
```

### Basic Usage

1. **Create the main `/dev/tty` node** (`config.ini`, loaded by `dmdevfs`):

```ini
[dmtty]
backing=/dev/dmuart1
echo=on
canonical=on
```

2. **Or attach additional nodes at runtime**:

```c
#include "dmtty.h"

/* Turn /dev/dmuart1 into a new terminal node -> creates /dev/tty1 */
dmtty_attach("/dev/dmuart1", NULL, DMTTY_FLAGS_DEFAULT);
```

3. **Use it like any other device file**, e.g. from `dmell`:

```
dmell < /dev/tty1 > /dev/tty1
```

## Building

### Prerequisites

- CMake 3.18 or higher
- A DMOD-compatible toolchain (automatically fetched via `dmod`)

### Build Commands

```bash
cmake -B build
cmake --build build
```

## Documentation

Comprehensive documentation is available in the `docs/` directory:

- **[dmtty.md](docs/dmtty.md)** - Module overview and architecture
- **[api-reference.md](docs/api-reference.md)** - Complete API documentation
- **[configuration.md](docs/configuration.md)** - Configuration guide with examples

View documentation using `dmf-man`:

```bash
dmf-man dmtty          # Main documentation
dmf-man dmtty api      # API reference
dmf-man dmtty config   # Configuration guide
```

## Development

### Project Structure

```
dmtty/
├── configs/            # Default dmdevfs configuration
├── docs/               # Documentation (markdown format)
├── examples/           # Example configurations
├── include/
│   ├── dmtty.h        # Main API (direct attach/detach + DMDRVI interface)
│   └── dmtty_types.h  # IO flags, ioctl commands, dmhaman event
├── src/
│   └── dmtty.c        # Implementation
├── CMakeLists.txt     # Build configuration
└── manifest.dmm       # DMOD manifest
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Authors

- Patryk Kubiak - Initial work

## Related Projects

- [DMOD](https://github.com/choco-technologies/dmod) - Dynamic Modular System framework
- [DMDRVI](https://github.com/choco-technologies/dmdrvi) - DMOD Driver Interface
- [DMDEVFS](https://github.com/choco-technologies/dmdevfs) - DMOD Driver File System (manages driver device nodes)
- [DMHAMAN](https://github.com/choco-technologies/dmhaman) - Handler registry used for hot-plug announcements
- [DMUART](https://github.com/choco-technologies/dmuart) - UART driver module (a common dmtty backing device)
- [DMLOG](https://github.com/choco-technologies/dmlog) - Logging/monitor console (the original source of IO-flag handling)

## Support

For issues, questions, or contributions:

- Open an issue on GitHub
- Check the documentation in `docs/`
- Use `dmf-man dmtty` for command-line help
