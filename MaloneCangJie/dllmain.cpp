// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "Globals.h"
#include "logging.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    log_to_file(LOG_LEVEL_INFO, "Version 0.0.0.00007");
    log_to_file(LOG_LEVEL_DEBUG, "Begin DllMain");
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        log_to_file(LOG_LEVEL_DEBUG, "DLL_PROCESS_ATTACH");
        SetModuleInstance(hModule);
        // We do not need DLL_THREAD_ATTACH / DLL_THREAD_DETACH notifications.
        DisableThreadLibraryCalls(hModule);

        // Keep DllMain minimal.
        // Do NOT do heavy work here.
        break;
    case DLL_PROCESS_DETACH:
        log_to_file(LOG_LEVEL_DEBUG, "DLL_PROCESS_DETACH");
        break;
    }
    log_to_file(LOG_LEVEL_DEBUG, "End DllMain");
    return TRUE;
}

