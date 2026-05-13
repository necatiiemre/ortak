#ifndef RESOLUTION_H
#define RESOLUTION_H

#include <stdio.h>
#include <regex>
#include "DebugLog.h"

/*Checking card resolution setting. If want to check other resolution spec added and compare*/

// Global expected resolution values
constexpr int width_2560 = 2560;
constexpr int height_1024 = 1024;
constexpr int width_1280 = 1280;
constexpr int width_1920 = 1920;
constexpr int height_1080 = 1080;
// constexpr int height_1024 = 1024;
constexpr int link_rate_6 = 6;

/*The features we want to compare are specified, while making a direct comparison, the features coming from the cart. This comparison is performed using the variables found in the header file,*/
inline void check_resolutionVelocity(int width, int height, int link_rate, int expected_width, int expected_height, int expected_link_rate,const std::string &cardName)
{
    if (width != expected_width)
    {
        LOG_ERROR(cardName << "Image width does not match required! Width: " << width << ", Expected: " << expected_width);
    }
    else if (height != expected_height)
    {
        LOG_ERROR(cardName << "Image height does not match required! Height: " << height << ", Expected: " << expected_height);
    }
    else if (link_rate != expected_link_rate)
    {
        LOG_ERROR(cardName << "Link rate does not match required! Link rate: " << link_rate << ", Expected: " << expected_link_rate);
    }
    else
    {
        LOG_INFO(cardName << "Image Resolution is correct: " << width << "x" << height << " @ " << link_rate << " Gbps");
    }
}

inline void check_resolutionDvi(int width, int height, int expected_width, int expected_height, int channel)
{
    if (width != expected_width)
    {
        LOG_ERROR("Channel " << channel << " : DVI Image width mismatch! Width: " << width << ", Expected: " << expected_width);
    }
    else if (height != expected_height)
    {
        LOG_ERROR("Channel " << channel << " : DVI Image height mismatch! Height: " << height << ", Expected: " << expected_height);
    }
    else
    {
        LOG_INFO("Channel " << channel << " : DVI Image Resolution is correct: " << width << "x" << height);
    }
}

inline std::pair<int, int> getInputResolution(int channelId)
{
    std::string cmd = "mwcap-info --info-input-video 0:" + std::to_string(channelId);
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe)
    {
        LOG_ERROR("Failed to run mwcap-info for resolution on channel " << channelId);
        return {0, 0};
    }

    char buffer[256];
    std::regex resRegex(R"((\d+)x(\d+))"); // örnek: 1920x1080
    std::smatch match;

    while (fgets(buffer, sizeof(buffer), pipe))
    {
        std::string line(buffer);
        if (line.find("Resolution") != std::string::npos)
        {
            if (std::regex_search(line, match, resRegex))
            {
                int width = std::stoi(match[1]);
                int height = std::stoi(match[2]);
                pclose(pipe);
               // LOG_TRACE("Detected input resolution: " << width << "x" << height << " on channel " << channelId);
                return {width, height};
            }
        }
    }

    pclose(pipe);
    LOG_WARN("No resolution detected for channel " << channelId << ", returning fallback (0, 0)");
    return {0, 0};
}

#endif // RESOLUTION_H
