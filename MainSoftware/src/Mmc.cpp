#include "Mmc.h"
#include "Dtn.h"
#include "FlickerDetectionRunner.h"
#include "ReportManager.h"
#include "SafeShutdown.h"
#include "ErrorPrinter.h"
#include "Utils.h"
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <iomanip>
// 28V 3.0A

Mmc g_mmc;

Mmc::Mmc()
{
}

Mmc::~Mmc()
{
}

bool Mmc::ensureLogDirectories()
{
    try
    {
        std::filesystem::create_directories(LogPaths::MMC());
        DEBUG_LOG("MMC: Log directories created/verified at " << LogPaths::MMC());
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "MMC: Failed to create log directories: " << e.what() << std::endl;
        return false;
    }
}

bool Mmc::configureSequence()
{
    auto& shutdown = SafeShutdown::getInstance();

    if (!g_Server.onWithWait(3))
    {
        ErrorPrinter::error("SERVER", "MMC: Server could not be started!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerServerOn();

    // Create PSU G300 (300V, 5.6A)
    if (!g_DeviceManager.create(PSUG300))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to create PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    // Connect to PSU G300
    if (!g_DeviceManager.connect(PSUG300))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to connect to PSU G300!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerPsuConnected(PSUG300);

    if (!g_DeviceManager.setCurrent(PSUG300, 1.5))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to set current on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    if (!g_DeviceManager.setVoltage(PSUG300, 20.0))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to set voltage on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    if (!g_DeviceManager.enableOutput(PSUG300, true))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to enable output on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerPsuOutputEnabled(PSUG300);

    // Record Unit Power On Time when PSU output is enabled
    g_ReportManager.recordUnitPowerOnTime();

    serial::sendSerialCommand("/dev/ttyACM0", "VMC_ID 1");

    // Start local FlickerDetection. Parameters come from
    // FlickerDetectionConfig::Mmc; log + error frames + videos go to LOGS/MMC/.
    ensureLogDirectories();
    FlickerDetectionRunner flicker;
    std::string flicker_log    = LogPaths::MMC() + "/flicker_detection.log";
    std::string flicker_output = LogPaths::MMC() + "/flicker_output";
    if (!flicker.startForMmc(flicker_log, flicker_output))
    {
        ErrorPrinter::warn("FLICKER",
            "MMC: FlickerDetection could not be started, continuing without it.");
    }
    else
    {
        std::cout << "MMC: FlickerDetection running (log: " << flicker_log
                  << ", output: " << flicker_output << ")" << std::endl;
    }

    for (int i = 0; i < 1000; i++)
    {
        if (i % 20 == 0)
        {
            double current = g_DeviceManager.measureCurrent(PSUG300);
            double voltage = g_DeviceManager.measureVoltage(PSUG300);
            double power = g_DeviceManager.measurePower(PSUG300);
            double get_current = g_DeviceManager.getCurrent(PSUG300);
            double get_voltage = g_DeviceManager.getVoltage(PSUG300);

#if DEBUG_PRINT
            {
                utils::FloatFormatGuard guard(std::cout, 2, true);
                std::cout << "Current: " << current << " " << "Voltage: " << voltage << " " << "Power: " << power << " " << "Get Current: " << get_current << " " << "Get Voltage:" << get_voltage << std::endl;
            }
#endif
        }
    }

    // Stop local FlickerDetection before powering down the unit
    std::cout << "MMC: Stopping FlickerDetection..." << std::endl;
    flicker.stop();

    if (!g_DeviceManager.enableOutput(PSUG300, false))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to disable output on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    // Verify PSU output is actually off, retry if not
    for (int retry = 0; retry < 3; retry++)
    {
        usleep(500000); // 500ms wait before checking
        if (!g_DeviceManager.isOutputEnabled(PSUG300))
        {
            std::cout << "MMC: PSU G300 output verified OFF." << std::endl;
            break;
        }
        ErrorPrinter::warn("PSU", "MMC: PSU G300 still ON, retry " + std::to_string(retry + 1) + "/3...");
        g_DeviceManager.enableOutput(PSUG300, false);
    }
    shutdown.unregisterPsuOutputEnabled(PSUG300);

    // Record Power Off Time when PSU output is disabled
    g_ReportManager.recordPowerOffTime();

    if (!g_DeviceManager.disconnect(PSUG300))
    {
        ErrorPrinter::error("PSU", "MMC: Failed to disconnect PSU G300!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.unregisterPsuConnected(PSUG300);

    g_Server.offWithWait(300);
    shutdown.unregisterServerOn();

    std::cout << "MMC: PSU configured successfully." << std::endl;
    return true;
}