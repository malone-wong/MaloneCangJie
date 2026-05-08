#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include "sqlite3.h"

struct CandidateItem
{
    std::wstring text;
    std::wstring code;
    long long usageCount = 0;
    bool isAssociatedWord = false;
};

class DictionaryEngine
{
public:
    DictionaryEngine();
    ~DictionaryEngine();

    bool Initialize(const std::wstring& dbPath);
    void Shutdown();

    std::vector<CandidateItem> LookupByCode(const std::wstring& code);
    std::vector<CandidateItem> LookupAssociatedWords(const std::wstring& committedText, int leadingMaxLength = 0, int pageSize = 10);
    int GetAssociatedLeadingMaxLength() const;

private:
    sqlite3* _db;
    sqlite3_stmt* _stmtLookupByCode;
    sqlite3_stmt* _stmtLookupByWildcard;
    sqlite3_stmt* _stmtLookupAssociatedWords;
    int _associatedLeadingMaxLength;
};
