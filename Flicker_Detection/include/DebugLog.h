#pragma once

#include <iostream>

#define ERROR_LOG(x) std::cerr << x << std::endl
#define LOG_ERROR(x) ERROR_LOG("[ERROR] " << x)

#ifdef DEBUG
	#define DEBUG_LOG(x) std::cout << x << std::endl
	#define LOG_INFO(x)  DEBUG_LOG("[INFO] " << x)
	#define LOG_WARN(x)  DEBUG_LOG("[WARN] " << x)
	#define LOG_TRACE(x) DEBUG_LOG("[TRACE] " << __FILE__ << ":" << __LINE__ << " - " << x)
#else
	#define DEBUG_LOG(x)
	#define LOG_INFO(x)
	#define LOG_WARN(x)
	#define LOG_TRACE(x)
#endif
