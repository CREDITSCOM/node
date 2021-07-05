#pragma once

#ifdef _WIN32
#include "win_service.hpp"
#else
#include "unix_service.hpp"
#endif
