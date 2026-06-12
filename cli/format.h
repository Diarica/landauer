/*
 * format.h — 输出格式化
 */

#pragma once

#include <windows.h>
#include "..\protocol.h"

/* ── 错误输出 ── */
void FmtErr(const char* fmt, ...);

/* ── 标准输出 ── */
void FmtOut(const char* fmt, ...);

/* ── Hex Dump ── */
void FmtHexDump(const UINT8* data, ULONG len, UINT64 baseAddr);

/* ── NTSTATUS → 可读字符串 ── */
const char* FmtNtStatus(NTSTATUS status);

/* ── 格式化 BDF 字符串 (复用 protocol.h 的 pci_bdf_str) ── */

/* ── 常见格式化 ── */
void FmtPciDeviceShort(UINT8 bus, UINT8 dev, UINT8 func,
                       UINT16 vendor, UINT16 device_id,
                       UINT8 baseClass, UINT8 subClass,
                       UINT8 headerType);

void FmtPciDeviceFull(PCI_CFG_INFO* info, UINT8 bus, UINT8 dev, UINT8 func);

void FmtPciCap(PVOID capOut, const char* capType);

void FmtBarInfo(PCI_BAR_INFO_OUT* info, UINT8 bus, UINT8 dev, UINT8 func);
