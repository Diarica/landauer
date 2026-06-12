/*
 * format.c — 输出格式化
 *
 * 铁律: 所有输出可 grep/parse, 错误到 stderr。
 */

#include "format.h"
#include <stdio.h>
#include <stdarg.h>

/* ── stderr 错误 ── */
void
FmtErr(const char* fmt, ...)
{
    va_list args;
    fprintf(stderr, "ERR: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ── stdout ── */
void
FmtOut(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* ── NTSTATUS → 字符串 ── */
const char*
FmtNtStatus(NTSTATUS status)
{
    switch ((ULONG)status) {
    case 0x00000000: return "OK";
    case 0xC0000001: return "UNSUCCESSFUL";
    case 0xC0000002: return "NOT_IMPLEMENTED";
    case 0xC0000003: return "INVALID_HANDLE";
    case 0xC000000D: return "INVALID_PARAMETER";
    case 0xC0000010: return "INVALID_DEVICE_REQUEST";
    case 0xC0000022: return "ACCESS_DENIED";
    case 0xC000009A: return "INSUFFICIENT_RESOURCES";
    case 0xC00000F0: return "NO_SUCH_DEVICE";
    case 0xC0000135: return "DLL_NOT_FOUND";
    default:
        if ((ULONG)status == 0xFFFFFFFF)
            return "DEVICE_NOT_FOUND (all Fs)";
        return "(unknown)";
    }
}

/* ── Hex Dump ── */
void
FmtHexDump(const UINT8* data, ULONG len, UINT64 baseAddr)
{
    for (ULONG i = 0; i < len; i += 16) {
        /* 地址 */
        printf("0x%04llX: ", (unsigned long long)(baseAddr + i));

        /* Hex */
        for (ULONG j = 0; j < 16; j++) {
            if (i + j < len) {
                printf("%02X ", data[i + j]);
            } else {
                printf("   ");
            }
        }

        /* ASCII */
        printf(" |");
        for (ULONG j = 0; j < 16 && (i + j) < len; j++) {
            UINT8 c = data[i + j];
            putchar((c >= 32 && c < 127) ? c : '.');
        }
        printf("|\n");
    }
}

/* ── 简短设备行 ── */
void
FmtPciDeviceShort(UINT8 bus, UINT8 dev, UINT8 func,
                  UINT16 vendor, UINT16 device_id,
                  UINT8 baseClass, UINT8 subClass,
                  UINT8 headerType)
{
    char bdf[PCI_BDF_STR_SIZE];
    pci_bdf_str(bus, dev, func, bdf);

    /* 友好的 class 名 */
    const char* class_name = "?";
    switch (baseClass) {
    case 0x00: class_name = "(Before PCI 2.0)"; break;
    case 0x01: class_name = "Mass Storage Controller"; break;
    case 0x02: class_name = "Network Controller"; break;
    case 0x03: class_name = "Display Controller"; break;
    case 0x04: class_name = "Multimedia Controller"; break;
    case 0x05: class_name = "Memory Controller"; break;
    case 0x06: class_name = "Bridge Device"; break;
    case 0x07: class_name = "Communication Controller"; break;
    case 0x08: class_name = "Base System Peripheral"; break;
    case 0x09: class_name = "Input Device"; break;
    case 0x0A: class_name = "Docking Station"; break;
    case 0x0B: class_name = "Processor"; break;
    case 0x0C: class_name = "Serial Bus Controller"; break;
    case 0x0D: class_name = "Wireless Controller"; break;
    case 0x0E: class_name = "Intelligent Controller"; break;
    case 0x0F: class_name = "Satellite Controller"; break;
    case 0x10: class_name = "Encryption Controller"; break;
    case 0x11: class_name = "Signal Processing Controller"; break;
    case 0x12: class_name = "Processing Accelerator"; break;
    case 0x13: class_name = "Non-Essential Instrumentation"; break;
    case 0x40: class_name = "Coprocessor"; break;
    case 0xFF: class_name = "Unassigned"; break;
    default:   class_name = "Reserved"; break;
    }

    printf("PCI %s %04X:%04X %02X:%02X:%02X %s%s\n",
           bdf, vendor, device_id,
           baseClass, subClass, (headerType & 0x7F),
           class_name,
           (headerType & 0x80) ? " [MF]" : "");
}

/* ── 完整设备信息 ── */
void
FmtPciDeviceFull(PCI_CFG_INFO* info, UINT8 bus, UINT8 dev, UINT8 func)
{
    char bdf[PCI_BDF_STR_SIZE];
    pci_bdf_str(bus, dev, func, bdf);

    printf("Device: %s\n", bdf);
    printf("  Vendor: %04X  Device: %04X\n",
           info->vendor_id, info->device_id);
    printf("  Class:  %02X%02X%02X",
           info->base_class, info->sub_class, info->prog_if);
    switch (info->base_class) {
    case 0x02:
        if (info->sub_class == 0x00) printf(" (Ethernet Controller)");
        break;
    case 0x06:
        if (info->sub_class == 0x04) printf(" (PCI-to-PCI Bridge)");
        else if (info->sub_class == 0x00) printf(" (Host Bridge)");
        else if (info->sub_class == 0x01) printf(" (ISA Bridge)");
        break;
    case 0x0C:
        if (info->sub_class == 0x03 && info->prog_if == 0x30)
            printf(" (XHCI USB Controller)");
        else if (info->sub_class == 0x03 && info->prog_if == 0x20)
            printf(" (EHCI USB Controller)");
        break;
    default: break;
    }
    printf("\n");
    printf("  Revision: %02X  Header Type: %02X\n",
           info->revision_id, info->header_type);

    /* BARs */
    for (int i = 0; i < info->num_bars; i++) {
        if (info->bars[i].base != 0 || info->bars[i].size != 0) {
            printf("  BAR%d: %s %d-bit @ 0x%llX  size=",
                   info->bars[i].index,
                   info->bars[i].type == 1 ? "IO" : "Memory",
                   info->bars[i].bits,
                   (unsigned long long)info->bars[i].base);
            if (info->bars[i].size >= 1024*1024)
                printf("%lluMB\n",
                       (unsigned long long)info->bars[i].size / (1024*1024));
            else if (info->bars[i].size >= 1024)
                printf("%lluKB\n",
                       (unsigned long long)info->bars[i].size / 1024);
            else
                printf("%lluB\n",
                       (unsigned long long)info->bars[i].size);
        }
    }
}

/* ── Capability ── */
void
FmtPciCap(PVOID capOutPtr, const char* capType)
{
    PCI_CAP_FIND_OUT* out = (PCI_CAP_FIND_OUT*)capOutPtr;
    if (out->found) {
        printf("  [%02X] %s Cap ID=%02X Value=0x%08X\n",
               out->cap_offset, capType, out->cap_id, out->cap_value);
    }
}

/* ── BAR 信息 ── */
void
FmtBarInfo(PCI_BAR_INFO_OUT* info, UINT8 bus, UINT8 dev, UINT8 func)
{
    char bdf[PCI_BDF_STR_SIZE];
    pci_bdf_str(bus, dev, func, bdf);

    printf("Device: %s BAR%d\n", bdf, info->bar_index);
    printf("  Type:        %s\n",
           info->type == 1 ? "IO Port" : "Memory");
    printf("  Width:       %d-bit\n", info->bits);
    if (info->type == 0) {
        printf("  Prefetchable: %s\n",
               info->prefetchable ? "Yes" : "No");
    }
    printf("  Base:        0x%llX\n",
           (unsigned long long)info->base);
    printf("  Size:        ");
    if (info->size >= 1024*1024)
        printf("%lluMB\n",
               (unsigned long long)info->size / (1024*1024));
    else if (info->size >= 1024)
        printf("%lluKB\n",
               (unsigned long long)info->size / 1024);
    else
        printf("%lluB\n", (unsigned long long)info->size);
    printf("  Mapped:       %s\n", info->mapped ? "Yes" : "No (use 'bar map')");
}
