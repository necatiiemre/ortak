#include "FlickerDetectionDvi.h"
#include <iostream>
#include <chrono>
#include <unistd.h>
#include "Globals.h"
#include "DebugLog.h"
#include "ErrorUtils.h"


// Starts flicker detection for DVI input on the specified channel
// Checks CUDA support, initializes DVI capture, and monitors activity until stopped
// @param channel: index of the DVI channel to monitor (0 = Ch1, 1 = Ch2, 2 = both)
// @return void
uint8_t startFlickerDetectionDvi(int channel)
{
    try
    {
        rc = cuda_manager.checkCudaSupport();
        checkReturnCode(rc, "CUDA support check failed.");
        sleep(2);

        LOG_INFO("Starting DVI Flicker Detection...");

        rc = dvi_manager.start(channel);
        checkReturnCode(rc, "DVI Manager start failed.");
        bool isAnyChannelRunning = false;
        rc = dvi_manager.isAnyChannelRunning(isAnyChannelRunning);
        checkReturnCode(rc, "DVI Manager check running failed.");
        while (isAnyChannelRunning)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            rc = dvi_manager.isAnyChannelRunning(isAnyChannelRunning);
            checkReturnCode(rc, "DVI Manager check running failed.");
        }

        rc = dvi_manager.printStatistics();
        checkReturnCode(rc, "DVI Manager print statistics failed.");
        LOG_INFO("DVI Flicker Detection stopped successfully.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("DVI Flicker Detection Error: " + std::string(e.what()));
        return CODE_STOP_FLICKER_DETECTION_FAILED;
    }
}