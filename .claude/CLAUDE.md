# NuttX RTOS — Project Guidelines

Apache NuttX is a real-time operating system emphasizing strict POSIX compliance, modular architecture, and portability across 15+ architectures (ARM, RISC-V, Xtensa, x86, etc.) and 360+ boards.

## Inviolable Principles

These are non-negotiable — see [INVIOLABLES.md](../INVIOLABLES.md) for full context:

- **Strict POSIX compliance** — never compromise the portable interface for expediency or performance
- **Modular architecture** — formalized internal interfaces, minimal globals, well-defined functional boundaries
- **Consistent coding style** — no exceptions; enforced by `nxstyle` tool
- **Open licensing** — Apache 2.0 or compatible only (no copyleft)
- **All users matter** — support Linux, macOS, Windows (native/MSYS/Cygwin), FreeBSD, Solaris; GCC, Clang, SDCC, IAR toolchains

## Code Style

**Max line width: 78 columns.** Unix line endings (LF) only. No trailing whitespace.

### C File Section Order (mandatory)

1. Included Files
2. Pre-processor Definitions
3. Private Types
4. Private Function Prototypes
5. Private Data
6. Public Data
7. Private Functions
8. Public Functions

Each section separated by block comments:

```c
/****************************************************************************
 * Section Name
 ****************************************************************************/
```

### Header File Structure

- Include guard format: `#ifndef __INCLUDE_NUTTX_ARCH_H` (path-based, double underscores)
- Sections: Included Files → Pre-processor Definitions → Public Types → Public Data → Inline Functions → Public Function Prototypes

### Naming

- Global variables: `g_` prefix (e.g., `g_can_dlc_to_len`)
- Static variables: no prefix required
- Functions: lowercase with underscores (e.g., `nxsem_wait`)
- Macros/constants: `UPPER_CASE`
- Struct types: `_s` suffix; enum types: `_e` suffix

### File Headers

Every file requires the relative path from repo root and Apache 2.0 license block.

### Style Checking

```bash
tools/checkpatch.sh -f path/to/file.c    # Check single file
tools/checkpatch.sh -g HEAD~1..HEAD       # Check git diff
```

The `nxstyle` tool (built from `tools/nxstyle.c`) enforces these rules automatically.

Full coding standard: https://nuttx.apache.org/docs/latest/contributing/coding_style.html

## Build and Test

### Configure and Build (Make)

```bash
tools/configure.sh <board>:<config>   # e.g., tools/configure.sh sim:nsh
make -j$(nproc)                       # Build
make menuconfig                       # Interactive Kconfig menu
make savedefconfig                    # Save minimal config
make distclean                        # Full clean (removes .config)
```

### Configure and Build (CMake)

```bash
cmake -S . -B build -DBOARD_CONFIG=sim:nsh
cmake --build build -j$(nproc)
```

### List Configurations

```bash
tools/configure.sh -L                 # List all available board:config pairs
```

### CI

CI scripts live in `tools/ci/`. Platform setup scripts: `tools/ci/platforms/{ubuntu,darwin,msys2}.sh`.

## Architecture

### Directory Layout

| Directory | Purpose |
|-----------|---------|
| `arch/<arch>/src/<chip>/` | Architecture & chip-specific code |
| `boards/<arch>/<chip>/<board>/` | Board support (configs, init, peripherals) |
| `sched/` | Scheduler, tasks, pthreads, semaphores, signals, timers, work queues |
| `drivers/` | Device drivers organized by type (serial, i2c, spi, can, usb, etc.) |
| `fs/` | Filesystem implementations (VFS, FAT, LittleFS, ROMFS, procfs, etc.) |
| `net/` | Network stack (TCP/IP, IPv6, routing, sockets) |
| `libs/libc/` | POSIX-compatible C library |
| `libs/libm/` | Math library |
| `mm/` | Memory management (heap, page allocator) |
| `include/nuttx/` | Public OS API headers |
| `tools/` | Build utilities, host tools, CI scripts |
| `Documentation/` | Sphinx-based documentation source |

### Board Configuration Path

```
boards/<arch>/<chip>/<board>/configs/<config>/defconfig
```

Example: `boards/arm/s32k3xx/s32k344evb/configs/nsh/defconfig`

### Kconfig System

Hierarchical configuration via Kconfig files at every level. `CONFIG_*` variables control feature inclusion. Board defconfigs store minimal delta from defaults.

## Conventions

- **Commit message format**: `area/subsystem: Short description.` followed by blank line, detailed description, blank line, `Signed-off-by:` — see [CONTRIBUTING.md](../CONTRIBUTING.md)
- **Documentation required**: New features must include docs in the same PR (under `Documentation/`)
- **No breaking changes**: Every commit must maintain build and runtime compatibility
- **PRs must include test logs**: Build and runtime logs on real hardware are mandatory for code changes
- **Separate concerns**: Different functionalities go in separate PRs
- **Prefer duplication over coupling**: Sometimes duplicating logic is better than introducing tight coupling between modules
