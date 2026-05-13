

const int WIDTH = 1280;
const int HEIGHT = 1024;

#include "VideoUtils.h"
#include "Helpers.h"

FILE *startFFmpegWriter(const std::string &videoFile, int width, int height, int fps)
{
    std::ostringstream ffmpegCmd;
    ffmpegCmd << "ffmpeg -y "
              << "-loglevel error "
              << "-f rawvideo -pix_fmt bgr24 -s "
              << width << "x" << height << " "
              << "-r " << fps << " -i - "
              << "-c:v libx264 -preset ultrafast -tune zerolatency -b:v 10M \"" << videoFile << "\""
              << " 2>>/tmp/ffmpeg_dvi.log";

    FILE *pipe = popen(ffmpegCmd.str().c_str(), "w");
    if (!pipe)
    {
        LOG_ERROR("Failed to start FFmpeg for file: " << videoFile);
        return nullptr;
    }

    return pipe;
}

uint8_t writeFFmpegFrame(FILE *ffmpeg_pipe, const cv::Mat &current_frame)
{
    const size_t expectedFrameSize = WIDTH * HEIGHT * 3;
    size_t written = fwrite(current_frame.data, 1, expectedFrameSize, ffmpeg_pipe);
    if (written != expectedFrameSize)
    {
        LOG_ERROR("Incomplete frame written to ffmpeg pipe.");
        return CODE_ERROR;
    }
    return CODE_SUCCESS;
}