# I210 BAR1 Physical Memory Driver — 需求文档


## 每擦除1bit信息都耗费kB*T*ln(2)能量，而firmware是你最接近这个物理过程的一次。


## 目标

Windows 内核驱动 + 用户态 CLI，让我（AI agent）能通过命令行直接读写 I210 网卡 BAR1 物理内存，
做到和 Linux `/dev/mem` + `mmap` 等价的能力。

## 整体架构

```
Reasonix (AI agent)
    │
    │  bash调用
    ▼
i210mem.exe  (CLI, 用户态)
    │  DeviceIoControl
    ▼
i210mem.sys  (KMDF 驱动, 内核态)
    │  MmMapIoSpace
    ▼
物理地址 0xDF100000  (I210 BAR1)
```

## 驱动需求

### 核心功能

| 功能 | 说明 |
|------|------|
| 映射物理内存 | `MmMapIoSpace` 将给定的物理地址 + 长度映射到内核 VA |
| READ IOCTL | 读取指定物理地址的 32-bit 值 |
| WRITE IOCTL | 写入指定物理地址的 32-bit 值 |
| REGION_INFO IOCTL | 返回当前映射的基址、大小、是否成功 |

### 接口设计

```
设备名: \\.\i210mem

IOCTL 代码:
  IOCTL_I210MEM_READ       — 输入: 物理地址(uint64), 输出: uint32
  IOCTL_I210MEM_WRITE      — 输入: {地址, 值}(16 bytes), 输出: 无
  IOCTL_I210MEM_MAP        — 输入: {基址, 大小}(16 bytes), 输出: 状态码
  IOCTL_I210MEM_UNMAP      — 清理映射
  IOCTL_I210MEM_READ_BLOCK — 输入: {地址, 长度}, 输出: buffer (用于 dump 大块)
```

### 技术要求

- KMDF (Kernel-Mode Driver Framework)
- 64-bit only (不必支持32位)
- 物理地址使用 `PHYSICAL_ADDRESS` 类型（64-bit）
- 环境：Windows 10/11 x64, VS2022 + WDK 10
- 测试模式签名即可（`testsigning on`），不需要 EV 签名

## CLI 工具需求 (i210mem.exe)

### 命令列表

```
i210mem.exe info                          — 显示 BAR1 映射状态
i210mem.exe read  <addr>                  — 读 4 字节 (地址是 BAR1 内的偏移, e.g. 12114)
i210mem.exe read  <addr> <bytes>          — 读指定字节数 (对齐到4)
i210mem.exe write <addr> <value>          — 写 4 字节 (value 用 hex, e.g. 0x00000000)
i210mem.exe bar0                          — 输出 BAR1 基址物理地址
```

### 便利命令（Flash 专用，针对 I210 SPI flash 操作）

```
i210mem.exe fl-secu                       — 读 FL_SECU (BAR1+0x12114), 并解析 bit0/1/2
i210mem.exe fl-unlock                     — 写 0 到 FL_SECU 尝试解锁, 读回确认
i210mem.exe fl-read  <flash_addr>         — 通过 FLA 读 flash 的一个 16-bit word
i210mem.exe fl-read  <flash_addr> <count> — 连续读多个 word
i210mem.exe fl-dump  <outfile> [bytes]    — dump 整片 flash 到文件 (默认 512KB)
i210mem.exe fl-info                       — 读取 FLMODE, FL_SECU, FLSWCTL, 打印状态
```

### 输出格式要求

所有输出必须是纯文本、可被 grep/parse 的格式：

```
# read 命令输出:
0xDF112114: 0x00000005

# fl-secu 输出:
FL_SECU @ 0xDF112114 = 0x00000005
  FL_LOCK  = 1
  FL_SM    = 0
  FL_DIS   = 1

# fl-read 输出:
0x000000: 0x4841
0x000002: 0x021C
...

# 错误输出到 stderr:
ERR: Failed to read physical address 0xDF112114
```

### 返回值 (exit code)

- 0 = 成功
- 非 0 = 失败（配合 stderr 输出错误信息）

## Flash 操作相关寄存器

| 寄存器 | BAR1 内偏移 | 功能 |
|--------|-----------|------|
| FLMODE  | 0x12000 | Flash 模式 |
| EERD    | 0x12014 | EEPROM 读 |
| FLA     | 0x1201C | Flash Access (GO + R/W + Addr) |
| FLSWCTL | 0x12048 | Flash Burst Control |
| FLSWDATA| 0x1204C | Flash Burst Data |
| FLSWCNT | 0x12050 | Flash Burst Count |
| FLASHOP | 0x12054 | Flash OP-Code |
| FL_SECU | 0x12114 | Flash Security |

### FLA 读 Flash 的步骤

```
1. 写 FLA = 0x80000000 | (flash_addr & 0x00FFFFFF)
   其中 0x80000000 = GO bit, CYCLE=READ(0x00000000)
2. 轮询 FLA 直到 bit31 (GO) = 0
3. 读 FLA, 低 16-bit 即为 Flash 数据
```

### FLSWCTL Burst Read 步骤

```
1. 写 FLSWCTL = (0x0B << 24) | (addr & 0x00FFFFFF)         // Fast Read cmd
2. 写 FLSWCNT = 4                                            // 读 4 字节
3. 写 FLSWCTL = (0x0B << 24) | (addr & 0x00FFFFFF) | (1<<28) // CMDV=1 触发
4. 等 FLSWCTL bit30 (DONE) = 1
5. 读 FLSWDATA 获得 4 字节
```

## 文件清单

```
i210mem/
├── driver/
│   ├── i210mem.sln          — VS 解决方案
│   ├── i210mem.vcxproj      — 驱动项目
│   ├── driver.c             — 驱动主逻辑 (DriverEntry, IOCTL 分发)
│   ├── device.c             — 设备创建/销毁
│   ├── ioctl.c              — IOCTL 处理 (READ/WRITE/MAP/UNMAP/READ_BLOCK)
│   ├── physmem.c            — MmMapIoSpace 封装
│   └── make.bat             — 编译脚本 (msbuild)
├── cli/
│   ├── i210mem.vcxproj      — CLI 项目
│   ├── main.c               — 命令行解析 + 派发
│   ├── flash.c              — fl-* 系列命令实现
│   ├── driver_if.c          — DeviceIoControl 封装
│   └── make.bat
├── install.bat              — 安装/卸载脚本 (sc create / sc delete)
└── README.md
```

## 驱动安装批处理

```bat
:: install.bat
:: 前置条件: bcdedit /set testsigning on  (需要重启一次)

:: 安装
sc create i210mem type=kernel binPath="C:\path\to\i210mem.sys"
sc start i210mem

:: 卸载
sc stop i210mem
sc delete i210mem
```

## 注意事项

1. **BAR1 基址**: 当前是 0xDF100000，但重映射后可能变化。建议支持命令行参数指定基址，或自动从 PCI 配置空间读取
2. **驱动签名**: 测试模式即可，不需要 WHQL
3. **线程安全**: 读写 IOCTL 加 spinlock 保护
4. **错误处理**: 物理地址无效 / 权限不足 / 驱动未加载 都要给出明确 stderr 信息
5. **不需要 GUI**, 纯 CLI 即可
