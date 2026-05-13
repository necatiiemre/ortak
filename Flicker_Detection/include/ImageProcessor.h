#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <opencv2/opencv.hpp>
#include <velocity/grtv_api.h>
#include "DirectoryManager.h"

class ImageProcessor
{
public:
    // Helper function that wraps the global CUDA-based SSIM function for use within the ImageProcessor class
    // @param img1: reference image (previous frame)
    // @param img2: target image (current frame)
    // @return similarity score between the two images (1.0 = identical, 0.0 = different)
    float ssimGPU(const cv::Mat &img1, const cv::Mat &img2);

    // Converts raw 32-bit RGB data into an OpenCV BGR image
    // Each pixel is assumed to be encoded in 0xRRGGBB format
    // @param rawData: pointer to raw 32-bit pixel array (RRGGBB format)
    // @param width: image width in pixels
    // @param height: image height in pixels
    // @return OpenCV BGR image (cv::Mat) with 8-bit 3-channel format
    void ConvertRawDataToBgr(UINT32 *rawData, int width, int height, cv::Mat &outBgr);

    // Converts raw 32-bit RGB data into a grayscale OpenCV image for SSIM processing
    // SSIM works on grayscale images, so the result is a single-channel 8-bit matrix
    // @param rawData: pointer to raw 32-bit pixel array (RRGGBB format)
    // @param width: image width in pixels
    // @param height: image height in pixels
    // @return grayscale image (cv::Mat) in CV_8UC1 format
    void ConvertRawDataToGray(UINT32 *rawData, int width, int height, cv::Mat &outGray);

    // Applies SSIM analysis on incoming frame data and detects flicker based on similarity threshold
    // Saves frames that are visually different from the previous frame based on SSIM score
    // @param pData: raw 32-bit RGB frame data (RRGGBB format)
    // @param outputPath: directory where error frames will be saved
    // @param width: frame width in pixels
    // @param height: frame height in pixels
    // @param frameCount: pointer to the current frame count (incremented each call)
    // @param errorFrameCount: pointer to the error frame count (incremented on flicker)
    // @param previousImgCpu: previous grayscale image (reference frame)
    // @param currentImgCpu: current grayscale image (to be compared)
    // @param firstFrame: pointer to flag indicating first frame (used to initialize previous frame)
    // @param scorePrevious: pointer to previous SSIM score (used for deviation threshold)
    // @param card: identifier string for the capture card
    // @param channel: identifier string for the video channel
    // @return void
    void calculateSSIM(const cv::Mat &bgrImage,
                       const std::string &outputPath,
                       int *frameCount,
                       int *errorFrameCount,
                       cv::Mat &previousImgCpu,
                       cv::Mat &currentImgCpu,
                       bool *firstFrame,
                       float *scorePrevious,
                       const std::string &card,
                       const std::string &channel);

    // Displays the given BGR image on a specific screen region using OpenCV's window system
    // Initializes the window only once and reuses it for subsequent frames
    // @param img: BGR image to be displayed
    // @param display: target display number (used internally with DISPLAY env variable)
    // @param card: identifier for the capture card (used in window title)
    // @param channel: identifier for the channel (used in window title)
    // @param x: horizontal screen position of the window
    // @param y: vertical screen position of the window
    // @param isFirst: pointer to a flag that indicates if this is the first time displaying (creates window only once)
    // @return void
    void setDisplayAndShow( cv::Mat &img, int display, std::string card,
                           std::string channel, int x, int y, bool *isFirst);

private:
};

#endif // IMAGE_PROCESSOR_H
