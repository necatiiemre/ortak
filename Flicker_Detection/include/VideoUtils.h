
using namespace std;

#include "Helpers.h"
#include <cstdio>
#include <cstdint>
#include <opencv2/opencv.hpp>

FILE* startFFmpegWriter(const string& videoFile, int width, int height, int fps = 60);
uint8_t writeFFmpegFrame(FILE *ffmpeg_pipe, const cv::Mat &current_frame);