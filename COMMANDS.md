# Landauer CLI — 完整命令手册 (for AI Agents)

> Landauer = RWEverything CLI 版本。所有输出可 grep/parse，所有错误到 stderr。Exit code 明确表示成功/失败原因。

---

## 约定

| 约定 | 说明 |
|------|------|
| `<bdf>` | `bus:dev.func` 十六进制，如 `00:1f.6` |
| `<hex>` | 十六进制地址/值，带不带 `0x` 均可 |
| `<width>` | 访问宽度: 1, 2, 4, 8 字节 |
| `<handle>` | BAR 映射句柄（从 `bar map` 命令获取） |
| `[可选]` | 可选参数 |

## Exit Code

| Code | 含义 |
|------|------|
| 0 | 成功 |
| 1 | 参数错误 |
| 2 | 驱动未加载或通信失败 |
| 3 | 硬件访问失败 |
| 4 | 资源不存在 |

---

## 1. 驱动管理

### `landauer driver status`

检查驱动是否已加载并正常工作。

```
> landauer driver status
Landauer driver: OK
PCI Bus accessible. Host bridge vendor: 0x8086
```

失败时:
```
ERR: Driver not accessible (error 2). Run 'landauer.bat install' as admin.
```

---

## 2. PCI — 设备枚举

### `landauer pci list`

列出系统中所有 PCI 设备。输出格式:

```
PCI 00:00.0 8086:3E34 06:00:00 Host Bridge
PCI 00:02.0 8086:3E9B 03:00:00 Display Controller
PCI 00:14.0 8086:A3AF 0C:03:30 Serial Bus Controller (XHCI USB Controller)
PCI 00:17.0 8086:A282 01:06:01 Mass Storage Controller [MF]
PCI 00:1f.6 8086:15BB 02:00:00 Network Controller
```

- 每行: `PCI <bdf> <vendor>:<device> <class>:<sub>:<progif> <class_name>`
- `[MF]` = 多功能设备
- 可 grep: `landauer pci list | grep "02:00"` 找网卡

---

## 3. PCI — 设备信息

### `landauer pci info <bdf>`

显示设备完整信息，包括 BAR 和 Capabilities。

```
> landauer pci info 00:1f.6
Device: 00:1f.6
  Vendor: 8086  Device: 15BB
  Class:  020000 (Ethernet Controller)
  Revision: 11  Header Type: 00
  BAR0: Memory 32-bit @ 0xDF100000  size=128KB
  BAR3: Memory 32-bit @ 0xDF000000  size=1MB
  Capabilities: [40] PM, [50] MSI, [70] MSI-X, [A0] PCIe
```

**Agent 使用场景**: 获取 BAR 基址用于后续 map/read/write。

---

## 4. PCI — 配置空间读写

### `landauer pci cfg read <bdf> <offset> [width]`

读 PCI 配置空间寄存器。

```
> landauer pci cfg read 00:1f.6 0
0x15BB

> landauer pci cfg read 00:1f.6 0x10 4
0xDF100004

> landauer pci cfg read 00:1f.6 0x34 1
0x40
```

- `offset`: 配置空间内偏移 (0x00–0xFFF)
- `width`: 1/2/4 (默认 4)

### `landauer pci cfg write <bdf> <offset> <value> [width]`

写 PCI 配置空间，写完后自动读回确认。

```
> landauer pci cfg write 00:1f.6 0x04 0x0006 2
0x0006
```

---

## 5. PCI — Capability 遍历

### `landauer pci cap list <bdf>`

列出标准 Capability 链表。

```
> landauer pci cap list 00:1f.6
Capabilities for 00:1f.6:
  [40] Standard Cap ID=01 Value=0x00000000
  [50] Standard Cap ID=05 Value=0x00000000
  [70] Standard Cap ID=11 Value=0x00000000
  [A0] Standard Cap ID=10 Value=0x00000000
```

Cap ID 常见值:
| ID | 名称 | 用途 |
|----|------|------|
| 0x01 | PM | Power Management |
| 0x05 | MSI | Message Signaled Interrupts |
| 0x10 | PCIe | PCI Express |
| 0x11 | MSI-X | Extended MSI |

### `landauer pci cap find <bdf> <cap_id>`

查找特定 Capability。

```
> landauer pci cap find 00:1f.6 11
00:1f.6 Cap 0x11 @ offset 0x70 = 0x00000000
```

未找到时:
```
ERR: Capability 0x12 not found
```

### `landauer pci ext-cap list <bdf>`

列出 PCIe Extended Capability 链表（从 0x100 开始）。

---

## 6. PCI — BAR / MMIO 访问

这是最核心的功能：映射物理 BAR，然后像 `/dev/mem` 一样自由读写。

### 工作流

```
1. pci info <bdf>        → 获取 BAR 基址和大小
2. pci bar map <bdf> <n>  → 映射 BAR, 获得 handle
3. pci bar read <h> <off> → 通过 handle + offset 读写
4. pci bar unmap <h>     → 释放映射
```

### `landauer pci bar info <bdf> <bar_index>`

```
> landauer pci bar info 00:1f.6 0
Device: 00:1f.6 BAR0
  Type:        Memory
  Width:       32-bit
  Prefetchable: No
  Base:        0xDF100000
  Size:        128KB
  Mapped:       No (use 'bar map')
```

### `landauer pci bar map <bdf> <bar_index> [cache_type]`

映射 BAR 到内核地址空间，返回 handle。

```
> landauer pci bar map 00:1f.6 0
BAR0 mapped: handle=1 base=0xDF100000 size=0x20000
Use: landauer pci bar read 1 <offset> [width]
```

- `cache_type`: 0=cached, 1=uncached (默认，MMIO 用这个), 2=write-combining

### `landauer pci bar read <handle> <offset> [width]`

```
> landauer pci bar read 1 0
0x00000005

> landauer pci bar read 1 0x12014 4
0x00001234
```

### `landauer pci bar write <handle> <offset> <value> [width]`

写 MMIO，读回确认。

```
> landauer pci bar write 1 0x12114 0x00000000 4
0x00000000
```

### `landauer pci bar dump <handle> <offset> <length> [outfile]`

Hex dump 到 stdout 或文件。

```
> landauer pci bar dump 1 0 0x40
0x0000: 05 00 00 00 00 00 00 00  FF FF FF FF 00 00 00 00  |................|
0x0010: ...

> landauer pci bar dump 1 0 0x20000 bar0.bin
Wrote 131072 bytes to bar0.bin
```

### `landauer pci bar unmap <handle>`

```
> landauer pci bar unmap 1
BAR handle 1 unmapped
```

---

## 7. 实用示例 (Agent 场景)

### 场景: 读 I210 网卡的 FL_SECU 寄存器

```
# 第一步: 找到网卡
landauer pci list | grep "8086.*Network"

# 第二步: 查看 BAR
landauer pci info 00:1f.6

# 第三步: 映射 BAR0
landauer pci bar map 00:1f.6 0
# → handle=1

# 第四步: 读 FL_SECU (BAR0+0x12114)
landauer pci bar read 1 0x12114
# → 0x00000005

# 第五步: 解锁 (写 0)
landauer pci bar write 1 0x12114 0x00000000
# → 0x00000000

# 第六步: 释放
landauer pci bar unmap 1
```

### 场景: Dump 整个 BAR

```
landauer pci bar map 00:1f.6 0
# → handle=1
landauer pci bar dump 1 0 0x20000 bar0.bin
landauer pci bar unmap 1
```

### 场景: 遍历 Capability 找 MSI-X

```
landauer pci cap find 00:1f.6 11
# → Cap 0x11 @ offset 0x70
landauer pci cfg read 00:1f.6 0x70 4
```

---

## 8. 错误处理

所有错误输出到 stderr，以 `ERR:` 开头。

```
ERR: Cannot open Landauer driver. Is it installed?
ERR: Invalid BDF: 00:32.0 (expected xx:xx.x)
ERR: Failed to read PCI config: ACCESS_DENIED (0xC0000022)
ERR: BAR handle 99 not found
```

Agent 应:
1. 检查 stderr 中的 `ERR:` 行
2. 根据 exit code 决定重试策略
3. `exit code 2` → 驱动未加载，需要安装
4. `exit code 4` → 资源不存在，换一个 BDF

---

## 9. 环境要求

| 项 | 要求 |
|----|------|
| OS | Windows 10/11 x64 |
| 权限 | Administrator |
| 签名 | Test Signing Mode (`bcdedit /set testsigning on`) |
| 驱动 | landauer.sys (KMDF 1.15) |
| CLI | landauer.exe (x64 console) |

---

## 10. 快速安装

```bat
:: 一次性操作:
bcdedit /set testsigning on
:: 重启

:: 编译 & 安装
install.bat build
```

---

*Rev 1.0 — Landauer v1*
