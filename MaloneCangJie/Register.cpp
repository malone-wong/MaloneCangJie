#include "pch.h"
#include <windows.h>
#include <msctf.h>
#include <atlbase.h>
#include <atlcom.h>
#include <string>

#include "Globals.h"
#include "Guids.h"
#include "logging.h"

// Forward declarations
static HRESULT RegisterProfiles();
static HRESULT UnregisterProfiles();

HRESULT RegisterCOMServer()
{
    wchar_t path[MAX_PATH];
    GetModuleFileName(GetModuleInstance(), path, MAX_PATH);

    wchar_t clsid[64];
    StringFromGUID2(CLSID_MyTextService, clsid, 64);

    std::wstring keyPath = L"CLSID\\";
    keyPath += clsid;
    keyPath += L"\\InprocServer32";

    HKEY hKey;
    LONG result = RegCreateKeyEx(
        HKEY_CLASSES_ROOT,
        keyPath.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr,
        &hKey,
        nullptr);

    if (result != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(result);

    RegSetValueEx(
        hKey,
        nullptr,
        0,
        REG_SZ,
        (const BYTE*)path,
        ((DWORD)wcslen(path) + 1) * sizeof(wchar_t));

    const wchar_t* threadingModel = L"Apartment";

    RegSetValueEx(
        hKey,
        L"ThreadingModel",
        0,
        REG_SZ,
        (const BYTE*)threadingModel,
        ((DWORD)wcslen(threadingModel) + 1) * sizeof(wchar_t));

    RegCloseKey(hKey);
    return S_OK;
}

HRESULT RegisterCategories()
{
    CComPtr<ITfCategoryMgr> categoryMgr;

    HRESULT hr = CoCreateInstance(
        CLSID_TF_CategoryMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfCategoryMgr,
        (void**)&categoryMgr);

    if (FAILED(hr))
        return hr;

    return categoryMgr->RegisterCategory(
        CLSID_MyTextService,
        GUID_TFCAT_TIP_KEYBOARD,
        CLSID_MyTextService);
}

STDAPI DllRegisterServer()
{
    log_to_file(LOG_LEVEL_DEBUG, "Begin DllRegisterServer");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HRESULT hrCom = RegisterCOMServer();
    if (FAILED(hrCom)) return hrCom;

    HRESULT hrProfile = RegisterProfiles();
    if (FAILED(hrProfile)) return hrProfile;

    RegisterCategories();

    if (SUCCEEDED(hr)) {
    //if (hr==S_OK) {
        log_to_file(LOG_LEVEL_DEBUG, "Begin CoUninitialize");
        CoUninitialize();
        log_to_file(LOG_LEVEL_DEBUG, "End CoUninitialize");
    }
    log_to_file(LOG_LEVEL_DEBUG, "End DllRegisterServer");
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    log_to_file(LOG_LEVEL_DEBUG, "Begin DllUnregisterServer");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    UnregisterProfiles();

    if (SUCCEEDED(hr))
        CoUninitialize();

    return S_OK;
}

static HRESULT RegisterProfiles()
{
	log_to_file(LOG_LEVEL_DEBUG, "RegisterProfiles");
    CComPtr<ITfInputProcessorProfiles> profiles;

    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,
        (void**)&profiles);

    if (FAILED(hr)) {
		log_to_file(LOG_LEVEL_ERROR, "RegisterProfiles failed");
        return hr;
    }

    HRESULT hrRegister = profiles->Register(CLSID_MyTextService);
    if (FAILED(hrRegister)) {
        log_to_file(LOG_LEVEL_ERROR, "profiles->Register failed");
        return hrRegister;
    }

    wchar_t path[MAX_PATH];
    GetModuleFileName(GetModuleInstance(), path, MAX_PATH);

    HRESULT result= profiles->AddLanguageProfile(
        CLSID_MyTextService,
        TEXTSERVICE_LANGID,
        GUID_Profile_MaloneCangjie,
        L"Malone Cangjie",
        (ULONG)wcslen(L"Malone Cangjie"),
        path,
        (ULONG)wcslen(path),
		0);
	if (FAILED(result)) {
        log_to_file(LOG_LEVEL_ERROR, "AddLanguageProfile failed");
	}else{
        log_to_file(LOG_LEVEL_DEBUG, "AddLanguageProfile succeeded");
    }

    return result;
}

static HRESULT UnregisterProfiles()
{
	log_to_file(LOG_LEVEL_DEBUG, "UnregisterProfiles");
    CComPtr<ITfInputProcessorProfiles> profiles;

    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfiles,
        (void**)&profiles);

    if (FAILED(hr)) {
        log_to_file(LOG_LEVEL_ERROR, "UnregisterProfiles failed");
        return hr;
    }

    profiles->Unregister(CLSID_MyTextService);
	log_to_file(LOG_LEVEL_DEBUG, "UnregisterProfiles succeeded");
    return S_OK;
}

// Add this to MaloneCangJie/Register.cpp
STDAPI DllInstall(BOOL fInstall, _In_opt_ LPCWSTR pszCmdLine)
{
    log_to_file(LOG_LEVEL_DEBUG, "DllInstall");
    if (fInstall)
    {
        return DllRegisterServer();
    }
    else
    {
        return DllUnregisterServer();
    }
}