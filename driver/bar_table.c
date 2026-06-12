/*
 * bar_table.c — BAR 映射表实现 (Legacy NT: KSPIN_LOCK)
 */

#include "bar_table.h"

static BAR_MAPPING  g_BarTable[MAX_BAR_MAPPINGS];
static KSPIN_LOCK   g_BarLock;
static KIRQL        g_BarOldIrql;
static ULONG        g_NextHandle = 1;
static BOOLEAN      g_BarInitialized = FALSE;

NTSTATUS
BarTableInit(VOID)
{
    RtlZeroMemory(g_BarTable, sizeof(g_BarTable));
    KeInitializeSpinLock(&g_BarLock);
    g_BarInitialized = TRUE;
    g_NextHandle = 1;
    return STATUS_SUCCESS;
}

VOID
BarTableCleanup(VOID)
{
    if (!g_BarInitialized) return;

    KeAcquireSpinLock(&g_BarLock, &g_BarOldIrql);
    for (int i = 0; i < MAX_BAR_MAPPINGS; i++) {
        if (g_BarTable[i].in_use && g_BarTable[i].virt_addr != NULL) {
            MmUnmapIoSpace(g_BarTable[i].virt_addr,
                           (SIZE_T)g_BarTable[i].size);
            g_BarTable[i].in_use = FALSE;
            g_BarTable[i].virt_addr = NULL;
        }
    }
    KeReleaseSpinLock(&g_BarLock, g_BarOldIrql);
    g_BarInitialized = FALSE;
}

NTSTATUS
BarMap(
    _In_  PHYSICAL_ADDRESS PhysBase,
    _In_  ULONG64          Size,
    _In_  UCHAR            CacheType,
    _Out_ PULONG           Handle)
{
    NTSTATUS status = STATUS_SUCCESS;
    PVOID    va = NULL;
    ULONG    slot = (ULONG)-1;

    if (Size == 0 || Handle == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (CacheType) {
    case 0: va = MmMapIoSpace(PhysBase, (SIZE_T)Size, MmCached);         break;
    case 1: va = MmMapIoSpace(PhysBase, (SIZE_T)Size, MmNonCached);      break;
    case 2: va = MmMapIoSpace(PhysBase, (SIZE_T)Size, MmWriteCombined);  break;
    default: return STATUS_INVALID_PARAMETER;
    }

    if (va == NULL) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "Landauer: MmMapIoSpace(0x%llx, %llu) failed\n",
                   PhysBase.QuadPart, Size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeAcquireSpinLock(&g_BarLock, &g_BarOldIrql);
    for (int i = 0; i < MAX_BAR_MAPPINGS; i++) {
        if (!g_BarTable[i].in_use) {
            slot = i;
            g_BarTable[i].in_use = TRUE;
            break;
        }
    }

    if (slot == (ULONG)-1) {
        KeReleaseSpinLock(&g_BarLock, g_BarOldIrql);
        MmUnmapIoSpace(va, (SIZE_T)Size);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_BarTable[slot].phys_base = PhysBase;
    g_BarTable[slot].virt_addr = va;
    g_BarTable[slot].size      = Size;
    g_BarTable[slot].handle    = g_NextHandle;
    *Handle = g_NextHandle;
    g_NextHandle++;
    KeReleaseSpinLock(&g_BarLock, g_BarOldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
BarUnmap(_In_ ULONG Handle)
{
    BOOLEAN found = FALSE;

    KeAcquireSpinLock(&g_BarLock, &g_BarOldIrql);
    for (int i = 0; i < MAX_BAR_MAPPINGS; i++) {
        if (g_BarTable[i].in_use && g_BarTable[i].handle == Handle) {
            if (g_BarTable[i].virt_addr != NULL) {
                MmUnmapIoSpace(g_BarTable[i].virt_addr,
                               (SIZE_T)g_BarTable[i].size);
            }
            RtlZeroMemory(&g_BarTable[i], sizeof(BAR_MAPPING));
            found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&g_BarLock, g_BarOldIrql);

    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

PBAR_MAPPING
BarLookup(_In_ ULONG Handle)
{
    PBAR_MAPPING result = NULL;

    KeAcquireSpinLock(&g_BarLock, &g_BarOldIrql);
    for (int i = 0; i < MAX_BAR_MAPPINGS; i++) {
        if (g_BarTable[i].in_use && g_BarTable[i].handle == Handle) {
            result = &g_BarTable[i];
            break;
        }
    }
    KeReleaseSpinLock(&g_BarLock, g_BarOldIrql);

    return result;
}
