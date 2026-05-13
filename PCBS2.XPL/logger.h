#pragma once
#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    static void Init(const std::string& dir) {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_file.open(dir + "PCBS2.XPL.log");
        if (s_file.is_open()) {
            s_file << "========================================" << std::endl;
            s_file << "PCBS2.XPL v1.0.0 - XML Part Loader" << std::endl;
            s_file << "========================================" << std::endl;
            s_file.flush();
        }
    }

    static void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_file.is_open()) { s_file << msg << std::endl; s_file.flush(); }
    }

    static void Close() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_file.is_open()) s_file.close();
    }

private:
    static std::ofstream s_file;
    static std::mutex    s_mutex;
};