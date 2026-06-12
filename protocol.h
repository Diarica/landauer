/*
 * protocol.h — Landauer 内核驱动 & CLI 共享协议定义
 *
 * 这个文件同时被:
 *   - landauer.sys  (内核态, KMDF)
 *   - landauer.exe  (用户态, Win32)
 * 包含，确保两边命令结构、枚举、常量完全一致。
 */

#pragma once

#include <stdint.h>

/* ── 用户态 NTSTATUS 兼容 ── */
#ifdef LANDAUER_USERMODE
typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#endif

/* ============================================================
 *  全局常量
 * ============================================================ */

#define LANDAUER_DEVICE_NAME    L"\\\\.\\Landauer"
#define LANDAUER_NT_DEVICE_NAME L"\\Device\\Landauer"
#define LANDAUER_DOS_DEVICE_NAME L"\\DosDevices\\Landauer"

/* IOCTL 代码: METHOD_BUFFERED, 单一入口 */
#define LANDAUER_IOCTL_CMD CTL_CODE(              \
    FILE_DEVICE_UNKNOWN,                           \
    0x800,                                         \
    METHOD_BUFFERED,                               \
    FILE_READ_DATA | FILE_WRITE_DATA)

/* 协议魔数 & 版本 */
#define LANDAUER_MAGIC     0x3144574C   /* "LWD1" */
#define LANDAUER_VERSION   1

/* 命令头定长大小 */
#define LANDAUER_HEADER_SIZE  32

/* 最大 payload (输入/输出分别) */
#define LANDAUER_MAX_PAYLOAD  65536   /* 64KB */

/* ============================================================
 *  资源类型
 * ============================================================ */

typedef enum _LANDAUER_RESOURCE_TYPE {

    /* ──── PCIe 子系统 (v1) ──── */
    LANDAUER_RESOURCE_PCI_CFG       = 0x01,   /* PCI 配置空间 */
    LANDAUER_RESOURCE_PCI_BAR       = 0x02,   /* BAR MMIO 映射 */

    /* ──── CPU 级资源 (v2) ──── */
    LANDAUER_RESOURCE_IO_PORT       = 0x10,   /* x86 IO 端口 */
    LANDAUER_RESOURCE_MSR           = 0x11,   /* Model-Specific Register */
    LANDAUER_RESOURCE_PHYS_MEM      = 0x12,   /* 任意物理内存 */
    LANDAUER_RESOURCE_CPUID         = 0x13,   /* CPUID 指令 */

    /* ──── 固件 / 表 (v3) ──── */
    LANDAUER_RESOURCE_SMBIOS        = 0x20,
    LANDAUER_RESOURCE_ACPI          = 0x21,
    LANDAUER_RESOURCE_EC            = 0x22,   /* Embedded Controller */

    /* ──── 外设总线 (v4) ──── */
    LANDAUER_RESOURCE_SPI           = 0x30,
    LANDAUER_RESOURCE_I2C           = 0x31,
    LANDAUER_RESOURCE_GPIO          = 0x32,

    /* ──── 扩展 ──── */
    LANDAUER_RESOURCE_VENDOR_BASE   = 0x80,
    LANDAUER_RESOURCE_MAX           = 0xFF

} LANDAUER_RESOURCE_TYPE;

/* ============================================================
 *  操作码
 * ============================================================ */

typedef enum _LANDAUER_OPERATION {

    /* 通用 */
    LANDAUER_OP_NONE               = 0x00,
    LANDAUER_OP_GET_INFO           = 0x01,   /* 查询资源信息 */
    LANDAUER_OP_READ               = 0x02,   /* 单次读取 (宽度=access_width) */
    LANDAUER_OP_WRITE              = 0x03,   /* 单次写入 (宽度=access_width) */
    LANDAUER_OP_READ_BLOCK         = 0x04,   /* 块读取 */
    LANDAUER_OP_WRITE_BLOCK        = 0x05,   /* 块写入 */
    LANDAUER_OP_MAP                = 0x06,   /* 映射资源 */
    LANDAUER_OP_UNMAP              = 0x07,   /* 解除映射 */

    /* PCIe 特化 */
    LANDAUER_OP_PCI_ENUMERATE      = 0x10,   /* 枚举 PCI 总线 */
    LANDAUER_OP_PCI_FIND_CAP       = 0x11,   /* 查找 Capability */
    LANDAUER_OP_PCI_FIND_EXT_CAP   = 0x12    /* 查找 Extended Capability */

} LANDAUER_OPERATION;

/* ============================================================
 *  flags (按位组合, 部分 flag 跨操作共用)
 * ============================================================ */

#define LANDAUER_FLAG_NONE              0x00

/* ── READ / WRITE flags ── */
#define LANDAUER_FLAG_ATOMIC            0x01   /* 要求原子访问 (如 PCI lock) */

/* ── MAP flags ── */
#define LANDAUER_FLAG_MAP_CACHED        0x00   /* 默认: 缓存映射 */
#define LANDAUER_FLAG_MAP_UNCACHED      0x01   /* 非缓存 (MMIO 必须) */
#define LANDAUER_FLAG_MAP_WRITECOMBINE  0x02   /* Write-Combining */

/* ── ENUMERATE flags ── */
#define LANDAUER_FLAG_ENUM_DEEP         0x01   /* 递归扫描所有 bus */

/* ============================================================
 *  命令头 (32 字节, 定长)
 * ============================================================ */

#pragma pack(push, 1)
typedef struct _LANDAUER_CMD_HEADER {
    uint32_t magic;            /* [IN]  必须 = LANDAUER_MAGIC                     */
    uint32_t version;          /* [IN]  请求协议版本; [OUT] 驱动支持的最高版本      */
    uint8_t  resource_type;    /* [IN]  LANDAUER_RESOURCE_TYPE                    */
    uint8_t  operation;        /* [IN]  LANDAUER_OPERATION                        */
    uint8_t  access_width;     /* [IN]  1/2/4/8 字节 (READ/WRITE 时有效)          */
    uint8_t  flags;            /* [IN]  按位组合, 见上                            */
    uint32_t status;           /* [OUT] NTSTATUS (0 = 成功)                        */
    uint32_t data_length;      /* [IN/OUT] payload 字节数                          */
    uint64_t address;          /* [IN]  资源特定地址 (见各资源定义)                  */
    uint32_t reserved[3];      /* 保留, 填 0                                       */
} LANDAUER_CMD_HEADER;

typedef LANDAUER_CMD_HEADER* PLANDAUER_CMD_HEADER;

#pragma pack(pop)

/* ---- 编译期大小检查 ---- */
#if defined(__cplusplus) || defined(_MSVC_LANG)
static_assert(sizeof(LANDAUER_CMD_HEADER) == 32, "Header must be 32 bytes");
#else
/* C11 _Static_assert — MSVC may not support in C mode; ignored */
#endif

/* ============================================================
 *  辅助宏: 构建请求 & 解析响应
 * ============================================================ */

/* 获取输入 payload 指针 (紧跟 header 之后) */
#define LANDAUER_INPUT_PAYLOAD(hdr)  \
    ((uint8_t*)(hdr) + LANDAUER_HEADER_SIZE)

/* 获取输出 payload 指针 */
#define LANDAUER_OUTPUT_PAYLOAD(hdr) \
    ((uint8_t*)(hdr) + LANDAUER_HEADER_SIZE)

/* 计算总 buffer 大小 */
#define LANDAUER_BUFFER_SIZE(payload_len) \
    (LANDAUER_HEADER_SIZE + (payload_len))

/* ============================================================
 *  PCI 地址编码 / 解码
 *
 *  将 {segment, bus, device, function, offset} 编入 uint64_t:
 *
 *  [63:52]  segment    (0-65535, 通常为 0)
 *  [51:36]  reserved
 *  [35:20]  bus        (0-255)
 *  [19:15]  device     (0-31)
 *  [14:12]  function   (0-7)
 *  [11:0]   offset     (0-4095)
 * ============================================================ */

#define PCI_ADDR_ENCODE(seg, bus, dev, func, off)  \
    ( ((uint64_t)(seg)  << 52) |                    \
      ((uint64_t)(bus)  << 20) |                    \
      ((uint64_t)(dev)  << 15) |                    \
      ((uint64_t)(func) << 12) |                    \
      ((uint64_t)(off)  & 0xFFF) )

#define PCI_ADDR_SEGMENT(x)   ((uint16_t)((x) >> 52))
#define PCI_ADDR_BUS(x)       ((uint8_t) ((x) >> 20) & 0xFF)
#define PCI_ADDR_DEVICE(x)    ((uint8_t) ((x) >> 15) & 0x1F)
#define PCI_ADDR_FUNCTION(x)  ((uint8_t) ((x) >> 12) & 0x07)
#define PCI_ADDR_OFFSET(x)    ((uint16_t)((x) & 0xFFF))

#define PCI_BDF_STR_SIZE  12   /* "00:1f.6" + NUL */
/* 辅助: 格式化 BDF 到 buffer, 返回 buffer 指针 */
static inline const char* pci_bdf_str(uint8_t bus, uint8_t dev, uint8_t func,
                                       char buf[PCI_BDF_STR_SIZE])
{
    int pos = 0;
    buf[pos++] = "0123456789ABCDEF"[(bus >> 4) & 0xF];
    buf[pos++] = "0123456789ABCDEF"[bus & 0xF];
    buf[pos++] = ':';
    buf[pos++] = "0123456789ABCDEF"[(dev >> 4) & 0xF];
    buf[pos++] = "0123456789ABCDEF"[dev & 0xF];
    buf[pos++] = '.';
    buf[pos++] = "0123456789ABCDEF"[func & 0xF];
    buf[pos]   = '\0';
    return buf;
}

/* ============================================================
 *  PCI 通用 payload 结构体
 * ============================================================ */

#pragma pack(push, 1)

#pragma pack(push, 1)

/* ── PCI_CFG GET_INFO 输出 ── */
typedef struct _PCI_CFG_INFO {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status_reg;
    uint8_t  revision_id;
    uint8_t  prog_if;
    uint8_t  sub_class;
    uint8_t  base_class;
    uint8_t  header_type;
    uint8_t  bist;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  num_bars;                  /* BAR 数量 (0-6) */
    struct {
        uint8_t  index;                 /* 0-5 */
        uint8_t  type;                  /* 0=memory, 1=io */
        uint8_t  bits;                  /* 32 或 64 */
        uint8_t  prefetchable;
        uint64_t base;
        uint64_t size;
    } bars[6];
} PCI_CFG_INFO;

/* ── PCI ENUMERATE 输入 ── */
typedef struct _PCI_ENUM_IN {
    uint16_t segment;
    uint16_t start_bus;
    uint16_t max_devices;               /* 最多返回多少设备 */
    uint16_t reserved;
} PCI_ENUM_IN;

/* ── PCI ENUMERATE 单个设备条目 ── */
typedef struct _PCI_ENUM_DEVICE {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint8_t  header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  base_class;
    uint8_t  sub_class;
    uint8_t  prog_if;
    uint8_t  reserved;
} PCI_ENUM_DEVICE;

/* ── PCI ENUMERATE 输出 ── */
typedef struct _PCI_ENUM_OUT {
    uint16_t count;
    uint16_t truncated;                 /* 1 = 结果被截断 */
    /* PCI_ENUM_DEVICE devices[count]; 跟在后面 */
} PCI_ENUM_OUT;

/* ── PCI FIND_CAP / FIND_EXT_CAP 输入 ── */
typedef struct _PCI_CAP_FIND_IN {
    uint8_t  cap_id;
    uint8_t  start_offset;              /* 标准 cap 的起始 offset */
    uint8_t  reserved[2];
} PCI_CAP_FIND_IN;

/* ── PCI FIND_CAP / FIND_EXT_CAP 输出 ── */
typedef struct _PCI_CAP_FIND_OUT {
    uint8_t  found;                     /* 0/1 */
    uint8_t  cap_id;
    uint8_t  cap_offset;
    uint8_t  reserved;
    uint32_t cap_value;                 /* Cap 首 4 字节 */
} PCI_CAP_FIND_OUT;

/* ── PCI_BAR MAP 输入 ── */
typedef struct _PCI_BAR_MAP_IN {
    uint64_t bar_base;                  /* 物理基址 */
    uint64_t bar_size;                  /* BAR 大小 */
    uint8_t  bar_index;                 /* BAR 编号 (0-5) */
    uint8_t  cache_type;                /* 0=cached, 1=uncached, 2=writecombine */
    uint8_t  reserved[6];
} PCI_BAR_MAP_IN;

/* ── PCI_BAR MAP 输出 ── */
typedef struct _PCI_BAR_MAP_OUT {
    uint32_t map_handle;                /* 后续操作用的句柄 */
    uint32_t reserved;
    uint64_t mapped_va;                  /* 调试用 (内核 VA) */
} PCI_BAR_MAP_OUT;

/* ── PCI_BAR GET_INFO 输入 ── */
typedef struct _PCI_BAR_INFO_IN {
    uint8_t  bar_index;                 /* 0-5 */
    uint8_t  reserved[7];
} PCI_BAR_INFO_IN;

/* ── PCI_BAR GET_INFO 输出 ── */
typedef struct _PCI_BAR_INFO_OUT {
    uint8_t  bar_index;
    uint8_t  type;                      /* 0=memory, 1=io */
    uint8_t  bits;
    uint8_t  prefetchable;
    uint8_t  mapped;                    /* 0/1 是否已被驱动映射 */
    uint8_t  reserved[3];
    uint64_t base;
    uint64_t size;
    uint64_t mapped_va;
} PCI_BAR_INFO_OUT;

#pragma pack(pop)

#pragma pack(pop)

/* ============================================================
 *  通用 read/write 输出 payload
 * ============================================================ */

/* 单次 READ: payload = data (access_width 字节)                   */
/* 单次 WRITE: input payload = data (access_width 字节)            */
/* 块读: output payload = raw bytes                               */
/* 块写: input payload = raw bytes                                */

/* ============================================================
 *  错误状态 → 字符串 (CLI 端)
 * ============================================================ */

/* CLI 可以调用此函数将 driver 返回的 NTSTATUS 转为可读字符串。
 * 内核端不需要此函数 (直接用 NTSTATUS)。
 * 这里只列出常见的几种用于 CLI 格式化。 */
#ifdef LANDAUER_USERMODE  /* 仅在用户态编译 */
static inline const char* landauer_status_str(uint32_t status)
{
    switch (status) {
    case 0x00000000: return "OK";
    case 0xC0000001: return "UNSUCCESSFUL";
    case 0xC000000D: return "INVALID_PARAMETER";
    case 0xC0000022: return "ACCESS_DENIED";
    case 0xC0000002: return "NOT_IMPLEMENTED";
    case 0xC0000003: return "INVALID_HANDLE";
    case 0xC000009A: return "INSUFFICIENT_RESOURCES";
    case 0xC0000010: return "INVALID_DEVICE_REQUEST";
    case 0xC00000F0: return "NO_SUCH_DEVICE";
    default:         return "(unknown NTSTATUS)";
    }
}
#endif
