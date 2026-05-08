// ClassFactory.h
#pragma once
#include <windows.h>
#include <unknwn.h>

class ClassFactory : public IClassFactory
{
public:
    ClassFactory();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;

private:
    long _refCount;
};