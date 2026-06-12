# Landauer

> Erasing a single bit of information costs  
> $$E = kT \ln(2)$$  
> of energy. And firmware is the closest you'll ever get to that physical process.  
> — Landauer's Principle

The CLI version of RWEverything. A Windows kernel driver + user-mode tool that lets AI agents read and write PCIe hardware registers directly. No GUI — all output is grep/parse-friendly, all errors are explicit.

[中文版](README.md) | English

---

## Quick Start

```bat
:: 1. Enable test signing (reboot once)
bcdedit /set testsigning on

:: 2. Right-click install.bat → Run as Administrator
::    One-click: build driver → build CLI → sign → install → start

:: 3. Verify
landauer driver status
landauer pci list
```

Uninstall:
```bat
:: Right-click uninstall.bat → Run as Administrator
```

---

## Command Reference

```
landauer pci list                             List all PCI devices
landauer pci info <bdf>                       Device details (BAR, Cap, Class)
landauer pci cfg read <bdf> <off> [w]        Read config space
landauer pci cfg write <bdf> <off> <val> [w] Write config space
landauer pci cap list <bdf>                   List capability chain
landauer pci cap find <bdf> <id>             Find a specific capability
landauer pci ext-cap list <bdf>              List extended capabilities
landauer pci bar info <bdf> <idx>            Show BAR base/size
landauer pci bar map <bdf> <idx> [cache]     Map BAR → handle
landauer pci bar read <handle> <off> [w]     Read BAR MMIO
landauer pci bar write <handle> <off> <v> [w]Write BAR MMIO
landauer pci bar dump <handle> <off> <len>  Dump MMIO (stdout/file)
landauer pci bar unmap <handle>              Release BAR mapping
landauer driver status                       Check driver status
```

BDF format: `bus:dev.func` in hex, e.g. `00:1f.6`

---

## Requirements

| Item | Notes |
|------|-------|
| OS | Windows 10/11 x64 |
| Privilege | Administrator |
| Compiler | Visual Studio 2022 (BuildTools suffices) |
| SDK | Windows 10 SDK (with DDK headers) |
| Signing | Test Signing Mode (`bcdedit /set testsigning on`) |

---

## Project Structure

```
landauer/
├── ARCHITECTURE.md      # Architecture — protocol, Provider model, roadmap
├── COMMANDS.md          # Full command reference (for AI Agents)
├── README.md            # Chinese README
├── README_EN.md         # This file
├── protocol.h           # Shared driver-CLI protocol header
├── install.bat          # One-click build & install
├── uninstall.bat        # One-click uninstall
│
├── driver/              # Legacy NT kernel driver
│   ├── driver.c         # DriverEntry + IRP dispatch
│   ├── dispatch.c       # Command parser + Provider router
│   ├── pci_provider.c   # PCI config space & BAR Provider
│   ├── bar_table.c/h    # BAR mapping table (MmMapIoSpace + KSPIN_LOCK)
│   ├── provider.h       # Provider interface
│   ├── landauer.inf     # Driver INF
│   ├── landauer.sln     # VS solution
│   ├── landauer.vcxproj # VS project
│   └── make.bat         # Direct cl.exe build
│
└── cli/                 # User-mode CLI
    ├── main.c           # CLI entry + subcommand routing
    ├── cmd_pci.c/h      # All PCI subcommands
    ├── driver_if.c/h    # DeviceIoControl wrapper
    ├── format.c/h       # Output formatting
    ├── landauer.vcxproj # VS project
    └── make.bat         # MSBuild build
```

---

## Agent Usage Examples

### Reading device registers

```bash
# Find the NIC
landauer pci list | grep "8086.*Network"
# → PCI 07:00.0 8086:1533 02:00:00 Network Controller

# Device details
landauer pci info 07:00.0

# Read vendor/device ID from config space
landauer pci cfg read 07:00.0 0 4
# → 0x15338086

# Walk capabilities
landauer pci cap list 07:00.0
# → [40] PM, [50] MSI, [70] MSI-X, [A0] PCIe
```

### MMIO read/write

```bash
# Map a BAR
landauer pci bar map 07:00.0 0
# → BAR0 mapped: handle=1 base=0xDF100000 size=0x100000

# Read/write registers
landauer pci bar read 1 0x5400 2
landauer pci bar write 1 0x12114 0x00000000

# Dump to file
landauer pci bar dump 1 0 0x200 bar0.bin

# Release
landauer pci bar unmap 1
```

---

## Architecture

- **Legacy NT driver** — pure NT API (IoCreateDevice + IRP dispatch), no WDF dependency
- **Provider model** — PCI_CFG Provider (HalGetBusDataByOffset) + PCI_BAR Provider (MmMapIoSpace)
- **Single IOCTL** — 32-byte command header + variable payload, `resource_type` + `operation` dispatch
- **Self-describing protocol** — magic/version/status in header; driver and CLI evolve independently

See [ARCHITECTURE.md](ARCHITECTURE.md)

---

## Roadmap

| Version | Scope |
|---------|-------|
| v1 (current) | Full PCIe: config space, BAR MMIO, capability traversal |
| v2 | IO ports, MSR, CPUID |
| v3 | Arbitrary physical memory, SMBIOS/ACPI |
| v4 | SPI Flash, I2C, GPIO |
| v5 | Hardware breakpoints, MSI-X tables, AER injection |

---

## Output Convention

All data goes to stdout, errors to stderr. Fixed format, grep/awk-parseable:

```
# Success: exit 0
0xDF100000: 0x00000005

# Failure: exit != 0, stderr
ERR: Failed to read PCI config: ACCESS_DENIED (0xC0000022)
```
