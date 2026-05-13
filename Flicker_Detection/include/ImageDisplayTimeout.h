#pragma once
#include <map>
#include <string>
#include <chrono>
#include <iostream>
#include <opencv2/highgui.hpp>
#include "Globals.h"
#include "ErrorUtils.h"

// Eklenen yapı: pencere bilgisi
struct DisplaySession
{
    std::chrono::steady_clock::time_point lastCallTime;
    Card card;
    Channel channel;
};

namespace DisplayTimeout
{

    // Her pencere için oturum bilgisi (zaman + card + channel)
    inline std::map<std::string, DisplaySession> lastCallSessions;

    /**
     * Pencerenin son gösterilme zamanını ve ilgili kart bilgilerini günceller.
     */
    inline void updateLastCall(const std::string &windowName, Card card, Channel channel)
    {
        lastCallSessions[windowName] = {
            std::chrono::steady_clock::now(),
            card,
            channel};
    }

    /**
     * Belirtilen süre boyunca hiç güncellenmemiş pencereleri kapatır.
     */
    inline void checkTimeouts(int timeoutSeconds)
    {
        const auto now = std::chrono::steady_clock::now();

        for (auto it = lastCallSessions.begin(); it != lastCallSessions.end();)
        {
            const std::string &windowName = it->first;
            const DisplaySession &session = it->second;

            auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - session.lastCallTime).count();


            if (elapsedSeconds >= 1 )
            {
                rc = driver_manager.releaseFpsCounter(session.card, session.channel);
                checkReturnCode(rc,"Release Fps Counter failed");
            }

            if (elapsedSeconds >= timeoutSeconds)
            {
                try
                {

                    rc = driver_manager.stopCard(session.card, session.channel);
                    checkReturnCode(rc, "Stop Card failed");
                    cv::destroyWindow(windowName);
                    cv::waitKey(1);
                    std::cout << "[DisplayTimeout] Window closed due to timeout: " << windowName << std::endl;
                }
                catch (const cv::Exception &e)
                {
                    std::cerr << "[DisplayTimeout] Error while closing window '" << windowName
                              << "': " << e.what() << std::endl;
                }
                it = lastCallSessions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
