#include "DirectoryManager.h"
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "DebugLog.h"
#include "ErrorUtils.h"
#include "Globals.h"

namespace fs = std::filesystem;

void DirectoryManager::setBaseOutputDir(const std::string &dir)
{
    baseOutputDir = dir;
    if (!baseOutputDir.empty() && baseOutputDir.back() == '/')
        baseOutputDir.pop_back();
}

std::string DirectoryManager::velocityBase() const
{
    return baseOutputDir.empty() ? std::string("output_velocity")
                                 : baseOutputDir + "/output_velocity";
}

std::string DirectoryManager::dviBase() const
{
    return baseOutputDir.empty() ? std::string("output_dvi")
                                 : baseOutputDir + "/output_dvi";
}

// Timestamp generator (format: YYYY-MM-DD_HH-MM-SS)
// Returns the current local time formatted as a string safe for filenames or logs
// @return string representing the current timestamp (e.g., "2025-04-08_10-04-08")
int8_t getCurrentTimestamp(std::string &outTimestamp)
{
    try
    {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        std::tm tm;

        // Thread-safe alternatif:
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm, &timeT);
#else
        if (!localtime_r(&timeT, &tm))
        {
            return CODE_GET_CURRENT_TIMESTAMP_FAILED;
        }
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
        outTimestamp = oss.str();

        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to generate timestamp: " << e.what());
        return CODE_GET_CURRENT_TIMESTAMP_FAILED;
    }
}

// Stores a session timestamp string for the given card and channel combination
// Used to organize output directories and filenames per recording session
// @param card: CARD_1 or CARD_2 indicating the capture device
// @param channel: CH_1 or CH_2 indicating the channel of the card
// @param timestamp: formatted timestamp string (e.g., "2025-04-08_10-04-08")
// @return void
uint8_t DirectoryManager::setSessionTimestamp(Card card, Channel channel, const std::string &timestamp)
{
    try
    {
        std::string cardName = (card == CARD_1) ? "card1" : "card2";
        std::string channelName = (channel == CH_1) ? "ch1" : "ch2";
        std::string key = cardName + "_" + channelName;

        sessionTimestamps[key] = timestamp;
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error setting session timestamp: " << e.what());
        return CODE_SET_SESSION_TIMESTAMP_FAILED;
    }
}

// Creates a nested directory structure if it doesn't already exist
// Applies full read/write/execute permissions (0777) after creation
// Logs the result or throws an error on failure
// @param path: full directory path to create (e.g., "output/card1/ch1")
// @return void
uint8_t DirectoryManager::createNestedDirectories(const std::string &path)
{
    try
    {
        if (!fs::exists(path))
        {
            if (fs::create_directories(path))
            {
                LOG_INFO("Directory created: " << path);
                chmod(path.c_str(), S_IRWXU | S_IRWXG | S_IRWXO); // 0777
            }
        }
        else
        {
            LOG_WARN("Directory already exists: " << path);
        }

        return CODE_SUCCESS;
    }
    catch (const fs::filesystem_error &e)
    {
        LOG_ERROR("Directory creation error: " << e.what());
        return CODE_CREATE_NESTED_DIRECTORY_FAILED;
    }
}

uint8_t DirectoryManager::createDirectory(Card card, Channel channel,
                                          std::optional<Card> card2,
                                          std::optional<Channel> channel2,
                                          bool loopback_test)
{
    try
    {
        // 🟡 Eğer loopback testiyse sadece loopback klasör yapısını oluştur
        if (loopback_test)
        {
            auto addLoopbackPath = [&](Card c, Channel ch)
            {
                std::string cardName = (c == CARD_1) ? "card1" : "card2";
                std::string basePath = velocityBase() + "/output_" + cardName + "/loopback";

                if (ch == CH_BOTH || ch == CH_1)
                {
                    std::string ch1Path = basePath + "/ch1";
                    createNestedDirectories(ch1Path);
                    folderPaths[cardName + "_loopback_ch1"] = ch1Path;
                }

                if (ch == CH_BOTH || ch == CH_2)
                {
                    std::string ch2Path = basePath + "/ch2";
                    createNestedDirectories(ch2Path);
                    folderPaths[cardName + "_loopback_ch2"] = ch2Path;
                }
            };

            addLoopbackPath(card, channel);

            if (card2 && channel2)
                addLoopbackPath(*card2, *channel2);

            if (card == CARD_BOTH && channel == CH_BOTH)
            {
                addLoopbackPath(CARD_1, CH_BOTH);
                addLoopbackPath(CARD_2, CH_BOTH);
            }
            else if (card == CARD_BOTH)
            {
                addLoopbackPath(CARD_1, channel);
                addLoopbackPath(CARD_2, channel);
            }
            else if (channel == CH_BOTH)
            {
                addLoopbackPath(card, CH_BOTH);
            }

            return CODE_SUCCESS;
        }

        // 🟢 Normal path oluşturma bloğu
        auto addPath = [&](Card c, Channel ch)
        {
            std::string cardName = (c == CARD_1) ? "card1" : "card2";
            std::string channelName = (ch == CH_1) ? "ch1" : "ch2";

            std::string cardPath = velocityBase() + "/output_" + cardName;
            createNestedDirectories(cardPath);

            std::string errorPath = cardPath + "/error_frames_" + channelName;
            createNestedDirectories(errorPath);

            std::string videoPath = cardPath + "/video_" + channelName;
            createNestedDirectories(videoPath);

            folderPaths[cardName + "_" + channelName + "_error"] = errorPath;
            folderPaths[cardName + "_" + channelName + "_video"] = videoPath;
        };

        addPath(card, channel);

        if (card2 && channel2)
        {
            addPath(*card2, *channel2);
        }

        if (channel == CH_BOTH)
        {
            addPath(card, CH_1);
            addPath(card, CH_2);
        }

        if (card == CARD_BOTH)
        {
            addPath(CARD_1, channel);
            addPath(CARD_2, channel);
        }

        if (card == CARD_BOTH && channel == CH_BOTH)
        {
            addPath(CARD_1, CH_1);
            addPath(CARD_1, CH_2);
            addPath(CARD_2, CH_1);
            addPath(CARD_2, CH_2);
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Directory creation error: " << e.what());
        return CODE_CREATE_DIRECTORY_FAILED;
    }
}

// Returns the full path for saving error frames for the given card and channel
// Appends the current session timestamp to the base error path and ensures the directory exists
// @param card: CARD_1 or CARD_2
// @param channel: CH_1 or CH_2
// @return string: full path to the error frame directory; empty string if not found
uint8_t DirectoryManager::getErrorFramePath(Card card, Channel channel, std::string &outPath) const
{
    std::string cardName = (card == CARD_1) ? "card1" : "card2";
    std::string channelName = (channel == CH_1) ? "ch1" : "ch2";
    std::string key = cardName + "_" + channelName + "_error";
    auto it = folderPaths.find(key);

    if (it == folderPaths.end())
    {
        LOG_ERROR("Error frame base path not found for " << cardName << " " << channelName);
        return CODE_GET_BASE_PATH_FOUND_FAILED;
    }

    std::string basePath = it->second;
    std::string timestampKey = cardName + "_" + channelName;
    auto tsIt = sessionTimestamps.find(timestampKey);
    std::string usedTimestamp = (tsIt != sessionTimestamps.end()) ? tsIt->second : "unknown";

    std::string fullPath = basePath + "/" + usedTimestamp;

    try
    {
        if (!fs::exists(fullPath))
        {
            fs::create_directories(fullPath);
            chmod(fullPath.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
        }
    }
    catch (const fs::filesystem_error &e)
    {
        LOG_ERROR("Error creating directory for error frames: " << e.what());
        return CODE_ERROR_FRAME_PATH_NOT_FOUND;
    }

    outPath = fullPath;
    return CODE_SUCCESS;
}

// Retrieves the base directory path for storing video recordings for the given card and channel
// Looks up the path from the folderPaths map using a constructed key
// @param card: CARD_1 or CARD_2
// @param channel: CH_1 or CH_2
// @return string: path to the video directory, or empty string if not found
uint8_t DirectoryManager::getVideoPath(Card card, Channel channel, std::string &outPath) const
{
    std::string cardName = (card == CARD_1) ? "card1" : "card2";
    std::string channelName = (channel == CH_1) ? "ch1" : "ch2";
    std::string key = cardName + "_" + channelName + "_video";

    auto it = folderPaths.find(key);
    if (it != folderPaths.end())
    {
        outPath = it->second;
        return CODE_SUCCESS;
    }

    LOG_ERROR("Error: Video path not found for " << cardName << " " << channelName);
    return CODE_DVI_VIDEO_PATH_NOT_FOUND;
}

// Creates timestamped directory structure for DVI-based error frames and video recordings
// Initializes separate folders for channel 1 and 2 under "output_dvi", and updates folderPaths map
// @return void
uint8_t DirectoryManager::createDviDirectories()
{

    try
    {
        std::string base = dviBase();

        rc = createNestedDirectories(base + "/error_frames_channel1");
        checkReturnCode(rc, "Failed to create error_frames_channel1 directory for DVI error frames.");
        createNestedDirectories(base + "/error_frames_channel2");
        checkReturnCode(rc, "Failed to create error_frames_channel2 directory for DVI error frames.");

        std::string timestamp;
        rc = getCurrentTimestamp(timestamp);
        checkReturnCode(rc, "Failed to get current timestamp for DVI directories.");

        folderPaths["dvi_ch1"] = base + "/error_frames_channel1/" + timestamp;
        folderPaths["dvi_ch2"] = base + "/error_frames_channel2/" + timestamp;

        rc = createNestedDirectories(folderPaths["dvi_ch1"]);
        checkReturnCode(rc, "Failed to create error_frames_channel1  directory for DVI error frames.");
        rc = createNestedDirectories(folderPaths["dvi_ch2"]);
        checkReturnCode(rc, "Failed to create error_frames_channel2 directory for DVI error frames.");
        folderPaths["dvi_ch1_video"] = base + "/video_channel1/" + timestamp;
        folderPaths["dvi_ch2_video"] = base + "/video_channel2/" + timestamp;

        rc = createNestedDirectories(folderPaths["dvi_ch1_video"]);
        checkReturnCode(rc, "Failed to create dvi_ch1_video directory for DVI error frames.");
        rc = createNestedDirectories(folderPaths["dvi_ch2_video"]);
        checkReturnCode(rc, "Failed to create vi_ch2_video directory for DVI error frames.");
        LOG_INFO("DVI directories initialized.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_CREATE_DVI_DIRECTORIES_FAILED;
    }
}

// Retrieves the DVI error frame directory path for the specified channel index (1 or 2)
// Looks up the timestamped path from folderPaths; returns fallback if not found
// @param channelIndex: channel number (1 or 2) for which to get the error frame path
// @return string: full path to the error frame directory, or fallback path if unavailable

uint8_t DirectoryManager::getDviErrorPath(int channelIndex, std::string& outPath) const
{
    if (channelIndex == 1)
    {
        auto it = folderPaths.find("dvi_ch1");
        if (it != folderPaths.end())
        {
            outPath = it->second;
            return CODE_SUCCESS;
        }
    }
    else if (channelIndex == 2)
    {
        auto it = folderPaths.find("dvi_ch2");
        if (it != folderPaths.end())
        {
            outPath = it->second;
            return CODE_SUCCESS;
        }
    }

    LOG_ERROR("DVI error frame path not found for channel " << channelIndex);
    return CODE_DVI_ERROR_PATH_NOT_FOUND;
}

// std::string DirectoryManager::getDviErrorPath(int channelIndex) const
// {
//     if (channelIndex == 1 && folderPaths.count("dvi_ch1"))
//         return folderPaths.at("dvi_ch1");
//     if (channelIndex == 2 && folderPaths.count("dvi_ch2"))
//         return folderPaths.at("dvi_ch2");

//     LOG_ERROR("DVI path not found for channel " << channelIndex);
//     return "output_dvi/unknown/";
// }

// Retrieves the DVI video output directory path for the specified channel index (1 or 2)
// Returns the timestamped folder path if available, otherwise a fallback path
// @param channelIndex: channel number (1 or 2) for which to get the video output path
// @return string: full path to the video directory, or fallback path if not found
uint8_t DirectoryManager::getDviVideoPath(int channelIndex, std::string &outPath) const
{
    std::string key = (channelIndex == 1) ? "dvi_ch1_video" : "dvi_ch2_video";

    auto it = folderPaths.find(key);
    if (it != folderPaths.end())
    {
        outPath = it->second;
        return CODE_SUCCESS;
    }

    LOG_ERROR("DVI video path not found for channel " << channelIndex);
    return CODE_DVI_VIDEO_PATH_NOT_FOUND;
}

uint8_t DirectoryManager::createLoopbackDirectories(Card card)
{
    try
    {
        std::string cardName = (card == CARD_1) ? "card1" : "card2";
        std::string basePath = velocityBase() + "/output_" + cardName + "/loopback";

        std::string ch1Path = basePath + "/ch1";
        std::string ch2Path = basePath + "/ch2";

        rc = createNestedDirectories(ch1Path);
        checkReturnCode(rc, "Failed to create loopback directory for ch1.");
        rc = createNestedDirectories(ch2Path);
        checkReturnCode(rc, "Failed to create loopback directory for ch2.");

        folderPaths[cardName + "_loopback_ch1"] = ch1Path;
        folderPaths[cardName + "_loopback_ch2"] = ch2Path;
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error creating loopback directories: " << e.what());
        return CODE_CREATE_LOOPBACK_DIRECTORY_FAILED;
    }
}
uint8_t DirectoryManager::getChannelName(Channel channel, std::string &outChannelName)
{
    switch (channel)
    {
    case CH_1:
        outChannelName = "ch1";
        return CODE_SUCCESS;
    case CH_2:
        outChannelName = "ch2";
        return CODE_SUCCESS;
    default:
        LOG_ERROR("Unknown channel enum value.");
        return CODE_INVALID_CHANNEL;
    }
}

// std::string DirectoryManager::getChannelName(Channel channel)
// {
//     switch (channel)
//     {
//     case CH_1:
//         return "ch1";
//     case CH_2:
//         return "ch2";
//     default:
//         return "unknown";
//     }
// }

uint8_t DirectoryManager::getLoopbackVideoPath(Card card, Channel channel, std::string &outPath)
{
    std::string cardName = (card == CARD_1) ? "card1" : "card2";
    std::string channelName; 
    rc = getChannelName(channel, channelName); // "ch1" veya "ch2"
    checkReturnCode(rc, "Failed to get channel name for loopback video path.");
    std::string key = cardName + "_loopback_" + channelName;

    auto it = folderPaths.find(key);
    if (it == folderPaths.end())
    {
        LOG_ERROR("Loopback path not found for key: " << key);
        return CODE_LOOPBACK_PATH_NOT_FOUND;
    }

    fs::path channelDir = it->second;

    try
    {
        if (!fs::exists(channelDir))
        {
            fs::create_directories(channelDir);
            chmod(channelDir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
        }
    }
    catch (const fs::filesystem_error &e)
    {
        LOG_ERROR("Failed to create loopback video directory: " << e.what());
        return CODE_FS_ERROR;
    }

    std::string timestamp;
    int8_t rc = getCurrentTimestamp(timestamp);
    checkReturnCode(rc, "Failed to get current timestamp for loopback video path.");

    std::string filename = "loopback_" + timestamp + ".avi";
    outPath = (channelDir / filename).string();
    return CODE_SUCCESS;
}