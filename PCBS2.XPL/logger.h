#pragma once
#include <windows.h>
#include <mutex>
#include <string>

class Logger {
public:
    static void Init(const std::string& dir) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_file = CreateFileA((dir + "PCBS2.XPL.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (s_file != INVALID_HANDLE_VALUE) {
            Write("========================================\r\n");
            Write("PCBS2.XPL v1.0.2 - XML Part Loader\r\n");
            Write("========================================\r\n");
            FlushFileBuffers(s_file);
        }
    }

    static void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_file != INVALID_HANDLE_VALUE) { Write(msg); Write("\r\n"); FlushFileBuffers(s_file); }
    }

    static void Close() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_file != INVALID_HANDLE_VALUE) { CloseHandle(s_file); s_file = INVALID_HANDLE_VALUE; }
    }

private:
    static void Write(const std::string& s) {
        DWORD w = 0;
        WriteFile(s_file, s.data(), (DWORD)s.size(), &w, nullptr);
    }
    static HANDLE     s_file;
    static std::mutex s_mutex;
};