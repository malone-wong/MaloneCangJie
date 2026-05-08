#include "pch.h"
#include "DictionaryEngine.h"
#include "logging.h"
#include <algorithm>
#include <cwctype>

namespace
{
    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty())
            return {};

        int size = WideCharToMultiByte(
            CP_UTF8,
            0,
            text.c_str(),
            static_cast<int>(text.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

        if (size <= 0)
            return {};

        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            text.c_str(),
            static_cast<int>(text.size()),
            result.data(),
            size,
            nullptr,
            nullptr);

        return result;
    }
}

DictionaryEngine::DictionaryEngine()
    : _db(nullptr),
      _stmtLookupByCode(nullptr),
      _stmtLookupByWildcard(nullptr),
      _stmtLookupAssociatedWords(nullptr),
      _associatedLeadingMaxLength(3)
{
}

DictionaryEngine::~DictionaryEngine()
{
    Shutdown();
}

bool DictionaryEngine::Initialize(const std::wstring& dbPath)
{
    Shutdown();

    std::string dbPathUtf8 = WideToUtf8(dbPath);
    if (dbPathUtf8.empty())
    {
        log_to_file(LOG_LEVEL_ERROR, "DictionaryEngine::Initialize failed to convert database path");
        return false;
    }

    if (sqlite3_open_v2(dbPathUtf8.c_str(), &_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        log_to_file(LOG_LEVEL_ERROR, "DictionaryEngine::Initialize failed to open database");
        Shutdown();
        return false;
    }

    sqlite3_exec(_db, "PRAGMA query_only = ON; PRAGMA temp_store = MEMORY; PRAGMA cache_size = -32768;", nullptr, nullptr, nullptr);

    const char* lookupByCodeSql =
        "SELECT character, code, usage_count "
        "FROM ( "
        "    SELECT "
        "        m.character, "
        "        m.code, "
        "        c.usage_count, "
        "        m.line_number, "
        "        0 AS match_stage "
        "    FROM ime_cangjie_mapping m "
        "    JOIN ime_character c ON c.character = m.character "
        "    WHERE m.code = ?1 "
        "    UNION ALL "
        "    SELECT "
        "        m.character, "
        "        m.code, "
        "        c.usage_count, "
        "        m.line_number, "
        "        2 AS match_stage "
        "    FROM ime_cangjie_mapping m "
        "    JOIN ime_character c ON c.character = m.character "
        "    WHERE m.code >= ?2 AND m.code < ?3 AND m.code <> ?1 "
        ") "
        "ORDER BY match_stage ASC, usage_count DESC, length(code) ASC, line_number ASC "
        "LIMIT 10;";

    if (sqlite3_prepare_v2(_db, lookupByCodeSql, -1, &_stmtLookupByCode, nullptr) != SQLITE_OK)
    {
        log_to_file(LOG_LEVEL_ERROR, "DictionaryEngine::Initialize failed to prepare lookup statement");
        Shutdown();
        return false;
    }

    const char* lookupByWildcardSql =
        "SELECT m.character, m.code, c.usage_count "
        "FROM ime_cangjie_mapping m "
        "JOIN ime_character c ON c.character = m.character "
        "WHERE m.code GLOB ?1 "
        "ORDER BY usage_count DESC, length(m.code) ASC, m.line_number ASC "
        "LIMIT 10;";

    if (sqlite3_prepare_v2(_db, lookupByWildcardSql, -1, &_stmtLookupByWildcard, nullptr) != SQLITE_OK)
    {
        log_to_file(LOG_LEVEL_ERROR, "DictionaryEngine::Initialize failed to prepare wildcard lookup statement");
        Shutdown();
        return false;
    }

    const char* lookupAssociatedWordsSql =
        "SELECT associated_text, usage_count "
        "FROM ime_character_associated_word "
        "WHERE leading_text = ?1 "
        "ORDER BY usage_count DESC, length(associated_text) DESC, associated_text ASC, id ASC "
        "LIMIT ?2;";

    if (sqlite3_prepare_v2(_db, lookupAssociatedWordsSql, -1, &_stmtLookupAssociatedWords, nullptr) != SQLITE_OK)
    {
        log_to_file(LOG_LEVEL_ERROR, "DictionaryEngine::Initialize failed to prepare associated words statement");
        Shutdown();
        return false;
    }

    const char* leadingMaxLengthSql =
        "SELECT CAST(value AS INTEGER) "
        "FROM ime_metadata "
        "WHERE key = 'associated_leading_max_length';";
    sqlite3_stmt* stmtLeadingMaxLength = nullptr;
    if (sqlite3_prepare_v2(_db, leadingMaxLengthSql, -1, &stmtLeadingMaxLength, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmtLeadingMaxLength) == SQLITE_ROW)
        {
            int leadingMaxLength = sqlite3_column_int(stmtLeadingMaxLength, 0);
            if (leadingMaxLength > 0)
                _associatedLeadingMaxLength = leadingMaxLength;
        }
        sqlite3_finalize(stmtLeadingMaxLength);
    }

    return true;
}

void DictionaryEngine::Shutdown()
{
    if (_stmtLookupAssociatedWords)
    {
        sqlite3_finalize(_stmtLookupAssociatedWords);
        _stmtLookupAssociatedWords = nullptr;
    }

    if (_stmtLookupByCode)
    {
        sqlite3_finalize(_stmtLookupByCode);
        _stmtLookupByCode = nullptr;
    }

    if (_stmtLookupByWildcard)
    {
        sqlite3_finalize(_stmtLookupByWildcard);
        _stmtLookupByWildcard = nullptr;
    }

    if (_db)
    {
        sqlite3_close(_db);
        _db = nullptr;
    }

    _associatedLeadingMaxLength = 3;
}

std::vector<CandidateItem> DictionaryEngine::LookupByCode(const std::wstring& code)
{
    std::vector<CandidateItem> result;
    if (!_stmtLookupByCode)
        return result;

    std::wstring normalizedCode = code;
    std::transform(
        normalizedCode.begin(),
        normalizedCode.end(),
        normalizedCode.begin(),
        [](wchar_t ch)
        {
            /*if (ch == L'%')
                return L'*';*/

            return static_cast<wchar_t>(std::towlower(ch));
        });

    bool hasWildcard =
        normalizedCode.find(L'*') != std::wstring::npos ||
        normalizedCode.find(L'?') != std::wstring::npos;

    sqlite3_stmt* statement = hasWildcard ? _stmtLookupByWildcard : _stmtLookupByCode;
    if (!statement)
        return result;

    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);

    sqlite3_bind_text16(
        statement,
        1,
        normalizedCode.c_str(),
        static_cast<int>(normalizedCode.size() * sizeof(wchar_t)),
        SQLITE_TRANSIENT);

    if (!hasWildcard)
    {
        std::wstring prefixEnd = normalizedCode;
        if (!prefixEnd.empty())
            ++prefixEnd.back();

        sqlite3_bind_text16(
            statement,
            2,
            normalizedCode.c_str(),
            static_cast<int>(normalizedCode.size() * sizeof(wchar_t)),
            SQLITE_TRANSIENT);
        sqlite3_bind_text16(
            statement,
            3,
            prefixEnd.c_str(),
            static_cast<int>(prefixEnd.size() * sizeof(wchar_t)),
            SQLITE_TRANSIENT);
    }

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        const void* text = sqlite3_column_text16(statement, 0);
        const void* codeText = sqlite3_column_text16(statement, 1);
        if (text)
        {
            CandidateItem item;
            item.text = static_cast<const wchar_t*>(text);
            if (codeText)
                item.code = static_cast<const wchar_t*>(codeText);
            item.usageCount = sqlite3_column_int64(statement, 2);
            item.isAssociatedWord = false;
            result.push_back(item);
        }
    }

    return result;
}

std::vector<CandidateItem> DictionaryEngine::LookupAssociatedWords(
    const std::wstring& committedText,
    int leadingMaxLength,
    int pageSize)
{
    std::vector<CandidateItem> result;
    if (leadingMaxLength < 1)
        leadingMaxLength = _associatedLeadingMaxLength;

    if (!_stmtLookupAssociatedWords || committedText.empty() || leadingMaxLength < 1 || pageSize < 1)
        return result;

    int maxContextLength = std::min<int>(leadingMaxLength, static_cast<int>(committedText.size()));

    for (int contextLength = maxContextLength; contextLength >= 1; --contextLength)
    {
        std::wstring context = committedText.substr(committedText.size() - contextLength);

        sqlite3_reset(_stmtLookupAssociatedWords);
        sqlite3_clear_bindings(_stmtLookupAssociatedWords);

        sqlite3_bind_text16(
            _stmtLookupAssociatedWords,
            1,
            context.c_str(),
            static_cast<int>(context.size() * sizeof(wchar_t)),
            SQLITE_TRANSIENT);
        sqlite3_bind_int(_stmtLookupAssociatedWords, 2, pageSize);

        result.clear();
        while (sqlite3_step(_stmtLookupAssociatedWords) == SQLITE_ROW)
        {
            const void* text = sqlite3_column_text16(_stmtLookupAssociatedWords, 0);
            if (text)
            {
                CandidateItem item;
                item.text = static_cast<const wchar_t*>(text);
                item.usageCount = sqlite3_column_int64(_stmtLookupAssociatedWords, 1);
                item.isAssociatedWord = true;
                result.push_back(item);
            }
        }

        if (!result.empty())
            return result;
    }

    return {};
}

int DictionaryEngine::GetAssociatedLeadingMaxLength() const
{
    return _associatedLeadingMaxLength;
}
