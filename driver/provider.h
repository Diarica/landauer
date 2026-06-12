/*
 * provider.h — Provider 接口 & 全局注册表 (Legacy NT 版)
 */

#pragma once

#include <ntddk.h>
#include "..\protocol.h"

/* ── Provider 处理函数签名 ── */
typedef NTSTATUS (*PROVIDER_HANDLER)(
    _Inout_ PLANDAUER_CMD_HEADER   header,
    _In_    PVOID                  input_payload,
    _In_    ULONG                  input_len,
    _Out_   PVOID                  output_payload,
    _Inout_ PULONG                 output_len);

/* ── Provider 接口 ── */
typedef struct _PROVIDER_INTERFACE {
    UCHAR               resource_type;
    const CHAR*         name;
    PROVIDER_HANDLER    handler;
} PROVIDER_INTERFACE;

typedef PROVIDER_INTERFACE* PPROVIDER_INTERFACE;

/* ── 驱动初始化 & 清理 ── */
NTSTATUS ProviderInit(VOID);
VOID     ProviderCleanup(VOID);

/* ── 查找 Provider ── */
PPROVIDER_INTERFACE ProviderLookup(UCHAR resource_type);

/* ── PCI Provider ── */
extern PROVIDER_INTERFACE g_PciCfgProvider;
extern PROVIDER_INTERFACE g_PciBarProvider;
