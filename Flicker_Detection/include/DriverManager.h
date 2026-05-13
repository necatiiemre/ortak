#ifndef DRIVER_MANAGER_H
#define DRIVER_MANAGER_H

#include <iostream>
#include <optional>
#include <velocity/grtv_api.h>
#include <opencv2/opencv.hpp>
#include "DirectoryManager.h"
#include "Resolution.h"
#include "ImageProcessor.h"
#include <unistd.h> // usleep için
#include <sys/stat.h>
#include <stdexcept>
#include <system_error>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <thread>
#include "ThreadSafeQueue.h"

struct CardProperties
{
    int width;
    int height;
    int link_rate;
    int *frameCount_Ch1;
    int *frameCount_Ch2;
    int *errorFrameCount_Ch1;
    int *errorFrameCount_Ch2;
    bool firstFrame_Ch1;
    bool firstFrame_Ch2;
    float scorePrevious_ch1;
    float scorePrevious_ch2;
    bool *isFirst_channel1;
    bool *isFirst_channel2;
    UINT32 err_b810b_ch1 = 0;
    UINT32 err_b810b_ch2 = 0;
    UINT32 err_crc_ch1 = 0;
    UINT32 err_crc_ch2 = 0;
};

class DriverManager
{
public:
    DriverManager();
    ~DriverManager();

    uint8_t initializeDriver(Card card_1, Channel channel_1,
                             std::optional<Card> card_2 = std::nullopt,
                             std::optional<Channel> channel_2 = std::nullopt);

    uint8_t closeDrivers();
    uint8_t cleanup();
    uint8_t setFlowControl(bool enable);
    uint8_t startVideoCapture();
    uint8_t stopVideoCapture();
    uint8_t resetStatistics(Card card);
    uint8_t resetFrameCounters(Card card);
    uint8_t printStatistics();
    uint8_t closeCard(UINT_PTR &driver, void *mem1, void *mem2, const std::string &cardName);
    uint8_t stopFlickerDetection();
    uint8_t stopCard(Card card, Channel channel);
    uint8_t releaseFpsCounter(Card card, Channel channel);
    uint8_t restartCard(Card card, Channel channel);
    uint8_t configureDriver(UINT_PTR &driver, bool enable);
    uint8_t printVersionInfo(UINT_PTR &driver);
    uint8_t checkCardResolution(UINT_PTR &driver, const std::string &cardName);
    uint8_t configureRouting(UINT_PTR &driver);
    uint8_t allocateMemoryBuffers(UINT_PTR &driver, Card card, Channel channel);
    uint8_t deAllocateMemoryBuffers(UINT_PTR &driver, Card card, Channel channel);
    uint8_t initializeCardProperties(CardProperties &props);
    uint8_t initializeVideoWriters(bool isLoopback);
    uint8_t releaseResources();
    uint8_t videoWritersRelease(Card card);
    uint8_t getCurrentTimestamp(std::string &time);
    uint8_t cardSelfTest(UINT_PTR &driver, int options);
    uint8_t pciControl(UINT_PTR &driver);
    uint8_t startTestLoopback();

    void threadWorker_Card1_CH1();
    void threadWorker_Card1_CH2();
    void threadWorker_Card2_CH1();
    void threadWorker_Card2_CH2();
    void handleCard1Channel1(UINT32 *data);
    void handleCard1Channel2(UINT32 *data);
    void handleCard2Channel1(UINT32 *data);
    void handleCard2Channel2(UINT32 *data);

    // Thread management functions
    void startCard1Channel1Thread();
    void startCard1Channel2Thread();
    void startCard2Channel1Thread();
    void startCard2Channel2Thread();
    void stopCard1Channel1Thread();
    void stopCard1Channel2Thread();
    void stopCard2Channel1Thread();
    void stopCard2Channel2Thread();

    std::once_flag start_threads_card1_channel1_flag;
    std::once_flag start_threads_card2_channel1_flag;
    std::once_flag start_threads_card1_channel2_flag;
    std::once_flag start_threads_card2_channel2_flag;

    std::chrono::steady_clock::time_point card1StartTime;
    std::chrono::steady_clock::time_point card2StartTime;

    std::chrono::steady_clock::time_point card2_ch2_time;
    std::chrono::steady_clock::time_point card2_ch1_time;
    std::chrono::steady_clock::time_point card1_ch2_time;
    std::chrono::steady_clock::time_point card1_ch1_time;

    std::chrono::steady_clock::time_point card2_ch2_time_test_start;
    std::chrono::steady_clock::time_point card2_ch1_time_test_start;
    std::chrono::steady_clock::time_point card1_ch2_time_test_start;
    std::chrono::steady_clock::time_point card1_ch1_time_test_start;

    std::chrono::steady_clock::time_point card2_ch2_time_test_end;
    std::chrono::steady_clock::time_point card2_ch1_time_test_end;
    std::chrono::steady_clock::time_point card1_ch2_time_test_end;
    std::chrono::steady_clock::time_point card1_ch1_time_test_end;

    bool card1Running;
    bool card2Running;

    UINT_PTR First_Driver;
    UINT_PTR Second_Driver;
    UINT32 *gpMem1_1, *gpMem1_2, *gpMem2_1, *gpMem2_2;
    UINT32 gMemSize;
    UINT32 gAsyncCount_1, gAsyncCount_2;
    UINT32 err_crc_card1_ch1;
    UINT32 err_8b10b_card1_ch1;
    UINT32 err_crc_card1_ch2;
    UINT32 err_8b10b_card1_ch2;
    UINT32 err_crc_card2_ch1;
    UINT32 err_8b10b_card2_ch1;
    UINT32 err_crc_card2_ch2;
    UINT32 err_8b10b_card2_ch2;

    bool *card1_ch1;
    bool *card1_ch2;
    bool *card2_ch1;
    bool *card2_ch2;

    // Card Properties struct
    CardProperties card1Props;
    CardProperties card2Props;

    // Card properties
    int width_card1, height_card1, link_rate_card1;
    int width_card2, height_card2, link_rate_card2;

    // Card 1 Variables
    int *frameCount_card1_Ch1;
    int *frameCount_card1_Ch2;
    int frameCountFps;
    int *errorFrameCount_card1_Ch1;
    int *errorFrameCount_card1_Ch2;
    float scorePrevious_card1_ch1;
    float scorePrevious_card1_ch2;
    bool *isFirst_card1_channel1;
    bool *isFirst_card1_channel2;
    bool firstFrame_card1_Ch1;
    bool firstFrame_card1_Ch2;
    float fps_card1_ch1;
    float fps_card1_ch2;

    // Card 2 Variables
    int *frameCount_card2_Ch1;
    int *frameCount_card2_Ch2;
    int *errorFrameCount_card2_Ch1;
    int *errorFrameCount_card2_Ch2;
    float scorePrevious_card2_ch1;
    float scorePrevious_card2_ch2;
    bool *isFirst_card2_channel1;
    bool *isFirst_card2_channel2;
    bool firstFrame_card2_Ch1;
    bool firstFrame_card2_Ch2;
    float fps_card2_ch1;
    float fps_card2_ch2;

    // IMage Processing variables
    cv::Mat currentImgCpu_card1_ch1;
    cv::Mat previousImgCpu_card1_ch1;
    cv::Mat currentImgCpu_card1_ch2;
    cv::Mat previousImgCpu_card1_ch2;
    cv::Mat currentImgCpu_card2_ch1;
    cv::Mat previousImgCpu_card2_ch1;
    cv::Mat currentImgCpu_card2_ch2;
    cv::Mat previousImgCpu_card2_ch2;

    // Video writers
    cv::VideoWriter video_card1_ch1;
    cv::VideoWriter video_card1_ch2;
    cv::VideoWriter video_card2_ch1;
    cv::VideoWriter video_card2_ch2;

    // String variables for card channel
    std::string channel_1;
    std::string channel_2;
    std::string card_1;
    std::string card_2;
    double scale_down_width;
    double scale_down_height;

    int counter_card1_ch1 = 0;
    int counter_card1_ch2 = 0;
    int counter_card2_ch1 = 0;
    int counter_card2_ch2 = 0;

    std::once_flag affinity_flag_card1;
    std::once_flag affinity_flag_card2;

    cv::Mat bgrImageVideo_card1_ch1;
    cv::Mat bgrImageVideo_card1_ch2;
    cv::Mat bgrImageVideo_card2_ch1;
    cv::Mat bgrImageVideo_card2_ch2;

    cv::Mat resized_img_card2_ch2;
    cv::Mat resized_img_card2_ch1;
    cv::Mat resized_img_card1_ch2;
    cv::Mat resized_img_card1_ch1;

    // Callback functions
    static UINT32 UserCallBack_Card1(UINT32 u32Param);
    static UINT32 UserCallBack_Card2(UINT32 u32Param);

    bool isRunning;

    std::unique_ptr<ThreadSafeQueue<std::unique_ptr<cv::Mat>>> display_queue_card1_ch1;
    std::unique_ptr<ThreadSafeQueue<std::unique_ptr<cv::Mat>>> display_queue_card1_ch2;
    std::unique_ptr<ThreadSafeQueue<std::unique_ptr<cv::Mat>>> display_queue_card2_ch1;
    std::unique_ptr<ThreadSafeQueue<std::unique_ptr<cv::Mat>>> display_queue_card2_ch2;

    void display_worker_card1_ch1();
    void startDisplayThreadCard1Ch1();
    void stopDisplayThreadCard1Ch1();

    void display_worker_card1_ch2();
    void startDisplayThreadCard1Ch2();
    void stopDisplayThreadCard1Ch2();

    void display_worker_card2_ch1();
    void startDisplayThreadCard2Ch1();
    void stopDisplayThreadCard2Ch1();

    void display_worker_card2_ch2();
    void startDisplayThreadCard2Ch2();
    void stopDisplayThreadCard2Ch2();

    std::thread display_card1_ch1_thread;
    std::thread display_card1_ch2_thread;
    std::thread display_card2_ch1_thread;
    std::thread display_card2_ch2_thread;

    bool stopRequested_card1_ch1 = false;
    bool stopRequested_card1_ch2 = false;
    bool stopRequested_card2_ch1 = false;
    bool stopRequested_card2_ch2 = false;

    bool fpsBelow10_card1_ch1 = false;
    bool fpsBelow10_card1_ch2 = false;
    bool fpsBelow10_card2_ch1 = false;
    bool fpsBelow10_card2_ch2 = false;

    bool oneMinuteResetDone_card1_ch1 = false;
    bool oneMinuteResetDone_card1_ch2 = false;
    bool oneMinuteResetDone_card2_ch1 = false;
    bool oneMinuteResetDone_card2_ch2 = false;

private:
    struct ImageBuffers
    {
        cv::Mat currentImgCpu_ch1;
        cv::Mat previousImgCpu_ch1;
        cv::Mat currentImgCpu_ch2;
        cv::Mat previousImgCpu_ch2;
    };

    ImageBuffers card1Buffers;
    ImageBuffers card2Buffers;
};

#endif // DRIVER_MANAGER_H