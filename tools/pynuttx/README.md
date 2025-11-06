# PyNuttX - NuttX/Vela GDB Extension Toolkit

[![Python Version](https://img.shields.io/badge/python-3.10%2B-blue)](https://www.python.org/downloads/)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](https://www.apache.org/licenses/LICENSE-2.0)
[![Version](https://img.shields.io/badge/version-0.0.6a3-orange)](https://pypi.org/project/pynuttx/)

A comprehensive Python-based GDB debugging toolkit for **NuttX RTOS**, providing powerful extensions for thread analysis, memory debugging, crash diagnostics, filesystem inspection, and much more.

> **Production Ready**: Extensively used in Xiaomi Vela OS for embedded systems debugging, crash analysis, and performance profiling.

---

## 📋 Table of Contents

- [Introduction](#-introduction)
- [Installation & Setup](#-installation--setup)
- [Loading GDB Extensions](#-loading-gdb-extensions)
- [Usage Scenarios](#-usage-scenarios)
- [Command Reference](#-command-reference)
- [Quick Start Examples](#-quick-start-examples)
- [Advanced Features](#-advanced-features)
- [FAQ](#-faq)
- [Contributing](#-contributing)

---

## 🎯 Introduction

PyNuttX implements numerous custom GDB commands under `nuttx/tools/pynuttx/`, primarily focused on:
- **Thread Management**: Thread inspection, backtrace, context switching
- **Memory Analysis**: Memory dumps, leak detection, heap validation
- **System Inspection**: Process status, filesystem, network, sensors, and more

### Key Features

✅ **100+ Custom GDB Commands** covering all aspects of embedded debugging
✅ **Post-Mortem Analysis** via coredump and memory dump files
✅ **Multi-Architecture Support**: ARM, RISC-V, Xtensa, Tricore, and more
✅ **Production Tested** in commercial IoT products (Xiaomi Vela OS)
✅ **Extensible Framework** for custom command development

---

## 🚀 Installation & Setup

### Prerequisites

**GDB Version Requirement**:
- GDB **11 or higher** with python enabled is required

```bash
# Use prebuilt gdb-multiarch (recommended)
trunk/prebuilts/gcc/linux/gdb-multiarch/bin/gdb-multiarch

# Special architectures require specific GDB:
# - Tricore: prebuilts/gcc/linux/tricore/bin/tricore-gdb
# - Xtensa: xtensa-esp32s3-elf-gdb (xt-gdb does NOT support Python extensions)
```

> ⚠️ **Note**: Some vendor toolchains may have compilation issues causing missing `libpython` support, preventing Python module usage.

### Install Dependencies

Install required Python packages:

```bash
# Install dependencies (Linux)
/usr/bin/python3 -m pip install -r nuttx/tools/pynuttx/requirements.txt

# Or via pip if pynuttx is installed
pip install pynuttx
```

### Important Notes

1. **Code-Tool Compatibility**: ⚠️ Always match your code version with tool version
   - Don't use `stable` tools to analyze `dev` branch code
   - Keep tool and firmware in sync

2. **Architecture-Specific Tools**: Use the correct GDB for your target architecture
   - Tricore: `tricore-gdb`
   - Xtensa: `xtensa-esp32s3-elf-gdb` (see [reference](https://gerrit.pt.mioffice.cn/c/vela/prebuilts/gcc/linux/xtensa-esp-elf-gdb/+/5279361))
   - ARM: `gdb-multiarch` or `arm-none-eabi-gdb`

---

## 📥 Loading GDB Extensions

### Method 1: Load via `-ex` Parameter (Recommended)

```bash
# Start GDB with automatic loading
gdb-multiarch nuttx/vela_ap.elf -ex "source nuttx/tools/pynuttx/gdbinit.py"

# Or load manually in GDB shell
(gdb) source nuttx/tools/pynuttx/gdbinit.py
```

### Method 2: Import as Python Module

```bash
# Set PYTHONPATH so Python can find the module
export PYTHONPATH=/myworkspace/nuttx/tools/pynuttx:$PYTHONPATH

# Option 1: Load during GDB startup
gdb-multiarch nuttx/nuttx.elf -ex "py import nxgdb"

# Option 2: Import after GDB starts
(gdb) py import nxgdb
```

### Verify Installation

```gdb
# Check available custom commands
(gdb) help user-defined

# Get help for specific command
(gdb) memdump --help
(gdb) info nxthread --help
```

---

## 🎪 Usage Scenarios

PyNuttX works in multiple debugging contexts:

| Scenario | Description | Use Case |
|----------|-------------|----------|
| **Online Debugging** | Live device via JTAG/SWD | JLink, OpenOCD, TRACE32 |
| **GDB Stub** | Serial/USB debugging | Embedded GDB stub on target |
| **Post-Mortem** | Crash dump analysis | Load memory dumps via `gdbserver.py` |
| **Coredump** | ELF coredump files | Analyze with `gdb <elf> <coredump>` |

### Example: Connect to Live Target

```bash
# Via OpenOCD (JTAG/SWD)
openocd -f interface/jlink.cfg -f target/stm32f4x.cfg &
gdb-multiarch nuttx -ex "target remote :3333" -ex "source tools/pynuttx/gdbinit.py"

# Via Serial GDB Stub
gdb-multiarch nuttx -ex "target remote /dev/ttyUSB0" -ex "source tools/pynuttx/gdbinit.py"
```

### Example: Post-Mortem Analysis

```bash
# Start gdbserver with memory dumps
python3 tools/pynuttx/gdbserver.py \
    -a arm \
    -e nuttx.elf \
    -r ram.bin:0x20000000 psram.bin:0x60000000 \
    -p 6667

# Connect and analyze
gdb-multiarch nuttx.elf -ex "target remote :6667" -ex "source tools/pynuttx/gdbinit.py"
```

---

## 📚 Command Reference

### 🧵 Thread & Scheduling

| Command | Description |
|---------|-------------|
| `info nxthread` | Display NuttX threads with Vela-specific data (replaces `info threads` for better NuttX integration) |
| `nxthread <id>` | Switch to specific thread context |
| `setregs <addr>` | Restore register context from saved state |
| `deadlock` | Detect deadlock conditions |
| `critmon` | Critical section monitor (same as device `critmon`) |
| `ps` | Process status (same as device `ps`) |
| `crash busyloop` | Detect busy-loop threads |

**Example:**
```gdb
(gdb) info nxthread
  PID GROUP PRI POLICY   TYPE    NPX STATE   EVENT      SIGMASK  STACKBASE  STACKSIZE   USED  FILLED  COMMAND
    0     0   0 FIFO     Kthread -   Running            00000000 0x00000000         0      0    0.0%!  Idle_Task
    1     1 100 RR       Task    -   Waiting Semaphore  00000000 0x20600000      2048    824   40.2%   nsh_main

(gdb) nxthread 1
(gdb) bt
```

### 💾 Memory Management

| Command | Description |
|---------|-------------|
| `memdump` | Dump memory allocation info (same as device `memdump`) |
| `memleak` | Detect memory leaks by analyzing unreachable allocations |
| `memcheck` | Verify heap integrity and core data structures |
| `memfind <pattern>` | Search for pattern in heap/bss/data (auto address range) |
| `kasan` | KASAN helper to check address tag accessibility |
| `memclassify` | Classify memory by backtrace to identify module usage |
| `mm visualize` | Visualize memory state with size distribution charts |
| `dump ram` | Export memory data to file |
| `mm range` | Show memory regions in current system |
| `crash stackoverflow` | Detect stack overflow conditions |

**Example:**
```gdb
# Show top 20 memory consumers without backtrace (faster)
(gdb) memdump --top 20 --no-backtrace

# Detect memory leaks
(gdb) memleak
Searching for leaked memory, please wait a moment...
Leak catch!, use '*' mark pid is not exist:
   CNT   PID        Size    Sequence    Address Callstack
    34    30         256        8810 0x3d52f080  [0x00c3210e0] <malloc+12>
...

# Check heap integrity
(gdb) mm check

# Visualize memory fragmentation
(gdb) mm visualize
```

### 🖥️ System & Diagnostics

| Command | Description |
|---------|-------------|
| `foreach list` | Traverse NuttX linked lists |
| `tlsdump` | Dump and verify TLS info / task info integrity |
| `nxgcore` | Pull device coredump (wraps GDB's `gcore`) |
| `free` | Show memory info (same as device `free`) |
| `uname` | Display firmware version |
| `irqinfo` | Show IRQ statistics (same as device `irqinfo`) |
| `dmesg` | Display kernel log (ramlog) |
| `wdog` | Show registered watchdog timer info |
| `worker` | Show registered worker info |
| `noteram` | View note trace data, perform `notedump` operations |
| `notesnap` | View task switching state before crash |
| `target nxstub` | Connect to gdbserver via Vela proxy (native GDB thread support) |
| `diagnose` | Run all diagnostic commands, generate JSON report |
| `circbuf` | View circular buffer state (`libc/misc/lib_circbuf.c`) |
| `pmconfig` | View PM configuration info |
| `elfimport` | Import ELF symbols |
| `crash thread` | Identify which thread caused the crash |

**Example:**
```gdb
# View kernel log
(gdb) dmesg

# Generate comprehensive diagnostic report
(gdb) diagnose

# Check system info
(gdb) uname
(gdb) free
```

### 🗂️ Filesystem

| Command | Description |
|---------|-------------|
| `fdinfo` | Show open file descriptors per task |
| `mount` | Display mount information |
| `foreach inode` | Traverse inode tree |
| `info shm` | Show shared memory usage |
| `info yaffs` | View YAFFS partition context |
| `info romfs` | View ROMFS partition context |
| `info fatfs` | View FAT filesystem partition context |
| `info lrofs` | View LROFS partition context |

**Example:**
```gdb
# Check open files
(gdb) fdinfo

# View mount points
(gdb) mount

# Inspect filesystem partition
(gdb) info fatfs /dev/mmcsd0
```

### 🌐 Network & Connectivity

| Command | Description |
|---------|-------------|
| `netstats` | Print IOB and protocol/socket info |
| `netcheck` | Run network diagnostics |
| `btsocket` | Print Bluetooth IPC server/client cache queue |
| `btdev` | Print Bluetooth adapter BLE/BREDR device info |
| `bttimeval` | Print Bluetooth thread peak timestamp |
| `btstack` | Print protocol stack list (e.g., GATT pending list) |
| `btsnoop` | Convert Bluetooth `/dev/tty` circular buffer to HCI log |
| `lyrainfo` | Display Lyra Lite connection, mesh, and transmission info |

**Example:**
```gdb
# Network statistics
(gdb) netstats

# Bluetooth diagnostics
(gdb) btdev
(gdb) btsnoop /dev/ttyBT0 output.hci
```

### 🎨 Graphics (LVGL)

See [GDB Plug-In](https://docs.lvgl.io/master/details/debugging/gdb_plugin.html)

| Command | Description |
|---------|-------------|
| `dump cache image` | View image cache memory |
| `dump cache image_header` | View image header cache memory |
| `info draw_unit` | View current draw unit info, verify rendering context |
| `info style` | View LVGL object style and type information |
| `dump obj` | Display LVGL object tree |
| `show fb` | Display framebuffer content |

### 📡 RPC & IPC

| Command | Description |
|---------|-------------|
| `rpmsgservice` | View RPMSG service information |


### 📊 Sensors

| Command | Description |
|---------|-------------|
| `uorb` | View `/dev/uorb/` node subscriptions and publications |

### 🎬 Media

| Command | Description |
|---------|-------------|
| `mediadump` | View pipeline filter EOF state and internal dump data |

### 📱 Application Frameworks

| Command | Description |
|---------|-------------|
| `dump aiotmemory` | Print Quick App JS memory usage |

### 🔧 Development Tools

| Command | Description |
|---------|-------------|
| `profile` | Analyze Python function call time (performance profiling) |
| `viztracer` | Capture Python trace data, view with Perfetto |
| `time` | Measure Python tool execution time (like shell `time`) |
| `addr2line` | Convert addresses/variables/expressions to backtrace |
| `hexdump` | Dump memory at address with printable characters |
| `debugpy` | Debug Python code in IDE |

---

## 🚀 Quick Start Examples

### Example 1: Memory Leak Detection

```gdb
# Connect to target or load memory dump
gdb-multiarch nuttx.elf -ex "target remote :3333" -ex "source tools/pynuttx/gdbinit.py"

# Run memory leak detection
(gdb) memleak
Searching for leaked memory, please wait a moment
Searching global symbol in: /path/to/nuttx.elf
Search all memory use 28.98 seconds

Leak catch!, use '*' mark pid is not exist:
   CNT   PID        Size    Sequence    Address Callstack
    34    30         256        8810 0x3d52f080
        [0x00c3210e0] <malloc+12>
        [0x00c47c904] <lv_malloc+6>
        ...

Alloc 44 count, have 8 some backtrace leak, total leak memory is 10112 bytes
```

### Example 2: Crash Analysis

```gdb
# After a crash, connect to device
gdb-multiarch nuttx.elf -ex "target remote /dev/ttyUSB0" -ex "source tools/pynuttx/gdbinit.py"

# View kernel log
(gdb) dmesg
[  123.456] Assertion failed at file:line

# Check thread status
(gdb) info nxthread

# Identify crashing thread
(gdb) crash thread

# Run comprehensive diagnostics
(gdb) diagnose

# Check for memory corruption
(gdb) memcheck

# Detect stack overflow
(gdb) crash stackoverflow
```

### Example 3: Post-Mortem Analysis

```bash
# 1. Collect memory dumps from crashed device
(gdb) dump memory ram.bin 0x20000000 0x20100000
(gdb) dump memory psram.bin 0x60000000 0x64000000

# 2. Start gdbserver with memory files
python3 tools/pynuttx/gdbserver.py \
    -a arm \
    -e nuttx.elf \
    -r ram.bin:0x20000000 psram.bin:0x60000000 \
    -p 6667

# 3. Connect and analyze
gdb-multiarch nuttx.elf \
    -ex "target remote :6667" \
    -ex "source tools/pynuttx/gdbinit.py"

# 4. Analyze the crash
(gdb) info nxthread
(gdb) bt
(gdb) memdump --top 20
```

### Example 4: Filesystem Debugging

```gdb
# Check open file descriptors
(gdb) fdinfo

# View mount points
(gdb) mount

# Inspect filesystem details
(gdb) info fatfs /dev/mmcsd0

# Traverse inode tree
(gdb) foreach inode
```

### Example 5: Network Debugging

```gdb
# View network statistics
(gdb) netstats

# Run network diagnostics
(gdb) netcheck

# Check Bluetooth status
(gdb) btdev
(gdb) btsocket
```

---

## 🎓 Advanced Features

### Save Command Output to File

```gdb
# Use pipe to save output
(gdb) pipe info nxthread | tee output.log
(gdb) pipe memdump | tee memdump.txt
```

### View All Custom Commands

```gdb
# List all user-defined commands
(gdb) help user-defined

# Get help for specific command
(gdb) <command> --help
```

### GDB Proxy for Native Thread Support

```gdb
# Connect via nxstub for better thread awareness
(gdb) target nxstub --proxy :1234 # For example, connect to qemu
(gdb) info threads  # Now shows NuttX threads correctly
```

### Comprehensive Diagnostics

```gdb
# Run all diagnostic commands and generate JSON report
(gdb) diagnose

# This automatically runs:
# - Thread analysis
# - Memory checks
# - System state inspection
# - Network diagnostics
# - And more...
```

---

## ❓ FAQ

### Q: How do I add my own custom commands?

**A:** Follow the [GDB Python API]([docs/gdb-plugin.md](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Python-API.html)) to create custom commands.

Example:
```python
import gdb

class MyCommand(gdb.Command):
    """My custom command description"""

    def __init__(self):
        super().__init__("mycommand", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        print("Hello from my command!")

MyCommand()
```

Save as `mycommand.py` and load:
```gdb
(gdb) source mycommand.py
(gdb) mycommand
```

### Q: Can thread functionality display in VSCode or other frontends?

**A:** Not yet perfectly. Current implementation replaces same-named commands which doesn't perfectly adapt to frontend tools. Frontend adaptation is under development.


### Q: GDB version error or Python module not found?

**A:**
1. Ensure GDB 11+ is installed
2. Check GDB Python support: `gdb --batch -ex "python import sys; print(sys.version)"`
3. Use prebuilt GDB from Vela repository
4. Install dependencies: `pip install -r requirements.txt`

### Q: Commands are slow on embedded target?

**A:** Use post-mortem analysis instead:
1. Dump memory from target
2. Use `gdbserver.py` to load dumps
3. Analyze offline (much faster!)

### Q: How to debug multi-core systems?

**A:**
1. Dump memory from each core separately
2. Load each core's memory with corresponding ELF file
3. Use separate GDB sessions or load all in one gdbserver instance

### Q: Code and tool version mismatch?

**A:** ⚠️ Always keep tool and firmware versions synchronized!
- Use same branch for both code and tools
- Don't mix stable branch tools with dev branch code

---

## 🤝 Contributing

Contributions are welcome! PyNuttX is part of the Apache NuttX project.

### Development Setup

```bash
# Clone repository
git clone https://github.com/apache/nuttx.git
cd nuttx/tools/pynuttx

# Install in development mode
pip install -e .

# Install development dependencies
pip install -r requirements.txt
```

### Adding New Commands

1. Create your command in appropriate module under `nxgdb/`
2. Add documentation in `docs/`
3. Add tests in `tests/`
4. Submit pull request

See [Contributing Guide](CONTRIBUTING.md) for details.

---

## 📖 Resources

- **Documentation**: See `docs/` directory for detailed command documentation
- **NuttX Website**: https://nuttx.apache.org/
- **Issue Tracker**: https://github.com/apache/nuttx/issues
- **Mailing List**: https://nuttx.apache.org/community/

---

## 📄 License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

---

## 🙏 Acknowledgments

Maintained by the Apache NuttX community and extensively developed by the Xiaomi Vela team.

**Special Thanks To:**
- All contributors and maintainers
- Xiaomi Vela OS team for production testing and enhancements
- The embedded systems community

---

**For support and questions, please refer to the [NuttX Documentation](https://nuttx.apache.org/docs/latest/) or contact the community via [mailing lists](https://nuttx.apache.org/community/).**
