#include "logger.h"

HANDLE     Logger::s_file = INVALID_HANDLE_VALUE;
std::mutex Logger::s_mutex;