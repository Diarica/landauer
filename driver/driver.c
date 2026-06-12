/*
 * driver.c — Landauer Legacy NT 驱动入口
 *
 * 不使用 KMDF (因为环境可能没有 WDF 头文件)。
 * 纯 NT API: IoCreateDevice, IRP dispatch, KSPIN_LOCK。
 * 编译: VS2022 + Windows SDK/DDK, x64 only
 */

#include <ntddk.h>
#include "..\protocol.h"
#include "provider.h"

/* ── 全局设备对象 ── */
static PDEVICE_OBJECT g_DeviceObject = NULL;

/* ── 前向声明 ── */
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     DriverUnload;

DRIVER_DISPATCH   DispatchCreateClose;
DRIVER_DISPATCH   DispatchDeviceControl;

/* ── 外部: dispatch.c ── */
NTSTATUS DispatchIoCommand(
    _Inout_ PVOID   Buffer,
    _In_    SIZE_T  InputBufferLength,
    _In_    SIZE_T  OutputBufferLength,
    _Out_   PSIZE_T BytesReturned);

/* ============================================================
 *  DriverEntry
 * ============================================================ */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS       status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING devName;
    UNICODE_STRING symLink;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "Landauer: DriverEntry\n");

    /* 设置分发函数 */
    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

    /* 创建设备 */
    RtlInitUnicodeString(&devName, LANDAUER_NT_DEVICE_NAME);
    RtlInitUnicodeString(&symLink, LANDAUER_DOS_DEVICE_NAME);

    status = IoCreateDevice(
        DriverObject,
        0,                              /* 设备扩展 */
        &devName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,                          /* 非独占 */
        &deviceObject);

    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "Landauer: IoCreateDevice failed 0x%08X\n", status);
        return status;
    }

    /* 设置 buffer IO 方式 */
    deviceObject->Flags |= DO_BUFFERED_IO;

    /* 创建符号链接 */
    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "Landauer: IoCreateSymbolicLink failed 0x%08X\n", status);
        IoDeleteDevice(deviceObject);
        return status;
    }

    /* 初始化 Provider */
    status = ProviderInit();
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "Landauer: ProviderInit failed 0x%08X\n", status);
        IoDeleteSymbolicLink(&symLink);
        IoDeleteDevice(deviceObject);
        return status;
    }

    g_DeviceObject = deviceObject;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "Landauer: Device created, symlink = %wZ\n", &symLink);

    return STATUS_SUCCESS;
}

/* ============================================================
 *  DriverUnload
 * ============================================================ */
VOID
DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symLink;

    UNREFERENCED_PARAMETER(DriverObject);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "Landauer: DriverUnload\n");

    ProviderCleanup();

    if (g_DeviceObject != NULL) {
        RtlInitUnicodeString(&symLink, LANDAUER_DOS_DEVICE_NAME);
        IoDeleteSymbolicLink(&symLink);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }
}

/* ============================================================
 *  DispatchCreateClose
 * ============================================================ */
NTSTATUS
DispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ============================================================
 *  DispatchDeviceControl
 * ============================================================ */
NTSTATUS
DispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION irpStack;
    PVOID              buffer;
    SIZE_T             inputLen, outputLen;
    SIZE_T             bytesReturned = 0;
    NTSTATUS           status;

    UNREFERENCED_PARAMETER(DeviceObject);

    irpStack = IoGetCurrentIrpStackLocation(Irp);

    if (irpStack->Parameters.DeviceIoControl.IoControlCode !=
        LANDAUER_IOCTL_CMD) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto complete;
    }

    /* METHOD_BUFFERED: buffer = Irp->AssociatedIrp.SystemBuffer */
    buffer    = Irp->AssociatedIrp.SystemBuffer;
    inputLen  = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    outputLen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (buffer == NULL || inputLen < LANDAUER_HEADER_SIZE) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }

    status = DispatchIoCommand(buffer, inputLen, outputLen, &bytesReturned);

complete:
    Irp->IoStatus.Status      = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
