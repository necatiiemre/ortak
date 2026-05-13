#pragma once

#include <cstdint>
#include <pthread.h>
#include <thread>
#include <iostream>
#include <sstream>
#include <limits>
#include <chrono>
#include <unistd.h>
#include <string>
#include <filesystem>
#include <sys/stat.h> //for chmod permission
#include <sys/types.h>
#include <iomanip>
#include <ctime>
#include <map>
#include <atomic>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <opencv2/opencv.hpp>
#include <linux/videodev2.h>
#include <velocity/grtv_api.h>
#include <vector>
#include <cstdio>
#include <regex>
#include <optional>
#include <cstdio>   
#include <cstdint> 
#include "ErrorUtils.h"
#include "DebugLog.h" 

uint8_t drawDviStats(cv::Mat &current_frame, std::string time, int fps, int error_frame_counter, int frame_counter, float temperature);
