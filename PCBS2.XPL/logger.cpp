#include "logger.h"

HANDLE  Logger::s_file = INVALID_HANDLE_VALUE;
SRWLOCK Logger::s_lock = SRWLOCK_INIT;