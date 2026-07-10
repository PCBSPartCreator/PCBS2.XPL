#pragma once
#include <windows.h>
#include <string>
#include <cstring>
#include <strsafe.h>

class Logger {
public:
    static void Init(const std::string& dir) { Init(dir.c_str()); }

    static void Init(const char* dir) {
        AcquireSRWLockExclusive(&s_lock);

        if (s_file != INVALID_HANDLE_VALUE) {
            CloseHandle(s_file);
            s_file = INVALID_HANDLE_VALUE;
        }

        char path[MAX_PATH] = {};
        if (dir && *dir)
            StringCchPrintfA(path, MAX_PATH, "%s%s", dir, "PCBS2.XPL.log");
        else
            StringCchCopyA(path, MAX_PATH, "PCBS2.XPL.log");

        s_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (s_file != INVALID_HANDLE_VALUE) {
            WriteRaw("========================================\r\n");
            WriteRaw("PCBS2.XPL v1.0.5 - XML Part Loader\r\n");
            WriteRaw("========================================\r\n");
            FlushFileBuffers(s_file);
        }

        ReleaseSRWLockExclusive(&s_lock);
    }

    static void Log(const std::string& msg) { Log(msg.c_str()); }

    static void Log(const char* msg) {
        if (!msg) return;

        AcquireSRWLockExclusive(&s_lock);
        if (s_file != INVALID_HANDLE_VALUE) {
            WriteRaw(msg);
            WriteRaw("\r\n");
            FlushFileBuffers(s_file);
        }
        ReleaseSRWLockExclusive(&s_lock);
    }

    static void Close() {
        AcquireSRWLockExclusive(&s_lock);
        if (s_file != INVALID_HANDLE_VALUE) {
            CloseHandle(s_file);
            s_file = INVALID_HANDLE_VALUE;
        }
        ReleaseSRWLockExclusive(&s_lock);
    }

private:
    static void WriteRaw(const char* s) {
        if (s_file == INVALID_HANDLE_VALUE || !s) return;
        DWORD w = 0;
        WriteFile(s_file, s, (DWORD)std::strlen(s), &w, nullptr);
    }

    static HANDLE  s_file;
    static SRWLOCK s_lock;
};
