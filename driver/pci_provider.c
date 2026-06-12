/*
 * pci_provider.c — PCI 配置空间 & BAR Provider
 *
 * 支持操作:
 *   PCI_CFG:  GET_INFO, READ, WRITE, READ_BLOCK, WRITE_BLOCK,
 *             ENUMERATE, FIND_CAP, FIND_EXT_CAP
 *   PCI_BAR:  GET_INFO, MAP, UNMAP, READ, WRITE, READ_BLOCK, WRITE_BLOCK
 *
 * PCI 配置空间访问: HalGetBusDataByOffset / HalSetBusDataByOffset
 * BAR MMIO 访问:    通过 BarTable 里的 MmMapIoSpace 映射
 */

#include "provider.h"
#include "bar_table.h"

/* ── 前向声明 ── */
static NTSTATUS PciCfgHandler(
    _Inout_ PLANDAUER_CMD_HEADER header,
    _In_    PVOID    input_payload,
    _In_    ULONG    input_len,
    _Out_   PVOID    output_payload,
    _Inout_ PULONG   output_len);

static NTSTATUS PciBarHandler(
    _Inout_ PLANDAUER_CMD_HEADER header,
    _In_    PVOID    input_payload,
    _In_    ULONG    input_len,
    _Out_   PVOID    output_payload,
    _Inout_ PULONG   output_len);

/* ── Provider 实例 ── */
PROVIDER_INTERFACE g_PciCfgProvider = {
    LANDAUER_RESOURCE_PCI_CFG,
    "PCI_CFG",
    PciCfgHandler
};

PROVIDER_INTERFACE g_PciBarProvider = {
    LANDAUER_RESOURCE_PCI_BAR,
    "PCI_BAR",
    PciBarHandler
};

/* ============================================================
 *  PCI 配置空间 辅助函数
 * ============================================================ */

/*
 * ReadCfg: 读指定 BDF 的配置空间
 * offset: 配置空间内偏移 (0-4095)
 * len:    字节数 (1/2/4)
 */
static NTSTATUS
ReadCfg(
    UCHAR  bus,
    UCHAR  device,
    UCHAR  function,
    USHORT offset,
    UCHAR  len,
    _Out_ PULONG value)
{
    ULONG slot = (device << 3) | function;
    ULONG bytes;

    *value = 0;  /* zero before read to avoid garbage in upper bits */
    bytes = HalGetBusDataByOffset(
        PCIConfiguration,
        bus,
        slot,
        value,
        offset,
        len);

    if (bytes != len) {
        *value = 0xFFFFFFFF;
        return STATUS_DEVICE_DOES_NOT_EXIST;  /* 设备不存在 */
    }
    return STATUS_SUCCESS;
}

/*
 * WriteCfg: 写指定 BDF 的配置空间
 */
static NTSTATUS
WriteCfg(
    UCHAR  bus,
    UCHAR  device,
    UCHAR  function,
    USHORT offset,
    UCHAR  len,
    ULONG  value)
{
    ULONG slot = (device << 3) | function;
    ULONG bytes;

    bytes = HalSetBusDataByOffset(
        PCIConfiguration,
        bus,
        slot,
        &value,
        offset,
        len);

    if (bytes != len) {
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }
    return STATUS_SUCCESS;
}

/*
 * CheckDevice: 快速检查设备是否存在 (读 vendor_id)
 */
static BOOLEAN
DeviceExists(UCHAR bus, UCHAR device, UCHAR function)
{
    ULONG vendor;
    NTSTATUS s = ReadCfg(bus, device, function, 0, 2, &vendor);
    return NT_SUCCESS(s) && vendor != 0xFFFF && vendor != 0x0000;
}

/* ============================================================
 *  PCI_CFG Handler
 * ============================================================ */

static NTSTATUS
PciCfgHandler(
    _Inout_ PLANDAUER_CMD_HEADER header,
    _In_    PVOID    input_payload,
    _In_    ULONG    input_len,
    _Out_   PVOID    output_payload,
    _Inout_ PULONG   output_len)
{
    UCHAR  bus, dev, func;
    USHORT offset;
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(input_len);

    /* 解码 PCI 地址 */
    bus    = PCI_ADDR_BUS(header->address);
    dev    = PCI_ADDR_DEVICE(header->address);
    func   = PCI_ADDR_FUNCTION(header->address);
    offset = PCI_ADDR_OFFSET(header->address);

    switch (header->operation) {

    /* ── GET_INFO ── */
    case LANDAUER_OP_GET_INFO:
        {
            PCI_CFG_INFO info;
            ULONG val;
            UCHAR hdr_type;
            UCHAR num_bars;
            UCHAR bar_count;

            if (*output_len < sizeof(info)) {
                *output_len = 0;
                return STATUS_BUFFER_TOO_SMALL;
            }

            RtlZeroMemory(&info, sizeof(info));

            /* 读标准 CFG 头 */
            ReadCfg(bus, dev, func, 0x00, 2, &val);
            info.vendor_id = (USHORT)val;
            ReadCfg(bus, dev, func, 0x02, 2, &val);
            info.device_id = (USHORT)val;
            ReadCfg(bus, dev, func, 0x04, 2, &val);
            info.command = (USHORT)val;
            ReadCfg(bus, dev, func, 0x06, 2, &val);
            info.status_reg = (USHORT)val;
            ReadCfg(bus, dev, func, 0x08, 1, &val);
            info.revision_id = (UCHAR)val;
            ReadCfg(bus, dev, func, 0x09, 1, &val);
            info.prog_if = (UCHAR)val;
            ReadCfg(bus, dev, func, 0x0A, 1, &val);
            info.sub_class = (UCHAR)val;
            ReadCfg(bus, dev, func, 0x0B, 1, &val);
            info.base_class = (UCHAR)val;
            ReadCfg(bus, dev, func, 0x0C, 1, &val);
            info.cache_line_size = (UCHAR)val;
            ReadCfg(bus, dev, func, 0x0D, 1, &val);
            info.latency_timer = (UCHAR)val;
            ReadCfg(bus, dev, func, 0x0E, 1, &val);
            hdr_type = (UCHAR)val;
            info.header_type = hdr_type;
            ReadCfg(bus, dev, func, 0x0F, 1, &val);
            info.bist = (UCHAR)val;

            /* 确定 BAR 数量 */
            if ((hdr_type & 0x7F) == 0x00) {
                num_bars = 6;  /* Type 0: 通用设备 */
            } else if ((hdr_type & 0x7F) == 0x01) {
                num_bars = 2;  /* Type 1: PCI-PCI 桥 */
            } else {
                num_bars = 0;
            }

            info.num_bars = num_bars;

            /* 读 BAR */
            for (bar_count = 0; bar_count < num_bars; bar_count++) {
                USHORT bar_offset = 0x10 + (bar_count * 4);
                ULONG bar_val, bar_val_high = 0;
                ULONG64 bar_base, bar_size;
                BOOLEAN is_64bit = FALSE;
                BOOLEAN is_io;

                ReadCfg(bus, dev, func, bar_offset, 4, &bar_val);

                if (bar_val == 0) continue;

                is_io = (bar_val & 0x1) ? TRUE : FALSE;

                if (is_io) {
                    bar_base = bar_val & 0xFFFFFFFCULL;
                    /* IO BAR size probe */
                    ULONG probe_val;
                    WriteCfg(bus, dev, func, bar_offset, 4, 0xFFFFFFFF);
                    ReadCfg(bus, dev, func, bar_offset, 4, &probe_val);
                    WriteCfg(bus, dev, func, bar_offset, 4, bar_val);
                    /* check if write took effect */
                    if ((probe_val & 0xFFFFFFFC) == (bar_val & 0xFFFFFFFC)) {
                        bar_size = 0;  /* probe failed, size unknown */
                    } else {
                        bar_size = (~(probe_val & 0xFFFFFFFCULL) + 1) & 0xFFFFFFFF;
                    }
                } else {
                    UCHAR mem_type = (bar_val >> 1) & 0x3;
                    if (mem_type == 0x0) {
                        /* 32-bit memory BAR */
                        bar_base = bar_val & 0xFFFFFFF0ULL;
                    } else if (mem_type == 0x2) {
                        /* 64-bit memory BAR */
                        is_64bit = TRUE;
                        ReadCfg(bus, dev, func, bar_offset + 4, 4, &bar_val_high);
                        bar_base = (bar_val & 0xFFFFFFF0ULL) |
                                   ((ULONG64)bar_val_high << 32);
                    } else {
                        continue;
                    }

                    /* size probe */
                    {
                        ULONG probe_val, verify_val;
                        WriteCfg(bus, dev, func, bar_offset, 4, 0xFFFFFFFF);
                        ReadCfg(bus, dev, func, bar_offset, 4, &probe_val);
                        WriteCfg(bus, dev, func, bar_offset, 4, bar_val);
                        ReadCfg(bus, dev, func, bar_offset, 4, &verify_val);
                        /* if probe didn't change the BAR value, size is unknown */
                        if ((probe_val & 0xFFFFFFF0) == (verify_val & 0xFFFFFFF0)) {
                            bar_size = 0;
                        } else {
                            bar_size = (~(probe_val & 0xFFFFFFF0ULL) + 1) & 0xFFFFFFFF;
                        }
                    }
                    if (is_64bit && bar_size != 0) {
                        ULONG probe_high;
                        WriteCfg(bus, dev, func, bar_offset + 4, 4, 0xFFFFFFFF);
                        ReadCfg(bus, dev, func, bar_offset + 4, 4, &probe_high);
                        WriteCfg(bus, dev, func, bar_offset + 4, 4, bar_val_high);
                        bar_size |= ((ULONG64)(~(probe_high) + 1)) << 32;
                    }
                }

                info.bars[bar_count].index        = bar_count;
                info.bars[bar_count].type          = is_io ? 1 : 0;
                info.bars[bar_count].bits          = is_64bit ? 64 : 32;
                info.bars[bar_count].prefetchable  =
                    (bar_val >> 3) & 0x1;
                info.bars[bar_count].base          = bar_base;
                info.bars[bar_count].size          = bar_size;

                if (is_64bit) bar_count++;  /* skip next BAR slot */
            }

            RtlCopyMemory(output_payload, &info, sizeof(info));
            *output_len = sizeof(info);
        }
        break;

    /* ── READ ── */
    case LANDAUER_OP_READ:
        {
            ULONG val;

            if (header->access_width == 0 ||
                header->access_width > 8 ||
                header->access_width & (header->access_width - 1)) {
                return STATUS_INVALID_PARAMETER;
            }
            if (*output_len < header->access_width) {
                *output_len = 0;
                return STATUS_BUFFER_TOO_SMALL;
            }

            status = ReadCfg(bus, dev, func, offset,
                             header->access_width, &val);
            if (!NT_SUCCESS(status)) return status;

            RtlCopyMemory(output_payload, &val, header->access_width);
            *output_len = header->access_width;
        }
        break;

    /* ── WRITE ── */
    case LANDAUER_OP_WRITE:
        {
            ULONG val;

            if (header->access_width == 0 ||
                header->access_width > 8 ||
                header->access_width & (header->access_width - 1)) {
                return STATUS_INVALID_PARAMETER;
            }
            if (input_len < header->access_width) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            val = 0;
            RtlCopyMemory(&val, input_payload, header->access_width);

            status = WriteCfg(bus, dev, func, offset,
                              header->access_width, val);
            *output_len = 0;
        }
        break;

    /* ── READ_BLOCK ── */
    case LANDAUER_OP_READ_BLOCK:
        {
            ULONG bytes_to_read = header->data_length;
            ULONG offset_in_block = 0;

            if (bytes_to_read > *output_len) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            /* 4 字节对齐分块读取 */
            while (offset_in_block < bytes_to_read) {
                ULONG chunk = 4;
                ULONG val;

                if (offset_in_block + chunk > bytes_to_read) {
                    chunk = bytes_to_read - offset_in_block;
                }
                if (chunk < 4) {
                    /* 不完整块: 读 4 字节再截断 */
                    ReadCfg(bus, dev, func,
                            offset + (USHORT)offset_in_block, 4, &val);
                    RtlCopyMemory((PUINT8)output_payload + offset_in_block,
                                  &val, chunk);
                } else {
                    ReadCfg(bus, dev, func,
                            offset + (USHORT)offset_in_block, 4, &val);
                    RtlCopyMemory((PUINT8)output_payload + offset_in_block,
                                  &val, 4);
                }
                offset_in_block += chunk;
            }

            *output_len = bytes_to_read;
        }
        break;

    /* ── WRITE_BLOCK ── */
    case LANDAUER_OP_WRITE_BLOCK:
        {
            ULONG bytes_to_write = header->data_length;
            ULONG offset_in_block = 0;

            if (bytes_to_write > input_len) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            /* 4 字节对齐分块写入 */
            while (offset_in_block < bytes_to_write) {
                ULONG chunk = 4;
                ULONG val = 0;

                if (offset_in_block + chunk > bytes_to_write) {
                    chunk = bytes_to_write - offset_in_block;
                }
                /* 读-改-写 (只对不完整的 4 字节块) */
                if (chunk < 4) {
                    ReadCfg(bus, dev, func,
                            offset + (USHORT)offset_in_block, 4, &val);
                }
                RtlCopyMemory(&val,
                    (PUINT8)input_payload + offset_in_block, chunk);
                WriteCfg(bus, dev, func,
                         offset + (USHORT)offset_in_block, 4, val);
                offset_in_block += chunk;
            }

            *output_len = 0;
        }
        break;

    /* ── ENUMERATE ── */
    case LANDAUER_OP_PCI_ENUMERATE:
        {
            PCI_ENUM_OUT* out;
            PCI_ENUM_DEVICE* devList;
            USHORT max_devices;
            USHORT start_bus;
            USHORT count = 0;
            USHORT b, d, f;

            if (input_len < sizeof(PCI_ENUM_IN)) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            PCI_ENUM_IN* in = (PCI_ENUM_IN*)input_payload;
            start_bus   = in->start_bus;
            max_devices = in->max_devices;
            if (max_devices == 0) max_devices = 64;

            if (*output_len < sizeof(PCI_ENUM_OUT) +
                              max_devices * sizeof(PCI_ENUM_DEVICE)) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            out     = (PCI_ENUM_OUT*)output_payload;
            devList = (PCI_ENUM_DEVICE*)(out + 1);

            RtlZeroMemory(out, sizeof(PCI_ENUM_OUT));
            out->count = 0;
            out->truncated = 0;

            for (b = start_bus; b < 256; b++) {
                for (d = 0; d < 32; d++) {
                    UCHAR max_func = 1;

                    for (f = 0; f < max_func; f++) {
                        if (!DeviceExists((UCHAR)b, (UCHAR)d, (UCHAR)f)) {
                            continue;
                        }

                        if (count >= max_devices) {
                            out->truncated = 1;
                            goto enum_done;
                        }

                        /* 读设备信息 */
                        ULONG val;
                        UCHAR cls[3], hdr;

                        ReadCfg((UCHAR)b, (UCHAR)d, (UCHAR)f, 0x00, 2, &val);
                        devList[count].vendor_id = (USHORT)val;
                        ReadCfg((UCHAR)b, (UCHAR)d, (UCHAR)f, 0x02, 2, &val);
                        devList[count].device_id = (USHORT)val;
                        ReadCfg((UCHAR)b, (UCHAR)d, (UCHAR)f, 0x09, 1, &val);
                        cls[0] = (UCHAR)val;  /* prog-if */
                        ReadCfg((UCHAR)b, (UCHAR)d, (UCHAR)f, 0x0A, 1, &val);
                        cls[1] = (UCHAR)val;  /* sub-class */
                        ReadCfg((UCHAR)b, (UCHAR)d, (UCHAR)f, 0x0B, 1, &val);
                        cls[2] = (UCHAR)val;  /* base-class */
                        ReadCfg((UCHAR)b, (UCHAR)d, (UCHAR)f, 0x0E, 1, &val);
                        hdr = (UCHAR)val;

                        devList[count].bus         = (UCHAR)b;
                        devList[count].device      = (UCHAR)d;
                        devList[count].function    = (UCHAR)f;
                        devList[count].header_type = hdr;
                        devList[count].base_class  = cls[2];
                        devList[count].sub_class   = cls[1];
                        devList[count].prog_if     = cls[0];

                        count++;

                        /* 多功能设备? */
                        if (f == 0 && (hdr & 0x80)) {
                            max_func = 8;
                        }
                    }
                }
            }

enum_done:
            out->count = count;
            *output_len = sizeof(PCI_ENUM_OUT) +
                          count * sizeof(PCI_ENUM_DEVICE);
        }
        break;

    /* ── FIND_CAP ── */
    case LANDAUER_OP_PCI_FIND_CAP:
        {
            PCI_CAP_FIND_IN*  in;
            PCI_CAP_FIND_OUT* out;
            UCHAR cap_offset, cap_id;
            ULONG val;
            int count = 0;

            if (input_len < sizeof(PCI_CAP_FIND_IN) ||
                *output_len < sizeof(PCI_CAP_FIND_OUT)) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            in  = (PCI_CAP_FIND_IN*)input_payload;
            out = (PCI_CAP_FIND_OUT*)output_payload;

            /* CAPTURE INPUT FIRST — buffer shared for in/out! */
            UCHAR req_cap_id = in->cap_id;
            UCHAR req_start  = in->start_offset;

            RtlZeroMemory(out, sizeof(*out));

            cap_offset = req_start;

            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                "Landauer: FIND_CAP BDF=%02X:%02X.%X cap_id=%02X start_off=%02X\n",
                bus, dev, func, req_cap_id, cap_offset);

            if (cap_offset == 0) {
                ReadCfg(bus, dev, func, 0x34, 1, &val);
                cap_offset = (UCHAR)val;
            }
            if (cap_offset == 0 || cap_offset >= 0x100) {
                *output_len = sizeof(*out);
                break;
            }

            /* walk capability list */
            while (count < 48) {
                ReadCfg(bus, dev, func, cap_offset, 4, &val);
                cap_id = (UCHAR)(val & 0xFF);

                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                    "Landauer: FIND_CAP walking off=%02X cap_id=%02X val=%08X\n",
                    cap_offset, cap_id, val);

                if (req_cap_id == 0 || cap_id == req_cap_id) {
                    out->found      = 1;
                    out->cap_id     = cap_id;
                    out->cap_offset = cap_offset;
                    out->cap_value  = val;
                    *output_len = sizeof(*out);
                    return STATUS_SUCCESS;
                }

                UCHAR next = (UCHAR)((val >> 8) & 0xFF);
                if (next == 0 || next <= cap_offset) break;
                cap_offset = next;
                count++;
            }

            *output_len = sizeof(*out);
        }
        break;

    /* ── FIND_EXT_CAP ── */
    case LANDAUER_OP_PCI_FIND_EXT_CAP:
        {
            PCI_CAP_FIND_IN*  in;
            PCI_CAP_FIND_OUT* out;
            USHORT ext_offset;
            ULONG val;
            int count = 0;

            if (input_len < sizeof(PCI_CAP_FIND_IN) ||
                *output_len < sizeof(PCI_CAP_FIND_OUT)) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            in  = (PCI_CAP_FIND_IN*)input_payload;
            out = (PCI_CAP_FIND_OUT*)output_payload;

            /* CAPTURE INPUT FIRST */
            UCHAR ext_req_cap_id = in->cap_id;

            RtlZeroMemory(out, sizeof(*out));

            /* ext cap offset from address field (12 bits) */
            ext_offset = (USHORT)(header->address & 0xFFF);
            if (ext_offset < 0x100) ext_offset = 0x100;

            /* 遍历 Extended Capability 链表 */
            while (ext_offset >= 0x100 && ext_offset < 0x1000 && count < 48) {
                ReadCfg(bus, dev, func, ext_offset, 4, &val);
                USHORT cap_id = (USHORT)(val & 0xFFFF);

                if (cap_id == 0x0000 || cap_id == 0xFFFF) {
                    break;  /* 链表结束 */
                }

                if (ext_req_cap_id == 0 || cap_id == ext_req_cap_id) {
                    out->found      = 1;
                    out->cap_id     = (UCHAR)(cap_id & 0xFF);
                    out->cap_offset = (UCHAR)(ext_offset & 0xFF);
                    out->cap_value  = val;
                    *output_len = sizeof(*out);
                    return STATUS_SUCCESS;
                }

                USHORT next = (USHORT)((val >> 20) & 0xFFF);
                if (next == 0 || next <= ext_offset) break;
                ext_offset = next;
                count++;
            }

            *output_len = sizeof(*out);
        }
        break;

    default:
        return STATUS_NOT_IMPLEMENTED;
    }

    return status;
}

/* ============================================================
 *  PCI_BAR Handler
 * ============================================================ */

static NTSTATUS
PciBarHandler(
    _Inout_ PLANDAUER_CMD_HEADER header,
    _In_    PVOID    input_payload,
    _In_    ULONG    input_len,
    _Out_   PVOID    output_payload,
    _Inout_ PULONG   output_len)
{
    UNREFERENCED_PARAMETER(input_len);

    switch (header->operation) {

    /* ── GET_INFO ── */
    case LANDAUER_OP_GET_INFO:
        {
            PCI_BAR_INFO_IN*  in;
            PCI_BAR_INFO_OUT* out;
            UCHAR  bus, dev, func, bar_idx;
            ULONG  bar_val, bar_val_high = 0, bar_val_orig;
            ULONG64 bar_base = 0, bar_size = 0;
            BOOLEAN is_64bit = FALSE, is_io;
            USHORT bar_offset;

            if (input_len < sizeof(PCI_BAR_INFO_IN) ||
                *output_len < sizeof(PCI_BAR_INFO_OUT)) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            in  = (PCI_BAR_INFO_IN*)input_payload;
            out = (PCI_BAR_INFO_OUT*)output_payload;
            RtlZeroMemory(out, sizeof(*out));

            /* 解码 BDF (address 字段复用 PCI 编码) */
            bus    = PCI_ADDR_BUS(header->address);
            dev    = PCI_ADDR_DEVICE(header->address);
            func   = PCI_ADDR_FUNCTION(header->address);
            bar_idx = in->bar_index;

            if (bar_idx > 5) return STATUS_INVALID_PARAMETER;

            bar_offset = 0x10 + (bar_idx * 4);
            ReadCfg(bus, dev, func, bar_offset, 4, &bar_val);
            bar_val_orig = bar_val;

            if (bar_val == 0) {
                *output_len = sizeof(*out);
                return STATUS_SUCCESS;
            }

            is_io = (bar_val & 0x1) ? TRUE : FALSE;
            out->type = is_io ? 1 : 0;

            if (is_io) {
                bar_base = bar_val & 0xFFFFFFFCULL;
                out->bits = 32;
                /* IO BAR size probe */
                {
                    ULONG probe_val, verify_val;
                    WriteCfg(bus, dev, func, bar_offset, 4, 0xFFFFFFFF);
                    ReadCfg(bus, dev, func, bar_offset, 4, &probe_val);
                    WriteCfg(bus, dev, func, bar_offset, 4, bar_val_orig);
                    ReadCfg(bus, dev, func, bar_offset, 4, &verify_val);
                    if ((probe_val & 0xFFFFFFFC) == (verify_val & 0xFFFFFFFC))
                        bar_size = 0;
                    else
                        bar_size = (~(probe_val & 0xFFFFFFFCULL) + 1) & 0xFFFFFFFF;
                }
            } else {
                UCHAR mem_type = (bar_val >> 1) & 0x3;
                out->prefetchable = (bar_val >> 3) & 0x1;

                if (mem_type == 0x0) {
                    bar_base = bar_val & 0xFFFFFFF0ULL;
                    out->bits = 32;
                } else if (mem_type == 0x2) {
                    is_64bit = TRUE;
                    out->bits = 64;
                    ReadCfg(bus, dev, func, bar_offset + 4, 4,
                            &bar_val_high);
                    bar_base = (bar_val & 0xFFFFFFF0ULL) |
                               ((ULONG64)bar_val_high << 32);
                } else {
                    *output_len = sizeof(*out);
                    return STATUS_SUCCESS;
                }

                /* size probe — check if write actually took effect */
                {
                    ULONG probe_val, verify_val;
                    WriteCfg(bus, dev, func, bar_offset, 4, 0xFFFFFFFF);
                    ReadCfg(bus, dev, func, bar_offset, 4, &probe_val);
                    WriteCfg(bus, dev, func, bar_offset, 4, bar_val_orig);
                    ReadCfg(bus, dev, func, bar_offset, 4, &verify_val);
                    if ((probe_val & 0xFFFFFFF0) == (verify_val & 0xFFFFFFF0)) {
                        bar_size = 0;
                    } else {
                        bar_size = (~(probe_val & 0xFFFFFFF0ULL) + 1) & 0xFFFFFFFF;
                    }
                }
                if (is_64bit && bar_size != 0) {
                    ULONG probe_high;
                    WriteCfg(bus, dev, func, bar_offset + 4, 4, 0xFFFFFFFF);
                    ReadCfg(bus, dev, func, bar_offset + 4, 4, &probe_high);
                    WriteCfg(bus, dev, func, bar_offset + 4, 4, bar_val_high);
                    bar_size |= ((ULONG64)(~(probe_high) + 1)) << 32;
                }
            }

            out->bar_index = bar_idx;
            out->base      = bar_base;
            out->size      = bar_size;
            out->mapped    = 0;  /* CLI 需要主动 MAP */

            *output_len = sizeof(*out);
        }
        break;

    /* ── MAP ── */
    case LANDAUER_OP_MAP:
        {
            PCI_BAR_MAP_IN*  in;
            PCI_BAR_MAP_OUT* out;
            PHYSICAL_ADDRESS phys;
            NTSTATUS         st;
            ULONG            handle;

            if (input_len < sizeof(PCI_BAR_MAP_IN) ||
                *output_len < sizeof(PCI_BAR_MAP_OUT)) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            in  = (PCI_BAR_MAP_IN*)input_payload;
            out = (PCI_BAR_MAP_OUT*)output_payload;
            RtlZeroMemory(out, sizeof(*out));

            phys.QuadPart = in->bar_base;

            /* if size unknown (0), map at least one page */
            ULONG64 map_size = in->bar_size;
            if (map_size == 0) map_size = 0x1000;  /* 4KB minimum */

            st = BarMap(phys, map_size, in->cache_type, &handle);
            if (!NT_SUCCESS(st)) return st;

            /* 获取映射信息 */
            PBAR_MAPPING mapping = BarLookup(handle);
            if (mapping != NULL) {
                out->mapped_va  = (ULONG64)(ULONG_PTR)mapping->virt_addr;
                out->map_handle = handle;
            }

            *output_len = sizeof(*out);
        }
        break;

    /* ── UNMAP ── */
    case LANDAUER_OP_UNMAP:
        {
            ULONG handle;

            /* handle 存在 address 低 32 位 */
            handle = (ULONG)(header->address & 0xFFFFFFFF);
            if (handle == 0) {
                return STATUS_INVALID_PARAMETER;
            }

            BarUnmap(handle);
            *output_len = 0;
        }
        break;

    /* ── READ ── */
    case LANDAUER_OP_READ:
        {
            ULONG64 addr = header->address;
            ULONG64 offset = addr & 0xFFFFFFFFULL;
            ULONG   handle = (ULONG)((addr >> 32) & 0xFFFFFFFF);
            PBAR_MAPPING mapping;

            if (handle == 0 || header->access_width == 0 ||
                header->access_width > 8 ||
                header->access_width & (header->access_width - 1)) {
                return STATUS_INVALID_PARAMETER;
            }
            if (*output_len < header->access_width) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            mapping = BarLookup(handle);
            if (mapping == NULL) return STATUS_NOT_FOUND;

            if (offset + header->access_width > mapping->size) {
                return STATUS_INVALID_PARAMETER;
            }

            PUINT8 src = (PUINT8)mapping->virt_addr + offset;
            RtlCopyMemory(output_payload, src, header->access_width);
            *output_len = header->access_width;
        }
        break;

    /* ── WRITE ── */
    case LANDAUER_OP_WRITE:
        {
            ULONG64 addr = header->address;
            ULONG64 offset = addr & 0xFFFFFFFFULL;
            ULONG   handle = (ULONG)((addr >> 32) & 0xFFFFFFFF);
            PBAR_MAPPING mapping;

            if (handle == 0 || header->access_width == 0 ||
                header->access_width > 8 ||
                header->access_width & (header->access_width - 1)) {
                return STATUS_INVALID_PARAMETER;
            }
            if (input_len < header->access_width) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            mapping = BarLookup(handle);
            if (mapping == NULL) return STATUS_NOT_FOUND;

            if (offset + header->access_width > mapping->size) {
                return STATUS_INVALID_PARAMETER;
            }

            PUINT8 dst = (PUINT8)mapping->virt_addr + offset;
            RtlCopyMemory(dst, input_payload, header->access_width);
            *output_len = 0;
        }
        break;

    /* ── READ_BLOCK ── */
    case LANDAUER_OP_READ_BLOCK:
        {
            ULONG64 addr   = header->address;
            ULONG64 offset = addr & 0xFFFFFFFFULL;
            ULONG   handle = (ULONG)((addr >> 32) & 0xFFFFFFFF);
            ULONG   len    = header->data_length;
            PBAR_MAPPING mapping;

            if (handle == 0 || len == 0) {
                return STATUS_INVALID_PARAMETER;
            }
            if (*output_len < len) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            mapping = BarLookup(handle);
            if (mapping == NULL) return STATUS_NOT_FOUND;

            if (offset + len > mapping->size) {
                return STATUS_INVALID_PARAMETER;
            }

            RtlCopyMemory(output_payload,
                          (PUINT8)mapping->virt_addr + offset, len);
            *output_len = len;
        }
        break;

    /* ── WRITE_BLOCK ── */
    case LANDAUER_OP_WRITE_BLOCK:
        {
            ULONG64 addr   = header->address;
            ULONG64 offset = addr & 0xFFFFFFFFULL;
            ULONG   handle = (ULONG)((addr >> 32) & 0xFFFFFFFF);
            ULONG   len    = header->data_length;
            PBAR_MAPPING mapping;

            if (handle == 0 || len == 0) {
                return STATUS_INVALID_PARAMETER;
            }
            if (input_len < len) {
                return STATUS_BUFFER_TOO_SMALL;
            }

            mapping = BarLookup(handle);
            if (mapping == NULL) return STATUS_NOT_FOUND;

            if (offset + len > mapping->size) {
                return STATUS_INVALID_PARAMETER;
            }

            RtlCopyMemory((PUINT8)mapping->virt_addr + offset,
                          input_payload, len);
            *output_len = 0;
        }
        break;

    default:
        return STATUS_NOT_IMPLEMENTED;
    }

    return STATUS_SUCCESS;
}
