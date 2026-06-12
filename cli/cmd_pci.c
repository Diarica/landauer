/*
 * cmd_pci.c — PCI subcommand implementations
 *
 * ARGV convention (v2): every leaf function receives:
 *   argv[0] = action name (e.g. "read", "list", "map", "info")
 *   argv[1] = first real argument
 *   argv[2] = second real argument
 *   ...
 * argc = total count including argv[0].
 *
 * Required arg count for each: argc >= (1 + min_args).
 */

#include "cmd_pci.h"
#include "driver_if.h"
#include "format.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ── helpers ── */
static BOOL ParseBdf(const wchar_t* s, PUINT8 bus, PUINT8 dev, PUINT8 func)
{
    unsigned int b, d, f;
    if (swscanf_s(s, L"%x:%x.%x", &b, &d, &f) != 3) return FALSE;
    if (b > 255 || d > 31 || f > 7) return FALSE;
    *bus = (UINT8)b; *dev = (UINT8)d; *func = (UINT8)f;
    return TRUE;
}

static BOOL ParseHex(const wchar_t* s, PULONG64 val)
{
    return swscanf_s(s, L"%llx", val) == 1 ||
           swscanf_s(s, L"0x%llx", val) == 1;
}

/* ============================================================
 *  pci list  →  argv[0]="list"
 * ============================================================ */
int CmdPciList(HANDLE hDevice, int argc, wchar_t* argv[])
{
    UNREFERENCED_PARAMETER(argc); UNREFERENCED_PARAMETER(argv);

    LANDAUER_CMD_HEADER hdr;
    PCI_ENUM_IN  ein;
    UINT8        buffer[65536];
    ULONG        outLen;

    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic         = LANDAUER_MAGIC;
    hdr.version       = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_CFG;
    hdr.operation     = LANDAUER_OP_PCI_ENUMERATE;

    RtlZeroMemory(&ein, sizeof(ein));
    ein.start_bus   = 0;
    ein.max_devices = 1024;

    NTSTATUS st = LandauerCommand(hDevice, &hdr,
        &ein, sizeof(ein), buffer, sizeof(buffer), &outLen);
    if (!NT_SUCCESS(st)) {
        FmtErr("Enumeration failed: %s (0x%08X)", FmtNtStatus(st), (ULONG)st);
        return 3;
    }

    PCI_ENUM_OUT* out = (PCI_ENUM_OUT*)buffer;
    PCI_ENUM_DEVICE* devs = (PCI_ENUM_DEVICE*)(out + 1);

    if (outLen < sizeof(PCI_ENUM_OUT)) { FmtErr("No data"); return 3; }

    for (USHORT i = 0; i < out->count; i++) {
        FmtPciDeviceShort(devs[i].bus, devs[i].device, devs[i].function,
            devs[i].vendor_id, devs[i].device_id,
            devs[i].base_class, devs[i].sub_class, devs[i].header_type);
    }
    if (out->truncated) FmtErr("Result truncated");
    return 0;
}

/* ============================================================
 *  pci info <bdf>  →  argv[0]="info", argv[1]=bdf
 * ============================================================ */
int CmdPciInfo(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 2) { FmtErr("Usage: pci info <bdf>"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }

    LANDAUER_CMD_HEADER hdr;
    PCI_CFG_INFO info;
    ULONG outLen;

    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_CFG;
    hdr.operation     = LANDAUER_OP_GET_INFO;
    hdr.address       = PCI_ADDR_ENCODE(0, bus, dev, func, 0);

    NTSTATUS st = LandauerCommand(hDevice, &hdr, NULL, 0, &info, sizeof(info), &outLen);
    if (!NT_SUCCESS(st)) { FmtErr("Failed: %s", FmtNtStatus(st)); return 3; }

    FmtPciDeviceFull(&info, bus, dev, func);

    /* Print capabilities inline */
    printf("  Capabilities: ");
    UCHAR capOff = 0; BOOL first = TRUE; int capCount = 0;
    while (capCount < 48) {
        PCI_CAP_FIND_IN  ci; PCI_CAP_FIND_OUT co;
        LANDAUER_CMD_HEADER ch;
        ULONG ol;
        RtlZeroMemory(&ch, sizeof(ch));
        ch.magic = LANDAUER_MAGIC; ch.version = LANDAUER_VERSION;
        ch.resource_type = LANDAUER_RESOURCE_PCI_CFG;
        ch.operation = LANDAUER_OP_PCI_FIND_CAP;
        ch.address   = PCI_ADDR_ENCODE(0, bus, dev, func, 0);
        RtlZeroMemory(&ci, sizeof(ci));
        ci.cap_id = 0; ci.start_offset = capOff;
        NTSTATUS st = LandauerCommand(hDevice, &ch, &ci, sizeof(ci), &co, sizeof(co), &ol);
        if (!NT_SUCCESS(st) || !co.found) break;
        if (!first) printf(", ");
        printf("[%02X] %s", co.cap_offset,
            co.cap_id == 0x01 ? "PM" : co.cap_id == 0x05 ? "MSI" :
            co.cap_id == 0x10 ? "PCIe" : co.cap_id == 0x11 ? "MSI-X" : "?");
        first = FALSE;
        UCHAR next = (UCHAR)((co.cap_value >> 8) & 0xFF);
        if (next == 0 || next <= capOff) break;
        capOff = next;
        capCount++;
    }
    printf("\n");
    return 0;
}

/* ============================================================
 *  pci cfg read <bdf> <offset> [width]  →  argv[0]="read", argv[1]=bdf, argv[2]=off, argv[3]=w
 * ============================================================ */
int CmdPciCfgRead(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 3) { FmtErr("Usage: pci cfg read <bdf> <offset> [width]"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }

    ULONG64 off64; if (!ParseHex(argv[2], &off64)) { FmtErr("Invalid offset: %ls", argv[2]); return 1; }
    USHORT offset = (USHORT)off64;

    UCHAR width = 4;
    if (argc >= 4) { ULONG64 w; if (!ParseHex(argv[3], &w) || (w!=1&&w!=2&&w!=4)) { FmtErr("Invalid width"); return 1; } width = (UCHAR)w; }

    ULONG64 addr = PCI_ADDR_ENCODE(0, bus, dev, func, offset);
    ULONG64 val = 0;
    NTSTATUS st = LandauerRead(hDevice, LANDAUER_RESOURCE_PCI_CFG, width, addr, &val);
    if (!NT_SUCCESS(st)) { FmtErr("Read failed: %s", FmtNtStatus(st)); return 3; }

    if (width == 1) printf("0x%02llX\n", (unsigned long long)(val & 0xFF));
    else if (width == 2) printf("0x%04llX\n", (unsigned long long)(val & 0xFFFF));
    else printf("0x%08llX\n", (unsigned long long)(val & 0xFFFFFFFF));
    return 0;
}

/* ============================================================
 *  pci cfg write <bdf> <offset> <value> [width]  →  argv[0]="write", argv[1]=bdf, argv[2]=off, argv[3]=val, argv[4]=w
 * ============================================================ */
int CmdPciCfgWrite(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 4) { FmtErr("Usage: pci cfg write <bdf> <offset> <value> [width]"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }

    ULONG64 off64; if (!ParseHex(argv[2], &off64)) { FmtErr("Invalid offset: %ls", argv[2]); return 1; }
    USHORT offset = (USHORT)off64;

    ULONG64 value; if (!ParseHex(argv[3], &value)) { FmtErr("Invalid value: %ls", argv[3]); return 1; }

    UCHAR width = 4;
    if (argc >= 5) { ULONG64 w; if (!ParseHex(argv[4], &w) || (w!=1&&w!=2&&w!=4)) { FmtErr("Invalid width"); return 1; } width = (UCHAR)w; }

    ULONG64 addr = PCI_ADDR_ENCODE(0, bus, dev, func, offset);
    NTSTATUS st = LandauerWrite(hDevice, LANDAUER_RESOURCE_PCI_CFG, width, addr, value);
    if (!NT_SUCCESS(st)) { FmtErr("Write failed: %s", FmtNtStatus(st)); return 3; }

    /* readback */
    ULONG64 rb = 0;
    st = LandauerRead(hDevice, LANDAUER_RESOURCE_PCI_CFG, width, addr, &rb);
    if (NT_SUCCESS(st)) {
        if (width == 1) printf("0x%02llX\n", (unsigned long long)(rb & 0xFF));
        else if (width == 2) printf("0x%04llX\n", (unsigned long long)(rb & 0xFFFF));
        else printf("0x%08llX\n", (unsigned long long)(rb & 0xFFFFFFFF));
    }
    return 0;
}

/* ============================================================
 *  pci cap list <bdf>  →  argv[0]="list", argv[1]=bdf
 * ============================================================ */
int CmdPciCapList(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 2) { FmtErr("Usage: pci cap list <bdf>"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }

    char bdf[PCI_BDF_STR_SIZE]; pci_bdf_str(bus, dev, func, bdf);
    printf("Capabilities for %s:\n", bdf);

    UCHAR capOff = 0;
    int capCount = 0;
    while (capCount < 48) {
        PCI_CAP_FIND_IN  ci; PCI_CAP_FIND_OUT co;
        LANDAUER_CMD_HEADER hdr; ULONG ol;
        RtlZeroMemory(&hdr, sizeof(hdr));
        hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
        hdr.resource_type = LANDAUER_RESOURCE_PCI_CFG;
        hdr.operation = LANDAUER_OP_PCI_FIND_CAP;
        hdr.address = PCI_ADDR_ENCODE(0, bus, dev, func, 0);
        RtlZeroMemory(&ci, sizeof(ci)); ci.cap_id = 0; ci.start_offset = capOff;
        NTSTATUS st = LandauerCommand(hDevice, &hdr, &ci, sizeof(ci), &co, sizeof(co), &ol);
        if (!NT_SUCCESS(st) || !co.found) break;
        FmtPciCap(&co, "Standard");
        UCHAR next = (UCHAR)((co.cap_value >> 8) & 0xFF);
        if (next == 0 || next <= capOff) break;
        capOff = next;
        capCount++;
    }
    return 0;
}

/* ============================================================
 *  pci cap find <bdf> <cap_id>  →  argv[0]="find", argv[1]=bdf, argv[2]=cap_id
 * ============================================================ */
int CmdPciCapFind(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 3) { FmtErr("Usage: pci cap find <bdf> <cap_id>"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }
    ULONG64 c64; if (!ParseHex(argv[2], &c64) || c64 > 255) { FmtErr("Invalid cap ID"); return 1; }
    UCHAR capId = (UCHAR)c64;

    PCI_CAP_FIND_IN  ci; PCI_CAP_FIND_OUT co;
    LANDAUER_CMD_HEADER hdr; ULONG ol;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_CFG;
    hdr.operation = LANDAUER_OP_PCI_FIND_CAP;
    hdr.address = PCI_ADDR_ENCODE(0, bus, dev, func, 0);
    RtlZeroMemory(&ci, sizeof(ci)); ci.cap_id = capId; ci.start_offset = 0;

    NTSTATUS st = LandauerCommand(hDevice, &hdr, &ci, sizeof(ci), &co, sizeof(co), &ol);
    if (!NT_SUCCESS(st)) { FmtErr("Failed: %s", FmtNtStatus(st)); return 3; }
    if (!co.found) { FmtErr("Capability 0x%02X not found", capId); return 4; }
    char bdf[PCI_BDF_STR_SIZE]; pci_bdf_str(bus, dev, func, bdf);
    printf("%s Cap 0x%02X @ offset 0x%02X = 0x%08X\n", bdf, co.cap_id, co.cap_offset, co.cap_value);
    return 0;
}

/* ============================================================
 *  pci ext-cap list <bdf>  →  argv[0]="list", argv[1]=bdf
 * ============================================================ */
int CmdPciExtCapList(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 2) { FmtErr("Usage: pci ext-cap list <bdf>"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }

    char bdf[PCI_BDF_STR_SIZE]; pci_bdf_str(bus, dev, func, bdf);
    printf("Extended Capabilities for %s:\n", bdf);

    USHORT extOff = 0x100;
    int extCount = 0;
    while (extOff >= 0x100 && extOff < 0x1000 && extCount < 48) {
        PCI_CAP_FIND_IN  ci; PCI_CAP_FIND_OUT co;
        LANDAUER_CMD_HEADER hdr; ULONG ol;
        RtlZeroMemory(&hdr, sizeof(hdr));
        hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
        hdr.resource_type = LANDAUER_RESOURCE_PCI_CFG;
        hdr.operation = LANDAUER_OP_PCI_FIND_EXT_CAP;
        hdr.address = PCI_ADDR_ENCODE(0, bus, dev, func, 0) | extOff;

        RtlZeroMemory(&ci, sizeof(ci));
        NTSTATUS st = LandauerCommand(hDevice, &hdr, &ci, sizeof(ci), &co, sizeof(co), &ol);
        if (!NT_SUCCESS(st)) { FmtErr("Failed at 0x%03X: %s", extOff, FmtNtStatus(st)); break; }
        if (!co.found) break;

        USHORT capId = (USHORT)(co.cap_value & 0xFFFF);
        printf("  [%03X] Ext Cap ID=0x%04X Value=0x%08X\n", extOff, capId, co.cap_value);

        USHORT next = (USHORT)((co.cap_value >> 20) & 0xFFF);
        if (next == 0 || next <= extOff) break;
        extOff = next;
        extCount++;
    }
    return 0;
}

/* ============================================================
 *  pci bar info <bdf> <bar_index>  →  argv[0]="info", argv[1]=bdf, argv[2]=bar_idx
 * ============================================================ */
int CmdPciBarInfo(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 3) { FmtErr("Usage: pci bar info <bdf> <bar_index>"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }
    ULONG64 bi64; if (!ParseHex(argv[2], &bi64) || bi64 > 5) { FmtErr("Invalid BAR index"); return 1; }
    UCHAR barIdx = (UCHAR)bi64;

    LANDAUER_CMD_HEADER hdr; PCI_BAR_INFO_IN  bi; PCI_BAR_INFO_OUT bo; ULONG ol;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_BAR;
    hdr.operation = LANDAUER_OP_GET_INFO;
    hdr.address   = PCI_ADDR_ENCODE(0, bus, dev, func, 0);
    RtlZeroMemory(&bi, sizeof(bi)); bi.bar_index = barIdx;

    NTSTATUS st = LandauerCommand(hDevice, &hdr, &bi, sizeof(bi), &bo, sizeof(bo), &ol);
    if (!NT_SUCCESS(st)) { FmtErr("Failed: %s", FmtNtStatus(st)); return 3; }
    if (bo.base == 0 && bo.size == 0) { FmtErr("BAR%d not present", barIdx); return 4; }
    FmtBarInfo(&bo, bus, dev, func);
    return 0;
}

/* ============================================================
 *  pci bar map <bdf> <bar_index> [cache]  →  argv[0]="map", argv[1]=bdf, argv[2]=idx, argv[3]=cache
 * ============================================================ */
int CmdPciBarMap(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 3) { FmtErr("Usage: pci bar map <bdf> <bar_index> [cache]"); return 1; }
    UINT8 bus, dev, func;
    if (!ParseBdf(argv[1], &bus, &dev, &func)) { FmtErr("Invalid BDF: %ls", argv[1]); return 1; }
    ULONG64 bi64; if (!ParseHex(argv[2], &bi64) || bi64 > 5) { FmtErr("Invalid BAR index"); return 1; }
    UCHAR barIdx = (UCHAR)bi64;
    UCHAR cacheType = 1;
    if (argc >= 4) { ULONG64 ct; if (!ParseHex(argv[3], &ct) || ct > 2) { FmtErr("Invalid cache"); return 1; } cacheType = (UCHAR)ct; }

    /* Get BAR info first */
    LANDAUER_CMD_HEADER hdr; PCI_BAR_INFO_IN bi; PCI_BAR_INFO_OUT bo; ULONG ol;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_BAR;
    hdr.operation = LANDAUER_OP_GET_INFO;
    hdr.address = PCI_ADDR_ENCODE(0, bus, dev, func, 0);
    RtlZeroMemory(&bi, sizeof(bi)); bi.bar_index = barIdx;
    NTSTATUS st = LandauerCommand(hDevice, &hdr, &bi, sizeof(bi), &bo, sizeof(bo), &ol);
    if (!NT_SUCCESS(st) || bo.base == 0) { FmtErr("BAR%d not available", barIdx); return 3; }
    if (bo.type == 1) { FmtErr("BAR%d is IO port, not supported yet", barIdx); return 3; }

    /* Map */
    LANDAUER_CMD_HEADER mh; PCI_BAR_MAP_IN mi; PCI_BAR_MAP_OUT mo;
    RtlZeroMemory(&mh, sizeof(mh));
    mh.magic = LANDAUER_MAGIC; mh.version = LANDAUER_VERSION;
    mh.resource_type = LANDAUER_RESOURCE_PCI_BAR;
    mh.operation = LANDAUER_OP_MAP;
    RtlZeroMemory(&mi, sizeof(mi));
    mi.bar_base = bo.base; mi.bar_size = bo.size; mi.bar_index = barIdx; mi.cache_type = cacheType;

    st = LandauerCommand(hDevice, &mh, &mi, sizeof(mi), &mo, sizeof(mo), &ol);
    if (!NT_SUCCESS(st)) { FmtErr("Map failed: %s", FmtNtStatus(st)); return 3; }

    printf("BAR%d mapped: handle=%lu base=0x%llX size=0x%llX\n",
           barIdx, mo.map_handle, (unsigned long long)bo.base, (unsigned long long)bo.size);
    printf("Use: landauer pci bar read %lu <offset> [width]\n", mo.map_handle);
    return 0;
}

/* ============================================================
 *  pci bar read <handle> <offset> [width]  →  argv[0]="read", argv[1]=handle, argv[2]=off, argv[3]=w
 * ============================================================ */
int CmdPciBarRead(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 3) { FmtErr("Usage: pci bar read <handle> <offset> [width]"); return 1; }
    ULONG64 h64; if (!ParseHex(argv[1], &h64)) { FmtErr("Invalid handle: %ls", argv[1]); return 1; }
    ULONG handle = (ULONG)h64;
    ULONG64 off64; if (!ParseHex(argv[2], &off64)) { FmtErr("Invalid offset: %ls", argv[2]); return 1; }

    UCHAR width = 4;
    if (argc >= 4) { ULONG64 w; if (!ParseHex(argv[3], &w) || (w!=1&&w!=2&&w!=4&&w!=8)) { FmtErr("Invalid width"); return 1; } width = (UCHAR)w; }

    ULONG64 addr = ((ULONG64)handle << 32) | off64;
    ULONG64 val = 0;
    NTSTATUS st = LandauerRead(hDevice, LANDAUER_RESOURCE_PCI_BAR, width, addr, &val);
    if (!NT_SUCCESS(st)) { FmtErr("Read failed: %s", FmtNtStatus(st)); return 3; }

    if (width == 1) printf("0x%02llX\n", (unsigned long long)(val & 0xFF));
    else if (width == 2) printf("0x%04llX\n", (unsigned long long)(val & 0xFFFF));
    else if (width == 4) printf("0x%08llX\n", (unsigned long long)(val & 0xFFFFFFFF));
    else printf("0x%016llX\n", (unsigned long long)val);
    return 0;
}

/* ============================================================
 *  pci bar write <handle> <offset> <value> [width]
 * ============================================================ */
int CmdPciBarWrite(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 4) { FmtErr("Usage: pci bar write <handle> <offset> <value> [width]"); return 1; }
    ULONG64 h64; if (!ParseHex(argv[1], &h64)) { FmtErr("Invalid handle: %ls", argv[1]); return 1; }
    ULONG handle = (ULONG)h64;
    ULONG64 off64; if (!ParseHex(argv[2], &off64)) { FmtErr("Invalid offset: %ls", argv[2]); return 1; }
    ULONG64 value; if (!ParseHex(argv[3], &value)) { FmtErr("Invalid value: %ls", argv[3]); return 1; }
    UCHAR width = 4;
    if (argc >= 5) { ULONG64 w; if (!ParseHex(argv[4], &w)) { FmtErr("Invalid width"); return 1; } width = (UCHAR)w; }

    ULONG64 addr = ((ULONG64)handle << 32) | off64;
    NTSTATUS st = LandauerWrite(hDevice, LANDAUER_RESOURCE_PCI_BAR, width, addr, value);
    if (!NT_SUCCESS(st)) { FmtErr("Write failed: %s", FmtNtStatus(st)); return 3; }

    ULONG64 rb = 0;
    st = LandauerRead(hDevice, LANDAUER_RESOURCE_PCI_BAR, width, addr, &rb);
    if (NT_SUCCESS(st)) {
        if (width == 1) printf("0x%02llX\n", (unsigned long long)(rb & 0xFF));
        else if (width == 2) printf("0x%04llX\n", (unsigned long long)(rb & 0xFFFF));
        else if (width == 4) printf("0x%08llX\n", (unsigned long long)(rb & 0xFFFFFFFF));
        else printf("0x%016llX\n", (unsigned long long)rb);
    }
    return 0;
}

/* ============================================================
 *  pci bar dump <handle> <offset> <len> [file]
 * ============================================================ */
int CmdPciBarDump(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 4) { FmtErr("Usage: pci bar dump <handle> <offset> <len> [file]"); return 1; }
    ULONG64 h64; if (!ParseHex(argv[1], &h64)) { FmtErr("Invalid handle: %ls", argv[1]); return 1; }
    ULONG handle = (ULONG)h64;
    ULONG64 off64; if (!ParseHex(argv[2], &off64)) { FmtErr("Invalid offset: %ls", argv[2]); return 1; }
    ULONG64 len64; if (!ParseHex(argv[3], &len64) || len64 == 0) { FmtErr("Invalid length: %ls", argv[3]); return 1; }
    ULONG len = (ULONG)len64;
    if (len > 65536) { FmtErr("Length too big (max 65536)"); return 1; }

    UINT8* buf = (UINT8*)HeapAlloc(GetProcessHeap(), 0, len);
    if (!buf) { FmtErr("Out of memory"); return 3; }

    LANDAUER_CMD_HEADER hdr; ULONG outLen;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_BAR;
    hdr.operation = LANDAUER_OP_READ_BLOCK;
    hdr.address = ((ULONG64)handle << 32) | off64;
    hdr.data_length = len;

    NTSTATUS st = LandauerCommand(hDevice, &hdr, NULL, 0, buf, len, &outLen);
    if (!NT_SUCCESS(st)) { FmtErr("Dump failed: %s", FmtNtStatus(st)); HeapFree(GetProcessHeap(), 0, buf); return 3; }

    if (argc >= 5) {
        HANDLE fh = CreateFileW(argv[4], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fh == INVALID_HANDLE_VALUE) { FmtErr("Cannot create: %ls", argv[4]); HeapFree(GetProcessHeap(), 0, buf); return 3; }
        DWORD written; WriteFile(fh, buf, outLen, &written, NULL); CloseHandle(fh);
        printf("Wrote %lu bytes to %ls\n", outLen, argv[4]);
    } else {
        FmtHexDump(buf, outLen, off64);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return 0;
}

/* ============================================================
 *  pci bar unmap <handle>
 * ============================================================ */
int CmdPciBarUnmap(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 2) { FmtErr("Usage: pci bar unmap <handle>"); return 1; }
    ULONG64 h64; if (!ParseHex(argv[1], &h64)) { FmtErr("Invalid handle: %ls", argv[1]); return 1; }
    ULONG handle = (ULONG)h64;

    LANDAUER_CMD_HEADER hdr;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic = LANDAUER_MAGIC; hdr.version = LANDAUER_VERSION;
    hdr.resource_type = LANDAUER_RESOURCE_PCI_BAR;
    hdr.operation = LANDAUER_OP_UNMAP;
    hdr.address = handle;

    NTSTATUS st = LandauerCommand(hDevice, &hdr, NULL, 0, NULL, 0, NULL);
    if (!NT_SUCCESS(st)) { FmtErr("Unmap failed: %s", FmtNtStatus(st)); return 3; }
    printf("BAR handle %lu unmapped\n", handle);
    return 0;
}
