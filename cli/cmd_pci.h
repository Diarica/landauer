/*
 * cmd_pci.h — PCI 子命令
 */

#pragma once

#include <windows.h>

/* 返回 exit code */
int CmdPciList(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciInfo(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciCfgRead(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciCfgWrite(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciCapList(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciCapFind(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciExtCapList(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciBarInfo(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciBarMap(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciBarRead(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciBarWrite(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciBarDump(HANDLE hDevice, int argc, wchar_t* argv[]);
int CmdPciBarUnmap(HANDLE hDevice, int argc, wchar_t* argv[]);
