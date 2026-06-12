/*
 * main.c — Landauer CLI 入口
 *
 * 命令格式: landauer <resource> <action> [args...]
 * 资源:
 *   pci      — PCI/PCIe 全功能
 *   driver   — 驱动管理
 *
 * 用法:
 *   landauer pci list
 *   landauer pci info <bdf>
 *   landauer pci cfg read|write <bdf> <offset> [value] [width]
 *   landauer pci cap list|find <bdf> [cap_id]
 *   landauer pci ext-cap list <bdf>
 *   landauer pci bar info|map|read|write|dump|unmap ...
 */

#include "cmd_pci.h"
#include "driver_if.h"
#include "format.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── 用法 ── */
static void
PrintUsage(VOID)
{
    printf(
        "Landauer — Hardware Access CLI  v1.0\n"
        "\n"
        "USAGE:\n"
        "  landauer pci list                              List all PCI devices\n"
        "  landauer pci info <bdf>                        Show device details\n"
        "  landauer pci cfg read <bdf> <offset> [w]       Read config space\n"
        "  landauer pci cfg write <bdf> <offset> <val> [w] Write config space\n"
        "  landauer pci cap list <bdf>                    List capabilities\n"
        "  landauer pci cap find <bdf> <cap_id>           Find a capability\n"
        "  landauer pci ext-cap list <bdf>                List ext capabilities\n"
        "  landauer pci bar info <bdf> <idx>              Show BAR info\n"
        "  landauer pci bar map <bdf> <idx> [cache]       Map a BAR\n"
        "  landauer pci bar read <handle> <off> [w]       Read BAR MMIO\n"
        "  landauer pci bar write <handle> <off> <val> [w] Write BAR MMIO\n"
        "  landauer pci bar dump <handle> <off> <len> [f] Dump MMIO\n"
        "  landauer pci bar unmap <handle>                Unmap a BAR\n"
        "\n"
        "  landauer driver status                         Driver status\n"
        "\n"
        "BDF format: bus:dev.func in hex (e.g. 00:02.0)\n"
        "Width: 1, 2, 4 (config) or 1,2,4,8 (BAR)\n"
        "Cache types: 0=cached, 1=uncached (default), 2=WC\n"
        "\n"
        "All output is grep-parseable. Errors go to stderr.\n"
        "Exit codes: 0=ok 1=args 2=driver 3=hw 4=notfound\n"
    );
}

/* ── 驱动状态 ── */
static int
CmdDriverStatus(int argc, wchar_t* argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    HANDLE h = LandauerOpen();
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        FmtErr("Driver not accessible (error %lu). Run 'landauer.bat install' as admin.",
               err);
        return 2;
    }

    /* 尝试一个简单的命令验证通信 */
    ULONG64 val = 0;
    NTSTATUS st = LandauerRead(h, LANDAUER_RESOURCE_PCI_CFG,
                               2, PCI_ADDR_ENCODE(0, 0, 0, 0, 0), &val);
    LandauerClose(h);

    if (NT_SUCCESS(st)) {
        printf("Landauer driver: OK\n");
        printf("PCI Bus accessible. Host bridge vendor: 0x%04lX\n",
               (unsigned long)(val & 0xFFFF));
        return 0;
    } else {
        printf("Landauer driver: Loaded but PCI access failed: %s (0x%08lX)\n",
               FmtNtStatus(st), (unsigned long)(ULONG)st);
        return 1;
    }
}

/* ── PCI 子命令路由 ── */
static int
CmdPciDispatch(HANDLE hDevice, int argc, wchar_t* argv[])
{
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const wchar_t* action = argv[1];

    /* Level-1 commands: skip "pci", pass remaining → leaf sees argv[0]=action, argv[1]=arg1 ... */
    if (_wcsicmp(action, L"list") == 0) {
        return CmdPciList(hDevice, argc - 1, argv + 1);
    }
    if (_wcsicmp(action, L"info") == 0) {
        return CmdPciInfo(hDevice, argc - 1, argv + 1);
    }
    /* Level-2 commands: skip "pci <cmd>", pass remaining → leaf sees argv[0]=subaction, argv[1]=arg1 ... */
    if (_wcsicmp(action, L"cfg") == 0) {
        if (argc < 3) { FmtErr("Usage: pci cfg read|write ..."); return 1; }
        if (_wcsicmp(argv[2], L"read") == 0)  return CmdPciCfgRead(hDevice,  argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"write") == 0) return CmdPciCfgWrite(hDevice, argc - 2, argv + 2);
        FmtErr("Unknown cfg action: %ls", argv[2]); return 1;
    }
    if (_wcsicmp(action, L"cap") == 0) {
        if (argc < 3) { FmtErr("Usage: pci cap list|find ..."); return 1; }
        if (_wcsicmp(argv[2], L"list") == 0) return CmdPciCapList(hDevice, argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"find") == 0) return CmdPciCapFind(hDevice, argc - 2, argv + 2);
        FmtErr("Unknown cap action: %ls", argv[2]); return 1;
    }
    if (_wcsicmp(action, L"ext-cap") == 0) {
        if (argc < 3 || _wcsicmp(argv[2], L"list") != 0) { FmtErr("Usage: pci ext-cap list <bdf>"); return 1; }
        return CmdPciExtCapList(hDevice, argc - 2, argv + 2);
    }
    if (_wcsicmp(action, L"bar") == 0) {
        if (argc < 3) { FmtErr("Usage: pci bar info|map|read|write|dump|unmap ..."); return 1; }
        if (_wcsicmp(argv[2], L"info")   == 0) return CmdPciBarInfo(hDevice,  argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"map")    == 0) return CmdPciBarMap(hDevice,   argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"read")   == 0) return CmdPciBarRead(hDevice,  argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"write")  == 0) return CmdPciBarWrite(hDevice, argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"dump")   == 0) return CmdPciBarDump(hDevice,  argc - 2, argv + 2);
        if (_wcsicmp(argv[2], L"unmap")  == 0) return CmdPciBarUnmap(hDevice, argc - 2, argv + 2);
        FmtErr("Unknown bar action: %ls", argv[2]); return 1;
    }

    FmtErr("Unknown pci action: %ls. Try: list, info, cfg, cap, ext-cap, bar", action);
    return 1;
}

/* ============================================================
 *  main
 * ============================================================ */
int
__cdecl wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const wchar_t* resource = argv[1];

    /* ── driver 命令 (不需要打开驱动句柄) ── */
    if (_wcsicmp(resource, L"driver") == 0) {
        if (argc >= 3 && _wcsicmp(argv[2], L"status") == 0) {
            return CmdDriverStatus(argc, argv);
        }
        FmtErr("Usage: landauer driver status");
        return 1;
    }

    /* ── 打开驱动 ── */
    HANDLE hDevice = LandauerOpen();
    if (hDevice == INVALID_HANDLE_VALUE) {
        FmtErr("Cannot open Landauer driver. Is it installed?\n"
               "  Run: landauer.bat install  (as Administrator)");
        return 2;
    }

    int result = 0;

    /* ── pci 命令 ── */
    if (_wcsicmp(resource, L"pci") == 0) {
        result = CmdPciDispatch(hDevice, argc - 1, argv + 1);
    } else {
        FmtErr("Unknown resource: %ls. Available: pci, driver", resource);
        result = 1;
    }

    LandauerClose(hDevice);
    return result;
}
