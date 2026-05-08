#include "pch.h"
#include <initguid.h>
#include "Globals.h"

long g_cDllRef = 0;

namespace
{
    HMODULE g_module = nullptr;
}

void SetModuleInstance(HMODULE h)
{
    g_module = h;
}

HMODULE GetModuleInstance()
{
    return g_module;
}