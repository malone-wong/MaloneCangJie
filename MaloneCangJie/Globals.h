#pragma once
#include <Windows.h>

extern long g_cDllRef;

HINSTANCE GetModuleInstance();
void SetModuleInstance(HMODULE h);

// {1E6ACA11-DDE5-4F5D-B642-7FA82BD840FD}
DEFINE_GUID(CLSID_MyTextService,
    0x1e6aca11, 0xdde5, 0x4f5d, 0xb6, 0x42, 0x7f, 0xa8, 0x2b, 0xd8, 0x40, 0xfd);

// {51E80CDC-CC41-4A4C-8BBC-ACC8CE8C29B6}
DEFINE_GUID(GUID_Profile_MaloneCangjie,
    0x51e80cdc, 0xcc41, 0x4a4c, 0x8b, 0xbc, 0xac, 0xc8, 0xce, 0x8c, 0x29, 0xb6);
