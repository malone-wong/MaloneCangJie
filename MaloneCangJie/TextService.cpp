#include "pch.h"// TextService.cpp
#include "TextService.h"
#include "Globals.h"
#include "logging.h"
#include <new>
#include <utility>

namespace
{
    constexpr int CandidatePageSize = 10;
    constexpr wchar_t CandidateWindowClassName[] = L"MaloneCangJieCandidateWindow";
    constexpr int CandidateWindowPaddingX = 10;
    constexpr int CandidateWindowPaddingY = 6;
    constexpr COLORREF CandidateWindowBackground = RGB(255, 252, 235);
    constexpr COLORREF CandidateWindowBorder = RGB(120, 120, 120);
    constexpr COLORREF CandidateWindowText = RGB(20, 20, 20);

    class InsertTextEditSession final : public ITfEditSession
    {
    public:
        InsertTextEditSession(ITfContext* context, std::wstring text)
            : _refCount(1), _context(context), _text(std::move(text)), _result(E_FAIL)
        {
        }

        HRESULT GetResult() const
        {
            return _result;
        }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (!ppvObj)
                return E_POINTER;

            *ppvObj = nullptr;
            if (riid == IID_IUnknown || riid == IID_ITfEditSession)
            {
                *ppvObj = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        IFACEMETHODIMP_(ULONG) AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        IFACEMETHODIMP_(ULONG) Release() override
        {
            long refCount = InterlockedDecrement(&_refCount);
            if (refCount == 0)
                delete this;

            return static_cast<ULONG>(refCount);
        }

        IFACEMETHODIMP DoEditSession(TfEditCookie editCookie) override
        {
            if (!_context || _text.empty())
            {
                _result = S_OK;
                return _result;
            }

            TF_SELECTION selection = {};
            ULONG fetched = 0;
            HRESULT hr = _context->GetSelection(editCookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
            if (SUCCEEDED(hr) && fetched == 1 && selection.range)
            {
                selection.range->Collapse(editCookie, TF_ANCHOR_END);
                _result = selection.range->SetText(
                    editCookie,
                    0,
                    _text.c_str(),
                    static_cast<LONG>(_text.size()));
                selection.range->Collapse(editCookie, TF_ANCHOR_END);
                _context->SetSelection(editCookie, 1, &selection);
                selection.range->Release();
                return _result;
            }

            _result = FAILED(hr) ? hr : E_FAIL;
            return _result;
        }

    private:
        ~InsertTextEditSession() = default;

        long _refCount;
        CComPtr<ITfContext> _context;
        std::wstring _text;
        HRESULT _result;
    };

    class SetSelectionTextEditSession final : public ITfEditSession
    {
    public:
        SetSelectionTextEditSession(ITfContext* context, std::wstring text, LONG replaceLength, ITfRange* displayRange, ITfComposition* composition)
            : _refCount(1), _context(context), _displayRange(displayRange), _composition(composition), _text(std::move(text)), _replaceLength(replaceLength), _result(E_FAIL)
        {
        }

        HRESULT GetResult() const
        {
            return _result;
        }

        HRESULT CopyUpdatedRange(ITfRange** range) const
        {
            if (!range)
                return E_POINTER;

            *range = nullptr;
            if (!_updatedRange)
                return S_FALSE;

            return _updatedRange->Clone(range);
        }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (!ppvObj)
                return E_POINTER;

            *ppvObj = nullptr;
            if (riid == IID_IUnknown || riid == IID_ITfEditSession)
            {
                *ppvObj = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        IFACEMETHODIMP_(ULONG) AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        IFACEMETHODIMP_(ULONG) Release() override
        {
            long refCount = InterlockedDecrement(&_refCount);
            if (refCount == 0)
                delete this;

            return static_cast<ULONG>(refCount);
        }

        IFACEMETHODIMP DoEditSession(TfEditCookie editCookie) override
        {
            if (!_context)
            {
                _result = E_INVALIDARG;
                return _result;
            }

            if (_displayRange)
            {
                CComPtr<ITfRange> replacementRange;
                HRESULT hr = _displayRange->Clone(&replacementRange);
                if (SUCCEEDED(hr) && replacementRange)
                {
                    _result = replacementRange->SetText(
                        editCookie,
                        0,
                        _text.c_str(),
                        static_cast<LONG>(_text.size()));
                    if (SUCCEEDED(_result))
                    {
                        if (!_text.empty())
                            replacementRange->Clone(&_updatedRange);

                        CComPtr<ITfRange> caretRange;
                        hr = replacementRange->Clone(&caretRange);
                        if (SUCCEEDED(hr) && caretRange)
                        {
                            caretRange->Collapse(editCookie, TF_ANCHOR_END);
                            if (_composition)
                                _composition->ShiftEnd(editCookie, caretRange);

                            TF_SELECTION newSelection = {};
                            newSelection.range = caretRange;
                            newSelection.style.ase = TF_AE_NONE;
                            newSelection.style.fInterimChar = FALSE;
                            _context->SetSelection(editCookie, 1, &newSelection);
                        }
                    }

                    return _result;
                }
            }

            TF_SELECTION selection = {};
            ULONG fetched = 0;
            HRESULT hr = _context->GetSelection(editCookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
            if (FAILED(hr) || fetched != 1 || !selection.range)
            {
                _result = FAILED(hr) ? hr : E_FAIL;
                return _result;
            }

            selection.range->Collapse(editCookie, TF_ANCHOR_END);
            if (_replaceLength > 0)
            {
                LONG shifted = 0;
                selection.range->ShiftStart(editCookie, -_replaceLength, &shifted, nullptr);
            }

            _result = selection.range->SetText(
                editCookie,
                0,
                _text.c_str(),
                static_cast<LONG>(_text.size()));
            if (SUCCEEDED(_result) && !_text.empty())
                selection.range->Clone(&_updatedRange);

            selection.range->Collapse(editCookie, TF_ANCHOR_END);
            if (SUCCEEDED(_result) && _composition)
                _composition->ShiftEnd(editCookie, selection.range);

            _context->SetSelection(editCookie, 1, &selection);
            selection.range->Release();
            return _result;
        }

    private:
        ~SetSelectionTextEditSession() = default;

        long _refCount;
        CComPtr<ITfContext> _context;
        CComPtr<ITfRange> _displayRange;
        CComPtr<ITfComposition> _composition;
        CComPtr<ITfRange> _updatedRange;
        std::wstring _text;
        LONG _replaceLength;
        HRESULT _result;
    };

    class StartCompositionEditSession final : public ITfEditSession
    {
    public:
        explicit StartCompositionEditSession(ITfContext* context)
            : _refCount(1), _context(context), _result(E_FAIL)
        {
        }

        HRESULT GetResult() const
        {
            return _result;
        }

        HRESULT CopyComposition(ITfComposition** composition) const
        {
            if (!composition)
                return E_POINTER;

            *composition = nullptr;
            if (!_composition)
                return S_FALSE;

            *composition = _composition;
            (*composition)->AddRef();
            return S_OK;
        }

        HRESULT CopyRange(ITfRange** range) const
        {
            if (!range)
                return E_POINTER;

            *range = nullptr;
            if (!_range)
                return S_FALSE;

            return _range->Clone(range);
        }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (!ppvObj)
                return E_POINTER;

            *ppvObj = nullptr;
            if (riid == IID_IUnknown || riid == IID_ITfEditSession)
            {
                *ppvObj = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        IFACEMETHODIMP_(ULONG) AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        IFACEMETHODIMP_(ULONG) Release() override
        {
            long refCount = InterlockedDecrement(&_refCount);
            if (refCount == 0)
                delete this;

            return static_cast<ULONG>(refCount);
        }

        IFACEMETHODIMP DoEditSession(TfEditCookie editCookie) override
        {
            if (!_context)
            {
                _result = E_INVALIDARG;
                return _result;
            }

            CComPtr<ITfContextComposition> contextComposition;
            HRESULT hr = _context->QueryInterface(IID_ITfContextComposition, (void**)&contextComposition);
            if (FAILED(hr))
            {
                _result = hr;
                return _result;
            }

            TF_SELECTION selection = {};
            ULONG fetched = 0;
            hr = _context->GetSelection(editCookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
            if (FAILED(hr) || fetched != 1 || !selection.range)
            {
                _result = FAILED(hr) ? hr : E_FAIL;
                return _result;
            }

            selection.range->Collapse(editCookie, TF_ANCHOR_END);
            hr = selection.range->Clone(&_range);
            if (SUCCEEDED(hr))
                hr = contextComposition->StartComposition(editCookie, selection.range, nullptr, &_composition);

            selection.range->Release();
            _result = hr;
            return _result;
        }

    private:
        ~StartCompositionEditSession() = default;

        long _refCount;
        CComPtr<ITfContext> _context;
        CComPtr<ITfRange> _range;
        CComPtr<ITfComposition> _composition;
        HRESULT _result;
    };

    class EndCompositionEditSession final : public ITfEditSession
    {
    public:
        explicit EndCompositionEditSession(ITfComposition* composition)
            : _refCount(1), _composition(composition), _result(E_FAIL)
        {
        }

        HRESULT GetResult() const
        {
            return _result;
        }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (!ppvObj)
                return E_POINTER;

            *ppvObj = nullptr;
            if (riid == IID_IUnknown || riid == IID_ITfEditSession)
            {
                *ppvObj = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        IFACEMETHODIMP_(ULONG) AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        IFACEMETHODIMP_(ULONG) Release() override
        {
            long refCount = InterlockedDecrement(&_refCount);
            if (refCount == 0)
                delete this;

            return static_cast<ULONG>(refCount);
        }

        IFACEMETHODIMP DoEditSession(TfEditCookie editCookie) override
        {
            if (!_composition)
            {
                _result = S_OK;
                return _result;
            }

            _result = _composition->EndComposition(editCookie);
            return _result;
        }

    private:
        ~EndCompositionEditSession() = default;

        long _refCount;
        CComPtr<ITfComposition> _composition;
        HRESULT _result;
    };

    class CandidateAnchorEditSession final : public ITfEditSession
    {
    public:
        CandidateAnchorEditSession(ITfContext* context, ITfRange* range)
            : _refCount(1), _context(context), _range(range), _result(E_FAIL), _clipped(FALSE)
        {
            SetRectEmpty(&_rect);
        }

        HRESULT GetResult() const
        {
            return _result;
        }

        RECT GetRect() const
        {
            return _rect;
        }

        IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override
        {
            if (!ppvObj)
                return E_POINTER;

            *ppvObj = nullptr;
            if (riid == IID_IUnknown || riid == IID_ITfEditSession)
            {
                *ppvObj = static_cast<ITfEditSession*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        IFACEMETHODIMP_(ULONG) AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&_refCount));
        }

        IFACEMETHODIMP_(ULONG) Release() override
        {
            long refCount = InterlockedDecrement(&_refCount);
            if (refCount == 0)
                delete this;

            return static_cast<ULONG>(refCount);
        }

        IFACEMETHODIMP DoEditSession(TfEditCookie editCookie) override
        {
            if (!_context || !_range)
            {
                _result = E_INVALIDARG;
                return _result;
            }

            CComPtr<ITfContextView> view;
            HRESULT hr = _context->GetActiveView(&view);
            if (FAILED(hr) || !view)
            {
                _result = FAILED(hr) ? hr : E_FAIL;
                return _result;
            }

            _result = view->GetTextExt(editCookie, _range, &_rect, &_clipped);
            return _result;
        }

    private:
        ~CandidateAnchorEditSession() = default;

        long _refCount;
        CComPtr<ITfContext> _context;
        CComPtr<ITfRange> _range;
        RECT _rect;
        BOOL _clipped;
        HRESULT _result;
    };

    std::wstring GetDatabasePath()
    {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(GetModuleInstance(), modulePath, ARRAYSIZE(modulePath));
        if (length == 0 || length == ARRAYSIZE(modulePath))
            return L"";

        std::wstring path(modulePath, length);
        size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return L"MaloneCangJie.db";

        path.resize(slash + 1);
        path += L"MaloneCangJie.db";
        return path;
    }

    POINT GetFallbackCandidateWindowAnchor()
    {
        POINT point = {};
        HWND focusWindow = GetFocus();
        if (focusWindow && GetCaretPos(&point))
        {
            ClientToScreen(focusWindow, &point);
            point.y += 24;
            return point;
        }

        GetCursorPos(&point);
        point.y += 24;
        return point;
    }
}

TextService::TextService()
    : _refCount(1),
      _clientId(TF_CLIENTID_NULL),
      _keySinkCookie(TF_INVALID_COOKIE),
      _profileNotifySinkCookie(TF_INVALID_COOKIE),
      _keyEventSinkAdvised(false),
      _profileNotifySinkAdvised(false),
      _threadMgr(nullptr),
      _candidateWindow(nullptr),
      _selectedIndex(0),
      _engine(std::make_unique<DictionaryEngine>())
{
    InterlockedIncrement(&g_cDllRef);
	log_to_file(LOG_LEVEL_DEBUG, "TextService constructor");
}

TextService::~TextService()
{
    HideCandidateWindow();
    if (_candidateWindow && IsWindow(_candidateWindow))
        DestroyWindow(_candidateWindow);
    InterlockedDecrement(&g_cDllRef);
	log_to_file(LOG_LEVEL_DEBUG, "TextService destructor");
}

ULONG TextService::AddRef()
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::AddRef");
    return (ULONG)InterlockedIncrement(&_refCount);
}

ULONG TextService::Release()
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::Release");
    long c = InterlockedDecrement(&_refCount);
    if (c == 0) delete this;
    return (ULONG)c;
}

HRESULT TextService::QueryInterface(REFIID riid, void** ppvObj)
{
	log_to_file(LOG_LEVEL_DEBUG, "begin TextService::QueryInterface");
    if (!ppvObj) {
		log_to_file(LOG_LEVEL_ERROR, "TextService::QueryInterface E_POINTER");
        return E_POINTER;
    }
    *ppvObj = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor || riid == IID_ITfTextInputProcessorEx)
    {
        *ppvObj = static_cast<ITfTextInputProcessorEx*>(this);
        AddRef();
		log_to_file(LOG_LEVEL_DEBUG, "end TextService::QueryInterface - ITfTextInputProcessor");
        return S_OK;
    }
    if (riid == IID_ITfKeyEventSink)
    {
        *ppvObj = static_cast<ITfKeyEventSink*>(this);
        AddRef();
		log_to_file(LOG_LEVEL_DEBUG, "end TextService::QueryInterface - ITfKeyEventSink");
        return S_OK;
    }
    if (riid == IID_ITfActiveLanguageProfileNotifySink)
    {
        *ppvObj = static_cast<ITfActiveLanguageProfileNotifySink*>(this);
        AddRef();
        log_to_file(LOG_LEVEL_DEBUG, "end TextService::QueryInterface - ITfActiveLanguageProfileNotifySink");
        return S_OK;
    }

    log_guid_to_file(LOG_LEVEL_ERROR, "TextService::QueryInterface E_NOINTERFACE riid=", riid);
    return E_NOINTERFACE;
}

HRESULT TextService::Activate(ITfThreadMgr* ptim, TfClientId tid)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::Activate");
    return ActivateEx(ptim, tid, 0);
}

HRESULT TextService::ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::ActivateEx");
    UNREFERENCED_PARAMETER(dwFlags);

    if (!ptim)
        return E_INVALIDARG;

    _threadMgr = ptim;
    _clientId = tid;

    std::wstring dbPath = GetDatabasePath();
    if (dbPath.empty() || !_engine->Initialize(dbPath))
        log_to_file(LOG_LEVEL_ERROR, "TextService::ActivateEx failed to initialize dictionary database");
    else
        log_to_file(LOG_LEVEL_INFO, "TextService::ActivateEx initialized dictionary database");

    HRESULT hr = AdviseKeyEventSink();
    if (FAILED(hr)) {
        log_to_file(LOG_LEVEL_ERROR, "TextService::ActivateEx AdviseKeyEventSink failed");
        return hr;
    }

    hr = AdviseActiveLanguageProfileNotifySink();
    if (FAILED(hr))
        log_to_file(LOG_LEVEL_ERROR, "TextService::ActivateEx AdviseActiveLanguageProfileNotifySink failed");

    return S_OK;
}

HRESULT TextService::Deactivate()
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::Deactivate");
    ClearInputState();
    UnadviseActiveLanguageProfileNotifySink();
    UnadviseKeyEventSink();
    if (_engine)
        _engine->Shutdown();
    _threadMgr.Release();
    _clientId = TF_CLIENTID_NULL;
    return S_OK;
}

HRESULT TextService::AdviseKeyEventSink()
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::AdviseKeyEventSink");
    if (!_threadMgr || _clientId == TF_CLIENTID_NULL)
        return E_UNEXPECTED;

    if (_keyEventSinkAdvised)
        return S_OK;

    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    HRESULT hr = _threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keystrokeMgr);
    if (FAILED(hr))
        return hr;

    hr = keystrokeMgr->AdviseKeyEventSink(
        _clientId,
        static_cast<ITfKeyEventSink*>(this),
        TRUE);

    if (hr == TF_E_ALREADY_EXISTS) {
        _keyEventSinkAdvised = true;
        return S_OK;
    }

    if (SUCCEEDED(hr))
        _keyEventSinkAdvised = true;

    return hr;
}

HRESULT TextService::UnadviseKeyEventSink()
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::UnadviseKeyEventSink");
    if (!_threadMgr || !_keyEventSinkAdvised || _clientId == TF_CLIENTID_NULL)
        return S_OK;

    CComPtr<ITfKeystrokeMgr> keystrokeMgr;
    HRESULT hr = _threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keystrokeMgr);
    if (FAILED(hr))
        return hr;

    hr = keystrokeMgr->UnadviseKeyEventSink(_clientId);
    if (SUCCEEDED(hr))
        _keyEventSinkAdvised = false;

    return hr;
}

HRESULT TextService::AdviseActiveLanguageProfileNotifySink()
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::AdviseActiveLanguageProfileNotifySink");
    if (!_threadMgr)
        return E_UNEXPECTED;

    if (_profileNotifySinkAdvised)
        return S_OK;

    CComPtr<ITfSource> source;
    HRESULT hr = _threadMgr->QueryInterface(IID_ITfSource, (void**)&source);
    if (FAILED(hr))
        return hr;

    hr = source->AdviseSink(
        IID_ITfActiveLanguageProfileNotifySink,
        static_cast<ITfActiveLanguageProfileNotifySink*>(this),
        &_profileNotifySinkCookie);

    if (hr == CONNECT_E_ADVISELIMIT || hr == TF_E_ALREADY_EXISTS)
    {
        _profileNotifySinkAdvised = true;
        return S_OK;
    }

    if (SUCCEEDED(hr))
        _profileNotifySinkAdvised = true;

    return hr;
}

HRESULT TextService::UnadviseActiveLanguageProfileNotifySink()
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::UnadviseActiveLanguageProfileNotifySink");
    if (!_threadMgr || !_profileNotifySinkAdvised || _profileNotifySinkCookie == TF_INVALID_COOKIE)
        return S_OK;

    CComPtr<ITfSource> source;
    HRESULT hr = _threadMgr->QueryInterface(IID_ITfSource, (void**)&source);
    if (FAILED(hr))
        return hr;

    hr = source->UnadviseSink(_profileNotifySinkCookie);
    if (SUCCEEDED(hr))
    {
        _profileNotifySinkAdvised = false;
        _profileNotifySinkCookie = TF_INVALID_COOKIE;
    }

    return hr;
}

HRESULT TextService::OnSetFocus(BOOL fForeground)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::OnSetFocus");
    if (!fForeground)
        ClearInputState();

    return S_OK;
}

HRESULT TextService::OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL activated)
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::OnActivated");
    UNREFERENCED_PARAMETER(guidProfile);

    bool isThisService = IsEqualGUID(clsid, CLSID_MyTextService) != FALSE;
    if ((isThisService && !activated) || (!isThisService && activated))
        ClearInputState();

    return S_OK;
}

HRESULT TextService::OnTestKeyDown(ITfContext*, WPARAM wParam, LPARAM, BOOL* pfEaten)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::OnTestKeyDown");
    if (!pfEaten) {
		log_to_file(LOG_LEVEL_ERROR, "OnTestKeyDown E_POINTER");
        return E_POINTER;
    }
    int candidateIndex = -1;
    bool hasActiveInput = HasActiveInput();
    *pfEaten = IsImeOn() &&
        !HasShortcutModifier() &&
        (IsCodeKey(wParam) ||
         IsPunctuationKey(wParam) ||
         (IsCommitKey(wParam) && hasActiveInput) ||
         (IsCancelKey(wParam) && hasActiveInput) ||
         ((wParam == VK_BACK || wParam == VK_DELETE) && hasActiveInput) ||
         (IsSelectCandidateKey(wParam, candidateIndex) && !_candidates.empty()));
    return S_OK;
}

HRESULT TextService::OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::OnTestKeyUp");
    if (!pfEaten) {
		log_to_file(LOG_LEVEL_ERROR, "OnTestKeyUp E_POINTER");
        return E_POINTER;
    }
    *pfEaten = FALSE;
	log_to_file(LOG_LEVEL_DEBUG, "end TextService::OnTestKeyUp");
    return S_OK;
}

HRESULT TextService::OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL* pfEaten)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::OnKeyUp");
    if (!pfEaten) {
		log_to_file(LOG_LEVEL_ERROR, "OnKeyUp E_POINTER");
        return E_POINTER;
    }
    *pfEaten = FALSE;
	log_to_file(LOG_LEVEL_DEBUG, "end TextService::OnKeyUp");
    return S_OK;
}

HRESULT TextService::OnPreservedKey(ITfContext*, REFGUID, BOOL* pfEaten)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::OnPreservedKey");
    if (!pfEaten) {
		log_to_file(LOG_LEVEL_ERROR, "OnPreservedKey E_POINTER");
        return E_POINTER;
    }
    *pfEaten = FALSE;
	log_to_file(LOG_LEVEL_DEBUG, "end TextService::OnPreservedKey");
    return S_OK;
}

HRESULT TextService::OnKeyDown(ITfContext* context, WPARAM wParam, LPARAM, BOOL* pfEaten)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::OnKeyDown");
    if (!pfEaten) {
		log_to_file(LOG_LEVEL_ERROR, "OnKeyDown E_POINTER");
        return E_POINTER;
    }
    *pfEaten = FALSE;

    if (!IsImeOn() || !context)
        return S_OK;

    if (HasShortcutModifier())
        return S_OK;

    int candidateIndex = -1;

    if (IsPunctuationKey(wParam))
    {
        *pfEaten = TRUE;
        wchar_t ch = PunctuationKeyToFullWidthChar(wParam);
        return HandlePunctuationInput(context, ch);
    }

    if (IsSelectCandidateKey(wParam, candidateIndex) && !_candidates.empty())
    {
        *pfEaten = TRUE;
        return HandleSelectCandidate(context, candidateIndex);
    }

    if (IsCodeKey(wParam))
    {
        *pfEaten = TRUE;
        wchar_t ch = CodeKeyToChar(wParam);
        return HandleCodeInput(context, ch);
    }

    if (wParam == VK_BACK && HasActiveInput())
    {
        *pfEaten = TRUE;
        return HandleBackspace(context);
    }

    if (wParam == VK_DELETE && HasActiveInput())
    {
        *pfEaten = TRUE;
        return HandleDelete(context);
    }

    if (IsCommitKey(wParam) && HasActiveInput())
    {
        *pfEaten = TRUE;
        return HandleCommit(context);
    }

    if (IsCancelKey(wParam) && HasActiveInput())
    {
        *pfEaten = TRUE;
        return HandleCancel(context);
    }

    return S_OK;
}

HRESULT TextService::HandleSelectCandidate(ITfContext* context, int index)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::HandleSelectCandidate");
    if (index < 0 || static_cast<size_t>(index) >= _candidates.size())
        return S_OK;

    _selectedIndex = static_cast<size_t>(index);
    return HandleCommit(context);
}

HRESULT TextService::HandleCommit(ITfContext* context)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::HandleCommit");
    if (_readingBuffer.empty() && _candidates.empty())
        return S_OK;

    std::wstring commitText;

    if (!_candidates.empty() && _selectedIndex < _candidates.size())
        commitText = _candidates[_selectedIndex].text;
    else
        commitText = _readingBuffer;

    HRESULT replaceHr = S_OK;
    if (!_displayText.empty())
    {
        replaceHr = ReplaceDisplayedText(context, commitText);
    }
    else
    {
        replaceHr = InsertTextAtSelection(context, commitText);
    }

    if (FAILED(replaceHr))
    {
        log_to_file(LOG_LEVEL_ERROR, "TextService::HandleCommit failed to commit text");
        return replaceHr;
    }

    _displayRange.Release();
    RememberCommittedText(commitText);

    _readingBuffer.clear();
    _displayText.clear();
    _candidates.clear();
    _selectedIndex = 0;
    HRESULT endHr = EndComposition(context);

    RefreshAssociatedWordCandidates();
    UpdateCandidateWindow(context);

    return endHr;
}

HRESULT TextService::HandleCodeInput(ITfContext* context, wchar_t ch)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::HandleCodeInput");
    if (_readingBuffer.empty() && !_candidates.empty())
    {
        _candidates.clear();
        _selectedIndex = 0;
    }

    _readingBuffer.push_back(ch);
    RefreshCandidates();

    if (!_composition)
        StartComposition(context);

    HRESULT hr = ReplaceDisplayedText(context, BuildDisplayText());
    if (SUCCEEDED(hr))
        UpdateCandidateWindow(context);

    return hr;
}

HRESULT TextService::HandlePunctuationInput(ITfContext* context, wchar_t ch)
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::HandlePunctuationInput");
    if (!context || ch == L'\0')
        return S_OK;

    if (HasActiveInput())
    {
        HRESULT commitHr = HandleCommit(context);
        if (FAILED(commitHr))
            return commitHr;
    }

    std::wstring punctuation(1, ch);
    HRESULT hr = InsertTextAtSelection(context, punctuation);
    if (FAILED(hr))
        return hr;

    RememberCommittedText(punctuation);
    _candidates.clear();
    _selectedIndex = 0;
    RefreshAssociatedWordCandidates();
    UpdateCandidateWindow(context);

    return hr;
}

HRESULT TextService::HandleBackspace(ITfContext* context)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::HandleBackspace");
    if (_readingBuffer.empty())
    {
        if (!HasActiveInput())
            return S_OK;

        return HandleCancel(context);
    }

    _readingBuffer.pop_back();
    RefreshCandidates();

    if (_readingBuffer.empty())
    {
        HRESULT hr = ReplaceDisplayedText(context, L"");
        if (FAILED(hr))
            return hr;
        _displayRange.Release();
        HideCandidateWindow();
        return EndComposition(context);
    }

    HRESULT hr = ReplaceDisplayedText(context, BuildDisplayText());
    if (SUCCEEDED(hr))
        UpdateCandidateWindow(context);

    return hr;
}

HRESULT TextService::HandleDelete(ITfContext* context)
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::HandleDelete");
    if (!HasActiveInput())
        return S_OK;

    return HandleCancel(context);
}

HRESULT TextService::HandleCancel(ITfContext* context)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::HandleCancel");
    _readingBuffer.clear();
    _candidates.clear();
    _selectedIndex = 0;
    HideCandidateWindow();
    HRESULT hr = ReplaceDisplayedText(context, L"");
    if (FAILED(hr))
        return hr;
    _displayRange.Release();
    return EndComposition(context);
}

std::wstring TextService::BuildDisplayText() const
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::BuildDisplayText");
    return _readingBuffer;
}

std::wstring TextService::BuildCandidateWindowText() const
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::BuildCandidateWindowText");
    if (!_candidates.empty())
    {
        std::wstring displayText;

        for (size_t i = 0; i < _candidates.size(); ++i)
        {
            if (i > 0)
                displayText += L"  ";

            displayText += std::to_wstring((i + 1) % 10);
            displayText += L".";
            displayText += _candidates[i].text;
        }

        return displayText;
    }

    return L"";
}

void TextService::RefreshCandidates()
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::RefreshCandidates");
    _candidates.clear();
    _selectedIndex = 0;

    if (_readingBuffer.empty())
        return;

    if (_engine)
        _candidates = _engine->LookupByCode(_readingBuffer);
}

void TextService::RefreshAssociatedWordCandidates()
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::RefreshAssociatedWordCandidates");
    _candidates.clear();
    _selectedIndex = 0;

    if (_committedText.empty())
        return;

    if (_engine)
        _candidates = _engine->LookupAssociatedWords(_committedText, 0, CandidatePageSize);
}

void TextService::RememberCommittedText(const std::wstring& text)
{
    if (text.empty())
        return;

    _committedText += text;
    int leadingMaxLength = _engine ? _engine->GetAssociatedLeadingMaxLength() : 3;
    if (leadingMaxLength > 0 && _committedText.size() > static_cast<size_t>(leadingMaxLength))
        _committedText.erase(0, _committedText.size() - static_cast<size_t>(leadingMaxLength));
}

void TextService::ClearInputState()
{
    _readingBuffer.clear();
    _displayText.clear();
    _candidateWindowText.clear();
    _candidates.clear();
    _selectedIndex = 0;
    _displayRange.Release();
    _composition.Release();
    HideCandidateWindow();
}

HRESULT TextService::StartComposition(ITfContext* context)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::StartComposition");
    if (_composition || !context)
        return S_OK;

    if (_clientId == TF_CLIENTID_NULL)
        return E_UNEXPECTED;

    StartCompositionEditSession* editSession = new (std::nothrow) StartCompositionEditSession(context);
    if (!editSession)
        return E_OUTOFMEMORY;

    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = context->RequestEditSession(
        _clientId,
        editSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &editSessionResult);

    if (SUCCEEDED(hr))
        hr = editSessionResult;

    if (SUCCEEDED(hr))
    {
        _composition.Release();
        _displayRange.Release();
        editSession->CopyComposition(&_composition);
        editSession->CopyRange(&_displayRange);
    }

    editSession->Release();

    return hr;
}

HRESULT TextService::UpdateCompositionText(ITfContext* context, const std::wstring& text)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::UpdateCompositionText");
    if (!context || _clientId == TF_CLIENTID_NULL)
        return S_OK;

    SetSelectionTextEditSession* editSession = new (std::nothrow) SetSelectionTextEditSession(context, text, 0, nullptr, _composition);
    if (!editSession)
        return E_OUTOFMEMORY;

    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = context->RequestEditSession(
        _clientId,
        editSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &editSessionResult);

    if (SUCCEEDED(hr))
        hr = editSessionResult;

    editSession->Release();
    return hr;
}

HRESULT TextService::ReplaceDisplayedText(ITfContext* context, const std::wstring& text)
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::ReplaceDisplayedText");
    if (!context || _clientId == TF_CLIENTID_NULL)
        return S_OK;

    LONG replaceLength = static_cast<LONG>(_displayText.size());
    SetSelectionTextEditSession* editSession = new (std::nothrow) SetSelectionTextEditSession(context, text, replaceLength, _displayRange, _composition);
    if (!editSession)
        return E_OUTOFMEMORY;

    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = context->RequestEditSession(
        _clientId,
        editSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &editSessionResult);

    if (SUCCEEDED(hr))
        hr = editSessionResult;

    if (SUCCEEDED(hr))
    {
        _displayText = text;
        _displayRange.Release();
        if (!text.empty())
            editSession->CopyUpdatedRange(&_displayRange);
    }

    editSession->Release();

    return hr;
}

bool TextService::EnsureCandidateWindow()
{
    if (_candidateWindow && IsWindow(_candidateWindow))
        return true;

    static ATOM windowClass = 0;
    if (!windowClass)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TextService::CandidateWindowProc;
        wc.hInstance = GetModuleInstance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = CandidateWindowClassName;
        windowClass = RegisterClassExW(&wc);
        if (!windowClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    _candidateWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        CandidateWindowClassName,
        L"",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        GetModuleInstance(),
        this);

    return _candidateWindow != nullptr;
}

void TextService::UpdateCandidateWindow(ITfContext* context)
{
    _candidateWindowText = BuildCandidateWindowText();
    if (_candidateWindowText.empty())
    {
        HideCandidateWindow();
        return;
    }

    if (!EnsureCandidateWindow())
        return;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return;

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(screenDc, font);
    RECT textRect = { 0, 0, 0, 0 };
    DrawTextW(
        screenDc,
        _candidateWindowText.c_str(),
        static_cast<int>(_candidateWindowText.size()),
        &textRect,
        DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(screenDc, oldFont);
    ReleaseDC(nullptr, screenDc);

    int width = (textRect.right - textRect.left) + (CandidateWindowPaddingX * 2);
    int height = (textRect.bottom - textRect.top) + (CandidateWindowPaddingY * 2);
    POINT anchor = {};
    if (!TryGetCandidateWindowAnchor(context, anchor))
        anchor = GetFallbackCandidateWindowAnchor();

    SetWindowPos(
        _candidateWindow,
        HWND_TOPMOST,
        anchor.x,
        anchor.y,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(_candidateWindow, nullptr, TRUE);
}

bool TextService::TryGetCandidateWindowAnchor(ITfContext* context, POINT& anchor) const
{
    if (!context || !_displayRange || _clientId == TF_CLIENTID_NULL)
        return false;

    CandidateAnchorEditSession* editSession = new (std::nothrow) CandidateAnchorEditSession(context, _displayRange);
    if (!editSession)
        return false;

    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = context->RequestEditSession(
        _clientId,
        editSession,
        TF_ES_SYNC | TF_ES_READ,
        &editSessionResult);

    if (SUCCEEDED(hr))
        hr = editSessionResult;

    RECT textExt = editSession->GetRect();
    editSession->Release();

    if (FAILED(hr) || IsRectEmpty(&textExt))
        return false;

    anchor.x = textExt.left;
    anchor.y = textExt.bottom + 4;
    return true;
}

void TextService::HideCandidateWindow()
{
    _candidateWindowText.clear();
    if (_candidateWindow && IsWindow(_candidateWindow))
        ShowWindow(_candidateWindow, SW_HIDE);
}

LRESULT CALLBACK TextService::CandidateWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    TextService* service = reinterpret_cast<TextService*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        service = reinterpret_cast<TextService*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(service));
    }

    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect = {};
        GetClientRect(hwnd, &rect);

        HBRUSH backgroundBrush = CreateSolidBrush(CandidateWindowBackground);
        FillRect(dc, &rect, backgroundBrush);
        DeleteObject(backgroundBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, CandidateWindowBorder);
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);

        if (service)
        {
            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, CandidateWindowText);

            RECT textRect = rect;
            textRect.left += CandidateWindowPaddingX;
            textRect.right -= CandidateWindowPaddingX;
            textRect.top += CandidateWindowPaddingY;
            textRect.bottom -= CandidateWindowPaddingY;

            DrawTextW(
                dc,
                service->_candidateWindowText.c_str(),
                static_cast<int>(service->_candidateWindowText.size()),
                &textRect,
                DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, oldFont);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HRESULT TextService::InsertTextAtSelection(ITfContext* context, const std::wstring& text)
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::InsertTextAtSelection");
    if (!context || text.empty() || _clientId == TF_CLIENTID_NULL)
        return S_OK;

    InsertTextEditSession* editSession = new (std::nothrow) InsertTextEditSession(context, text);
    if (!editSession)
        return E_OUTOFMEMORY;

    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = context->RequestEditSession(
        _clientId,
        editSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &editSessionResult);

    if (SUCCEEDED(hr))
        hr = editSessionResult;

    editSession->Release();
    return hr;
}

HRESULT TextService::EndComposition(ITfContext* context)
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::EndComposition");
    if (!_composition)
        return S_OK;

    if (!context || _clientId == TF_CLIENTID_NULL)
    {
        _composition.Release();
        return S_OK;
    }

    EndCompositionEditSession* editSession = new (std::nothrow) EndCompositionEditSession(_composition);
    if (!editSession)
        return E_OUTOFMEMORY;

    HRESULT editSessionResult = E_FAIL;
    HRESULT hr = context->RequestEditSession(
        _clientId,
        editSession,
        TF_ES_SYNC | TF_ES_READWRITE,
        &editSessionResult);

    if (SUCCEEDED(hr))
        hr = editSessionResult;

    editSession->Release();
    _composition.Release();
    return hr;
}

bool TextService::IsImeOn() const
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::IsImeOn");
    return true;
}

bool TextService::IsCodeKey(WPARAM vk) const
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::IsCodeKey");
    return (vk >= 'A' && vk <= 'Z') ||
           (IsShiftDown() && vk == '5') ||
           (IsShiftDown() && vk == '8') ||
           (IsShiftDown() && vk == VK_OEM_2);
}

wchar_t TextService::CodeKeyToChar(WPARAM vk) const
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::CodeKeyToChar");
    if (vk >= 'A' && vk <= 'Z')
        return static_cast<wchar_t>(towlower(static_cast<wchar_t>(vk)));

    /*if (IsShiftDown() && vk == '5')
        return L'%';*/

    if (IsShiftDown() && vk == '8')
        return L'*';

    if (IsShiftDown() && vk == VK_OEM_2)
        return L'?';

    return static_cast<wchar_t>(vk);
}

bool TextService::IsPunctuationKey(WPARAM vk) const
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::IsPunctuationKey");
    bool shiftDown = IsShiftDown();

    if (!shiftDown && (vk == VK_OEM_COMMA || vk == VK_OEM_PERIOD ||
        vk == VK_OEM_1 || vk == VK_OEM_2 || vk == VK_OEM_7))
        return true;

    return shiftDown && (vk == '1' || vk == '9' || vk == '0' ||
        vk == VK_OEM_1 || vk == VK_OEM_2 || vk == VK_OEM_7);
}

wchar_t TextService::PunctuationKeyToFullWidthChar(WPARAM vk) const
{
    log_to_file(LOG_LEVEL_DEBUG, "TextService::PunctuationKeyToFullWidthChar");
    bool shiftDown = IsShiftDown();

    if (!shiftDown)
    {
        switch (vk)
        {
        case VK_OEM_COMMA:
            return L'\uFF0C';
        case VK_OEM_PERIOD:
            return L'\u3002';
        case VK_OEM_1:
            return L'\uFF1B';
        case VK_OEM_2:
            return L'\u3001';
        case VK_OEM_7:
            return L'\uFF07';
        default:
            break;
        }
    }

    switch (vk)
    {
    case '1':
        return L'\uFF01';
    case '9':
        return L'\uFF08';
    case '0':
        return L'\uFF09';
    case VK_OEM_1:
        return L'\uFF1A';
    case VK_OEM_2:
        return L'\uFF1F';
    case VK_OEM_7:
        return L'\uFF02';
    default:
        return L'\0';
    }
}

bool TextService::HasShortcutModifier() const
{
    return (GetKeyState(VK_CONTROL) & 0x8000) ||
           (GetKeyState(VK_MENU) & 0x8000) ||
           (GetKeyState(VK_LWIN) & 0x8000) ||
           (GetKeyState(VK_RWIN) & 0x8000);
}

bool TextService::IsShiftDown() const
{
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool TextService::HasActiveInput() const
{
    return !_readingBuffer.empty() || !_displayText.empty() || !_candidates.empty();
}

bool TextService::IsCommitKey(WPARAM vk) const
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::IsCommitKey");
    return vk == VK_SPACE || vk == VK_RETURN;
}

bool TextService::IsCancelKey(WPARAM vk) const
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::IsCancelKey");
    return vk == VK_ESCAPE;
}

bool TextService::IsSelectCandidateKey(WPARAM vk, int& index) const
{
	log_to_file(LOG_LEVEL_DEBUG, "TextService::IsSelectCandidateKey");
    if (IsShiftDown())
        return false;

    if (vk >= '1' && vk <= '9')
    {
        index = static_cast<int>(vk - '1');
        return true;
    }
    if (vk == '0')
    {
        index = 9;
        return true;
    }
    return false;
}
