/*
 * driver_if.c — DeviceIoControl 封装
 */

#include "driver_if.h"
#include <stdio.h>

/* ── 打开驱动 ── */
HANDLE
LandauerOpen(VOID)
{
    HANDLE h = CreateFileW(
        L"\\\\.\\Landauer",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    return h;
}

/* ── 关闭驱动 ── */
VOID
LandauerClose(HANDLE hDevice)
{
    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
    }
}

/* ── 发送命令 ── */
NTSTATUS
LandauerCommand(
    HANDLE              hDevice,
    PLANDAUER_CMD_HEADER hdr,
    PVOID               input_payload,
    ULONG               input_len,
    PVOID               output_payload,
    ULONG               output_capacity,
    PULONG              output_len)
{
    BOOL    ok;
    DWORD   bytesReturned = 0;
    DWORD   totalIn  = LANDAUER_HEADER_SIZE + input_len;
    DWORD   totalOut = LANDAUER_HEADER_SIZE + output_capacity;
    DWORD   allocSize = (totalIn > totalOut) ? totalIn : totalOut;
    PUINT8  buffer;
    NTSTATUS status;

    if (hDevice == INVALID_HANDLE_VALUE) {
        return (NTSTATUS)0xC0000001;  /* STATUS_UNSUCCESSFUL */
    }

    buffer = (PUINT8)HeapAlloc(GetProcessHeap(), 0, allocSize);
    if (buffer == NULL) {
        return (NTSTATUS)0xC000009A;  /* STATUS_INSUFFICIENT_RESOURCES */
    }

    /* 组装请求 */
    if (input_len > 0 || (input_payload != NULL)) {
        hdr->data_length = input_len;  /* only override if caller passes input */
    }
    RtlCopyMemory(buffer, hdr, LANDAUER_HEADER_SIZE);
    if (input_payload != NULL && input_len > 0) {
        RtlCopyMemory(buffer + LANDAUER_HEADER_SIZE,
                      input_payload, input_len);
    }

    /* 调用驱动 */
    ok = DeviceIoControl(
        hDevice,
        LANDAUER_IOCTL_CMD,
        buffer, totalIn,
        buffer, totalOut,
        &bytesReturned,
        NULL);

    if (!ok) {
        status = (NTSTATUS)GetLastError();
        HeapFree(GetProcessHeap(), 0, buffer);
        /* 统一转为 NTSTATUS 风格的错误 */
        if (status == 0) status = (NTSTATUS)0xC0000001;
        return status;
    }

    /* 解析响应 */
    PLANDAUER_CMD_HEADER outHdr = (PLANDAUER_CMD_HEADER)buffer;
    status = (NTSTATUS)outHdr->status;

    if (output_payload != NULL && output_len != NULL && bytesReturned > LANDAUER_HEADER_SIZE) {
        ULONG outLen = (ULONG)(bytesReturned - LANDAUER_HEADER_SIZE);
        if (outLen > output_capacity) outLen = output_capacity;
        RtlCopyMemory(output_payload, buffer + LANDAUER_HEADER_SIZE, outLen);
        *output_len = outLen;
    } else if (output_len != NULL) {
        *output_len = 0;
    }

    /* 回写 header */
    RtlCopyMemory(hdr, outHdr, LANDAUER_HEADER_SIZE);

    HeapFree(GetProcessHeap(), 0, buffer);
    return status;
}

/* ── 便捷: 单次读取 ── */
NTSTATUS
LandauerRead(
    HANDLE hDevice,
    UCHAR  resource_type,
    UCHAR  access_width,
    UINT64 address,
    PULONG64 value)
{
    LANDAUER_CMD_HEADER hdr;
    ULONG outLen = 0;
    ULONG64 val = 0;

    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic         = LANDAUER_MAGIC;
    hdr.version       = LANDAUER_VERSION;
    hdr.resource_type = resource_type;
    hdr.operation     = LANDAUER_OP_READ;
    hdr.access_width  = access_width;
    hdr.data_length   = 0;
    hdr.address       = address;

    NTSTATUS st = LandauerCommand(hDevice, &hdr,
        NULL, 0, &val, access_width, &outLen);
    *value = val;
    return st;
}

/* ── 便捷: 单次写入 ── */
NTSTATUS
LandauerWrite(
    HANDLE hDevice,
    UCHAR  resource_type,
    UCHAR  access_width,
    UINT64 address,
    ULONG64 value)
{
    LANDAUER_CMD_HEADER hdr;
    RtlZeroMemory(&hdr, sizeof(hdr));
    hdr.magic         = LANDAUER_MAGIC;
    hdr.version       = LANDAUER_VERSION;
    hdr.resource_type = resource_type;
    hdr.operation     = LANDAUER_OP_WRITE;
    hdr.access_width  = access_width;
    hdr.address       = address;

    return LandauerCommand(hDevice, &hdr,
        &value, access_width, NULL, 0, NULL);
}
