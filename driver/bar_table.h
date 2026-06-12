/*
 * bar_table.h — BAR 映射表 (Legacy NT 版)
 */

#pragma once

#include <ntddk.h>

typedef struct _BAR_MAPPING {
    ULONG              handle;
    PHYSICAL_ADDRESS   phys_base;
    PVOID              virt_addr;
    ULONG64            size;
    BOOLEAN            in_use;
} BAR_MAPPING;

typedef BAR_MAPPING* PBAR_MAPPING;

#define MAX_BAR_MAPPINGS  16

NTSTATUS BarTableInit(VOID);
VOID     BarTableCleanup(VOID);

NTSTATUS BarMap(
    _In_  PHYSICAL_ADDRESS PhysBase,
    _In_  ULONG64          Size,
    _In_  UCHAR            CacheType,
    _Out_ PULONG           Handle);

NTSTATUS BarUnmap(_In_ ULONG Handle);

PBAR_MAPPING BarLookup(_In_ ULONG Handle);
