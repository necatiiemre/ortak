#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/timeb.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
// Include the DMA driver CLI before OpenCV (important for proper initialization)
#include </usr/src/NWLogic/DMADriver-2015/TestCli/DmaDriverCli/DmaDriverCli.h>
/* OpenCV and hardware-specific API headers*/
#include <opencv2/opencv.hpp>
#include <velocity/grtv_api.h> // Include your hardware API
#include "Globals.h"
#include <filesystem>
#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <string>
#include <pthread.h>
#include <sched.h>
#include <thread>
#include <sys/syscall.h>
#include <sys/types.h>
/*Project-specific headers*/
#include "ImageDisplayTimeout.h"
#include "ssim_gpu.h"
#include "Resolution.h"
#include "DirectoryManager.h"
#include "CudaManager.h"
#include "DriverManager.h"
#include "DebugLog.h"
/*Additionally system headers*/
#include <thread>
#if WIN32 // WINDOWS
#include <sys/types.h>
#else // LINUX
#include <unistd.h>
#include <sys/time.h>
#include "ErrorUtils.h"

#endif

#define VERNUM "1.2"

#ifndef BOOL
typedef int BOOL;
#endif


// Main entry point for flicker detection; sets up directories, checks CUDA support, initializes drivers, and starts capture
// @param card_1: first video capture card identifier
// @param channel_1: channel associated with the first card
// @param card_2: optional second capture card identifier
// @param channel_2: optional channel for the second card
// @return void
uint8_t startFlickerDetection(Card card_1, Channel channel_1,
                           std::optional<Card> card_2,
                           std::optional<Channel> channel_2, bool loopback_test)
{
    try
    {
        LOG_INFO("Starting Flicker Detection...");


        /*Directory manager manager is create the necessary foldor directory for recording card and channnel*/
        rc = directory_manager.createDirectory(card_1, channel_1, card_2, channel_2, loopback_test);
        checkReturnCode(rc, "Creating Directory Failed.");
        sleep(2);

        /*Check if CUDA is available and supported*/
        rc = cuda_manager.checkCudaSupport();
        checkReturnCode(rc, "Check Cuda Support Failed");
        sleep(2);

        /* Initialize DMA drivers and related resources*/
        rc = driver_manager.initializeDriver(card_1, channel_1, card_2, channel_2);
        checkReturnCode(rc, "Error Initializing Velocity Driver.");
        sleep(2);

        if (loopback_test == true)
        {
            printf("printe girdi");

            rc = driver_manager.startTestLoopback();
            checkReturnCode(rc, "Error starting loopback test.");
            rc = driver_manager.stopFlickerDetection();
            checkReturnCode(rc, "Error stopping loopback test.");
            exit(0);
        }
        else
        {
            std::thread([]() {
                while (true) {
                    DisplayTimeout::checkTimeouts(5);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }).detach();
            // Begin video capture using initialized drivers
            driver_manager.startVideoCapture();

        }

        /*Keep the program running while video capture is active*/
        while (driver_manager.isRunning)
        {
            usleep(100000);
        }

        rc = driver_manager.printStatistics();
        checkReturnCode(rc, "Print Velocity Statistics Failed.");
        LOG_INFO("Velocity Flicker Detection stopped successfully.");

        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error: " << e.what());
        driver_manager.stopFlickerDetection();
        return CODE_START_FLICKER_DETECTION_FAILED;
    }
}