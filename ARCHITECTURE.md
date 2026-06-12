# Landauer — 通用硬件访问驱动架构设计

> "每擦除 1bit 信息都耗费 k·T·ln(2) 能量，而 firmware 是你最接近这个物理过程的一次。" — Landauer's Principle

## 1. 定位

Landauer 是 **RWEverything 的 CLI 版本**：一个 Windows 内核驱动 + 用户态 CLI，让 AI agent 能通过命令行直接读写硬件。摒弃 GUI，所有输出可 grep/parse，所有错误显式报告。

当前阶段目标：**PCIe 全功能**（配置空间、扩展配置空间、BAR 映射、Capability 遍历、MMIO）。架构预留了 IO Port、MSR、SPI、I2C、SMBIOS、ACPI 等扩展位。

---

## 2. 整体架构

```
┌──────────────────────────────────────────────────┐
│  Reasonix / 人类用户                              │
│    │  bash / cmd                                 │
│    ▼                                             │
│  landauer.exe  (用户态 CLI)                       │
│    │  DeviceIoControl( \\.\Landauer )            │
│    ▼                                             │
│  landauer.sys  (内核驱动, KMDF)                   │
│    │                                             │
│    ├──▶ PCI Provider     (配置空间, BAR, MMIO)    │
│    ├──▶ IO Provider      (IO Port — 未来)         │
│    ├──▶ MSR Provider     (CPU MSR — 未来)         │
│    ├──▶ Memory Provider  (任意物理内存 — 未来)     │
│    └──▶ ...              (SPI/I2C/SMBIOS 等)      │
└──────────────────────────────────────────────────┘
```

**Provider 模式**：驱动内部按"资源类型"分派到不同的 Provider。新增资源类型 = 新增一个 Provider，不改动调度框架。

---

## 3. 通信协议 (Protocol v1)

### 3.1 设备名

```
\\.\Landauer
```

### 3.2 单一 IOCTL 入口

所有操作通过**一个 IOCTL 代码**进入，由命令头内部的 `resource_type` + `operation` 字段做二级分派。这样：

- 新增操作不增加 IOCTL 代码，内核/用户态只需各自扩展分派表
- 命令结构自带版本号，向前兼容
- 一个 `DeviceIoControl` 调用完成一次完整的请求-响应

```
#define IOCTL_LANDAUER_CMD  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

### 3.3 命令结构体

```c
// ============================================================
// 命令头 — 所有操作共用
// ============================================================
#define LANDAUER_MAGIC   0x3144574C   // "LWD1" (Landauer Wire-protocol v1)
#define LANDAUER_VERSION 1

typedef struct _LANDAUER_CMD_HEADER {
    uint32_t magic;            // [IN]  LANDAUER_MAGIC
    uint32_t version;          // [IN]  协议版本 (当前 = 1)
    uint8_t  resource_type;    // [IN]  见 §4
    uint8_t  operation;        // [IN]  见 §5
    uint8_t  access_width;     // [IN]  1/2/4/8 字节 (READ/WRITE 时有效)
    uint8_t  flags;            // [IN]  按操作定义 (见 §5)
    uint32_t status;           // [OUT] 0 = 成功, 其他 = 错误码
    uint32_t data_length;      // [IN/OUT] payload 字节数
    uint32_t reserved[3];      // 保留, 必须为 0
    // uint8_t payload[...]    // 变长 payload 紧跟其后
} LANDAUER_CMD_HEADER;
// 总大小: 32 bytes 定长头
```

```c
// ============================================================
// 请求 = 命令头 + payload
// 响应 = 命令头 + payload (status 字段被驱动填入)
// ============================================================
// DeviceIoControl 调用:
//   lpInBuffer  → LANDAUER_CMD_HEADER + input_payload
//   nInBufferSize  = sizeof(header) + input_payload_length
//   lpOutBuffer ← LANDAUER_CMD_HEADER + output_payload
//   nOutBufferSize = sizeof(header) + expected_output_length
```

### 3.4 调用约定

| 项 | 规则 |
|---|---|
| magic 校验 | 驱动首先检查 magic，不匹配直接返回 STATUS_INVALID_PARAMETER |
| version | 驱动返回自己支持的最高版本；CLI 应检查并适配 |
| status | 驱动写入：0 = 成功；NTSTATUS 错误码反之 |
| payload | 输入/输出分别接在 header 之后，长度由 data_length 指示 |
| access_width | READ/WRITE 操作的有效宽度；块操作中忽略（块大小由 data_length 决定） |

---

## 4. 资源类型 (Resource Type)

```c
typedef enum _LANDAUER_RESOURCE_TYPE {
    RESOURCE_NONE          = 0x00,   // 未指定

    // ─── PCIe 子系统 (当前实现) ───
    RESOURCE_PCI_CFG       = 0x01,   // PCI 配置空间 (legacy 256B + extended 4KB)
    RESOURCE_PCI_BAR       = 0x02,   // BAR 映射的 MMIO 区域

    // ─── 预留 (未来) ───
    RESOURCE_IO_PORT       = 0x10,   // x86 IO 端口
    RESOURCE_MSR           = 0x11,   // CPU MSR
    RESOURCE_PHYS_MEM      = 0x12,   // 任意物理内存 (MmMapIoSpace)
    RESOURCE_CPUID         = 0x13,   // CPUID 指令封装
    RESOURCE_SMBIOS        = 0x20,   // SMBIOS 表
    RESOURCE_ACPI          = 0x21,   // ACPI 表
    RESOURCE_EC            = 0x22,   // Embedded Controller
    RESOURCE_SPI           = 0x30,   // SPI Flash (如 I210 flash)
    RESOURCE_I2C           = 0x31,   // I2C 总线
    RESOURCE_GPIO          = 0x32,   // GPIO

    // ─── 厂商/设备自定义区域 ───
    RESOURCE_VENDOR_BASE   = 0x80,   // 0x80–0xFE 留给厂商扩展
    RESOURCE_MAX           = 0xFF
} LANDAUER_RESOURCE_TYPE;
```

### 4.1 扩展策略

- **0x01–0x0F**: PCIe 子系统及相关
- **0x10–0x1F**: CPU 级资源 (IO, MSR, CPUID, 物理内存)
- **0x20–0x2F**: 固件/表 (SMBIOS, ACPI, EC)
- **0x30–0x3F**: 外设总线 (SPI, I2C, GPIO)
- **0x80–0xFE**: 厂商自定义
- 每个范围有 16 个槽位，足够细分

---

## 5. 操作类型 (Operation)

```c
typedef enum _LANDAUER_OPERATION {
    OP_NONE                = 0x00,   // 未指定

    // ─── 通用操作 ───
    OP_GET_INFO            = 0x01,   // 查询资源信息 (能力、基址、大小)
    OP_READ                = 0x02,   // 读取 (宽度由 access_width 指定)
    OP_WRITE               = 0x03,   // 写入 (宽度由 access_width 指定)
    OP_READ_BLOCK          = 0x04,   // 块读取 (data_length 字节)
    OP_WRITE_BLOCK         = 0x05,   // 块写入 (data_length 字节)
    OP_MAP                 = 0x06,   // 映射资源 (如 BAR 映射)
    OP_UNMAP               = 0x07,   // 解除映射

    // ─── PCIe 特化操作 ───
    OP_PCI_ENUMERATE       = 0x10,   // 枚举 PCI 总线
    OP_PCI_FIND_CAP        = 0x11,   // 查找 Capability (输入 cap_id, 返回 offset)
    OP_PCI_FIND_EXT_CAP    = 0x12,   // 查找 Extended Capability
} LANDAUER_OPERATION;
```

### 5.1 操作 × 资源 矩阵 (v1 实现范围)

| 操作 | PCI_CFG | PCI_BAR | (未来 IO/MSR/...) |
|------|---------|---------|---------------------|
| GET_INFO | ✅ 返回 BDF + vendor/device | ✅ 返回 BAR 基址/大小 | ✅ |
| READ | ✅ 8/16/32-bit | ✅ 8/16/32/64-bit | ✅ |
| WRITE | ✅ 8/16/32-bit | ✅ 8/16/32/64-bit | ✅ |
| READ_BLOCK | ✅ | ✅ | ✅ |
| WRITE_BLOCK | ✅ | ✅ | ✅ |
| MAP | — (CFG 总是可访问) | ✅ MmMapIoSpace | ✅ |
| UNMAP | — | ✅ MmUnmapIoSpace | ✅ |
| ENUMERATE | ✅ 枚举所有 BDF | — | — |
| FIND_CAP | ✅ | — | — |
| FIND_EXT_CAP | ✅ | — | — |

---

## 6. 各资源 Payload 格式

### 6.1 RESOURCE_PCI_CFG

#### GET_INFO — 查询设备信息

```
Input payload:  (空, data_length=0)
                address 字段: bus[23:16] | device[15:11] | function[10:8] | reserved[7:0]

Output payload:
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  class_code[3];   // base, sub, prog-if
    uint8_t  header_type;     // bit7=1 表示 multifunction
    uint8_t  num_bars;        // BAR 数量 (0-6)
    struct {
        uint8_t  index;       // BAR 0-5
        uint8_t  type;        // 0=memory, 1=io
        uint8_t  bits;        // 32 or 64
        uint8_t  prefetchable;
        uint64_t base;        // 物理地址
        uint64_t size;
    } bars[6];
```

#### READ / WRITE — 单次寄存器访问

```
address 编码:
    [63:52] segment (0-65535, 通常为 0)
    [51:36] reserved
    [35:20] bus      (0-255)
    [19:15] device   (0-31)
    [14:12] function (0-7)
    [11:0]  offset   (0-4095, legacy CFG 0-255, extended 256-4095)

Input payload  (WRITE): 要写入的数据 (access_width 字节)
Output payload (READ):  读取到的数据 (access_width 字节)
```

#### ENUMERATE — 枚举 PCI 总线

```
Input payload:
    uint16_t segment;      // 0 = 默认
    uint16_t start_bus;    // 从哪条总线开始
    uint16_t max_buses;    // 最多返回多少设备

Output payload:
    uint16_t count;        // 找到的设备数
    struct {
        uint8_t  bus, device, function;
        uint16_t vendor_id;
        uint16_t device_id;
        uint8_t  class_code[3];
        uint8_t  header_type;
    } devices[...];
```

#### FIND_CAP / FIND_EXT_CAP — 遍历 Capability 链表

```
address 编码同 READ, offset 被忽略

Input payload:
    uint8_t  cap_id;        // 要找的 Capability ID (0 表示返回第一个)
    uint8_t  start_offset;  // 从哪个 offset 开始搜索 (首次调用传 0)

Output payload:
    uint8_t  found;         // 0 = 未找到, 1 = 找到
    uint8_t  cap_id;        // 找到的 Capability ID
    uint8_t  cap_offset;    // Capability 在配置空间中的偏移
    uint8_t  reserved;
    uint32_t cap_value;     // Capability 寄存器的原始值 (前 4 字节)
```

### 6.2 RESOURCE_PCI_BAR

#### GET_INFO — 查询 BAR 信息

```
address: BAR index (0-5)

Output payload:
    uint8_t  type;          // 0=memory, 1=io
    uint8_t  bits;          // 32 or 64
    uint8_t  prefetchable;
    uint8_t  mapped;        // 是否已被驱动映射
    uint64_t base;          // 物理基址
    uint64_t size;          // BAR 大小
    uint64_t mapped_va;     // 驱动内部 VA (调试用)
```

#### MAP — 映射 BAR 到内核地址空间

```
Input payload:
    uint64_t bar_base;      // BAR 物理基址 (从 GET_INFO 获取)
    uint64_t bar_size;      // BAR 大小

Output payload:
    uint32_t map_handle;    // 映射句柄 (用于后续 READ/WRITE/UNMAP)
    uint32_t reserved;
    uint64_t mapped_va;     // 调试用
```

- 后续 READ/WRITE 使用 `address = (map_handle << 32) | offset_in_bar`
- 或更简洁：`address = offset_in_bar`，而 BAR 上下文由 `resource_type` + flags 指定

> **设计选择**：MAP 之后，对 BAR 的 READ/WRITE 使用 `address = offset_in_bar`，驱动内部维护一个映射表。map_handle 放在 flags 或 address 高位。具体实现时再细化。

#### READ / WRITE / READ_BLOCK / WRITE_BLOCK

```
address: BAR 内偏移量 (0 to bar_size-1)

Input payload  (WRITE): 要写入的数据
Output payload (READ):  读取到的数据
```

#### UNMAP

```
(无 payload)
```

---

## 7. CLI 设计 (landauer.exe)

### 7.1 命令结构

```
landauer <resource> <action> [options]
```

### 7.2 资源子命令

```
# ─── PCIe 配置空间 ───
landauer pci list                    # 枚举所有 PCI 设备
landauer pci info <bus> <dev> <func> # 显示设备详情 (BAR, caps)
landauer pci cfg read  <b> <d> <f> <offset> [width]  # 读配置空间
landauer pci cfg write <b> <d> <f> <offset> <value>  # 写配置空间
landauer pci cap list <b> <d> <f>    # 列出所有 Capability
landauer pci cap find <b> <d> <f> <cap_id>  # 查找特定 Capability
landauer pci ext-cap list <b> <d> <f> # 列出 Extended Capability

# ─── PCIe BAR / MMIO ───
landauer pci bar info <b> <d> <f> <bar_index>  # 查看 BAR 详情
landauer pci bar map   <b> <d> <f> <bar_index>  # 映射 BAR (获取 handle)
landauer pci bar read  <handle|bdf> <offset> [width]  # 读 MMIO
landauer pci bar write <handle|bdf> <offset> <value>   # 写 MMIO
landauer pci bar dump  <handle|bdf> <offset> <len> [file]  # dump 到文件
landauer pci bar unmap <handle>             # 解除映射
```

### 7.3 输出格式 (铁律)

```
# 单次读取:
0xDF100000: 0x00000005

# 块读取 / dump:
0x0000: 48 41 4C 54 02 1C 00 00  FF FF FF FF 00 00 00 00  |HALT............|
0x0010: ...

# 设备列表 (可 grep):
PCI 00:00.0 8086:3e34 (Host Bridge)
PCI 00:02.0 8086:3e9b (VGA Controller)
...

# 设备信息:
Device: 00:1f.6
  Vendor: 8086  Device: 15bb  Class: 020000 (Ethernet Controller)
  BAR0: Memory 64-bit @ 0xDF100000  size=128KB
  BAR3: Memory 32-bit @ 0xDF000000  size=1MB
  Capabilities: [40] PM, [50] MSI, [70] MSI-X, [A0] PCIe

# 错误输出 (stderr):
ERR: Failed to read PCI config space at 00:1f.6 offset 0x100: Access denied
ERR: Driver not loaded. Run: landauer driver install
```

### 7.4 Exit Code

| Code | 含义 |
|------|------|
| 0 | 成功 |
| 1 | 参数错误 |
| 2 | 驱动未加载或通信失败 |
| 3 | 硬件访问失败 (地址无效/权限不足) |
| 4 | 资源不存在 (设备未找到等) |

---

## 8. 驱动内部结构

```
landauer.sys
├── driver.c          DriverEntry + Unload
├── device.c          设备创建/删除, IOCTL 分发 (唯一入口)
├── dispatch.c        命令头解析 → 按 resource_type 分派到 Provider
├── pci_provider.c    PCIe 配置空间 & BAR Provider
│   └── HalGetBusDataByOffset / HalSetBusDataByOffset (配置空间)
│   └── MmMapIoSpace / MmUnmapIoSpace (BAR 映射)
├── provider.h        Provider 接口定义 (函数指针表)
└── protocol.h        命令头、枚举、常量定义 (共享给 CLI)
```

### 8.1 Provider 接口

```c
typedef NTSTATUS (*PROVIDER_HANDLER)(
    IN     PLANDAUER_CMD_HEADER  header,
    IN     PVOID                 input_payload,
    IN     ULONG                 input_length,
    OUT    PVOID                 output_payload,
    IN OUT PULONG                output_length
);

typedef struct _PROVIDER_INTERFACE {
    LANDAUER_RESOURCE_TYPE  type;
    const char*             name;
    PROVIDER_HANDLER        handlers[256];  // 按 operation 索引
} PROVIDER_INTERFACE;
```

新增资源类型 = 实现一个 `PROVIDER_INTERFACE`，在 `dispatch.c` 注册即可。

---

## 9. 文件清单 (计划)

```
landauer/
├── ARCHITECTURE.md          # 本文件
├── protocol.h               # 共享头文件 (命令结构体、枚举)
│
├── driver/
│   ├── landauer.sln
│   ├── landauer.vcxproj
│   ├── driver.c             # DriverEntry
│   ├── device.c             # 设备对象 & IOCTL 分发
│   ├── dispatch.c           # 命令解析 + Provider 分派
│   ├── pci_provider.c       # PCI_CFG + PCI_BAR 实现
│   ├── provider.h           # Provider 接口
│   ├── bar_table.c          # BAR 映射表管理
│   └── make.bat
│
├── cli/
│   ├── landauer.vcxproj
│   ├── main.c               # 命令行解析 + 子命令路由
│   ├── cmd_pci.c            # pci 子命令
│   ├── cmd_pci_cfg.c        # pci cfg 子命令
│   ├── cmd_pci_bar.c        # pci bar 子命令
│   ├── driver_if.c          # DeviceIoControl 封装
│   ├── format.c             # 输出格式化
│   └── make.bat
│
├── install.bat              # 驱动安装/卸载
└── README.md
```

---

## 10. 扩展路线图

| 阶段 | 内容 |
|------|------|
| **v1** (当前) | PCIe 全功能：CFG 空间读写、BAR 映射/读写、Capability 遍历、设备枚举 |
| **v2** | IO Port 读写、MSR 读写、CPUID |
| **v3** | 任意物理内存读写 (MmMapIoSpace)、SMBIOS/ACPI 表解析 |
| **v4** | 外设总线：SPI Flash (继承 I210 场景)、I2C、GPIO |
| **v5** | 硬件断点、MSI-X Table 访问、AER 错误注入 |

---

## 11. 关键设计决策

### 11.1 为什么单一 IOCTL 而不是多个 IOCTL 代码？

- **扩展性**: 新增操作只需在分派表加一行，不需要定义新 IOCTL 代码，不需要改 INF，不需要重新注册
- **协议自描述**: 命令头包含 `resource_type` + `operation`，一次调用即可确定完整的语义
- **版本管理**: 协议版本号在命令头里，CLI 和驱动可以协商

### 11.2 为什么 Provider 模式？

- 每种硬件资源有完全不同的访问方式和安全约束
- Provider 接口统一，调度器无需关心资源内部实现
- 新增资源 = 新文件 + 一行注册，不改调度框架

### 11.3 为什么用 BDF 编码地址而不是分开字段？

- PCI 配置空间地址天然是 bus:device:function:offset 的四层结构
- 压入一个 `uint64_t` 可以让 `address` 字段对所有资源类型通用
- GET_INFO / ENUMERATE 等操作使用独立的 payload 结构，不受此编码影响

---

## 12. 安全考量

- 驱动只接受来自用户态的管理员权限调用 (默认 ACL)
- BAR 映射前校验：基址 + 大小必须在 PCI BAR 声明的范围内
- 物理地址读写需白名单或按需映射，不允许随意访问任意物理地址当 PCI_CFG 用
- 所有 IOCTL buffer 使用 `METHOD_BUFFERED`，避免用户态指针直接进入内核
- Spinlock 保护并发 IOCTL (简单场景用 `WdfSpinLock`；如果 Provider 可能长时间阻塞，升级到 `Mutex`)

---

*Rev 1.0 — 待 review*
