#include "logger.h"

std::ofstream Logger::s_file;
std::mutex    Logger::s_mutex;