#include "FirmwareUpdaterRunner.h"
#include "FirmwareUpdaterDefaults.h"
#include "SSHDeployer.h"
#include "DeviceManager.h"
#include "Device.h"
#include "SystemCommand.h"
#include "ErrorPrinter.h"
#include "SafeShutdown.h"
#include "UnitManager.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

extern SSHDeployer g_ssh_deployer_server;
extern SSHDeployer g_ssh_deployer_cumulus;

namespace
{

std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string md5OfLocalFile(const std::string& path)
{
    if (!std::filesystem::exists(path)) return "";
    std::string cmd = "md5sum \"" + path + "\" | awk '{print $1}'";
    auto result = g_systemCommand.execute(cmd);
    if (!result.success) return "";
    return trim(result.output);
}

std::string md5OfRemoteCumulusFile(const std::string& remote_path)
{
    std::string output;
    std::string cmd = "md5sum " + remote_path + " | awk '{print $1}'";
    if (!g_ssh_deployer_cumulus.execute(cmd, &output, false, true))
    {
        return "";
    }
    return trim(output);
}

// Per-unit interfaces file path. Resolved relative to project root
// (where prepare_release.sh lives) via SSHDeployer::getSourceRoot().
// CMC/MMC/HSN folders don't exist in source yet — they're returned here so
// that when those units are wired up later, only a folder needs to be added.
std::string interfacesPathForUnit(Unit unit)
{
    std::string base = SSHDeployer::getSourceRoot() + "/CumulusInterfaces/";
    switch (unit)
    {
    case CMC: return base + "CMC/interfaces";
    case MMC: return base + "MMC/interfaces";
    case VMC: return base + "VMC/interfaces";
    case DTN: return base + "DTNIRSW/interfaces";
    case HSN: return base + "HSN/interfaces";
    }
    return "";
}

// Per-unit SPI drop-in folder.
// <source_root>/FirmwareUpdater/firmware/<UNIT>/
// User drops the .spi for that unit here; runner picks it up automatically.
std::string spiDirForUnit(Unit unit)
{
    std::string base = SSHDeployer::getSourceRoot() + "/FirmwareUpdater/firmware/";
    switch (unit)
    {
    case CMC: return base + "CMC";
    case MMC: return base + "MMC";
    case VMC: return base + "VMC";
    case DTN: return base + "DTN";
    case HSN: return base + "HSN";
    }
    return base;
}

bool isCumulusAlreadyConfigured(Unit unit)
{
    std::string localInterfaces = interfacesPathForUnit(unit);

    std::string localMd5 = md5OfLocalFile(localInterfaces);
    if (localMd5.empty())
    {
        ErrorPrinter::warn("CUMULUS",
            "Local interfaces file not found, will run cumulus_setup.");
        return false;
    }

    std::string remoteMd5 = md5OfRemoteCumulusFile("/etc/network/interfaces");
    if (remoteMd5.empty())
    {
        ErrorPrinter::warn("CUMULUS",
            "Could not read remote interfaces md5, will run cumulus_setup.");
        return false;
    }

    return localMd5 == remoteMd5;
}

std::string findOrPromptSpiFile(Unit unit)
{
    std::string dir = spiDirForUnit(unit);

    std::vector<std::string> spiFiles;
    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".spi")
            {
                spiFiles.push_back(entry.path().string());
            }
        }
    }

    if (spiFiles.size() == 1)
    {
        ErrorPrinter::info("FW", "Using SPI file: " + spiFiles[0]);
        return spiFiles[0];
    }

    if (spiFiles.size() > 1)
    {
        std::cout << "Multiple .spi files found in " << dir << ":\n";
        for (size_t i = 0; i < spiFiles.size(); ++i)
        {
            std::cout << "  " << (i + 1) << ") "
                      << std::filesystem::path(spiFiles[i]).filename().string()
                      << "\n";
        }

        int choice = 0;
        while (true)
        {
            std::cout << "Select file: ";
            std::cin >> choice;
            if (std::cin.fail())
            {
                std::cin.clear();
                std::cin.ignore(1000, '\n');
                std::cout << "Invalid input!\n";
                continue;
            }
            if (choice < 1 || choice > (int)spiFiles.size())
            {
                std::cout << "Invalid option!\n";
                continue;
            }
            break;
        }
        std::cin.ignore(1000, '\n');
        return spiFiles[choice - 1];
    }

    std::cout << "No .spi file found in " << dir << ".\n";
    std::cout << "Enter absolute path to .spi file: ";
    std::string path;
    std::getline(std::cin, path);
    path = trim(path);

    if (path.empty() || !std::filesystem::exists(path))
    {
        ErrorPrinter::error("FW", "SPI file does not exist: " + path);
        return "";
    }
    return path;
}

bool powerOnDeviceForFirmware()
{
    auto& shutdown = SafeShutdown::getInstance();

    if (!g_DeviceManager.create(PSUG30))
    {
        ErrorPrinter::error("PSU", "FW: Failed to create PSU G30!");
        return false;
    }

    if (!g_DeviceManager.connect(PSUG30))
    {
        ErrorPrinter::error("PSU", "FW: Failed to connect to PSU G30!");
        return false;
    }
    shutdown.registerPsuConnected(PSUG30);

    if (!g_DeviceManager.setCurrent(PSUG30, 3.0))
    {
        ErrorPrinter::error("PSU", "FW: Failed to set current on PSU G30!");
        return false;
    }

    if (!g_DeviceManager.setVoltage(PSUG30, 28.0))
    {
        ErrorPrinter::error("PSU", "FW: Failed to set voltage on PSU G30!");
        return false;
    }

    if (!g_DeviceManager.enableOutput(PSUG30, true))
    {
        ErrorPrinter::error("PSU", "FW: Failed to enable output on PSU G30!");
        return false;
    }
    shutdown.registerPsuOutputEnabled(PSUG30);

    ErrorPrinter::info("FW", "Device powered on. FPGA readiness will be "
                              "checked by the loader on the server.");
    return true;
}

void powerOffDevice()
{
    auto& shutdown = SafeShutdown::getInstance();
    g_DeviceManager.enableOutput(PSUG30, false);
    shutdown.unregisterPsuOutputEnabled(PSUG30);
    g_DeviceManager.disconnect(PSUG30);
    shutdown.unregisterPsuConnected(PSUG30);
}

// ============================================================================
// Per-unit firmware update sequences
// ----------------------------------------------------------------------------
// Each unit has its own runXxx() function so the flow, PSU settings, scripts
// and SPI handling can diverge as needed. Right now only DTN is implemented;
// other units are stubs ready to be filled in.
// ============================================================================

bool runDtn()
{
    std::cout << "--- DTN Firmware Update ---" << std::endl;

    // (a) Lokal SPI dosyasını bul: <source_root>/FirmwareUpdater/firmware/DTN/*.spi
    std::string localSpi = findOrPromptSpiFile(DTN);
    if (localSpi.empty())
    {
        return false;
    }

    // (b) Server bağlantı testi
    if (!g_ssh_deployer_server.testConnection())
    {
        ErrorPrinter::error("SSH", "FW: Cannot connect to server!");
        return false;
    }

    // (c) FirmwareUpdater binary'lerini server'a deploy et
    ErrorPrinter::info("FW", "Deploying FirmwareUpdater binaries to server...");
    if (!g_ssh_deployer_server.deployPrebuilt(
            "FirmwareUpdater",
            "",
            false,
            false,
            "",
            false))
    {
        ErrorPrinter::error("SSH", "FW: FirmwareUpdater deploy failed!");
        return false;
    }

    std::string remoteDir = g_ssh_deployer_server.getRemoteDirectory();
    std::string remoteFwDir = remoteDir + "/FirmwareUpdater";

    // (d) Cumulus setup (ya zaten konfigüreyse atla, ya da interaktif çalıştır)
    if (isCumulusAlreadyConfigured(DTN))
    {
        ErrorPrinter::info("CUMULUS",
            "Cumulus already configured, skipping setup.");
    }
    else
    {
        // cumulus_setup script'i server'da çalışacağı için interfaces dosyasını
        // server'a SCP'le ve --interfaces-path ile geçir (lokal path'i göremez).
        // Yol birim-bazlı: <source_root>/CumulusInterfaces/<UnitFolder>/interfaces
        std::string localInterfaces = interfacesPathForUnit(DTN);
        if (!std::filesystem::exists(localInterfaces))
        {
            ErrorPrinter::error("CUMULUS",
                "Interfaces file not found: " + localInterfaces);
            return false;
        }
        ErrorPrinter::info("CUMULUS",
            "Uploading interfaces file to server: " + localInterfaces);
        if (!g_ssh_deployer_server.deploy(localInterfaces))
        {
            ErrorPrinter::error("SSH", "FW: interfaces upload failed!");
            return false;
        }
        std::string remoteInterfaces = remoteDir + "/interfaces";

        ErrorPrinter::info("CUMULUS", "Running cumulus_setup on server...");
        std::string cmd = "echo 'q' | sudo -S -v && sudo " +
                          remoteFwDir + "/cumulus_setup" +
                          " --interfaces-path \"" + remoteInterfaces + "\"" +
                          " --yes";
        if (!g_ssh_deployer_server.executeInteractive(cmd, false))
        {
            ErrorPrinter::error("CUMULUS", "FW: cumulus_setup failed!");
            return false;
        }
    }

    // (e) Cihaza güç ver (lokal'den TCP)
    if (!powerOnDeviceForFirmware())
    {
        return false;
    }

    // (f) SPI'yi server'a SCP gönder
    ErrorPrinter::info("FW", "Uploading SPI file to server: " + localSpi);
    if (!g_ssh_deployer_server.deploy(localSpi))
    {
        ErrorPrinter::error("SSH", "FW: SPI upload failed!");
        powerOffDevice();
        return false;
    }
    std::string remoteSpi = remoteDir + "/" +
                            std::filesystem::path(localSpi).filename().string();

    // (g) FPGA loader (non-interactive, per-unit LRU ID, server'da çalışır)
    char lruIdHex[8];
    std::snprintf(lruIdHex, sizeof(lruIdHex), "0x%04X",
                  FirmwareUpdaterDefaults::lruIdForUnit(DTN));

    ErrorPrinter::info("FW", std::string("Starting FPGA firmware loader on server (LRU ") +
                              lruIdHex + ")...");
    std::string loaderCmd = "echo 'q' | sudo -S -v && sudo " +
                            remoteFwDir + "/fpga_firmware_loader" +
                            " --non-interactive" +
                            " --wait-ready" +
                            " --spi-path \"" + remoteSpi + "\"" +
                            " --lru-id " + lruIdHex;
    bool ok = g_ssh_deployer_server.executeInteractive(loaderCmd, false);

    // (h) Cleanup - başarı/başarısızlık fark etmez
    powerOffDevice();

    if (!ok)
    {
        ErrorPrinter::error("FW", "FPGA firmware loader returned error.");
    }
    return ok;
}

bool runCmc()
{
    std::cout << "--- CMC Firmware Update ---" << std::endl;
    ErrorPrinter::warn("FW", "CMC firmware update is not implemented yet.");
    return true;
}

bool runMmc()
{
    std::cout << "--- MMC Firmware Update ---" << std::endl;
    ErrorPrinter::warn("FW", "MMC firmware update is not implemented yet.");
    return true;
}

bool runVmc()
{
    std::cout << "--- VMC Firmware Update ---" << std::endl;
    ErrorPrinter::warn("FW", "VMC firmware update is not implemented yet.");
    return true;
}

bool runHsn()
{
    std::cout << "--- HSN Firmware Update ---" << std::endl;
    ErrorPrinter::warn("FW", "HSN firmware update is not implemented yet.");
    return true;
}

} // namespace

namespace FirmwareUpdaterRunner
{

bool run()
{
    std::cout << "======================================" << std::endl;
    std::cout << "       Firmware Updater Mode" << std::endl;
    std::cout << "======================================" << std::endl;

    Unit unit = g_UnitManager.unitSelector();

    switch (unit)
    {
    case CMC: return runCmc();
    case MMC: return runMmc();
    case VMC: return runVmc();
    case DTN: return runDtn();
    case HSN: return runHsn();
    }

    ErrorPrinter::error("FW", "Unknown unit selected.");
    return false;
}

} // namespace FirmwareUpdaterRunner
