# Landauer

> 每擦除 1bit 信息都耗费  
> ![E = kT \ln(2)](https://latex.codecogs.com/svg.image?E=kT\ln(2))  
> 的能量。而 firmware 是你最接近这个物理过程的一次。  
> — Landauer's Principle

RWEverything 的 CLI 版本。Windows 内核驱动 + 用户态命令行，让 AI agent 能直接操作 PCIe 硬件寄存器。摒弃 GUI，所有输出可 grep/parse，所有错误显式报告。

中文 | [English](README_EN.md)

---

## 快速开始

```bat
:: 1. 启用测试签名（需重启一次）
bcdedit /set testsigning on

:: 2. 右键 install.bat → 以管理员身份运行
::    自动完成: 编译驱动 → 编译CLI → 签名 → 安装 → 启动

:: 3. 验证
landauer driver status
landauer pci list
```

卸载：
```bat
:: 右键 uninstall.bat → 以管理员身份运行
```

---

## 命令速览

```
landauer pci list                             列出所有 PCI 设备
landauer pci info <bdf>                       设备详情 (BAR, Cap, Class)
landauer pci cfg read <bdf> <off> [w]        读配置空间
landauer pci cfg write <bdf> <off> <val> [w] 写配置空间
landauer pci cap list <bdf>                   列出 Capability 链表
landauer pci cap find <bdf> <id>             查找特定 Capability
landauer pci ext-cap list <bdf>              列出 Extended Capability
landauer pci bar info <bdf> <idx>            查看 BAR 基址/大小
landauer pci bar map <bdf> <idx> [cache]     映射 BAR → handle
landauer pci bar read <handle> <off> [w]     读 BAR MMIO
landauer pci bar write <handle> <off> <v> [w]写 BAR MMIO
landauer pci bar dump <handle> <off> <len>   Dump MMIO (stdout/文件)
landauer pci bar unmap <handle>              释放 BAR 映射
landauer driver status                       驱动状态检查
```

BDF 格式: `bus:dev.func` 十六进制，如 `00:1f.6`

---

## 依赖

| 项 | 说明 |
|----|------|
| OS | Windows 10/11 x64 |
| 权限 | Administrator |
| 编译器 | Visual Studio 2022 (BuildTools 即可) |
| SDK | Windows 10 SDK (含 DDK 头文件) |
| 签名 | Test Signing Mode (`bcdedit /set testsigning on`) |

---

## 项目结构

```
landauer/
├── ARCHITECTURE.md      # 架构设计——通信协议、Provider 模式、扩展路线
├── COMMANDS.md          # 完整命令手册 (for AI Agents)
├── README.md            # 本文件
├── protocol.h           # 驱动-CLI 共享协议头 (命令结构体、枚举、payload)
├── install.bat          # 一键编译安装
├── uninstall.bat        # 一键卸载
│
├── driver/              # Legacy NT 内核驱动
│   ├── driver.c         # DriverEntry + IRP 分发
│   ├── dispatch.c       # 命令解析 + Provider 分派
│   ├── pci_provider.c   # PCI 配置空间 & BAR Provider
│   ├── bar_table.c/h    # BAR 映射表 (MmMapIoSpace + KSPIN_LOCK)
│   ├── provider.h       # Provider 接口定义
│   ├── landauer.inf     # 驱动安装信息
│   ├── landauer.sln     # VS 解决方案
│   ├── landauer.vcxproj # VS 项目文件
│   └── make.bat         # cl.exe 直接编译
│
└── cli/                 # 用户态 CLI
    ├── main.c           # CLI 入口 + 子命令路由
    ├── cmd_pci.c/h      # 全部 PCI 子命令实现
    ├── driver_if.c/h    # DeviceIoControl 封装
    ├── format.c/h       # 输出格式化
    ├── landauer.vcxproj # VS 项目文件
    └── make.bat         # MSBuild 编译
```

---

## Agent 用法示例

### 读取设备寄存器

```bash
# 找到网卡
landauer pci list | grep "8086.*Network"
# → PCI 07:00.0 8086:1533 02:00:00 Network Controller

# 查看设备详情
landauer pci info 07:00.0

# 读配置空间 vendor/device ID
landauer pci cfg read 07:00.0 0 4
# → 0x15338086

# 遍历 Capability
landauer pci cap list 07:00.0
# → [40] PM, [50] MSI, [70] MSI-X, [A0] PCIe
```

### MMIO 读写

```bash
# 映射 BAR
landauer pci bar map 07:00.0 0
# → BAR0 mapped: handle=1 base=0xDF100000 size=0x100000

# 读写寄存器
landauer pci bar read 1 0x5400 2
landauer pci bar write 1 0x12114 0x00000000

# Dump 到文件
landauer pci bar dump 1 0 0x200 bar0.bin

# 释放
landauer pci bar unmap 1
```

---

## 架构

- **Legacy NT 驱动** — 纯 NT API (IoCreateDevice + IRP 分发)，无 WDF 依赖
- **Provider 模式** — PCI_CFG Provider (HalGetBusDataByOffset) + PCI_BAR Provider (MmMapIoSpace)
- **单一 IOCTL** — 32 字节命令头 + 变长 payload，`resource_type` + `operation` 二级分派
- **自描述协议** — 命令头含 magic/version/status，驱动和 CLI 可独立演化

详见 [ARCHITECTURE.md](ARCHITECTURE.md)

---

## 扩展路线

| 版本 | 内容 |
|------|------|
| v1 (当前) | PCIe 全功能: CFG 空间、BAR MMIO、Capability |
| v2 | IO Port、MSR、CPUID |
| v3 | 任意物理内存、SMBIOS/ACPI |
| v4 | SPI Flash、I2C、GPIO |
| v5 | 硬件断点、MSI-X Table、AER 注入 |

---

## 输出约定

所有数据输出到 stdout，错误输出到 stderr。格式固定，可被 grep/awk 解析：

```
# 成功: exit 0
0xDF100000: 0x00000005

# 失败: exit != 0, stderr
ERR: Failed to read PCI config: ACCESS_DENIED (0xC0000022)
```
