#include "Helpers.h"

uint8_t drawDviStats(cv::Mat &current_frame, std::string time, int fps, int error_frame_counter, int frame_counter, float temperature)
{
    cv::putText(current_frame, "Time: " + time, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    cv::putText(current_frame, "Fps: " + std::to_string(fps), cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    cv::putText(current_frame, "Total Frame Count: " + std::to_string(frame_counter), cv::Point(10, 90), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    cv::putText(current_frame, "Error Frame Count: " + std::to_string(error_frame_counter), cv::Point(10, 120), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    cv::putText(current_frame, "Card Temperature: " + std::to_string(int(temperature)), cv::Point(10, 150), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

    return CODE_SUCCESS;
}