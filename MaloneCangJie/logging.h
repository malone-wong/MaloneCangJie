#pragma once
#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_ERROR 2

bool is_logging_enabled();
void log_to_file(int level, const char* message);
void log_guid_to_file(int level, const char* prefix, REFGUID guid);
