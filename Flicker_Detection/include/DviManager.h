
#pragma once

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include "VideoUtils.h"
#include "ThreadSafeQueue.h"

struct DviChannel
{
    atomic<int> frame_counter{0};
    atomic<int> error_frame_counter{0};
    atomic<int> fps{0};
    atomic<bool> is_running{false};
    atomic<float> temperature{0.0f};
    unique_ptr<ThreadSafeQueue<unique_ptr<cv::Mat>>> frame_queue_channel = make_unique<ThreadSafeQueue<unique_ptr<cv::Mat>>>();
    unique_ptr<ThreadSafeQueue<unique_ptr<cv::Mat>>> display_queue_channel = make_unique<ThreadSafeQueue<unique_ptr<cv::Mat>>>();
    std::chrono::steady_clock::time_point start_time_elapsed;

    string elapsed_time_string;
    string error_path;
    string video_path;
    string time_stamp;
    string video_file;
};

class DviManager
{
public:
    DviManager();
    ~DviManager();

    DviChannel channel_1;
    DviChannel channel_2;

    uint8_t start(int channel);
    uint8_t stop(int channel);
    uint8_t printStatistics();
    uint8_t isAnyChannelRunning(bool &result);
    uint8_t isChannelRunning(int channel, bool &result);
    uint8_t resetStatistics(int channel);
    void display_worker(int channelId, int coreId);

    struct Buffer
    {
        void *start;
        size_t length;
    };
    std::vector<Buffer> buffersCh1;
    std::vector<Buffer> buffersCh2;

    // cv::VideoWriter videoWriter_1;
    // cv::VideoWriter videoWriter_2;

private:
    std::thread producerThread1, consumerThread1;
    std::thread producerThread2, consumerThread2;
    std::thread displayThread1, displayThread2;

    // std::atomic<bool> runningCh1{false}, runningCh2{false};
    // std::atomic<bool> stopRequestedArray[2]{false, false};

    uint8_t rc;

    std::mutex mtx;

    uint8_t producer_worker(const char *device, int channelId, int coreId1);
    uint8_t consumer_worker(int channelId, int core);

    uint8_t initDevice(const char *device, int &fd, std::vector<Buffer> &buffers, int channelId, bool &boolResult);
    uint8_t cleanup(int fd, std::vector<Buffer> &buffers);
    uint8_t getCurrentTimestamp(std::string &time);
    uint8_t drawStatistics(cv::Mat &frame, int channel);
    uint8_t checkCardResolution(int channel);
    uint8_t getDeviceTemperature(int channelId, DviChannel &channel);
    uint8_t isSignalPresent(int channelId, bool &signalPresent);
    uint8_t isErrorFrame(float previous_score, float current_score, cv::Mat &current_frame, std::string error_path, int &frame_counter, int &error_frame_counter);
};