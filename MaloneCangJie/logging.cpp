#include "pch.h"
#include "logging.h"
#include <objbase.h>
#include <cstdio>
#include <ctime>
#include <windows.h>
#include <string>

void log_to_file(int level, const char* message) {
	if (!is_logging_enabled()) return;
    FILE* file = fopen("c:/temp/malone.log", "a"); // "a" for append
    if (file == NULL) return;

    time_t now = time(NULL);
    char* ts = ctime(&now);
    ts[24] = '\0'; // Strip newline

	if (level == LOG_LEVEL_DEBUG) {
        fprintf(file, "[DEBUG] [%s] %s\n", ts, message);
    } else if (level == LOG_LEVEL_INFO) {
        fprintf(file, "[INFO] [%s] %s\n", ts, message);
    } else if (level == LOG_LEVEL_ERROR) {
        fprintf(file, "[ERROR] [%s] %s\n", ts, message);
    } else {
        fprintf(file, "[UNKNOWN] [%s] %s\n", ts, message);
    }

    fclose(file); // Note: For high frequency, keep the file open instead of closing/opening
}

bool is_logging_enabled() {
    static int enabled = -1;
    if (enabled < 0)
    {
        char value[8] = {};
        DWORD length = GetEnvironmentVariableA("MALONECANGJIE_LOG", value, ARRAYSIZE(value));
        enabled = (length > 0 && value[0] != '0') ? 1 : 0;
    }

    return enabled == 1;
}

void log_guid_to_file(int level, const char* prefix, REFGUID guid) {
    wchar_t guidText[64] = {};
    if (StringFromGUID2(guid, guidText, ARRAYSIZE(guidText)) == 0) {
        log_to_file(level, prefix);
        return;
    }

    char guidTextUtf8[64] = {};
    WideCharToMultiByte(
        CP_UTF8,
        0,
        guidText,
        -1,
        guidTextUtf8,
        ARRAYSIZE(guidTextUtf8),
        nullptr,
        nullptr);

    std::string message(prefix);
    message += " ";
    message += guidTextUtf8;
    log_to_file(level, message.c_str());
}
