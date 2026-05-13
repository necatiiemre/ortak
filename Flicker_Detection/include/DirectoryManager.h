#ifndef DIRECTORY_MANAGER_H
#define DIRECTORY_MANAGER_H

#include <string>
#include <map>
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

enum Card
{
    CARD_1,
    CARD_2,
    CARD_BOTH
};
enum Channel
{
    CH_1,
    CH_2,
    CH_BOTH
};

class DirectoryManager
{
public:
    void setBaseOutputDir(const std::string &dir);
    std::string getBaseOutputDir() const { return baseOutputDir; }

    uint8_t createDirectory(Card card, Channel channel,
                            std::optional<Card> card2 = std::nullopt,
                            std::optional<Channel> channel2 = std::nullopt, bool loopback_test = false);

    // std::string getDirectoryPath(Card card, Channel channel) const;
    uint8_t getErrorFramePath(Card card, Channel channel, std::string &outPath) const;
    uint8_t getVideoPath(Card card, Channel channel, std::string &outPath) const;
    uint8_t getDviVideoPath(int channelIndex, std::string &outPath) const;
    uint8_t getChannelName(Channel channel, std::string &outChannelName);

    uint8_t createLoopbackDirectories(Card card);
    uint8_t getLoopbackVideoPath(Card card, Channel channel, std::string &outPath);

    uint8_t setSessionTimestamp(Card card, Channel channel, const std::string &timestamp);

    uint8_t createDviDirectories();
    uint8_t getDviErrorPath(int channelIndex, std::string &outPath) const;

private:
    uint8_t createNestedDirectories(const std::string &path);
    std::string velocityBase() const;
    std::string dviBase() const;
    std::map<std::string, std::string> folderPaths;
    std::string sessionTimestamp;
    std::map<std::string, std::string> sessionTimestamps;
    std::string baseOutputDir;
};

uint8_t getCurrentTimestamp();

#endif // DIRECTORY_MANAGER_H