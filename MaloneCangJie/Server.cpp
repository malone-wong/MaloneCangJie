// Server.cpp
#include "pch.h"
#include <windows.h>
#include "Globals.h"
#include "ClassFactory.h"
#include "TextService.h"
#include <new>
#include "logging.h"

STDAPI DllCanUnloadNow()
{
    log_to_file(LOG_LEVEL_DEBUG, "DllCanUnloadNow");
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    log_to_file(LOG_LEVEL_DEBUG, "begin DllGetClassObject");
    if (!ppv) {
        log_to_file(LOG_LEVEL_ERROR, "DllGetClassObject E_POINTER");
        return E_POINTER;
    }
    *ppv = nullptr;

    if (rclsid != CLSID_MyTextService) {
        log_to_file(LOG_LEVEL_ERROR, "rclsid != CLSID_MyTextService");
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    log_to_file(LOG_LEVEL_DEBUG, "rclsid == CLSID_MyTextService");

    ClassFactory* factory = new (std::nothrow) ClassFactory();
    if (!factory) {
		log_to_file(LOG_LEVEL_ERROR, "DllGetClassObject E_OUTOFMEMORY");
        return E_OUTOFMEMORY;
    }

    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();

    log_to_file(LOG_LEVEL_DEBUG, "end DllGetClassObject");
    return hr;
}