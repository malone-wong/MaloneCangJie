// ClassFactory.cpp
#include "pch.h"
#include "ClassFactory.h"
#include "TextService.h"
#include "Globals.h"
#include <new>
#include "logging.h"

ClassFactory::ClassFactory() : _refCount(1)
{
	log_to_file(LOG_LEVEL_DEBUG, "ClassFactory constructor");
    InterlockedIncrement(&g_cDllRef);
}

ULONG ClassFactory::AddRef()
{
	log_to_file(LOG_LEVEL_DEBUG, "ClassFactory::AddRef");
    return (ULONG)InterlockedIncrement(&_refCount);
}

ULONG ClassFactory::Release()
{
	log_to_file(LOG_LEVEL_DEBUG, "ClassFactory::Release");
    long c = InterlockedDecrement(&_refCount);
    if (c == 0) delete this;
    return (ULONG)c;
}

HRESULT ClassFactory::QueryInterface(REFIID riid, void** ppvObj)
{
	log_to_file(LOG_LEVEL_DEBUG, "begin ClassFactory::QueryInterface");
    if (!ppvObj) return E_POINTER;
    *ppvObj = nullptr;

    if (riid == IID_IUnknown || riid == IID_IClassFactory)
    {
        *ppvObj = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    log_guid_to_file(LOG_LEVEL_ERROR, "ClassFactory::QueryInterface E_NOINTERFACE riid=", riid);

    return E_NOINTERFACE;
}

HRESULT ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj)
{
	log_to_file(LOG_LEVEL_DEBUG, "begin ClassFactory::CreateInstance");
    if (!ppvObj) return E_POINTER;
    *ppvObj = nullptr;

    if (pUnkOuter != nullptr)
        return CLASS_E_NOAGGREGATION;

    TextService* service = new (std::nothrow) TextService();
    if (!service)
        return E_OUTOFMEMORY;

    HRESULT hr = service->QueryInterface(riid, ppvObj);
    service->Release();
	log_to_file(LOG_LEVEL_DEBUG, "end ClassFactory::CreateInstance");
    return hr;
}

HRESULT ClassFactory::LockServer(BOOL fLock)
{
	log_to_file(LOG_LEVEL_DEBUG, "ClassFactory::LockServer");
    if (fLock)
        InterlockedIncrement(&g_cDllRef);
    else
        InterlockedDecrement(&g_cDllRef);

    return S_OK;
}
