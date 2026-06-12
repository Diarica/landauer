/*
 * dispatch.c — 命令分派器 (Legacy NT 版)
 */

#include "provider.h"
#include "bar_table.h"

#define MAX_PROVIDERS 8

static PPROVIDER_INTERFACE g_Providers[MAX_PROVIDERS];
static ULONG               g_ProviderCount = 0;

static NTSTATUS
RegisterProvider(_In_ PPROVIDER_INTERFACE prov)
{
    if (g_ProviderCount >= MAX_PROVIDERS)
        return STATUS_INSUFFICIENT_RESOURCES;
    g_Providers[g_ProviderCount++] = prov;
    return STATUS_SUCCESS;
}

NTSTATUS
ProviderInit(VOID)
{
    NTSTATUS status;

    RtlZeroMemory(g_Providers, sizeof(g_Providers));
    g_ProviderCount = 0;

    status = BarTableInit();
    if (!NT_SUCCESS(status)) return status;

    status = RegisterProvider(&g_PciCfgProvider);
    if (!NT_SUCCESS(status)) return status;

    status = RegisterProvider(&g_PciBarProvider);
    if (!NT_SUCCESS(status)) return status;

    return STATUS_SUCCESS;
}

VOID
ProviderCleanup(VOID)
{
    BarTableCleanup();
    RtlZeroMemory(g_Providers, sizeof(g_Providers));
    g_ProviderCount = 0;
}

PPROVIDER_INTERFACE
ProviderLookup(UCHAR resource_type)
{
    for (ULONG i = 0; i < g_ProviderCount; i++) {
        if (g_Providers[i]->resource_type == resource_type)
            return g_Providers[i];
    }
    return NULL;
}

NTSTATUS
DispatchIoCommand(
    _Inout_ PVOID   Buffer,
    _In_    SIZE_T  InputBufferLength,
    _In_    SIZE_T  OutputBufferLength,
    _Out_   PSIZE_T BytesReturned)
{
    PLANDAUER_CMD_HEADER header;
    PPROVIDER_INTERFACE  provider;
    PVOID                input_payload;
    PVOID                output_payload;
    ULONG                input_len, output_len;
    NTSTATUS             status;

    if (Buffer == NULL || InputBufferLength < LANDAUER_HEADER_SIZE) {
        *BytesReturned = 0;
        return STATUS_INVALID_PARAMETER;
    }

    header = (PLANDAUER_CMD_HEADER)Buffer;

    if (header->magic != LANDAUER_MAGIC) {
        header->status      = STATUS_INVALID_PARAMETER;
        header->data_length = 0;
        header->version     = LANDAUER_VERSION;
        *BytesReturned      = LANDAUER_HEADER_SIZE;
        return STATUS_SUCCESS;
    }

    provider = ProviderLookup(header->resource_type);
    if (provider == NULL) {
        header->status      = STATUS_NOT_IMPLEMENTED;
        header->data_length = 0;
        header->version     = LANDAUER_VERSION;
        *BytesReturned      = LANDAUER_HEADER_SIZE;
        return STATUS_SUCCESS;
    }

    input_payload  = (PUINT8)Buffer + LANDAUER_HEADER_SIZE;
    output_payload = (PUINT8)Buffer + LANDAUER_HEADER_SIZE;
    input_len      = header->data_length;

    /* Check input buffer only for operations that have input payload */
    if (header->operation != LANDAUER_OP_READ &&
        header->operation != LANDAUER_OP_READ_BLOCK &&
        header->operation != LANDAUER_OP_GET_INFO &&
        header->operation != LANDAUER_OP_PCI_ENUMERATE &&
        InputBufferLength < LANDAUER_HEADER_SIZE + input_len) {
        header->status      = STATUS_BUFFER_TOO_SMALL;
        header->data_length = 0;
        header->version     = LANDAUER_VERSION;
        *BytesReturned      = LANDAUER_HEADER_SIZE;
        return STATUS_SUCCESS;
    }

    /* COPY input payload — it overlaps with output in METHOD_BUFFERED */
    UINT8 saved_input[256];
    PVOID safe_input = input_payload;
    if (input_len > 0 && input_len <= sizeof(saved_input)) {
        RtlCopyMemory(saved_input, input_payload, input_len);
        safe_input = saved_input;
    }

    output_len = (ULONG)(OutputBufferLength - LANDAUER_HEADER_SIZE);

    status = provider->handler(header,
                               safe_input, input_len,
                               output_payload, &output_len);

    header->status      = (ULONG)status;
    header->data_length = output_len;
    header->version     = LANDAUER_VERSION;
    *BytesReturned      = LANDAUER_HEADER_SIZE + output_len;

    return STATUS_SUCCESS;
}
