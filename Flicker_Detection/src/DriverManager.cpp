
#include <velocity/grtv_api.h>
#include <pthread.h>
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include "Globals.h"
#include "DebugLog.h"
#include "ImageDisplayTimeout.h"
#include "ErrorUtils.h"
#include <atomic>
#include <opencv2/core/ocl.hpp>
#include <condition_variable>

extern UINT32 gAsyncCount;
extern UINT_PTR gDriver;

int thread_size;

int thread_card1_channel1;
int thread_card1_channel2;
int thread_card2_channel1;
int thread_card2_channel2;

cv::Mat bgrImage_card1_channel1;

cv::Mat bgrImage_card1_channel2;

cv::Mat bgrImage_card2_channel1;

cv::Mat bgrImage_card2_channel2;

cv::Mat resizedImage_card1_channel1;
cv::Mat resizedImage_card1_channel2;
cv::Mat resizedImage_card2_channel1;
cv::Mat resizedImage_card2_channel2;

thread_local char buffer_card1_channel_1[128];
thread_local char buffer_card1_channel_2[128];
thread_local char buffer_card2_channel_1[128];
thread_local char buffer_card2_channel_2[128];

int rc;

UINT32 *pData;

INT32 jtemp_card1_channel1 = 0;
INT32 jtemp_card1_channel2 = 0;
INT32 jtemp_card2_channel1 = 0;
INT32 jtemp_card2_channel2 = 0;

std::stringstream string_stream_card1_channel_1;
std::stringstream string_stream_card1_channel_2;
std::stringstream string_stream_card2_channel_1;
std::stringstream string_stream_card2_channel_2;

std::string error_frame_path_card1_channel1;
std::string error_frame_path_card1_channel2;
std::string error_frame_path_card2_channel1;
std::string error_frame_path_card2_channel2;

// Global thread kontrol değişkenleri
std::thread thread_card1_ch1, thread_card1_ch2;
std::queue<UINT32 *> queue_card1_ch1, queue_card1_ch2;
std::mutex mutex_card1_ch1, mutex_card1_ch2;
std::condition_variable cv_card1_ch1, cv_card1_ch2;

// Global thread kontrol değişkenleri
std::thread thread_card2_ch1, thread_card2_ch2;
std::queue<UINT32 *> queue_card2_ch1, queue_card2_ch2;
std::mutex mutex_card2_ch1, mutex_card2_ch2;
std::condition_variable cv_card2_ch1, cv_card2_ch2;

std::atomic<bool> isRunning_card2_channel1 = false;
std::atomic<bool> isRunning_card2_channel2 = false;
std::atomic<bool> isRunning_card1_channel1 = false;
std::atomic<bool> isRunning_card1_channel2 = false;

unsigned int getCoreCount()
{
    return std::thread::hardware_concurrency();
}

void setThreadAffinity(int coreId)
{
    int maxCores = std::thread::hardware_concurrency();
    if (maxCores <= 0)
    {
        LOG_ERROR("Failed to detect hardware concurrency.");
        return;
    }

    if (coreId < 0 || coreId >= maxCores)
    {
        LOG_ERROR("Invalid core ID: " << coreId);
        return;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);

    pthread_t thread = pthread_self();
    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0)
    {
        LOG_ERROR("Failed to set thread affinity to core " << coreId
                                                           << " - " << strerror(result));
    }
    else
    {
        std::cout << "Thread successfully pinned to core " << coreId << std::endl;
    }
}

// **Constructor**
DriverManager::DriverManager()
{

    display_queue_card1_ch1 = std::make_unique<ThreadSafeQueue<std::unique_ptr<cv::Mat>>>();
    display_queue_card1_ch2 = std::make_unique<ThreadSafeQueue<std::unique_ptr<cv::Mat>>>();
    display_queue_card2_ch1 = std::make_unique<ThreadSafeQueue<std::unique_ptr<cv::Mat>>>();
    display_queue_card2_ch2 = std::make_unique<ThreadSafeQueue<std::unique_ptr<cv::Mat>>>();
    // Defining Static Members
    First_Driver = 0;
    Second_Driver = 0;
    gMemSize = 0;
    gAsyncCount_1 = 0;
    gAsyncCount_2 = 0;

    err_crc_card1_ch1 = 0;
    err_8b10b_card1_ch1 = 0;
    err_crc_card1_ch2 = 0;
    err_8b10b_card1_ch2 = 0;
    err_crc_card2_ch1 = 0;
    err_8b10b_card2_ch1 = 0;
    err_crc_card2_ch2 = 0;
    err_8b10b_card2_ch2 = 0;

    // Frame counters and control variables for card 1
    frameCount_card1_Ch1 = new int(0);
    frameCount_card1_Ch2 = new int(0);
    errorFrameCount_card1_Ch1 = new int(0);
    errorFrameCount_card1_Ch2 = new int(0);
    scorePrevious_card1_ch1 = 0.0f;
    scorePrevious_card1_ch2 = 0.0f;
    isFirst_card1_channel1 = new bool(true);
    isFirst_card1_channel2 = new bool(true);
    firstFrame_card1_Ch1 = true;
    firstFrame_card1_Ch2 = true;

    // Frame counters and control variables for card 2
    frameCount_card2_Ch1 = new int(0);
    frameCount_card2_Ch2 = new int(0);
    errorFrameCount_card2_Ch1 = new int(0);
    errorFrameCount_card2_Ch2 = new int(0);
    scorePrevious_card2_ch1 = 0.0f;
    scorePrevious_card2_ch2 = 0.0f;
    isFirst_card2_channel1 = new bool(true);
    isFirst_card2_channel2 = new bool(true);
    firstFrame_card2_Ch1 = true;
    firstFrame_card2_Ch2 = true;

    // String variables
    channel_1 = "1";
    channel_2 = "2";
    card_1 = "1";
    card_2 = "2";

    /*Scaling factor for images */
    scale_down_width = 0.5;
    scale_down_height = 0.5;
    isRunning = true;

    card1Running = false;
    card2Running = false;
    rc = initializeCardProperties(card1Props);
    checkReturnCode(rc, "Initialize Card 1 Properties Failed.");
    rc = initializeCardProperties(card2Props);
    checkReturnCode(rc, "Initialize Card 2 Properties Failed.");
    gMemSize = width_2560 * height_1024 * 4;
}

// Initializes general-purpose card statistics and state tracking variables for both channels
// Unlike the internal driver statistics, these values are stored externally for overall monitoring
// @param props: reference to a CardProperties struct where values will be initialized
// @return void
uint8_t DriverManager::initializeCardProperties(CardProperties &props)
{
    try
    {
        props.frameCount_Ch1 = new int(0);
        props.frameCount_Ch2 = new int(0);
        props.errorFrameCount_Ch1 = new int(0);
        props.errorFrameCount_Ch2 = new int(0);
        props.firstFrame_Ch1 = true;
        props.firstFrame_Ch2 = true;
        props.scorePrevious_ch1 = 0.0f;
        props.scorePrevious_ch2 = 0.0f;
        props.isFirst_channel1 = new bool(true);
        props.isFirst_channel2 = new bool(true);
        props.width = 0;
        props.height = 0;
        props.link_rate = 0;
        props.err_b810b_ch1 = 0;
        props.err_b810b_ch2 = 0;
        props.err_crc_ch1 = 0;
        props.err_crc_ch2 = 0;
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_INITIALIZE_CARD_PROPERTIES_FAILED;
    }
}

// Destructor - deallocates heap memory and releases associated resources
DriverManager::~DriverManager()
{
    try
    {
        if (isRunning || First_Driver != 0 || Second_Driver != 0)
        {
            stopFlickerDetection();
        }
    }
    catch (...)
    {
        // best effort - process is going down anyway
    }

    rc = releaseResources();
    checkReturnCode(rc, "Release Resources Failed");
}

/*Explicit cleanup function.This function is used only by Stop Flicker Detection. It is left open for different customization..*/
uint8_t DriverManager::cleanup()
{
    try
    {
        rc = closeDrivers();
        checkReturnCode(rc, "Close Drivers Failed.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error in cleanup: " << e.what());
        return CODE_CLEANUP_FAILED;
    }
}

// Initializes DMA drivers for one or two video capture cards with specified channels
// Sets up routing, resolution check, memory allocation, and prints version info
// @param card: primary card to initialize (CARD_1, CARD_2, or CARD_BOTH)
// @param channel: associated channel for the primary card
// @param card2: optional secondary card to initialize
// @param channel2: optional channel for the secondary card
// @return void
uint8_t DriverManager::initializeDriver(Card card, Channel channel,
                                        std::optional<Card> card2,
                                        std::optional<Channel> channel2)
{
    int rc;

    auto initializeCard = [&](Card c, Channel ch, UINT_PTR &driver, UINT32 (*callback)(UINT32))
    {
        try
        {
            if (driver == 0)
            {

                rc = GRTV_OpenDriver(c, 0, 0, callback, &driver);

                if (rc == 0)
                {

                    LOG_INFO("Driver for " << ((c == CARD_1) ? "CARD_1" : "CARD_2") << " initialized successfully." << "Driver :" << driver);
                    rc = DriverManager::configureDriver(driver, false);
                    checkReturnCode(rc, "Configure Driver Failed.");
                    rc = DriverManager::printVersionInfo(driver);
                    checkReturnCode(rc, "Print version info failed.");
                    rc = DriverManager::checkCardResolution(driver, (c == CARD_1) ? "CARD_1" : "CARD_2");
                    checkReturnCode(rc, "Driver Manager Card Resolution Check Failed.");
                    rc = DriverManager::configureRouting(driver);
                    checkReturnCode(rc, "Driver Configure Failed");
                    rc = DriverManager::allocateMemoryBuffers(driver, c, ch);
                    checkReturnCode(rc, "Allocate Memory Buffers Failed.");
                    rc = DriverManager::cardSelfTest(driver, 0);
                    checkReturnCode(rc, "Card Self Test Failed.");
                    rc = DriverManager::pciControl(driver);
                    checkReturnCode(rc, "PCI Control Failed.");

                    if (c == CARD_1)
                    {
                        switch (ch)
                        {
                        case CH_1:
                            startCard1Channel1Thread();

                            break;
                        case CH_2:
                            startCard1Channel2Thread();

                            break;
                        case CH_BOTH:
                            startCard1Channel1Thread();
                            startCard1Channel2Thread();
                            break;
                        default:
                            break;
                        }
                    }
                    else if (c == CARD_2)
                    {
                        switch (ch)
                        {
                        case CH_1:
                            startCard2Channel1Thread();
                            break;
                        case CH_2:
                            startCard2Channel2Thread();
                            break;
                        case CH_BOTH:
                            startCard2Channel1Thread();
                            startCard2Channel2Thread();
                            break;
                        default:
                            break;
                        }
                    }
                    return CODE_SUCCESS;
                }
                else
                {
                    LOG_ERROR("Error initializing driver for " << ((c == CARD_1) ? "CARD_1" : "CARD_2"));
                    driver = 0;
                    return CODE_INITIALIZE_VELOCITY_DRIVER_FAILED;
                }
            }
            else
            {
                LOG_WARN(((c == CARD_1) ? "CARD_1" : "CARD_2") << " driver is already initialized.");
                return CODE_SUCCESS;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Error Initializing Velocity Driver: " << e.what());
            return CODE_INITIALIZE_VELOCITY_DRIVER_FAILED;
        }
    };

    // ** Single Card - Single or Both Channels **
    if (card == CARD_1 || card == CARD_2)
    {
        initializeCard(card, channel, (card == CARD_1) ? First_Driver : Second_Driver,
                       (card == CARD_1) ? UserCallBack_Card1 : UserCallBack_Card2);
    }

    // ** Both Cards - Single Channel or Both Channels **
    if (card == CARD_BOTH)
    {
        initializeCard(CARD_1, channel, First_Driver, UserCallBack_Card1);
        initializeCard(CARD_2, channel, Second_Driver, UserCallBack_Card2);
    }

    // ** Two Different Cards with Different Channels **
    if (card2 && channel2)
    {
        initializeCard(*card2, *channel2, (*card2 == CARD_1) ? First_Driver : Second_Driver,
                       (*card2 == CARD_1) ? UserCallBack_Card1 : UserCallBack_Card2);
    }
    return CODE_SUCCESS;
}

// Frees memory buffers and closes the specified driver for the given capture card
// Used internally by the close driver function to clean up card resources
// Called by the driver shutdown process.
// Takes the driver handle and associated memory buffers,
// frees allocated memory for both channels, closes the driver,
// and logs the shutdown status.
// @param driver: reference to the driver handle (will be set to 0 after closing)
// @param mem1: pointer to memory buffer for channel 1
// @param mem2: pointer to memory buffer for channel 2
// @param cardName: name of the card (used for log messages)
// @return void
u_int8_t DriverManager::closeCard(UINT_PTR &driver, void *mem1, void *mem2, const std::string &cardName)
{

    try
    {
        if (driver != 0)
        {
            if (mem1 != nullptr)
            {
                rc = GRTV_MemBufFree(driver, (UINT_PTR)mem1);
                if (rc == 0)
                    LOG_INFO("Channel 1 cleaned Card " << cardName.c_str());
                else
                    LOG_ERROR("Failed to free memory buffer for channel 1 of " << cardName.c_str() << " rc=" << rc);
            }

            if (mem2 != nullptr)
            {
                rc = GRTV_MemBufFree(driver, (UINT_PTR)mem2);
                if (rc == 0)
                    LOG_INFO("Channel 2 cleaned Card " << cardName.c_str());
                else
                    LOG_ERROR("Failed to free memory buffer for channel 2 of " << cardName.c_str() << " rc=" << rc);
            }

            rc = GRTV_CloseDriver(driver);
            if (rc == 0)
                LOG_INFO("Closed card " << cardName << " driver.");
            else
                LOG_ERROR("Failed to close driver for " << cardName << ": " << rc);

            driver = 0;
        }
        else
        {
            LOG_WARN("Card already closed " << cardName);
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error closing card " << cardName << ": " << e.what());
        return CODE_CLOSE_VELOCITY_DRIVER_FAILED;
    }
}

// Closes both capture card drivers and releases their associated memory buffers
// Calls closeCard for each driver (CARD_1 and CARD_2) with their corresponding memory pointers
// @return void
uint8_t DriverManager::closeDrivers()
{
    try
    {
        fprintf(stderr, "[closeDrivers] -> closeCard(First_Driver=%lu)\n", (unsigned long)First_Driver);
        rc = closeCard(First_Driver, gpMem1_1, gpMem1_2, card_1);
        checkReturnCode(rc, "Close Driver Card 1 Failed");
        gpMem1_1 = nullptr;
        gpMem1_2 = nullptr;
        fprintf(stderr, "[closeDrivers] First closed\n");

        fprintf(stderr, "[closeDrivers] -> closeCard(Second_Driver=%lu)\n", (unsigned long)Second_Driver);
        rc = closeCard(Second_Driver, gpMem2_1, gpMem2_2, card_2);
        checkReturnCode(rc, "Close Driver Card 2 Failed");
        gpMem2_1 = nullptr;
        gpMem2_2 = nullptr;
        fprintf(stderr, "[closeDrivers] Second closed\n");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "[closeDrivers] EXC: %s\n", e.what());
        LOG_ERROR("Error closing drivers: " << e.what());
        return CODE_CLOSE_VELOCITY_DRIVER_FAILED;
    }
}

// Configures memory mode and flow control settings for the specified driver
// Sets memory control to AUTO and enables or disables flow control based on the flag
// Updates internal state flags and timestamps for the corresponding card
// @param driver: reference to the driver handle (First_Driver or Second_Driver)
// @param enable: true to enable the driver and start tracking; false to disable and reset stats
// @return void
u_int8_t DriverManager::configureDriver(UINT_PTR &driver, bool enable)
{
    char *sErrString;

    // Set memory control to AUTO
    rc = GRTV_SetMemoryControl(driver, GRTV_AUTO);

    printf("Configure Driver %lu\n", driver);
    if (rc != GRTV_OK)
    {
        GRTV_GetErrorStr(rc, &sErrString);
        LOG_ERROR("Failed to set memory control: " << sErrString);
        return CODE_SET_MEMORY_CONTROL_FAILED;
    }

    if (!enable)
    {
        UINT32 u32Err = GRTV_Command(First_Driver, GRTV_FLOWTHRUDIS);
    }

    // Set flow control (ON or OFF)
    rc = GRTV_FlowControl(driver, GRTV_RCV, enable ? GRTV_ON : GRTV_OFF, 0);
    GRTV_GetErrorStr(rc, &sErrString);

    LOG_INFO("Flow Control " << (enable ? "ON" : "OFF")
                             << " for Driver " << static_cast<unsigned long>(driver)
                             << " (return code = " << rc << ", " << sErrString << ")");

    if (rc != GRTV_OK)
    {
        LOG_ERROR("Failed to set flow control: " << sErrString);
        return CODE_FLOW_CONTROL_FAILED;
    }

    auto now = std::chrono::steady_clock::now();

    if (driver == First_Driver)
    {
        if (enable)
        {
            card1StartTime = now;
            card1Running = true;
            UINT32 u32Err = GRTV_Command(First_Driver, GRTV_CLRSTATS);
            u32Err = GRTV_Command(First_Driver, GRTV_FLOWTHRUEN);
        }
        else
        {
            card1Running = false;
            rc = resetStatistics(CARD_1);
            checkReturnCode(rc, "Reset Statistics Card 1 Failed");
        }
    }
    else if (driver == Second_Driver)
    {
        if (enable)
        {
            card2StartTime = now;
            card2Running = true;
            UINT32 u32Err = GRTV_Command(Second_Driver, GRTV_CLRSTATS);
            u32Err = GRTV_Command(Second_Driver, GRTV_FLOWTHRUEN);
        }
        else
        {
            rc = resetStatistics(CARD_2);
            card2Running = false;
            checkReturnCode(rc, "Reset Statistics Card 2 Failed");
        }
    }
    LOG_INFO("Driver configuration successful");
    sleep(1);
    return CODE_SUCCESS;
}

// Enables or disables flow control for both cards if their drivers are initialized
// Calls configureDriver for each active driver with the given flow control setting
// @param enable: true to turn ON flow control, false to turn it OFF
// @return void
uint8_t DriverManager::setFlowControl(bool enable)
{
    try
    {
        if (First_Driver != 0)
        {
            rc = configureDriver(First_Driver, enable);
            checkReturnCode(rc, "Configure Driver Card 1 Failed");
        }

        if (Second_Driver != 0)
        {
            rc = configureDriver(Second_Driver, enable);
            checkReturnCode(rc, "Configure Driver Card 2 Failed");
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error setting flow control: " << e.what());
        return CODE_SET_FLOW_CONTROL_FAILED;
    }
}

// Retrieves and prints the API version and firmware version of the specified driver
// Useful for debugging and verifying driver compatibility
// @param driver: reference to the driver handle whose version info will be printed
// @return void
uint8_t DriverManager::printVersionInfo(UINT_PTR &driver)
{
    try
    {
        UINT32 rc;
        GRTV_GetFirmwareVer(driver, &rc);

        LOG_INFO("\nDriver opened for API Ver "
                 << static_cast<unsigned int>(GRTV_GetVerInfo(0) >> 16) << "."
                 << static_cast<unsigned int>(GRTV_GetVerInfo(0) & 0xFFFF)
                 << " and F/W version " << std::hex << rc << std::dec << "\n");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        return CODE_PRINT_VERSION_INFO_FAILED;
    }
}

// Retrieves resolution and link rate info from the driver and updates internal card properties
// Performs a resolution check against expected values and prints configuration details
// @param driver: reference to the driver handle
// @param cardName: name of the card ("CARD_1" or "CARD_2") used for updating corresponding properties
// @return void
uint8_t DriverManager::checkCardResolution(UINT_PTR &driver, const std::string &cardName)
{
    try
    {
        GRTV_CONFIG_MODE_INFO config_mode_info;
        int mode_index = 0; // Assuming mode_index is 0

        // Update card properties
        if (cardName == "CARD_1")
        {
            int rc = GRTV_GetConfigModeInfo(driver, &config_mode_info);
            if (rc != GRTV_OK)
            {
                char *errStr;
                GRTV_GetErrorStr(rc, &errStr);
                throw std::runtime_error("Failed to retrieve resolution info: " + std::string(errStr));
            }

            // Extract resolution details
            int width = config_mode_info.ModeParams[mode_index].ImageCols;
            int height = config_mode_info.ModeParams[mode_index].ImageRows;
            int link_rate = config_mode_info.LinkRate[mode_index];
            int fps = config_mode_info.ModeParams[mode_index].FrameRate;

            // Resolution check
            check_resolutionVelocity(width, height, link_rate, width_2560, height_1024, link_rate_6, cardName);

            LOG_INFO("Card " << cardName
                             << ", Mode " << mode_index
                             << " resolution " << width << " x " << height
                             << ", link rate " << link_rate << " Gbps" << "FPS" << fps);

            card1Props.width = width;
            card1Props.height = height;
            card1Props.link_rate = link_rate;
            LOG_INFO("CARD_1 properties updated: " << width << "x" << height << "@" << link_rate << "Gbps");
            LOG_INFO("CARD_1: " << card1Props.width << "x" << card1Props.height << "@" << card1Props.link_rate << "Gbps");
        }
        else if (cardName == "CARD_2")
        {
            int rc = GRTV_GetConfigModeInfo(driver, &config_mode_info);
            if (rc != GRTV_OK)
            {
                char *errStr;
                GRTV_GetErrorStr(rc, &errStr);
                throw std::runtime_error("Failed to retrieve resolution info: " + std::string(errStr));
            }

            // Extract resolution details
            int width = config_mode_info.ModeParams[mode_index].ImageCols;
            int height = config_mode_info.ModeParams[mode_index].ImageRows;
            int link_rate = config_mode_info.LinkRate[mode_index];

            // Resolution check
            check_resolutionVelocity(width, height, link_rate, width_2560, height_1024, link_rate_6, cardName);

            LOG_INFO("Card " << cardName
                             << ", Mode " << mode_index
                             << " resolution " << width << " x " << height
                             << ", link rate " << link_rate << " Gbps");

            card2Props.width = width;
            card2Props.height = height;
            card2Props.link_rate = link_rate;
            LOG_INFO("CARD_2 properties updated: " << width << "x" << height << "@" << link_rate << "Gbps");
            LOG_INFO("CARD_2: " << card2Props.width << "x" << card2Props.height << "@" << card2Props.link_rate << "Gbps");
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Check Card Resolution Failed" << e.what());
        return CODE_CHECK_CARD_RESOLUTION_FAILED;
    }
}

uint8_t DriverManager::pciControl(UINT_PTR &driver)
{
    try
    {
        UINT32 generation;
        rc = GRTV_GetPCIeGeneration(driver, &generation);
        if (rc == 0)
        {
            printf("Generation PCI: %u\n", generation);
        }
        else
        {
            printf("Failed to get PCIe Generation, error code: %d\n", rc);
            throw std::runtime_error("Failed to get PCIe Generation");
        }

        UINT32 lane_count;
        rc = GRTV_GetPCIeLaneCount(driver, &lane_count);
        if (rc == 0)
        {
            printf("Lane Count PCI: %u\n", lane_count);
        }
        else
        {
            printf("Failed to get PCIe Lane Count, error code: %d\n", rc);
            std::runtime_error("Failed to get PCIe Lane Count");
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_PCI_CONTROL_FAILED;
    }
}

uint8_t DriverManager::cardSelfTest(UINT_PTR &driver, int options = 0)
{
    try
    {
        UINT32 selftest = 0;
        rc = GRTV_GetCardSelfTest(driver, options, &selftest);
        LOG_INFO("Card Self Test result: " << selftest);
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_CARD_SELF_TEST_FAILED;
    }
}

// Configures routing control for both channels of the specified driver
// Sets both channels to PCIe routing mode and prints the result
// @param driver: reference to the driver handle to configure
// @return void
uint8_t DriverManager::configureRouting(UINT_PTR &driver)
{
    try
    {
        int RoutingCh1 = GRTV_ROUTING_PCIE;
        int RoutingCh2 = GRTV_ROUTING_PCIE;

        rc = GRTV_RoutingControlDch(driver, RoutingCh1, RoutingCh2);
        if (rc == GRTV_OK)
        {
            LOG_INFO("Routing configured successfully for driver " << driver);
            return CODE_SUCCESS;
        }
        else
        {
            throw std::runtime_error("Error configuring routing for driver ");
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_CONFIGURE_ROUTING_VELOCITY_FAILED;
    }
}

// Allocates memory buffers for the specified card and channel combination
// Checks if memory has already been allocated before allocating new buffers
// Allocating memory buffers for CARD_1 or CARD_2 with configured buffer size
// Checks if memory is already allocated for each channel and card
// Uses GRTV_MemBufAlloc to allocate memory with offset
// Prints success or error messages for each allocation attempt
// @param driver: reference to the driver handle associated with the card
// @param card: CARD_1 or CARD_2 indicating which capture card to allocate memory for
// @param channel: CH_1, CH_2, or CH_BOTH indicating the target channels
// @return void
uint8_t DriverManager::allocateMemoryBuffers(UINT_PTR &driver, Card card, Channel channel)
{

    try
    {

        LOG_INFO("Allocating memory buffers for " << ((card == CARD_1) ? "CARD_1" : "CARD_2") << gMemSize);

        if (channel == CH_1 || channel == CH_BOTH)
        {
            if (card == CARD_1)
            {
                if (!gpMem1_1)
                {
                    rc = GRTV_MemBufAlloc(driver, (UINT_PTR *)&gpMem1_1, gMemSize + GRTV_A818_IMG_OFFSET);
                    if (gpMem1_1)
                    {
                        LOG_INFO("Memory allocated for CARD_1 CH_1");
                    }
                    else
                    {
                        LOG_ERROR("Failed to allocate memory for CARD_1 CH_1");
                        throw std::runtime_error("Failed to allocate memory for CARD_1 CH_1");
                    }
                }
            }
            else if (card == CARD_2)
            {
                if (!gpMem2_1)
                {
                    rc = GRTV_MemBufAlloc(driver, (UINT_PTR *)&gpMem2_1, gMemSize + GRTV_A818_IMG_OFFSET);
                    if (gpMem2_1)
                    {
                        LOG_INFO("Memory allocated for CARD_2 CH_1");
                    }
                    else
                    {
                        LOG_ERROR("Failed to allocate memory for CARD_2 CH_1");
                        throw std::runtime_error("Failed to allocate memory for CARD_2 CH_1");
                    }
                }
            }
        }

        if (channel == CH_2 || channel == CH_BOTH)
        {
            if (card == CARD_1)
            {
                if (!gpMem1_2)
                {
                    rc = GRTV_MemBufAlloc(driver, (UINT_PTR *)&gpMem1_2, gMemSize + GRTV_A818_IMG_OFFSET);
                    if (gpMem1_2)
                    {
                        LOG_INFO("Memory allocated for CARD_1 CH_2");
                    }
                    else
                    {
                        LOG_ERROR("Failed to allocate memory for CARD_1 CH_2");
                        throw std::runtime_error("Failed to allocate memory for CARD_1 CH_2");
                    }
                }
            }
            else if (card == CARD_2)
            {
                if (!gpMem2_2)
                {
                    rc = GRTV_MemBufAlloc(driver, (UINT_PTR *)&gpMem2_2, gMemSize + GRTV_A818_IMG_OFFSET);
                    if (gpMem2_2)
                    {
                        LOG_INFO("Memory allocated for CARD_2 CH_2");
                    }
                    else
                    {
                        LOG_ERROR("Failed to allocate memory for CARD_2 CH_2");
                        throw std::runtime_error("Failed to allocate memory for CARD_2 CH_2");
                    }
                }
            }
        }

        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_ALLOCATE_MEMORY_BUFFER_FAILED;
    }
}

// Frees previously allocated memory buffers for the specified card and channel
// Checks each buffer before attempting to deallocate and logs the outcome
// @param driver: reference to the driver handle associated with the card
// @param card: CARD_1 or CARD_2 specifying which capture card to release memory from
// @param channel: CH_1, CH_2, or CH_BOTH indicating which channels to deallocate
// @return void
uint8_t DriverManager::deAllocateMemoryBuffers(UINT_PTR &driver, Card card, Channel channel)
{
    try
    {
        LOG_INFO("Deallocating memory buffers for " << ((card == CARD_1) ? "CARD_1" : "CARD_2"));

        if (channel == CH_1 || channel == CH_BOTH)
        {
            if (card == CARD_1)
            {
                rc = GRTV_MemBufFree(driver, (UINT_PTR)gpMem1_1);
                if (rc == 0)
                {
                    LOG_INFO("Memory deallocated for CARD_1 CH_1");
                    gpMem1_1 = nullptr;
                    counter_card1_ch1 = 0;
                }
                else
                {
                    LOG_ERROR("Failed to deallocate memory for CARD_1 CH_1 (Error code: " << rc);
                    throw std::runtime_error("Failed to deallocate memory for CARD_1 CH_1");
                }
            }
            else if (card == CARD_2)
            {
                rc = GRTV_MemBufFree(driver, (UINT_PTR)gpMem2_1);
                if (rc == 0)
                {
                    LOG_INFO("Memory deallocated for CARD_2 CH_1");
                    gpMem2_1 = nullptr;
                    counter_card2_ch1 = 0;
                }
                else
                {
                    LOG_ERROR("Failed to deallocate memory for CARD_2 CH_1 (Error code: " << rc);
                    throw std::runtime_error("Failed to deallocate memory for CARD_2 CH_1");
                }
            }
        }

        if (channel == CH_2 || channel == CH_BOTH)
        {
            if (card == CARD_1)
            {
                rc = GRTV_MemBufFree(driver, (UINT_PTR)gpMem1_2);
                if (rc == 0)
                {
                    LOG_INFO("Memory deallocated for CARD_1 CH_2");
                    gpMem1_2 = nullptr;
                    counter_card1_ch2 = 0;
                }
                else
                {
                    LOG_ERROR("Failed to deallocate memory for CARD_1 CH_2 (Error code: " << rc);
                    throw std::runtime_error("Failed to deallocate memory for CARD_1 CH_2");
                }
            }
            else if (card == CARD_2)
            {
                rc = GRTV_MemBufFree(driver, (UINT_PTR)gpMem2_2);
                if (rc == 0)
                {
                    LOG_INFO("Memory deallocated for CARD_2 CH_2");
                    gpMem2_2 = nullptr;
                    counter_card2_ch2 = 0;
                }
                else
                {
                    LOG_ERROR("Failed to deallocate memory for CARD_2 CH_2 (Error code: " << rc);
                    throw std::runtime_error("Failed to deallocate memory for CARD_2 CH_2");
                }
            }
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_DEALLOCATE_MEMORY_BUFFER_FAILED;
    }
}

// Initializes video writers and enables flow control for all opened drivers
// Used to begin the video recording process after drivers are configured
// @return void
uint8_t DriverManager::startVideoCapture()
{
    LOG_INFO("Recording in progress...");
    try
    {
        rc = initializeVideoWriters(false);
        checkReturnCode(rc, "Initialize Video Writers Failed");
        rc = setFlowControl(true);
        checkReturnCode(rc, "Set Flow Control Failed");

        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Video Capturing does't started: " << e.what());
        return CODE_VIDEO_CAPTURE_START_FAILED;
    }
}

void DriverManager::startCard1Channel1Thread()
{

    if (thread_card1_ch1.joinable())
        return;

    printf("Starting Card 1 Channel 1 Thread on CPU Core %d\n", 0);

    thread_card1_ch1 = std::thread([this]()
                                   {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(8, &cpuset);  
        pthread_t current_thread = pthread_self();
        int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
            LOG_ERROR("Failed to set affinity for Card 1 CH1 thread");

        isRunning_card1_channel1 = true;
        this->threadWorker_Card1_CH1(); });

    startDisplayThreadCard1Ch1();
}

void DriverManager::startCard1Channel2Thread()
{

    if (thread_card1_ch2.joinable())
        return;

    printf("Starting Card 1 Channel 2 Thread on CPU Core %d\n", 3);

    thread_card1_ch2 = std::thread([this]()
                                   {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);;
        CPU_SET(9, &cpuset);
        pthread_t current_thread = pthread_self();
        int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
            LOG_ERROR("Failed to set affinity for Card 1 CH2 thread");

        isRunning_card1_channel2 = true;
        this->threadWorker_Card1_CH2(); });
    startDisplayThreadCard1Ch2();
}

void DriverManager::startCard2Channel1Thread()
{

    if (thread_card2_ch1.joinable())
        return;

    printf("Starting Card 2 Channel 1 Thread on CPU Core %d\n", 6);

    thread_card2_ch1 = std::thread([this]()
                                   {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(10, &cpuset);
        pthread_t current_thread = pthread_self();
        int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
            LOG_ERROR("Failed to set affinity for Card 2 CH1 thread");

        isRunning_card2_channel1 = true;
        this->threadWorker_Card2_CH1(); });
    startDisplayThreadCard2Ch1();
}

void DriverManager::startCard2Channel2Thread()
{

    if (thread_card2_ch2.joinable())
        return;

    printf("Starting Card 2 Channel 2 Thread on CPU Core %d\n", 9);

    thread_card2_ch2 = std::thread([this]()
                                   {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(11, &cpuset); 
        pthread_t current_thread = pthread_self();
        int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
            LOG_ERROR("Failed to set affinity for Card 2 CH2 thread");

        isRunning_card2_channel2 = true;
        this->threadWorker_Card2_CH2(); });
    startDisplayThreadCard2Ch2();
}

void DriverManager::stopCard1Channel1Thread()
{
    isRunning_card1_channel1 = false;
    cv_card1_ch1.notify_all();

    stopRequested_card1_ch1 = true;

    if (thread_card1_ch1.joinable())
        thread_card1_ch1.join();

    stopDisplayThreadCard1Ch1();
}

void DriverManager::stopCard1Channel2Thread()
{
    isRunning_card1_channel2 = false;
    cv_card1_ch2.notify_all();

    stopRequested_card1_ch2 = true;

    if (thread_card1_ch2.joinable())
        thread_card1_ch2.join();

    stopDisplayThreadCard1Ch2();
}

void DriverManager::stopCard2Channel1Thread()
{
    isRunning_card2_channel1 = false;
    cv_card2_ch1.notify_all();

    stopRequested_card2_ch1 = true;

    if (thread_card2_ch1.joinable())
        thread_card2_ch1.join();

    stopDisplayThreadCard2Ch1();
}

void DriverManager::stopCard2Channel2Thread()
{
    isRunning_card2_channel2 = false;
    cv_card2_ch2.notify_all();

    stopRequested_card2_ch2 = true;
    ;

    if (thread_card2_ch2.joinable())
        thread_card2_ch2.join();

    stopDisplayThreadCard2Ch2();
}

void DriverManager::handleCard1Channel1(UINT32 *data)
{
    try
    {
        driver_manager.counter_card1_ch1++;
        if (driver_manager.counter_card1_ch1 == 1)
        {
            driver_manager.card1_ch1_time = std::chrono::steady_clock::now();
            rc = GRTV_GetStatusErrors(driver_manager.First_Driver, &driver_manager.err_crc_card1_ch1, &driver_manager.err_8b10b_card1_ch1, 1);
        }
        else
        {
            rc = GRTV_GetStatusErrors(driver_manager.First_Driver, &driver_manager.err_crc_card1_ch1, &driver_manager.err_8b10b_card1_ch1, 0);
        }

        if (rc != GRTV_OK)
        {
            char *errStr;
            GRTV_GetErrorStr(rc, &errStr);
            LOG_ERROR("GRTV_GetRawDataDch Fault: " << errStr);
        }

        // 2. Fetch the error bit values AND clear the latched bits.
        rc = GRTV_GetStatusErrors(driver_manager.First_Driver, &driver_manager.err_crc_card1_ch1, &driver_manager.err_8b10b_card1_ch1, 0);
        image_processor.ConvertRawDataToBgr(data, driver_manager.card1Props.width, driver_manager.card1Props.height, bgrImage_card1_channel1);

        if (bgrImage_card1_channel1.empty())
        {
            LOG_ERROR("Image Converting Fault\n");
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - driver_manager.card1_ch1_time);
        int totalSeconds = static_cast<int>(elapsed.count());
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;

        std::stringstream string_stream_card1_channel_1;

        string_stream_card1_channel_1 << std::setw(2) << std::setfill('0') << hours << ":"
                                      << std::setw(2) << std::setfill('0') << minutes << ":"
                                      << std::setw(2) << std::setfill('0') << seconds;

        std::string timeStr = string_stream_card1_channel_1.str();
        string_stream_card1_channel_1.str("");
        string_stream_card1_channel_1.clear();

        if (!driver_manager.oneMinuteResetDone_card1_ch1 && totalSeconds >= 60)
        {
            fprintf(stderr, "[FD][Velocity card1 ch1] elapsed 1 min, resetting frame/error counters at %s\n",
                    timeStr.c_str());
            fflush(stderr);
            if (driver_manager.frameCount_card1_Ch1)        *driver_manager.frameCount_card1_Ch1 = 0;
            if (driver_manager.errorFrameCount_card1_Ch1)   *driver_manager.errorFrameCount_card1_Ch1 = 0;
            if (driver_manager.card1Props.frameCount_Ch1)        *driver_manager.card1Props.frameCount_Ch1 = 0;
            if (driver_manager.card1Props.errorFrameCount_Ch1)   *driver_manager.card1Props.errorFrameCount_Ch1 = 0;
            driver_manager.oneMinuteResetDone_card1_ch1 = true;
        }

        if (totalSeconds % 2 == 0)
        {
            rc = GRTV_GetFPGAJunctionTemp(driver_manager.First_Driver, &jtemp_card1_channel1);
            auto nowfpstime = std::chrono::steady_clock::now();
            std::chrono::duration<float> elapsed_fps_time = std::chrono::duration_cast<std::chrono::seconds>(nowfpstime - driver_manager.card1_ch1_time);
            driver_manager.fps_card1_ch1 = driver_manager.counter_card1_ch1 / (elapsed_fps_time.count() + 1e-9);

            if (totalSeconds >= 10)
            {
                int curFps = int(driver_manager.fps_card1_ch1);
                if (curFps < 10 && !driver_manager.fpsBelow10_card1_ch1)
                {
                    fprintf(stderr, "[FD][Velocity card1 ch1] FPS dropped below 10 at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card1_ch1 = true;
                }
                else if (curFps >= 10 && driver_manager.fpsBelow10_card1_ch1)
                {
                    fprintf(stderr, "[FD][Velocity card1 ch1] FPS recovered at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card1_ch1 = false;
                }
            }
        }

        if (loopbackTestMode)
        {
            sprintf(buffer_card1_channel_1, "Temperature: %d", jtemp_card1_channel1);
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(bgrImage_card1_channel1.cols - 270, 980),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_1, "FPS: %d", int(driver_manager.fps_card1_ch1));
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(bgrImage_card1_channel1.cols - 140, 930),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card1_ch1 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);
        }
        else
        {
            sprintf(buffer_card1_channel_1, "Ellapsed Time: %s", timeStr.c_str());
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(15, bgrImage_card1_channel1.rows - 700),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_1, "FPS: %d", int(driver_manager.fps_card1_ch1));
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(bgrImage_card1_channel1.cols - 140, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card1_ch1 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);

            sprintf(buffer_card1_channel_1, "Card: %s", driver_manager.card_1.c_str());
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(15, bgrImage_card1_channel1.rows - 780),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_1, "Channel: %s", driver_manager.channel_1.c_str());
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(15, bgrImage_card1_channel1.rows - 740),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_1, "Frame Count: %d", int(*driver_manager.frameCount_card1_Ch1));
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(15, bgrImage_card1_channel1.rows - 60),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_1, "Error Count: %d", int(*driver_manager.errorFrameCount_card1_Ch1));
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(15, bgrImage_card1_channel1.rows - 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_1, "Temperature: %d", jtemp_card1_channel1);
            cv::putText(bgrImage_card1_channel1, buffer_card1_channel_1, cv::Point(15, bgrImage_card1_channel1.rows - 660),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        }

        // Video Recording process
        if (driver_manager.video_card1_ch1.isOpened())
        {
            // driver_manager.video_card1_ch1.write(bgrImage_card1_channel1);
        }

        if (!loopbackTestMode && totalSeconds > 10)
        {
            rc = directory_manager.getErrorFramePath(Card::CARD_1, Channel::CH_1, error_frame_path_card1_channel1);
            checkReturnCode(rc, "getErrorFramePath failed");
            image_processor.calculateSSIM(bgrImage_card1_channel1, error_frame_path_card1_channel1,
                                          driver_manager.frameCount_card1_Ch1, driver_manager.errorFrameCount_card1_Ch1,
                                          driver_manager.previousImgCpu_card1_ch1, driver_manager.currentImgCpu_card1_ch1,
                                          &driver_manager.firstFrame_card1_Ch1, &driver_manager.scorePrevious_card1_ch1,
                                          driver_manager.card_1, driver_manager.channel_1);
        }

        // Card properties update
        if (driver_manager.card1Props.frameCount_Ch1 && driver_manager.frameCount_card1_Ch1)
        {
            *driver_manager.card1Props.frameCount_Ch1 = *driver_manager.frameCount_card1_Ch1;
        }
        if (driver_manager.card1Props.errorFrameCount_Ch1 && driver_manager.errorFrameCount_card1_Ch1)
        {
            *driver_manager.card1Props.errorFrameCount_Ch1 = *driver_manager.errorFrameCount_card1_Ch1;
        }
        driver_manager.card1Props.err_crc_ch1 = driver_manager.err_crc_card1_ch1;
        driver_manager.card1Props.err_b810b_ch1 = driver_manager.err_8b10b_card1_ch1;

        // queue'ya clone edilmiş frame gönder
        auto displayFrame = std::make_unique<cv::Mat>(bgrImage_card1_channel1);
        display_queue_card1_ch1->push(std::move(displayFrame));
    }
    catch (const cv::Exception &e)
    {
        LOG_ERROR("OpenCV Fault: " << e.what());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("General Fault : " << e.what());
    }
}
void DriverManager::handleCard1Channel2(UINT32 *data)
{
    try
    {

        driver_manager.counter_card1_ch2++;
        if (driver_manager.counter_card1_ch2 == 1)
        {
            driver_manager.card1_ch2_time = std::chrono::steady_clock::now();
            rc = GRTV_GetStatusErrors(driver_manager.First_Driver, &driver_manager.err_crc_card1_ch2, &driver_manager.err_8b10b_card1_ch2, 1);
        }
        else
        {
            rc = GRTV_GetStatusErrors(driver_manager.First_Driver, &driver_manager.err_crc_card1_ch2, &driver_manager.err_8b10b_card1_ch2, 0);
        }

        // 2. Fetch the error bit values AND clear the latched bits.
        rc = GRTV_GetStatusErrors(driver_manager.First_Driver, &driver_manager.err_crc_card1_ch2, &driver_manager.err_8b10b_card1_ch2, 0);

        image_processor.ConvertRawDataToBgr(data, driver_manager.card1Props.width, driver_manager.card1Props.height, bgrImage_card1_channel2);

        if (bgrImage_card1_channel2.empty())
        {
            LOG_ERROR("Image Converting Fault\n");
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - driver_manager.card1_ch2_time);
        int totalSeconds = static_cast<int>(elapsed.count());
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;

        std::stringstream string_stream_card1_channel_2;

        string_stream_card1_channel_2 << std::setw(2) << std::setfill('0') << hours << ":"
                                      << std::setw(2) << std::setfill('0') << minutes << ":"
                                      << std::setw(2) << std::setfill('0') << seconds;

        std::string timeStr = string_stream_card1_channel_2.str();
        string_stream_card1_channel_2.str("");
        string_stream_card1_channel_2.clear();

        if (!driver_manager.oneMinuteResetDone_card1_ch2 && totalSeconds >= 60)
        {
            fprintf(stderr, "[FD][Velocity card1 ch2] elapsed 1 min, resetting frame/error counters at %s\n",
                    timeStr.c_str());
            fflush(stderr);
            if (driver_manager.frameCount_card1_Ch2)        *driver_manager.frameCount_card1_Ch2 = 0;
            if (driver_manager.errorFrameCount_card1_Ch2)   *driver_manager.errorFrameCount_card1_Ch2 = 0;
            if (driver_manager.card1Props.frameCount_Ch2)        *driver_manager.card1Props.frameCount_Ch2 = 0;
            if (driver_manager.card1Props.errorFrameCount_Ch2)   *driver_manager.card1Props.errorFrameCount_Ch2 = 0;
            driver_manager.oneMinuteResetDone_card1_ch2 = true;
        }

        if (totalSeconds % 2 == 0)
        {
            rc = GRTV_GetFPGAJunctionTemp(driver_manager.First_Driver, &jtemp_card1_channel2);
            auto nowfpstime = std::chrono::steady_clock::now();
            std::chrono::duration<float> elapsed_fps_time = std::chrono::duration_cast<std::chrono::seconds>(nowfpstime - driver_manager.card1_ch2_time);
            driver_manager.fps_card1_ch2 = driver_manager.counter_card1_ch2 / (elapsed_fps_time.count() + 1e-9);

            if (totalSeconds >= 10)
            {
                int curFps = int(driver_manager.fps_card1_ch2);
                if (curFps < 10 && !driver_manager.fpsBelow10_card1_ch2)
                {
                    fprintf(stderr, "[FD][Velocity card1 ch2] FPS dropped below 10 at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card1_ch2 = true;
                }
                else if (curFps >= 10 && driver_manager.fpsBelow10_card1_ch2)
                {
                    fprintf(stderr, "[FD][Velocity card1 ch2] FPS recovered at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card1_ch2 = false;
                }
            }
        }

        if (loopbackTestMode)
        {
            rc = GRTV_GetFPGAJunctionTemp(driver_manager.First_Driver, &jtemp_card1_channel2);
            sprintf(buffer_card1_channel_2, "Temperature: %d", jtemp_card1_channel2);
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(bgrImage_card1_channel2.cols - 270, 980),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_2, "FPS: %d", int(driver_manager.fps_card1_ch2));
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(bgrImage_card1_channel2.cols - 140, 930),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card1_ch2 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);
        }
        else
        {
            sprintf(buffer_card1_channel_2, "Ellapsed Time: %s", timeStr.c_str());
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(15, bgrImage_card1_channel2.rows - 700),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_2, "FPS: %d", int(driver_manager.fps_card1_ch2));
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(bgrImage_card1_channel2.cols - 140, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card1_ch2 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);

            sprintf(buffer_card1_channel_2, "Card: %s", driver_manager.card_1.c_str());
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(15, bgrImage_card1_channel2.rows - 780),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_2, "Channel: %s", driver_manager.channel_2.c_str());
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(15, bgrImage_card1_channel2.rows - 740),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_2, "Frame Count: %d", int(*driver_manager.frameCount_card1_Ch2));
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(15, bgrImage_card1_channel2.rows - 60),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_2, "Error Count: %d", int(*driver_manager.errorFrameCount_card1_Ch2));
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(15, bgrImage_card1_channel2.rows - 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card1_channel_2, "Temperature: %d", jtemp_card1_channel2);
            cv::putText(bgrImage_card1_channel2, buffer_card1_channel_2, cv::Point(15, bgrImage_card1_channel2.rows - 660),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        }

        // Video kaydet
        if (driver_manager.video_card1_ch2.isOpened())
        {
            // driver_manager.video_card1_ch2.write(bgrImage_card1_channel2);
        }

        if (!loopbackTestMode && totalSeconds > 10)
        {
            rc = directory_manager.getErrorFramePath(Card::CARD_1, Channel::CH_2, error_frame_path_card1_channel2);
            checkReturnCode(rc, "getErrorFramePath failed");
            // Calcualte SSIM score
            image_processor.calculateSSIM(bgrImage_card1_channel2, error_frame_path_card1_channel2,
                                          driver_manager.frameCount_card1_Ch2, driver_manager.errorFrameCount_card1_Ch2,
                                          driver_manager.previousImgCpu_card1_ch2, driver_manager.currentImgCpu_card1_ch2,
                                          &driver_manager.firstFrame_card1_Ch2, &driver_manager.scorePrevious_card1_ch2,
                                          driver_manager.card_1, driver_manager.channel_2);
        }

        // Card properties update
        if (driver_manager.card1Props.frameCount_Ch2 && driver_manager.frameCount_card1_Ch2)
        {
            *driver_manager.card1Props.frameCount_Ch2 = *driver_manager.frameCount_card1_Ch2;
        }
        if (driver_manager.card1Props.errorFrameCount_Ch2 && driver_manager.errorFrameCount_card1_Ch2)
        {
            *driver_manager.card1Props.errorFrameCount_Ch2 = *driver_manager.errorFrameCount_card1_Ch2;
        }

        driver_manager.card1Props.err_crc_ch2 = driver_manager.err_crc_card1_ch2;
        driver_manager.card1Props.err_b810b_ch2 = driver_manager.err_8b10b_card1_ch2;

        auto displayFrame = std::make_unique<cv::Mat>(bgrImage_card1_channel2);
        display_queue_card1_ch2->push(std::move(displayFrame));
    }
    catch (const cv::Exception &e)
    {
        LOG_ERROR("OpenCV fault: " << e.what());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("General Fault: " << e.what());
    }
}

void DriverManager::handleCard2Channel1(UINT32 *data)
{

    try
    {
        driver_manager.counter_card2_ch1++;
        if (driver_manager.counter_card2_ch1 == 1)
        {
            driver_manager.card2_ch1_time = std::chrono::steady_clock::now();
            rc = GRTV_GetStatusErrors(driver_manager.Second_Driver, &driver_manager.err_crc_card2_ch1, &driver_manager.err_8b10b_card2_ch1, 1);
        }
        else
        {
            rc = GRTV_GetStatusErrors(driver_manager.Second_Driver, &driver_manager.err_crc_card2_ch1, &driver_manager.err_8b10b_card2_ch1, 1);
        }

        if (rc != GRTV_OK)
        {
            char *errStr;
            GRTV_GetErrorStr(rc, &errStr);
            LOG_ERROR("GRTV_GetRawDataDch Fault: " << errStr);
        }

        // 2. Fetch the error bit values AND clear the latched bits.
        rc = GRTV_GetStatusErrors(driver_manager.Second_Driver, &driver_manager.err_crc_card2_ch1, &driver_manager.err_8b10b_card2_ch1, 0);

        image_processor.ConvertRawDataToBgr(data, driver_manager.card2Props.width, driver_manager.card2Props.height, bgrImage_card2_channel1);

        if (bgrImage_card2_channel1.empty())
        {
            LOG_ERROR("Image Converting Fault\n");
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - driver_manager.card2_ch1_time);
        int totalSeconds = static_cast<int>(elapsed.count());
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;

        std::stringstream string_stream_card2_channel_2;

        string_stream_card2_channel_1 << std::setw(2) << std::setfill('0') << hours << ":"
                                      << std::setw(2) << std::setfill('0') << minutes << ":"
                                      << std::setw(2) << std::setfill('0') << seconds;

        std::string timeStr = string_stream_card2_channel_1.str();
        string_stream_card2_channel_1.str("");
        string_stream_card2_channel_1.clear();

        if (!driver_manager.oneMinuteResetDone_card2_ch1 && totalSeconds >= 60)
        {
            fprintf(stderr, "[FD][Velocity card2 ch1] elapsed 1 min, resetting frame/error counters at %s\n",
                    timeStr.c_str());
            fflush(stderr);
            if (driver_manager.frameCount_card2_Ch1)        *driver_manager.frameCount_card2_Ch1 = 0;
            if (driver_manager.errorFrameCount_card2_Ch1)   *driver_manager.errorFrameCount_card2_Ch1 = 0;
            if (driver_manager.card2Props.frameCount_Ch1)        *driver_manager.card2Props.frameCount_Ch1 = 0;
            if (driver_manager.card2Props.errorFrameCount_Ch1)   *driver_manager.card2Props.errorFrameCount_Ch1 = 0;
            driver_manager.oneMinuteResetDone_card2_ch1 = true;
        }

        if (totalSeconds % 2 == 0)
        {
            rc = GRTV_GetFPGAJunctionTemp(driver_manager.Second_Driver, &jtemp_card2_channel1);
            auto nowfpstime = std::chrono::steady_clock::now();
            std::chrono::duration<float> elapsed_fps_time = std::chrono::duration_cast<std::chrono::seconds>(nowfpstime - driver_manager.card2_ch1_time);
            driver_manager.fps_card2_ch1 = driver_manager.counter_card2_ch1 / (elapsed_fps_time.count() + 1e-9);

            if (totalSeconds >= 10)
            {
                int curFps = int(driver_manager.fps_card2_ch1);
                if (curFps < 10 && !driver_manager.fpsBelow10_card2_ch1)
                {
                    fprintf(stderr, "[FD][Velocity card2 ch1] FPS dropped below 10 at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card2_ch1 = true;
                }
                else if (curFps >= 10 && driver_manager.fpsBelow10_card2_ch1)
                {
                    fprintf(stderr, "[FD][Velocity card2 ch1] FPS recovered at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card2_ch1 = false;
                }
            }
        }

        if (loopbackTestMode)
        {
            sprintf(buffer_card2_channel_1, "FPS: %d", int(driver_manager.fps_card2_ch1));
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(bgrImage_card2_channel1.cols - 140, 930),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card2_ch1 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);

            sprintf(buffer_card2_channel_1, "Temperature: %d", jtemp_card2_channel1);
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(bgrImage_card2_channel1.cols - 270, 980),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        }
        else
        {
            sprintf(buffer_card2_channel_1, "Ellapsed Time: %s", timeStr.c_str());
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(15, bgrImage_card2_channel1.rows - 700),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_1, "FPS: %d", int(driver_manager.fps_card2_ch1));
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(bgrImage_card2_channel1.cols - 140, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card2_ch1 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);

            sprintf(buffer_card2_channel_1, "Card: %s", driver_manager.card_2.c_str());
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(15, bgrImage_card2_channel1.rows - 780),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_1, "Channel: %s", driver_manager.channel_1.c_str());
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(15, bgrImage_card2_channel1.rows - 740),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_1, "Frame Count: %d", int(*driver_manager.frameCount_card2_Ch1));
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(15, bgrImage_card2_channel1.rows - 60),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_1, "Error Count: %d", int(*driver_manager.errorFrameCount_card2_Ch1));
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(15, bgrImage_card2_channel1.rows - 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_1, "Temperature: %d", jtemp_card2_channel1);
            cv::putText(bgrImage_card2_channel1, buffer_card2_channel_1, cv::Point(15, bgrImage_card2_channel1.rows - 660),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        }

        // Video kaydet
        if (driver_manager.video_card2_ch1.isOpened())
        {
            // driver_manager.video_card2_ch1.write(bgrImage_card2_channel1);
        }

        if (!loopbackTestMode && totalSeconds > 10)
        {
            rc = directory_manager.getErrorFramePath(Card::CARD_2, Channel::CH_1, error_frame_path_card2_channel1);
            checkReturnCode(rc, "getErrorFramePath failed");
            image_processor.calculateSSIM(bgrImage_card2_channel1, error_frame_path_card2_channel1,
                                          driver_manager.frameCount_card2_Ch1, driver_manager.errorFrameCount_card2_Ch1,
                                          driver_manager.previousImgCpu_card2_ch1, driver_manager.currentImgCpu_card2_ch1,
                                          &driver_manager.firstFrame_card2_Ch1, &driver_manager.scorePrevious_card2_ch1,
                                          driver_manager.card_2, driver_manager.channel_1);
        }

        // Calculate SSIM score and update statistic

        // Card properties update
        if (driver_manager.card2Props.frameCount_Ch1 && driver_manager.frameCount_card2_Ch1)
        {
            *driver_manager.card2Props.frameCount_Ch1 = *driver_manager.frameCount_card2_Ch1;
        }
        if (driver_manager.card2Props.errorFrameCount_Ch1 && driver_manager.errorFrameCount_card2_Ch1)
        {
            *driver_manager.card2Props.errorFrameCount_Ch1 = *driver_manager.errorFrameCount_card2_Ch1;
        }

        driver_manager.card2Props.err_crc_ch1 = driver_manager.err_crc_card2_ch1;
        driver_manager.card2Props.err_b810b_ch1 = driver_manager.err_8b10b_card2_ch1;

        auto displayFrame = std::make_unique<cv::Mat>(bgrImage_card2_channel1);
        display_queue_card2_ch1->push(std::move(displayFrame));
    }
    catch (const cv::Exception &e)
    {
        LOG_ERROR("OpenCV Fault: " << e.what());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("General Fault: " << e.what());
    }
}

void DriverManager::handleCard2Channel2(UINT32 *data)
{

    auto test_bas = std::chrono::steady_clock::now();
    try
    {
        driver_manager.counter_card2_ch2++;

        if (driver_manager.counter_card2_ch2 == 1)
        {
            driver_manager.card2_ch2_time = std::chrono::steady_clock::now();
            rc = GRTV_GetStatusErrors(driver_manager.Second_Driver, &driver_manager.err_crc_card2_ch2, &driver_manager.err_8b10b_card2_ch2, 1);
        }
        else
        {
            rc = GRTV_GetStatusErrors(driver_manager.Second_Driver, &driver_manager.err_crc_card2_ch2, &driver_manager.err_8b10b_card2_ch2, 0);
        }

        // 2. Fetch the error bit values AND clear the latched bits.
        rc = GRTV_GetStatusErrors(driver_manager.Second_Driver, &driver_manager.err_crc_card2_ch2, &driver_manager.err_8b10b_card2_ch2, 0);

        image_processor.ConvertRawDataToBgr(data, driver_manager.card2Props.width, driver_manager.card2Props.height, bgrImage_card2_channel2);

        if (bgrImage_card2_channel2.empty())
        {
            LOG_ERROR("Image Converting Fault\n");
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - driver_manager.card2_ch2_time);
        int totalSeconds = static_cast<int>(elapsed.count());
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;

        std::stringstream string_stream_card2_channel_2;
        string_stream_card2_channel_2 << std::setw(2) << std::setfill('0') << hours << ":"
                                      << std::setw(2) << std::setfill('0') << minutes << ":"
                                      << std::setw(2) << std::setfill('0') << seconds;

        std::string timeStr = string_stream_card2_channel_2.str();
        string_stream_card2_channel_2.str("");
        string_stream_card2_channel_2.clear();

        if (!driver_manager.oneMinuteResetDone_card2_ch2 && totalSeconds >= 60)
        {
            fprintf(stderr, "[FD][Velocity card2 ch2] elapsed 1 min, resetting frame/error counters at %s\n",
                    timeStr.c_str());
            fflush(stderr);
            if (driver_manager.frameCount_card2_Ch2)        *driver_manager.frameCount_card2_Ch2 = 0;
            if (driver_manager.errorFrameCount_card2_Ch2)   *driver_manager.errorFrameCount_card2_Ch2 = 0;
            if (driver_manager.card2Props.frameCount_Ch2)        *driver_manager.card2Props.frameCount_Ch2 = 0;
            if (driver_manager.card2Props.errorFrameCount_Ch2)   *driver_manager.card2Props.errorFrameCount_Ch2 = 0;
            driver_manager.oneMinuteResetDone_card2_ch2 = true;
        }

        if (totalSeconds % 2 == 0)
        {
            rc = GRTV_GetFPGAJunctionTemp(driver_manager.Second_Driver, &jtemp_card2_channel2);
            auto nowfpstime = std::chrono::steady_clock::now();
            std::chrono::duration<float> elapsed_fps_time = nowfpstime - driver_manager.card2_ch2_time;
            driver_manager.fps_card2_ch2 = driver_manager.counter_card2_ch2 / (elapsed_fps_time.count() + 1e-9f);

            if (totalSeconds >= 10)
            {
                int curFps = int(driver_manager.fps_card2_ch2);
                if (curFps < 10 && !driver_manager.fpsBelow10_card2_ch2)
                {
                    fprintf(stderr, "[FD][Velocity card2 ch2] FPS dropped below 10 at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card2_ch2 = true;
                }
                else if (curFps >= 10 && driver_manager.fpsBelow10_card2_ch2)
                {
                    fprintf(stderr, "[FD][Velocity card2 ch2] FPS recovered at %s (fps=%d)\n",
                            timeStr.c_str(), curFps);
                    fflush(stderr);
                    driver_manager.fpsBelow10_card2_ch2 = false;
                }
            }
        }

        if (loopbackTestMode)
        {
            sprintf(buffer_card2_channel_2, "FPS: %d", int(driver_manager.fps_card2_ch2));
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(bgrImage_card2_channel2.cols - 140, 930),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card2_ch2 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);

            sprintf(buffer_card2_channel_2, "Temperature: %d", jtemp_card2_channel2);
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(bgrImage_card2_channel2.cols - 270, 980),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        }
        else
        {
            sprintf(buffer_card2_channel_2, "Ellapsed Time: %s", timeStr.c_str());
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(15, bgrImage_card2_channel2.rows - 700),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_2, "FPS: %d", int(driver_manager.fps_card2_ch2));
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(bgrImage_card2_channel2.cols - 140, 40),
                        cv::FONT_HERSHEY_SIMPLEX, 1,
                        (driver_manager.fps_card2_ch2 < 10 ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0)), 2);

            sprintf(buffer_card2_channel_2, "Card: %s", driver_manager.card_2.c_str());
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(15, bgrImage_card2_channel2.rows - 780),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_2, "Channel: %s", driver_manager.channel_2.c_str());
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(15, bgrImage_card2_channel2.rows - 740),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_2, "Frame Count: %d", int(*driver_manager.frameCount_card2_Ch2));
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(15, bgrImage_card2_channel2.rows - 60),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_2, "Error Count: %d", int(*driver_manager.errorFrameCount_card2_Ch2));
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(15, bgrImage_card2_channel2.rows - 20),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

            sprintf(buffer_card2_channel_2, "Temperature: %d", jtemp_card2_channel2);
            cv::putText(bgrImage_card2_channel2, buffer_card2_channel_2, cv::Point(15, bgrImage_card2_channel2.rows - 660),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        }

        // Video Recording
        if (driver_manager.video_card2_ch2.isOpened())
        {
            // driver_manager.video_card2_ch2.write(bgrImage_card2_channel2);
        }

        if (!loopbackTestMode && totalSeconds > 10)
        {
            rc = directory_manager.getErrorFramePath(Card::CARD_2, Channel::CH_2, error_frame_path_card2_channel2);
            checkReturnCode(rc, "getErrorFramePath failed");
            image_processor.calculateSSIM(bgrImage_card2_channel2, error_frame_path_card2_channel2,
                                          driver_manager.frameCount_card2_Ch2, driver_manager.errorFrameCount_card2_Ch2,
                                          driver_manager.previousImgCpu_card2_ch2, driver_manager.currentImgCpu_card2_ch2,
                                          &driver_manager.firstFrame_card2_Ch2, &driver_manager.scorePrevious_card2_ch2,
                                          driver_manager.card_2, driver_manager.channel_2);
        }

        // Calculate SSIM score and update statictic

        // Card properties update
        if (driver_manager.card2Props.frameCount_Ch2 && driver_manager.frameCount_card2_Ch2)
        {
            *driver_manager.card2Props.frameCount_Ch2 = *driver_manager.frameCount_card2_Ch2;
        }
        if (driver_manager.card2Props.errorFrameCount_Ch2 && driver_manager.errorFrameCount_card2_Ch2)
        {
            *driver_manager.card2Props.errorFrameCount_Ch2 = *driver_manager.errorFrameCount_card2_Ch2;
        }

        driver_manager.card2Props.err_crc_ch2 = driver_manager.err_crc_card2_ch2;
        driver_manager.card2Props.err_b810b_ch2 = driver_manager.err_8b10b_card2_ch2;

        auto displayFrame = std::make_unique<cv::Mat>(bgrImage_card2_channel2);
        display_queue_card2_ch2->push(std::move(displayFrame));
    }
    catch (const cv::Exception &e)
    {
        LOG_ERROR("OpenCV Fault: " << e.what());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("General Fault: " << e.what());
    }

    auto test_bit = std::chrono::steady_clock::now();
    auto elapsed_test = std::chrono::duration_cast<std::chrono::milliseconds>(test_bit - test_bas);
}

void DriverManager::display_worker_card1_ch1()
{
    // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Eşzamanlı açılmayı engellemek için

    std::cout << "Card1 CH1 display thread başlatıldı." << std::endl;
    setThreadAffinity(4);

    const std::string windowName = "Card1 CH1";

    try
    {
        cv::namedWindow(windowName);
    }
    catch (const cv::Exception &e)
    {
        std::cerr << "[ERROR] namedWindow hatası: " << e.what() << std::endl;
        return;
    }
    std::unique_ptr<cv::Mat> frame;
    cv::Mat resizedFrame;

    while (isRunning_card1_channel1)
    {
        if (!display_queue_card1_ch1->wait_and_pop(frame))
            continue;

        if (frame && !frame->empty())
        {
            try
            {
                cv::resize(*frame, resizedFrame, cv::Size(), 0.5, 0.5);
                cv::imshow(windowName, resizedFrame);
                driver_manager.video_card1_ch1.write(*frame);

                cv::waitKey(1);
            }
            catch (const cv::Exception &e)
            {
                std::cerr << "[ERROR] imshow hatası: " << e.what() << std::endl;
            }
        }
    }

    cv::destroyWindow(windowName);
    cv::waitKey(1);
    std::cout << "Card1 CH1 display thread sonlandırıldı." << std::endl;
}

void DriverManager::display_worker_card1_ch2()
{
    // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Eşzamanlı açılmayı engellemek için

    std::cout << "Card1 CH2 display thread başlatıldı." << std::endl;
    setThreadAffinity(5);

    const std::string windowName = "Card1 CH2";

    try
    {
        cv::namedWindow(windowName);
    }
    catch (const cv::Exception &e)
    {
        std::cerr << "[ERROR] namedWindow hatası: " << e.what() << std::endl;
        return;
    }

    std::unique_ptr<cv::Mat> frame;
    cv::Mat resizedFrame;

    while (isRunning_card1_channel2)
    {
        if (!display_queue_card1_ch2->wait_and_pop(frame))
            continue;

        if (frame && !frame->empty())
        {
            try
            {
                cv::resize(*frame, resizedFrame, cv::Size(), 0.5, 0.5);
                cv::imshow(windowName, resizedFrame);
                driver_manager.video_card1_ch2.write(*frame);
                cv::waitKey(1);
            }
            catch (const cv::Exception &e)
            {
                std::cerr << "[ERROR] imshow hatası: " << e.what() << std::endl;
            }
        }
    }

    cv::destroyWindow(windowName);
    cv::waitKey(1);
    std::cout << "Card1 CH2 display thread sonlandırıldı." << std::endl;
}

void DriverManager::display_worker_card2_ch1()
{
    // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Eşzamanlı açılmayı engellemek için

    std::cout << "Card2 CH1 display thread başlatıldı." << std::endl;
    setThreadAffinity(6);

    const std::string windowName = "Card2 CH1";

    try
    {
        cv::namedWindow(windowName);
    }
    catch (const cv::Exception &e)
    {
        std::cerr << "[ERROR] namedWindow hatası: " << e.what() << std::endl;
        return;
    }

    std::unique_ptr<cv::Mat> frame;
    cv::Mat resizedFrame;
    while (isRunning_card2_channel1)
    {
        std::unique_ptr<cv::Mat> frame;
        if (!display_queue_card2_ch1->wait_and_pop(frame))
            continue;

        if (frame && !frame->empty())
        {
            try
            {
                cv::resize(*frame, resizedFrame, cv::Size(), 0.5, 0.5);
                cv::imshow(windowName, resizedFrame);
                driver_manager.video_card2_ch1.write(*frame);
                cv::waitKey(1);
            }
            catch (const cv::Exception &e)
            {
                std::cerr << "[ERROR] imshow hatası: " << e.what() << std::endl;
            }
        }
    }

    cv::destroyWindow(windowName);
    cv::waitKey(1);
    std::cout << "Card2 CH1 display thread sonlandırıldı." << std::endl;
}

void DriverManager::display_worker_card2_ch2()
{
    // std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Eşzamanlı açılmayı engellemek için

    std::cout << "Card2 CH2 display thread başlatıldı." << std::endl;
    setThreadAffinity(7);

    const std::string windowName = "Card2 CH2";

    try
    {
        cv::namedWindow(windowName);
    }
    catch (const cv::Exception &e)
    {
        std::cerr << "[ERROR] namedWindow hatası: " << e.what() << std::endl;
        return;
    }

    std::unique_ptr<cv::Mat> frame;
    cv::Mat resizedFrame;

    while (isRunning_card2_channel2)
    {
        std::unique_ptr<cv::Mat> frame;
        if (!display_queue_card2_ch2->wait_and_pop(frame))
            continue;

        if (frame && !frame->empty())
        {
            try
            {
                cv::resize(*frame, resizedFrame, cv::Size(), 0.5, 0.5);
                cv::imshow(windowName, resizedFrame);
                driver_manager.video_card2_ch2.write(*frame);

                cv::waitKey(1);
            }
            catch (const cv::Exception &e)
            {
                std::cerr << "[ERROR] imshow hatası: " << e.what() << std::endl;
            }
        }
    }

    cv::destroyWindow(windowName);
    cv::waitKey(1);
    std::cout << "Card2 CH2 display thread sonlandırıldı." << std::endl;
}

void DriverManager::startDisplayThreadCard1Ch1()
{
    display_card1_ch1_thread = std::thread(&DriverManager::display_worker_card1_ch1, this);
}
void DriverManager::startDisplayThreadCard1Ch2()
{
    display_card1_ch2_thread = std::thread(&DriverManager::display_worker_card1_ch2, this);
}
void DriverManager::startDisplayThreadCard2Ch1()
{
    display_card2_ch1_thread = std::thread(&DriverManager::display_worker_card2_ch1, this);
}
void DriverManager::startDisplayThreadCard2Ch2()
{
    display_card2_ch2_thread = std::thread(&DriverManager::display_worker_card2_ch2, this);
}

void DriverManager::stopDisplayThreadCard1Ch1()
{
    isRunning_card1_channel1 = false;
    display_queue_card1_ch1->push(std::make_unique<cv::Mat>());

    if (display_card1_ch1_thread.joinable())
        display_card1_ch1_thread.join();
}
void DriverManager::stopDisplayThreadCard1Ch2()
{
    isRunning_card1_channel2 = false;
    display_queue_card1_ch2->push(std::make_unique<cv::Mat>());

    if (display_card1_ch2_thread.joinable())
        display_card1_ch2_thread.join();
}

void DriverManager::stopDisplayThreadCard2Ch1()
{
    isRunning_card2_channel1 = false;
    display_queue_card2_ch1->push(std::make_unique<cv::Mat>());

    if (display_card2_ch1_thread.joinable())
        display_card2_ch1_thread.join();
}

void DriverManager::stopDisplayThreadCard2Ch2()
{
    isRunning_card2_channel2 = false;
    display_queue_card2_ch2->push(std::make_unique<cv::Mat>());

    if (display_card2_ch2_thread.joinable())
        display_card2_ch2_thread.join();
}

void DriverManager::threadWorker_Card1_CH1()
{
    std::unique_ptr<UINT32[]> data;
    while (isRunning_card1_channel1 && driver_manager.gpMem1_1)
    {

        {
            std::unique_lock<std::mutex> lock(mutex_card1_ch1);
            cv_card1_ch1.wait(lock, [this]
                              { return !queue_card1_ch1.empty() || !isRunning_card1_channel1; });

            if (!isRunning_card1_channel1 && queue_card1_ch1.empty())
                break;

            data.reset(queue_card1_ch1.front()); // sahipliği al
            queue_card1_ch1.pop();
        }

        this->handleCard1Channel1(data.get()); // ham pointer'ı kullan
    }
}

void DriverManager::threadWorker_Card1_CH2()
{
    std::unique_ptr<UINT32[]> data;
    while (isRunning_card1_channel2 && driver_manager.gpMem1_2)
    {

        {
            std::unique_lock<std::mutex> lock(mutex_card1_ch2);
            cv_card1_ch2.wait(lock, [this]
                              { return !queue_card1_ch2.empty() || !isRunning_card1_channel2; });

            if (!isRunning_card1_channel2 && queue_card1_ch2.empty())
                break;

            data.reset(queue_card1_ch2.front()); // sahipliği al
            queue_card1_ch2.pop();
        }

        this->handleCard1Channel2(data.get()); // ham pointer'ı işle
    }
}

void DriverManager::threadWorker_Card2_CH1()
{
    std::unique_ptr<UINT32[]> data;
    while (isRunning_card2_channel1 && driver_manager.gpMem2_1)
    {

        {
            std::unique_lock<std::mutex> lock(mutex_card2_ch1);
            cv_card2_ch1.wait(lock, [this]
                              { return !queue_card2_ch1.empty() || !isRunning_card2_channel1; });

            if (!isRunning_card2_channel1 && queue_card2_ch1.empty())
                break;

            data.reset(queue_card2_ch1.front()); // sahipliği devral
            queue_card2_ch1.pop();
        }

        this->handleCard2Channel1(data.get()); // veriyi işle
    }
}

void DriverManager::threadWorker_Card2_CH2()
{
    std::unique_ptr<UINT32[]> data;
    while (isRunning_card2_channel2 && driver_manager.gpMem2_2)
    {

        {
            std::unique_lock<std::mutex> lock(mutex_card2_ch2);
            cv_card2_ch2.wait(lock, [this]
                              { return !queue_card2_ch2.empty() || !isRunning_card2_channel2; });

            if (!isRunning_card2_channel2 && queue_card2_ch2.empty())
                break;

            data.reset(queue_card2_ch2.front()); // sahipliği al
            queue_card2_ch2.pop();
        }

        this->handleCard2Channel2(data.get()); // veriyi işle
    }
}

// Callback function triggered when new frame data is available from CARD_1
// Processes CH_1 or CH_2 based on interrupt parameter, handles image decoding, overlay text,
// SSIM analysis, video writing, and live display output
// Check if interrupt is for Channel 1 and memory buffer exists
// Retrieve raw frame data from memory
// Convert raw RGB data to BGR image using OpenCV
// Calculate FPS and elapsed time for on-screen overlay
// Overlay metadata (FPS, time, card name, channel, frame count, errors, temperature)
// Save frame to video file
// Calculate SSIM and detect flicker; save mismatches as images
// Update statistics and show the resized image on screen
// @param u32Param: interrupt identifier indicating which channel's frame is ready (e.g., GRTV_INTMEM1FRAMEDONE)
// @return 1 if frame was successfully processed, 0 otherwise
UINT32 DriverManager::UserCallBack_Card1(UINT32 u32Param)
{
    if (!driver_manager.isRunning)
        return 0;

    UINT32 interruptType = u32Param & 0x0F;
    driver_manager.gAsyncCount_1++;

    switch (interruptType)
    {
    case GRTV_INTMEM1FRAMEDONE:
        if (driver_manager.gpMem1_1)
        {
            rc = GRTV_GetRawDataDch(driver_manager.First_Driver,
                                    (UINT_PTR)driver_manager.gpMem1_1,
                                    driver_manager.gMemSize,
                                    GRTV_ADDR_MEM1 + GRTV_A818_IMG_OFFSET);

            if (rc != 0)
            {
                LOG_ERROR("CH1 - Failed to get DMA data");
                break;
            }

            auto copiedFrame = std::make_unique<UINT32[]>(driver_manager.gMemSize / sizeof(UINT32));
            std::memcpy(copiedFrame.get(), driver_manager.gpMem1_1, driver_manager.gMemSize);

            {
                std::lock_guard<std::mutex> lock(mutex_card1_ch1);
                queue_card1_ch1.push(copiedFrame.release());
            }

            cv_card1_ch1.notify_one();
        }
        break;

    case GRTV_INTMEM2FRAMEDONE:
        if (driver_manager.gpMem1_2)
        {
            rc = GRTV_GetRawDataDch(driver_manager.First_Driver,
                                    (UINT_PTR)driver_manager.gpMem1_2,
                                    driver_manager.gMemSize,
                                    GRTV_ADDR_MEM2 + GRTV_A818_IMG_OFFSET);

            if (rc != 0)
            {
                LOG_ERROR("CH2 - Failed to get DMA data");
                break;
            }

            auto copiedFrame = std::make_unique<UINT32[]>(driver_manager.gMemSize / sizeof(UINT32));
            std::memcpy(copiedFrame.get(), driver_manager.gpMem1_2, driver_manager.gMemSize);

            {
                std::lock_guard<std::mutex> lock(mutex_card1_ch2);
                queue_card1_ch2.push(copiedFrame.release());
            }

            cv_card1_ch2.notify_one();
        }
        break;

    default:
        if ((driver_manager.gAsyncCount_1 % 40) == 0)
        {
            LOG_WARN("Card1 is active but received unknown interrupt type.");
        }
        return 0;
    }

    return 1;
}

// Callback function triggered when new frame data is available from CARD_1
// Processes CH_1 or CH_2 based on interrupt parameter, handles image decoding, overlay text,
// SSIM analysis, video writing, and live display output
// Check if interrupt is for Channel 1 and memory buffer exists
// Retrieve raw frame data from memory
// Convert raw RGB data to BGR image using OpenCV
// Calculate FPS and elapsed time for on-screen overlay
// Overlay metadata (FPS, time, card name, channel, frame count, errors, temperature)
// Save frame to video file
// Calculate SSIM and detect flicker; save mismatches as images
// Update statistics and show the resized image on screen
// @param u32Param: interrupt identifier indicating which channel's frame is ready (e.g., GRTV_INTMEM1FRAMEDONE)
// @return 1 if frame was successfully processed, 0 otherwise

UINT32 DriverManager::UserCallBack_Card2(UINT32 u32Param)
{
    if (!driver_manager.isRunning)
        return 0;

    UINT32 interruptType = u32Param & 0x0F;

    driver_manager.gAsyncCount_2++;

    switch (interruptType)
    {
    case GRTV_INTMEM1FRAMEDONE:
        if (driver_manager.gpMem2_1)
        {
            std::lock_guard<std::mutex> lock(mutex_card2_ch1);

            rc = GRTV_GetRawDataDch(driver_manager.Second_Driver,
                                    (UINT_PTR)driver_manager.gpMem2_1,
                                    driver_manager.gMemSize,
                                    GRTV_ADDR_MEM1 + GRTV_A818_IMG_OFFSET);

            auto copiedFrame = std::make_unique<UINT32[]>(driver_manager.gMemSize / sizeof(UINT32));
            std::memcpy(copiedFrame.get(), driver_manager.gpMem2_1, driver_manager.gMemSize);

            queue_card2_ch1.push(copiedFrame.release());
            cv_card2_ch1.notify_one();
        }
        break;

    case GRTV_INTMEM2FRAMEDONE:
        if (driver_manager.gpMem2_2)
        {
            std::lock_guard<std::mutex> lock(mutex_card2_ch2);

            rc = GRTV_GetRawDataDch(driver_manager.Second_Driver,
                                    (UINT_PTR)driver_manager.gpMem2_2,
                                    driver_manager.gMemSize,
                                    GRTV_ADDR_MEM2 + GRTV_A818_IMG_OFFSET);

            auto copiedFrame = std::make_unique<UINT32[]>(driver_manager.gMemSize / sizeof(UINT32));
            std::memcpy(copiedFrame.get(), driver_manager.gpMem2_2, driver_manager.gMemSize);

            queue_card2_ch2.push(copiedFrame.release());
            cv_card2_ch2.notify_one();
        }
        break;

    default:
        if ((driver_manager.gAsyncCount_2 % 160) == 0)
        {
            LOG_WARN("Card is activate but channels are closed.");
        }
        return 0;
    }

    return 1;
}

// Releases video writer resources for the specified card
// Stops recording and closes output video streams for both channels of the selected card(s)
// @param card: CARD_1, CARD_2, or CARD_BOTH indicating which video writers to release
// @return void
uint8_t DriverManager::videoWritersRelease(Card card)
{
    try
    {
        if (card == CARD_1 || card == CARD_BOTH)
        {
            video_card1_ch1.release();
            video_card1_ch2.release();
        }

        if (card == CARD_2 || card == CARD_BOTH)
        {
            video_card2_ch1.release();
            video_card2_ch2.release();
        }
        LOG_INFO("Video writers released for card: " << card);
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error releasing video writers: " << e.what());
        return CODE_VIDEO_WRITER_RELEASE_FAILED;
    }
}

// Stops video capture by releasing all video writers and disabling flow control
// Ensures all output video files are properly finalized and written to disk
// @return void
uint8_t DriverManager::stopVideoCapture()
{
    try
    {
        fprintf(stderr, "[stopVideoCapture] -> setFlowControl(false)\n");
        rc = setFlowControl(false);
        checkReturnCode(rc, "setFlowControl failed");
        fprintf(stderr, "[stopVideoCapture] setFlowControl done\n");

        if (driver_manager.First_Driver != 0)
        {
            fprintf(stderr, "[stopVideoCapture] stopping CARD_1 threads\n");
            stopCard1Channel1Thread();
            stopCard1Channel2Thread();
        }
        if (driver_manager.Second_Driver != 0)
        {
            fprintf(stderr, "[stopVideoCapture] stopping CARD_2 threads\n");
            stopCard2Channel1Thread();
            stopCard2Channel2Thread();
        }

        usleep(200000);

        fprintf(stderr, "[stopVideoCapture] releasing video writers\n");
        video_card1_ch1.release();
        video_card1_ch2.release();
        video_card2_ch1.release();
        video_card2_ch2.release();

        fprintf(stderr, "[stopVideoCapture] done\n");
        LOG_INFO("Video capturing stopped.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "[stopVideoCapture] EXC: %s\n", e.what());
        LOG_ERROR("Error stopping video capture: " << e.what());
        return CODE_STOP_VIDEO_CAPTURE_FAILED;
    }
}

// Prints a summary of total and error frame counts for each card and channel
// Useful for evaluating flicker detection performance and signal reliability
// @return void
uint8_t DriverManager::printStatistics()
{
    try
    {
        std::cout << "\n📊 Velocity Statistic\n";
        std::cout << "------------------------\n";
        std::cout << "Card 1:\n";
        std::cout << "  Channel 1 - Error frame count: " << *card1Props.errorFrameCount_Ch1
                  << " / Total Frame: " << *card1Props.frameCount_Ch1 << std::endl;
        std::cout << "             CRC Errors: " << card1Props.err_crc_ch1
                  << " | 8B10B Errors: " << card1Props.err_b810b_ch1 << std::endl;

        std::cout << "  Channel 2 - Error frame count: " << *card1Props.errorFrameCount_Ch2
                  << " / Total Frame: " << *card1Props.frameCount_Ch2 << std::endl;
        std::cout << "             CRC Errors: " << card1Props.err_crc_ch2
                  << " | 8B10B Errors: " << card1Props.err_b810b_ch2 << std::endl;

        std::cout << "\nCard 2:\n";
        std::cout << "  Channel 1 - Error frame count: " << *card2Props.errorFrameCount_Ch1
                  << " / Total Frame: " << *card2Props.frameCount_Ch1 << std::endl;
        std::cout << "             CRC Errors: " << card2Props.err_crc_ch1
                  << " | 8B10B Errors: " << card2Props.err_b810b_ch1 << std::endl;

        std::cout << "  Channel 2 - Error frame count: " << *card2Props.errorFrameCount_Ch2
                  << " / Total Frame: " << *card2Props.frameCount_Ch2 << std::endl;
        std::cout << "             CRC Errors: " << card2Props.err_crc_ch2
                  << " | 8B10B Errors: " << card2Props.err_b810b_ch2 << std::endl;

        std::cout << "\n\n";
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error printing statistics: " << e.what());
        return CODE_PRINT_STATISTICS_FAILED;
    }
}

// Generates a formatted timestamp string for the current local time
// Format: YYYY-MM-DD_HH-MM-SS (safe for file naming)
// @return string containing the current timestamp
uint8_t DriverManager::getCurrentTimestamp(std::string &time)
{
    try
    {
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
        time = oss.str();
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Get Current Timestamp Failed.");
        return CODE_GET_CURRENT_TIMESTAMP_FAILED;
    }
}
uint8_t DriverManager::initializeVideoWriters(bool isLoopbackTest)
{
    const int fps = 60;
    const cv::Size frameSize(2560, 1024);
    std::string timestamp;

    try
    {
        rc = getCurrentTimestamp(timestamp);
        checkReturnCode(rc, "Failed Get Current Timestamp.");
        if (First_Driver != 0 && !card1Running)
        {
            if (gpMem1_1 != nullptr)
            {
                rc = directory_manager.setSessionTimestamp(CARD_1, CH_1, timestamp);
                checkReturnCode(rc, "setSessionTimestamp failed");
                std::string fullPath;
                std::string videoPath;

                if (isLoopbackTest)
                {
                    rc = directory_manager.getLoopbackVideoPath(CARD_1, CH_1, videoPath);
                    checkReturnCode(rc, "getLoopbackVideoPath failed");
                    fullPath = videoPath;
                }
                else
                {
                    rc = directory_manager.getVideoPath(CARD_1, CH_1, videoPath);
                    checkReturnCode(rc, "getVideoPath failed");

                    fullPath = videoPath + "/" + timestamp + ".avi";
                }

                LOG_INFO("Video path (Card1 Ch1): " << fullPath);

                if (video_card1_ch1.isOpened())
                    video_card1_ch1.release();
                video_card1_ch1.open(fullPath, cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, frameSize);
                if (!video_card1_ch1.isOpened())
                    throw std::runtime_error("Failed to open video writer for Card 1 Channel 1.");
            }

            if (gpMem1_2 != nullptr)
            {

                rc = directory_manager.setSessionTimestamp(CARD_1, CH_2, timestamp);
                checkReturnCode(rc, "setSessionTimestamp failed");
                std::string fullPath;
                std::string videoPath;

                if (isLoopbackTest)
                {
                    rc = directory_manager.getLoopbackVideoPath(CARD_1, CH_2, videoPath);
                    checkReturnCode(rc, "getLoopbackVideoPath failed");
                    fullPath = videoPath;
                }
                else
                {
                    rc = directory_manager.getVideoPath(CARD_1, CH_2, videoPath);
                    checkReturnCode(rc, "getVideoPath failed");

                    fullPath = videoPath + "/" + timestamp + ".avi";
                }

                LOG_INFO("Video path (Card1 Ch2): " << fullPath);

                if (video_card1_ch2.isOpened())
                    video_card1_ch2.release();
                video_card1_ch2.open(fullPath, cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, frameSize);
                if (!video_card1_ch2.isOpened())
                    throw std::runtime_error("Failed to open video writer for Card 1 Channel 2.");
            }
        }

        if (Second_Driver != 0 && !card2Running)
        {
            if (gpMem2_1 != nullptr)
            {
                rc = directory_manager.setSessionTimestamp(CARD_2, CH_1, timestamp);
                checkReturnCode(rc, "setSessionTimestamp failed");
                std::string fullPath;
                std::string videoPath;

                if (isLoopbackTest)
                {
                    rc = directory_manager.getLoopbackVideoPath(CARD_2, CH_1, videoPath);
                    checkReturnCode(rc, "getLoopbackVideoPath failed");
                    fullPath = videoPath;
                }
                else
                {
                    rc = directory_manager.getVideoPath(CARD_2, CH_1, videoPath);
                    checkReturnCode(rc, "getVideoPath failed");

                    fullPath = videoPath + "/" + timestamp + ".avi";
                }

                LOG_INFO("Video path (Card2 Ch1): " << fullPath);

                if (video_card2_ch1.isOpened())
                    video_card2_ch1.release();
                video_card2_ch1.open(fullPath, cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, frameSize);
                if (!video_card2_ch1.isOpened())
                    throw std::runtime_error("Failed to open video writer for Card 2 Channel 1.");
            }

            if (gpMem2_2 != nullptr)
            {

                rc = directory_manager.setSessionTimestamp(CARD_2, CH_2, timestamp);
                checkReturnCode(rc, "setSessionTimestamp failed");
                std::string fullPath;
                std::string videoPath;

                if (isLoopbackTest)
                {
                    rc = directory_manager.getLoopbackVideoPath(CARD_2, CH_2, videoPath);
                    checkReturnCode(rc, "getLoopbackVideoPath failed");
                    fullPath = videoPath;
                }
                else
                {
                    rc = directory_manager.getVideoPath(CARD_2, CH_2, videoPath);
                    checkReturnCode(rc, "getVideoPath failed");

                    fullPath = videoPath + "/" + timestamp + ".avi";
                }

                LOG_INFO("Video path (Card2 Ch2): " << fullPath);

                if (video_card2_ch2.isOpened())
                    video_card2_ch2.release();
                video_card2_ch2.open(fullPath, cv::VideoWriter::fourcc('H', '2', '6', '4'), fps, frameSize);
                if (!video_card2_ch2.isOpened())
                    throw std::runtime_error("Failed to open video writer for Card 2 Channel 2.");
            }
        }

        LOG_INFO("Video writers initialized successfully.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error initializing video writers: " << e.what());
        return CODE_INITIALIZE_VELOCITY_VIDEO_WRITERS_FAILED;
    }
}

// Releases dynamically allocated memory in card properties for both cards
// Frees frame counters, error counters, and first-frame flags to prevent memory leaks
// @return void
uint8_t DriverManager::releaseResources()
{
    try
    {
        delete card1Props.frameCount_Ch1;
        card1Props.frameCount_Ch1 = nullptr;
        delete card1Props.frameCount_Ch2;
        card1Props.frameCount_Ch2 = nullptr;
        delete card1Props.errorFrameCount_Ch1;
        card1Props.errorFrameCount_Ch1 = nullptr;
        delete card1Props.errorFrameCount_Ch2;
        card1Props.errorFrameCount_Ch2 = nullptr;
        delete card1Props.isFirst_channel1;
        card1Props.isFirst_channel1 = nullptr;
        delete card1Props.isFirst_channel2;
        card1Props.isFirst_channel2 = nullptr;

        delete card2Props.frameCount_Ch1;
        card2Props.frameCount_Ch1 = nullptr;
        delete card2Props.frameCount_Ch2;
        card2Props.frameCount_Ch2 = nullptr;
        delete card2Props.errorFrameCount_Ch1;
        card2Props.errorFrameCount_Ch1 = nullptr;
        delete card2Props.errorFrameCount_Ch2;
        card2Props.errorFrameCount_Ch2 = nullptr;
        delete card2Props.isFirst_channel1;
        card2Props.isFirst_channel1 = nullptr;
        delete card2Props.isFirst_channel2;
        card2Props.isFirst_channel2 = nullptr;
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR(e.what());
        return CODE_RELEASE_RESOURCES_FAILED;
    }
}

// Stops the flicker detection process by halting capture, releasing resources, and resetting flags
// Calls stopVideoCapture() and cleanup() to safely shut down all active systems
// @return void
uint8_t DriverManager::stopFlickerDetection()
{
    try
    {
        fprintf(stderr, "[stopFlickerDetection] enter\n");
        isRunning = false;

        fprintf(stderr, "[stopFlickerDetection] -> stopVideoCapture\n");
        rc = stopVideoCapture();
        checkReturnCode(rc, "stopVideoCapture failed");
        fprintf(stderr, "[stopFlickerDetection] stopVideoCapture done; -> cleanup\n");
        rc = cleanup();
        checkReturnCode(rc, "cleanup failed");
        fprintf(stderr, "[stopFlickerDetection] cleanup done\n");
        LOG_INFO("Flicker Detection Stopped.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "[stopFlickerDetection] EXC: %s\n", e.what());
        LOG_ERROR("Error stopping flicker detection: " << e.what());
        return CODE_STOP_FLICKER_DETECTION_FAILED;
    }
}

// Resets all runtime statistics and flags for the specified card(s)
// Re-initializes frame counters, error counters, first-frame flags, and SSIM tracking scores
// @param card: CARD_1, CARD_2, or CARD_BOTH indicating which card(s) to reset
// @return void
uint8_t DriverManager::resetStatistics(Card card)
{
    try
    {
        if (card == CARD_1 || card == CARD_BOTH)
        {
            frameCount_card1_Ch1 = new int(0);
            *card1Props.frameCount_Ch1 = *frameCount_card1_Ch1;

            frameCount_card1_Ch2 = new int(0);
            *card1Props.frameCount_Ch2 = *frameCount_card1_Ch2;

            errorFrameCount_card1_Ch1 = new int(0);
            *card1Props.errorFrameCount_Ch1 = *errorFrameCount_card1_Ch1;

            errorFrameCount_card1_Ch2 = new int(0);
            *card1Props.errorFrameCount_Ch2 = *errorFrameCount_card1_Ch2;

            isFirst_card1_channel1 = new bool(true);
            *card1Props.isFirst_channel1 = *isFirst_card1_channel1;

            isFirst_card1_channel2 = new bool(true);
            *card1Props.isFirst_channel2 = *isFirst_card1_channel2;

            scorePrevious_card1_ch1 = 0.0f;
            card1Props.scorePrevious_ch1 = scorePrevious_card1_ch1;

            scorePrevious_card1_ch2 = 0.0f;
            card1Props.scorePrevious_ch2 = scorePrevious_card1_ch2;

            firstFrame_card1_Ch1 = true;
            card1Props.firstFrame_Ch1 = firstFrame_card1_Ch1;

            firstFrame_card1_Ch2 = true;
            card1Props.firstFrame_Ch2 = firstFrame_card1_Ch2;
        }

        if (card == CARD_2 || card == CARD_BOTH)
        {
            frameCount_card2_Ch1 = new int(0);
            *card2Props.frameCount_Ch1 = *frameCount_card2_Ch1;

            frameCount_card2_Ch2 = new int(0);
            *card2Props.frameCount_Ch2 = *frameCount_card2_Ch2;

            errorFrameCount_card2_Ch1 = new int(0);
            *card2Props.errorFrameCount_Ch1 = *errorFrameCount_card2_Ch1;

            errorFrameCount_card2_Ch2 = new int(0);
            *card2Props.errorFrameCount_Ch2 = *errorFrameCount_card2_Ch2;

            isFirst_card2_channel1 = new bool(true);
            *card2Props.isFirst_channel1 = *isFirst_card2_channel1;

            isFirst_card2_channel2 = new bool(true);
            *card2Props.isFirst_channel2 = *isFirst_card2_channel2;

            scorePrevious_card2_ch1 = 0.0f;
            card2Props.scorePrevious_ch1 = scorePrevious_card2_ch1;

            scorePrevious_card2_ch2 = 0.0f;
            card2Props.scorePrevious_ch2 = scorePrevious_card2_ch2;

            firstFrame_card2_Ch1 = true;
            card2Props.firstFrame_Ch1 = firstFrame_card2_Ch1;

            firstFrame_card2_Ch2 = true;
            card2Props.firstFrame_Ch2 = firstFrame_card2_Ch2;
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error resetting statistics: " << e.what());
        return CODE_RESET_STATISTICS_FAILED;
    }
}

// Resets only the frame and error counters for the specified card(s) without
// touching SSIM state (firstFrame/isFirst/scorePrevious) or FPS-related fields.
// Intended for the periodic statistics reset so it does not perturb FPS or
// SSIM continuity.
uint8_t DriverManager::resetFrameCounters(Card card)
{
    try
    {
        if (card == CARD_1 || card == CARD_BOTH)
        {
            if (frameCount_card1_Ch1)        *frameCount_card1_Ch1 = 0;
            if (frameCount_card1_Ch2)        *frameCount_card1_Ch2 = 0;
            if (errorFrameCount_card1_Ch1)   *errorFrameCount_card1_Ch1 = 0;
            if (errorFrameCount_card1_Ch2)   *errorFrameCount_card1_Ch2 = 0;
            if (card1Props.frameCount_Ch1)        *card1Props.frameCount_Ch1 = 0;
            if (card1Props.frameCount_Ch2)        *card1Props.frameCount_Ch2 = 0;
            if (card1Props.errorFrameCount_Ch1)   *card1Props.errorFrameCount_Ch1 = 0;
            if (card1Props.errorFrameCount_Ch2)   *card1Props.errorFrameCount_Ch2 = 0;
        }
        if (card == CARD_2 || card == CARD_BOTH)
        {
            if (frameCount_card2_Ch1)        *frameCount_card2_Ch1 = 0;
            if (frameCount_card2_Ch2)        *frameCount_card2_Ch2 = 0;
            if (errorFrameCount_card2_Ch1)   *errorFrameCount_card2_Ch1 = 0;
            if (errorFrameCount_card2_Ch2)   *errorFrameCount_card2_Ch2 = 0;
            if (card2Props.frameCount_Ch1)        *card2Props.frameCount_Ch1 = 0;
            if (card2Props.frameCount_Ch2)        *card2Props.frameCount_Ch2 = 0;
            if (card2Props.errorFrameCount_Ch1)   *card2Props.errorFrameCount_Ch1 = 0;
            if (card2Props.errorFrameCount_Ch2)   *card2Props.errorFrameCount_Ch2 = 0;
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error resetting frame counters: " << e.what());
        return CODE_RESET_STATISTICS_FAILED;
    }
}

// Stops the specified card and releases its resources for the given channel(s)
// Disables flow control, releases video writers, and deallocates memory buffers
// Optionally re-enables flow control after cleanup
// @param card: CARD_1, CARD_2, or CARD_BOTH indicating which card to stop
// @param channel: CH_1, CH_2, or CH_BOTH indicating which channel's resources to clean up
// @return void
uint8_t DriverManager::stopCard(Card card, Channel channel)
{
    try
    {
        if (card == CARD_1)
        {
            if (channel == CH_1 || channel == CH_BOTH)
            {
                stopCard1Channel1Thread();
            }
            if (channel == CH_2 || channel == CH_BOTH)
            {
                stopCard1Channel2Thread();
            }
            rc = configureDriver(First_Driver, false);
            checkReturnCode(rc, "configureDriver failed");
            rc = videoWritersRelease(card);
            checkReturnCode(rc, "videoWritersRelease failed");
            rc = deAllocateMemoryBuffers(First_Driver, card, channel);
            checkReturnCode(rc, "deAllocateMemoryBuffers failed");
            rc = configureDriver(First_Driver, true);
            checkReturnCode(rc, "ConfigureDriver failed");
        }
        else if (card == CARD_2)
        {
            if (channel == CH_1 || channel == CH_BOTH)
            {
                stopCard2Channel1Thread();
            }
            if (channel == CH_2 || channel == CH_BOTH)
            {
                stopCard2Channel2Thread();
            }
            rc = configureDriver(Second_Driver, false);
            checkReturnCode(rc, "configureDriver failed");
            rc = videoWritersRelease(card);
            checkReturnCode(rc, "videoWritersRelease failed");
            rc = deAllocateMemoryBuffers(Second_Driver, card, channel);
            checkReturnCode(rc, "deAllocateMemoryBuffers failed");
            rc = configureDriver(Second_Driver, true);
            checkReturnCode(rc, "ConfigureDriver failed");
        }
        else
        {
            // If CARD_BOTH, stop both cards
            rc = videoWritersRelease(card);
            checkReturnCode(rc, "videoWritersRelease failed");
            rc = setFlowControl(false);
            checkReturnCode(rc, "setFlowControl failed");
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error stopping card: " << e.what());
        return CODE_STOP_CARD_FAILED;
    }
}

uint8_t DriverManager::releaseFpsCounter(Card card, Channel channel)
{
    try
    {
        LOG_INFO("Release Fps Counter Called | Card: " << card << " | Channel: " << channel);

        if (channel == CH_1 || channel == CH_BOTH)
        {
            if (card == CARD_1)
            {
                LOG_INFO("Releasing FPS Counter for Card 1 Channel 1");
                counter_card1_ch1 = 0;
            }
            else if (card == CARD_2)
            {
                LOG_INFO("Releasing FPS Counter for Card 2 Channel 1");
                counter_card2_ch1 = 0;
            }
        }

        if (channel == CH_2 || channel == CH_BOTH)
        {
            if (card == CARD_1)
            {
                LOG_INFO("Releasing FPS Counter for Card 1 Channel 2");
                counter_card1_ch2 = 0;
            }
            else if (card == CARD_2)
            {
                LOG_INFO("Releasing FPS Counter for Card 2 Channel 2");
                counter_card2_ch2 = 0;
            }
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Release FPS Counter Failed");
        return CODE_RELEASE_FPS_COUNTER_FAILED;
    }
}

// Restarts the specified card by reconfiguring its driver, allocating memory, and preparing video output
// Used when a card needs to be re-initialized without restarting the whole system
// @param card: CARD_1, CARD_2, or CARD_BOTH indicating which card to restart
// @param channel: CH_1, CH_2, or CH_BOTH specifying which channels to initialize
// @return void
uint8_t DriverManager::restartCard(Card card, Channel channel)
{
    try
    {
        if (card == CARD_1)
        {

            rc = configureDriver(First_Driver, false);
            checkReturnCode(rc, "configureDriver failed");
            rc = allocateMemoryBuffers(First_Driver, card, channel);
            checkReturnCode(rc, "allocateMemoryBuffers failed");
            rc = initializeVideoWriters(false);
            checkReturnCode(rc, "initializeVideoWriters failed");
            rc = directory_manager.createDirectory(card, channel);
            checkReturnCode(rc, "createDirectory failed");
            if (channel == CH_1 || channel == CH_BOTH)
            {
                startCard1Channel1Thread();
            }
            if (channel == CH_2 || channel == CH_BOTH)
            {
                startCard1Channel2Thread();
            }
            rc = configureDriver(First_Driver, true);
            checkReturnCode(rc, "ConfigureDriver failed");
        }
        else if (card == CARD_2)
        {

            rc = configureDriver(Second_Driver, false);
            checkReturnCode(rc, "configureDriver failed");
            rc = allocateMemoryBuffers(Second_Driver, card, channel);
            checkReturnCode(rc, "allocateMemoryBuffers failed");
            rc = initializeVideoWriters(false);
            checkReturnCode(rc, "initializeVideoWriters failed");
            rc = directory_manager.createDirectory(card, channel);
            checkReturnCode(rc, "createDirectory failed");
            if (channel == CH_1 || channel == CH_BOTH)
            {
                startCard2Channel1Thread();
            }
            if (channel == CH_2 || channel == CH_BOTH)
            {
                startCard2Channel2Thread();
            }
            rc = configureDriver(Second_Driver, true);
            checkReturnCode(rc, "ConfigureDriver failed");
        }
        else
        {
            rc = setFlowControl(true);
            checkReturnCode(rc, "setFlowControl failed");
        }
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Error restarting card: " << e.what());
        return CODE_RESTART_CARD_FAILED;
    }
}

uint8_t DriverManager::startTestLoopback()
{
    try
    {
        LOG_INFO("Starting loopback test...");

        initializeVideoWriters(true);

        //  CARD 1 ayarları
        if (First_Driver != 0)
        {
            LOG_INFO("Configuring CARD_1 loopback...");
            UINT32 u32Err = 0;

            u32Err = GRTV_Command(First_Driver, GRTV_FLOWTHRUDIS);
            u32Err = GRTV_RoutingControlDch(First_Driver, GRTV_ROUTING_PCIE, GRTV_ROUTING_PCIE);
            UINT32 pattern = GRTV_TPO_RGBW_HORZ_BARS;
            UINT32 options = GRTV_TPO_MARKERS | GRTV_TPO_SCROLL;
            u32Err = GRTV_TestPatternOptions(First_Driver, pattern, options);
            u32Err = GRTV_TestPatternControl(First_Driver, 1, GRTV_ON);
            u32Err = GRTV_FlowControl(First_Driver, GRTV_RCV, GRTV_ON, 0);
            u32Err = GRTV_Command(First_Driver, GRTV_CLRSTATS);
            u32Err = GRTV_Command(First_Driver, GRTV_FLOWTHRUEN);

            startCard1Channel1Thread();
            startCard1Channel2Thread();

            if (u32Err != GRTV_OK)
                throw std::runtime_error("Failed to configure CARD_1. Error code: " + std::to_string(u32Err));

            LOG_INFO("CARD_1 loopback pattern enabled.");
        }

        //  CARD 2 ayarları
        if (Second_Driver != 0)
        {
            LOG_INFO("Configuring CARD_2 loopback...");
            UINT32 u32Err = 0;
            u32Err = GRTV_Command(Second_Driver, GRTV_FLOWTHRUDIS);
            u32Err = GRTV_RoutingControlDch(Second_Driver, GRTV_ROUTING_PCIE, GRTV_ROUTING_PCIE);
            UINT32 pattern = GRTV_TPO_RGBW_HORZ_BARS;
            UINT32 options = GRTV_TPO_MARKERS | GRTV_TPO_SCROLL;
            u32Err = GRTV_TestPatternOptions(Second_Driver, pattern, options);
            u32Err = GRTV_TestPatternControl(Second_Driver, 1, GRTV_ON);
            u32Err = GRTV_FlowControl(Second_Driver, GRTV_RCV, GRTV_ON, 0);
            u32Err = GRTV_Command(Second_Driver, GRTV_CLRSTATS);
            u32Err = GRTV_Command(Second_Driver, GRTV_FLOWTHRUEN);

            startCard2Channel1Thread();
            startCard2Channel2Thread();

            if (u32Err != GRTV_OK)
                throw std::runtime_error("Failed to configure CARD_2. Error code: " + std::to_string(u32Err));

            LOG_INFO("CARD_2 loopback pattern enabled.");
        }

        LOG_INFO("Loopback test started successfully. Recording for 60 seconds...");
        sleep(60); // 1 dakika bekleme

        isRunning = false;
        isRunning_card1_channel1 = false;
        isRunning_card1_channel2 = false;
        isRunning_card2_channel1 = false;
        isRunning_card2_channel2 = false;

        LOG_INFO("Loopback test completed.");
        return CODE_SUCCESS;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Loopback test failed: " << e.what());
        return CODE_START_TEST_LOOPBACK_FAILED;
    }
}