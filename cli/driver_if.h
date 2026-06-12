/*
 * driver_if.h — DeviceIoControl 封装接口
 */

#pragma once

#include <windows.h>
#include "..\protocol.h"

/* 打开/关闭驱动句柄 */
HANDLE LandauerOpen(VOID);
VOID   LandauerClose(HANDLE hDevice);

/*
 * LandauerCommand: 发送一次命令, 返回 NTSTATUS
 *
 * hdr: [IN/OUT] 命令头 (status/data_length 由驱动填充)
 * input_payload:  [IN]  输入数据 (可为 NULL)
 * input_len:      [IN]  输入长度
 * output_payload: [OUT] 输出数据 buffer (可与 input 相同)
 * output_capacity:[IN]  output buffer 大小
 * output_len:     [OUT] 驱动实际写入的字节数
 */
NTSTATUS LandauerCommand(
    HANDLE              hDevice,
    PLANDAUER_CMD_HEADER hdr,
    PVOID               input_payload,
    ULONG               input_len,
    PVOID               output_payload,
    ULONG               output_capacity,
    PULONG              output_len);

/* 便捷函数 */
NTSTATUS LandauerRead(
    HANDLE hDevice,
    UCHAR resource_type,
    UCHAR access_width,
    UINT64 address,
    PULONG64 value);

NTSTATUS LandauerWrite(
    HANDLE hDevice,
    UCHAR resource_type,
    UCHAR access_width,
    UINT64 address,
    ULONG64 value);
