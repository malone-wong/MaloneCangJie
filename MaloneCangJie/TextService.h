// TextService.h
#pragma once
#include <windows.h>
#include <msctf.h>
#include <atlbase.h>
#include <atlcom.h>
#include <string>
#include <vector>
#include <memory>
#include "DictionaryEngine.h"

/*struct CandidateItem
{
    std::wstring text;
};*/

//class DictionaryEngine;

class TextService : public ITfTextInputProcessorEx, public ITfKeyEventSink, public ITfActiveLanguageProfileNotifySink
{
public:
    TextService();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    IFACEMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    IFACEMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    IFACEMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfKeyEventSink
    IFACEMETHODIMP OnSetFocus(BOOL fForeground) override;
    IFACEMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfActiveLanguageProfileNotifySink
    IFACEMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL activated) override;
private:
    ~TextService();

    HRESULT AdviseKeyEventSink();
    HRESULT UnadviseKeyEventSink();
    HRESULT AdviseActiveLanguageProfileNotifySink();
    HRESULT UnadviseActiveLanguageProfileNotifySink();

    bool IsImeOn() const;
    bool IsCodeKey(WPARAM vk) const;
    wchar_t CodeKeyToChar(WPARAM vk) const;
    bool IsPunctuationKey(WPARAM vk) const;
    wchar_t PunctuationKeyToFullWidthChar(WPARAM vk) const;
    bool IsCommitKey(WPARAM vk) const;
    bool IsCancelKey(WPARAM vk) const;
    bool IsSelectCandidateKey(WPARAM vk, int& index) const;
    bool HasShortcutModifier() const;
    bool IsShiftDown() const;
    bool HasActiveInput() const;

    HRESULT HandleCodeInput(ITfContext* context, wchar_t ch);
    HRESULT HandlePunctuationInput(ITfContext* context, wchar_t ch);
    HRESULT HandleBackspace(ITfContext* context);
    HRESULT HandleDelete(ITfContext* context);
    HRESULT HandleCommit(ITfContext* context);
    HRESULT HandleCancel(ITfContext* context);
    HRESULT HandleSelectCandidate(ITfContext* context, int index);

    HRESULT StartComposition(ITfContext* context);
    HRESULT UpdateCompositionText(ITfContext* context, const std::wstring& text);
    HRESULT InsertTextAtSelection(ITfContext* context, const std::wstring& text);
    HRESULT EndComposition(ITfContext* context);

    void RefreshCandidates();
    void RefreshAssociatedWordCandidates();
    void RememberCommittedText(const std::wstring& text);
    void ClearInputState();
    std::wstring BuildDisplayText() const;
    std::wstring BuildCandidateWindowText() const;
    HRESULT ReplaceDisplayedText(ITfContext* context, const std::wstring& text);
    void UpdateCandidateWindow();
    void HideCandidateWindow();
    bool EnsureCandidateWindow();
    static LRESULT CALLBACK CandidateWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    long _refCount;
    TfClientId _clientId;
    DWORD _keySinkCookie;
    DWORD _profileNotifySinkCookie;
    bool _keyEventSinkAdvised;
    bool _profileNotifySinkAdvised;

    CComPtr<ITfThreadMgr> _threadMgr;
    CComPtr<ITfComposition> _composition;
    CComPtr<ITfRange> _displayRange;
    HWND _candidateWindow;

    std::wstring _readingBuffer;
    std::wstring _displayText;
    std::wstring _candidateWindowText;
    std::wstring _committedText;
    std::vector<CandidateItem> _candidates;
    size_t _selectedIndex;

    std::unique_ptr<DictionaryEngine> _engine;
};
