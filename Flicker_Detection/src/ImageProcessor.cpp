// Optimized ImageProcessor.cpp
#include "ImageProcessor.h"
#include "ssim_gpu.h"
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <iostream>
#include <velocity/grtv_api.h>
#include "DebugLog.h"
#include <thread>
#include <mutex>
#include <map>
#include <chrono>
#include "ImageDisplayTimeout.h"
cv::Mat gray;

// SSIM wrapper
float ImageProcessor::ssimGPU(const cv::Mat &img1, const cv::Mat &img2)
{
    return ::ssimGPU(img1, img2); // ssim_gpu.h/cpp fonksiyonunu çağırır
}

// Convert raw RGBA data to BGR (0xRRGGBB formatted data)
void ImageProcessor::ConvertRawDataToBgr(UINT32 *rawData, int width, int height, cv::Mat &outBgr)
{
    // belleği güvenli kopyalayalım
    cv::Mat tmp(height, width, CV_8UC4, rawData);

    cv::cvtColor(tmp, outBgr, cv::COLOR_RGBA2RGB);
}
// Convert raw RGBA data to GRAY
void ImageProcessor::ConvertRawDataToGray(UINT32 *rawData, int width, int height, cv::Mat &outGray)
{
    cv::Mat tmp(height, width, CV_8UC4, rawData);
    cv::cvtColor(tmp, outGray, cv::COLOR_RGBA2GRAY);
}

// SSIM-based flicker detection
void ImageProcessor::calculateSSIM(const cv::Mat &bgrImage,
                                   const std::string &outputPath,
                                   int *frameCount,
                                   int *errorFrameCount,
                                   cv::Mat &previousGray,
                                   cv::Mat &currentGray,
                                   bool *firstFrame,
                                   float *scorePrevious,
                                   const std::string &card,
                                   const std::string &channel)
{
    cv::Mat gray;
    cv::cvtColor(bgrImage, gray, cv::COLOR_BGR2GRAY); 

    if (*firstFrame)
    {
        previousGray = gray.clone(); 
        *firstFrame = false;
    }
    else
    {
        currentGray = gray;
        float scoreCurrent = ssimGPU(previousGray, currentGray);  

        float low = *scorePrevious - 0.02f;
        float high = *scorePrevious + 0.02f;

        if (scoreCurrent < 0.96f && (scoreCurrent < low || scoreCurrent > high))
        {
            // Kaliteli ama küçük boyutlu JPEG çıktısı için parametre ayarla
            std::vector<int> compression_params = {
                cv::IMWRITE_JPEG_QUALITY, 60 // 0-100, düşürürsen dosya küçülür
            };

            std::string errorFramePath = outputPath + "/Frame_" + std::to_string(*frameCount) + ".jpg";
            cv::imwrite(errorFramePath, bgrImage);

            (*errorFrameCount)++;

            LOG_ERROR(" Flicker detected. Card: " << card
                                                  << ", Channel: " << channel
                                                  << ", Frame: " << *frameCount
                                                  << ", SSIM Score: " << scoreCurrent
                                                  << ", Low: " << low
                                                  << ", High: " << high);
        }
        else
        {
            std::swap(previousGray, currentGray);
        }

        *scorePrevious = scoreCurrent;
        (*frameCount)++;
    }
}



Card stringToCard(const std::string &str)
{

    if (str == "1")
        return CARD_1;
    if (str == "2")
        return CARD_2;
    throw std::invalid_argument("Invalid card string: " + str);
}
Channel stringToChannel(const std::string &str)
{

    if (str == "1")
        return CH_1;
    if (str == "2")
        return CH_2;
    throw std::invalid_argument("Invalid channel string: " + str);
}

#include <opencv2/opencv.hpp>
#include <mutex>
#include <map>

void ImageProcessor::setDisplayAndShow(cv::Mat &img, int display, std::string card,
                                       std::string channel, int x, int y, bool *isFirst)
{
    const std::string windowName = "Card " + card + " Channel " + channel;
    const int targetWidth = 640;
    const int targetHeight = 480;

    // Her pencere ismi için bir kez initialize edilmesini sağlamak için static once_flag map
    static std::map<std::string, std::once_flag> windowInitFlags;

    // Pencereyi sadece bir kere oluştur ve ayarla
    std::call_once(windowInitFlags[windowName], [&]() {
        setenv("DISPLAY", ":0", 0); // DISPLAY değişkeni, X11 ortamı için gerekli
        try {
            cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
            cv::resizeWindow(windowName, targetWidth, targetHeight);
            cv::moveWindow(windowName, x, y);
        } catch (const cv::Exception& e) {
            std::cerr << "[ERROR] Failed to create window '" << windowName
                      << "': " << e.what() << std::endl;
        }
    });

    try {
        cv::Mat resized;
        cv::resize(img, resized, cv::Size(targetWidth, targetHeight));
        cv::imshow(windowName, resized);
        cv::waitKey(1); // GUI olaylarını işler
    } catch (const cv::Exception& e) {
        std::cerr << "[ERROR] Failed to display in window '" << windowName
                  << "': " << e.what() << std::endl;
    }
}


