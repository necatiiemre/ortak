#include "Cmc.h"
#include "ReportManager.h"
#include "SafeShutdown.h"
#include "ErrorPrinter.h"
#include "CumulusHelper.h"
#include "SSHDeployer.h"
#include "Dtn.h"
#include "PsuTelemetry.h"
#include "PsuTelemetryPublisher.h"
#include "FlickerDetectionRunner.h"
#include "Utils.h"
#include <iostream>
#include <unistd.h>
#include <iomanip>
#include <filesystem>
#include <csignal>
#include <atomic>
// 270V 9A

// Global flag for Ctrl+C handling in DPDK CMC monitoring
static std::atomic<bool> g_dpdk_cmc_monitoring_running{true};

static void dpdk_cmc_monitor_signal_handler(int sig)
{
    (void)sig;
    g_dpdk_cmc_monitoring_running = false;
}

Cmc g_cmc;

Cmc::Cmc()
{
}

Cmc::~Cmc()
{
}

bool Cmc::ensureLogDirectories()
{
    try
    {
        std::filesystem::create_directories(LogPaths::CMC());
        DEBUG_LOG("CMC: Log directories created/verified at " << LogPaths::CMC());
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "CMC: Failed to create log directories: " << e.what() << std::endl;
        return false;
    }
}

bool Cmc::runDpdkCmcInteractive(const std::string &eal_args, const std::string &make_args)
{
    (void)make_args;

    std::cout << "======================================" << std::endl;
    std::cout << "CMC: DPDK CMC Interactive Deployment" << std::endl;
    std::cout << "======================================" << std::endl;

    if (!g_ssh_deployer_server.testConnection())
    {
        std::cerr << "CMC: Cannot connect to server!" << std::endl;
        return false;
    }

    DEBUG_LOG("CMC: Deploying prebuilt DPDK CMC binary...");
    if (!g_ssh_deployer_server.deployPrebuilt(
            "dpdk_cmc",  // prebuilt folder (inside prebuilt/)
            "",          // app name (auto-detect: dpdk_app)
            false,       // DON'T run after deploy (we'll run interactively)
            false,       // no sudo for deploy
            "",          // no run args (not running yet)
            false        // not background
            ))
    {
        std::cerr << "CMC: DPDK CMC prebuilt deploy failed!" << std::endl;
        return false;
    }

    std::cout << std::endl;
    std::cout << "======================================" << std::endl;
    DEBUG_LOG("CMC: Starting DPDK CMC Interactive Mode");
    DEBUG_LOG("CMC: You can answer ATE/latency prompts (y/n)");
    DEBUG_LOG("CMC: After tests, DPDK CMC will continue in background");
    std::cout << "======================================" << std::endl;
    std::cout << std::endl;

    std::string remote_dir = g_ssh_deployer_server.getRemoteDirectory();

    std::string dpdk_command = "cd " + remote_dir + "/dpdk_cmc && "
                                                    "echo 'q' | sudo -S -v && "
                                                    "sudo ./dpdk_app --daemon " +
                               eal_args;

    bool result = g_ssh_deployer_server.executeInteractive(dpdk_command, false);

    if (result)
    {
        std::cout << std::endl;
        std::cout << "======================================" << std::endl;
        std::cout << "CMC: DPDK CMC started successfully!" << std::endl;
        std::cout << "CMC: Running in background on server" << std::endl;
        std::cout << "CMC: Log file: /tmp/dpdk_app.log" << std::endl;
        std::cout << "======================================" << std::endl;
    }
    else
    {
        std::cerr << "CMC: DPDK CMC interactive execution failed!" << std::endl;
    }

    return result;
}

bool Cmc::configureSequence()
{
    auto& shutdown = SafeShutdown::getInstance();

    if (!g_Server.onWithWait(3))
    {
        ErrorPrinter::error("SERVER", "CMC: Server could not be started!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerServerOn();

    // Create PSU G300 (300V, 9A)
    if (!g_DeviceManager.create(PSUG300))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to create PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    // Connect to PSU G300
    if (!g_DeviceManager.connect(PSUG300))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to connect to PSU G300!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerPsuConnected(PSUG300);

    if (!g_DeviceManager.setCurrent(PSUG300, 9.0))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to set current on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    if (!g_DeviceManager.setVoltage(PSUG300, 270.0))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to set voltage on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    serial::sendSerialCommand("/dev/ttyACM0", "VMC_ID 31");

    if (!g_DeviceManager.enableOutput(PSUG300, true))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to enable output on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerPsuOutputEnabled(PSUG300);

    // Record Unit Power On Time when PSU output is enabled
    g_ReportManager.recordUnitPowerOnTime();

    // Start PSU telemetry publisher (same mechanism as VMC):
    // push PSU V/I/W over UDP to the server-side DPDK CMC app so the health
    // dashboard can show live PSU telemetry and detect staleness.
    PsuTelemetryPublisher psu_publisher(
        PSUG300,
        g_ssh_deployer_server.getHost(),
        PSU_TELEM_PORT);
    if (!psu_publisher.start()) {
        ErrorPrinter::warn("PSU-TELEM",
            "CMC: Failed to start PSU telemetry publisher - "
            "DPDK CMC will continue without PSU telemetry rows.");
    }

    sleep(30);

    sleep(2);
    if (!g_cumulus.deployNetworkInterfaces(SSHDeployer::getPrebuiltRoot() + "/CumulusInterfaces/CMC/interfaces"))
    {
        ErrorPrinter::error("CUMULUS", "CMC: Failed to deploy network configuration!");
        shutdown.executeShutdown();
        return false;
    }
    DEBUG_LOG("CMC: Network configuration deployed successfully.");

    sleep(1);

    // Configure Cumulus switch VLANs (CMC-specific)
    if (!g_cumulus.configureSequenceCmc())
    {
        ErrorPrinter::error("CUMULUS", "CMC: Cumulus configuration failed!");
        shutdown.executeShutdown();
        return false;
    }

    sleep(1);

    // Start local FlickerDetection right before DPDK CMC. Parameters come
    // from FlickerDetectionConfig::Cmc; log + error frames + videos go to
    // LOGS/CMC/.
    ensureLogDirectories();
    FlickerDetectionRunner flicker;
    std::string flicker_log    = LogPaths::CMC() + "/flicker_detection.log";
    std::string flicker_output = LogPaths::CMC() + "/flicker_output";
    if (!flicker.startForCmc(flicker_log, flicker_output))
    {
        ErrorPrinter::warn("FLICKER",
            "CMC: FlickerDetection could not be started, continuing without it.");
    }
    else
    {
        std::cout << "CMC: FlickerDetection running (log: " << flicker_log
                  << ", output: " << flicker_output << ")" << std::endl;
    }

    // DPDK CMC - Interactive mode with embedded ATE/latency prompts
    if (!runDpdkCmcInteractive("-l 0-255 -n 16"))
    {
        ErrorPrinter::error("DPDK", "CMC: DPDK CMC deployment unsuccessful!");
        flicker.stop();
        shutdown.executeShutdown();
        return false;
    }
    shutdown.registerDpdkRunning();

    std::cout << "CMC: DPDK CMC is running in background, continuing..." << std::endl;

    // Monitor DPDK CMC stats every 10 seconds until Ctrl+C
    std::cout << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << "CMC: Monitoring DPDK CMC (every 10 seconds)" << std::endl;
    std::cout << "CMC: Press Ctrl+C to stop" << std::endl;
    std::cout << "======================================" << std::endl;

    // Setup signal handler for Ctrl+C (DPDK CMC monitoring only)
    g_dpdk_cmc_monitoring_running = true;
    struct sigaction sa;
    sa.sa_handler = dpdk_cmc_monitor_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    while (g_dpdk_cmc_monitoring_running)
    {
        for (int i = 0; i < 10 && g_dpdk_cmc_monitoring_running; i++)
        {
            sleep(1);
        }

        if (!g_dpdk_cmc_monitoring_running)
            break;

        std::string raw;
        // tail -n 4000: dpdk_cmc her saniye bir stats tablosu (~50 satır) +
        // health-monitor dashboard (~1400 satır) yazıyor. 4000 satır 2-3 tam
        // cycle yakalanmasına ve rfind'in iki stats marker'ını bulmasına yetiyor.
        g_ssh_deployer_server.execute(
            "tail -n 4000 /tmp/dpdk_app.log", &raw, false, true);

        if (!raw.empty())
        {
            // Check if DPDK CMC has exited or is shutting down
            if (raw.find("Application exited cleanly") != std::string::npos ||
                raw.find("=== Shutting down ===") != std::string::npos)
            {
                std::cout << "\033[2J\033[H";
                std::cout << "======================================" << std::endl;
                std::cout << "CMC: DPDK CMC has exited on server." << std::endl;
                std::cout << "CMC: Auto-exiting monitoring loop..." << std::endl;
                std::cout << "======================================" << std::endl;
                g_dpdk_cmc_monitoring_running = false;
                break;
            }

            const std::string sep = "========== [";
            size_t last = raw.rfind(sep);
            size_t prev = (last != std::string::npos && last > 0)
                              ? raw.rfind(sep, last - 1)
                              : std::string::npos;

            std::string block;
            if (prev != std::string::npos && last != std::string::npos)
                block = raw.substr(prev, last - prev);
            else if (last != std::string::npos)
                block = raw.substr(last);
            else
                block = raw;

            std::cout << "\033[2J\033[H";
            std::cout << "=== DPDK CMC Live Stats (Press Ctrl+C to stop) ===" << std::endl;
            std::cout << block << std::endl;
        }
        else
        {
            DEBUG_LOG("(No log output yet - DPDK CMC might still be starting)");
        }
    }

    // Re-install SafeShutdown signal handlers after DPDK CMC monitoring
    shutdown.installSignalHandlers();

    // Stop local FlickerDetection
    std::cout << "CMC: Stopping FlickerDetection..." << std::endl;
    flicker.stop();

    // Stop DPDK CMC on server.
    //
    // The new SIGINT handler in dpdk_cmc treats the first signal as a request
    // to enter the MMMS file-fetch handover (stop_normal_tx). dpdk_cmc emits
    // a trigger packet, drains any files the peer streams back into
    // /tmp/mmms_logs/, then exits cleanly. We have to wait for that to
    // complete before fetching the log + files; sending SIGKILL too early
    // would truncate the transfer.
    std::cout << "CMC: Initiating MMMS handover (graceful SIGTERM)..." << std::endl;
    if (g_ssh_deployer_server.isApplicationRunning("dpdk_app"))
    {
        // One-shot SIGTERM. dpdk_cmc's two-stage handler will enter the MMMS
        // phase on this signal; a second signal would force-quit immediately.
        std::string term_out;
        g_ssh_deployer_server.execute(
            "pkill -TERM -f dpdk_app 2>/dev/null; echo TERM_SENT",
            &term_out, true, true);

        // Poll up to ~90s for either the clean-exit banner or the MMMS phase
        // line to appear in the log, or for the process to disappear. 60s
        // covers the dpdk_cmc internal first-packet timeout; the remaining
        // ~30s covers the post-transfer cleanup (15s flush + EAL teardown).
        const int max_wait_seconds = 90;
        bool dpdk_exited = false;
        for (int i = 0; i < max_wait_seconds; i++)
        {
            sleep(1);

            if (!g_ssh_deployer_server.isApplicationRunning("dpdk_app"))
            {
                std::cout << "CMC: dpdk_cmc exited after " << (i + 1)
                          << "s (MMMS phase complete)" << std::endl;
                dpdk_exited = true;
                break;
            }

            if ((i + 1) % 10 == 0)
            {
                std::string tail_out;
                g_ssh_deployer_server.execute(
                    "tail -n 50 /tmp/dpdk_app.log 2>/dev/null",
                    &tail_out, false, true);
                if (tail_out.find("MMMS_PHASE: DONE") != std::string::npos ||
                    tail_out.find("MMMS_PHASE: TIMEOUT") != std::string::npos)
                {
                    DEBUG_LOG("CMC: MMMS phase marker seen in log, waiting for exit...");
                }
                std::cout << "CMC: waiting for dpdk_cmc exit ("
                          << (i + 1) << "/" << max_wait_seconds << "s)" << std::endl;
            }
        }

        if (!dpdk_exited)
        {
            ErrorPrinter::warn("DPDK",
                "CMC: dpdk_cmc did not exit within "
                + std::to_string(max_wait_seconds) + "s, forcing stop");
            g_ssh_deployer_server.stopApplication("dpdk_app", true);
        }
        std::cout << "CMC: DPDK CMC stopped." << std::endl;
    }
    else
    {
        DEBUG_LOG("CMC: DPDK CMC was not running.");
    }
    shutdown.unregisterDpdkRunning();

    // Fetch DPDK CMC log from server to local PC
    DEBUG_LOG("CMC: Fetching DPDK CMC log from server...");
    ensureLogDirectories();
    std::string local_dpdk_log = LogPaths::CMC() + "/dpdk_app.log";
    if (g_ssh_deployer_server.fetchFile("/tmp/dpdk_app.log", local_dpdk_log))
    {
        std::cout << "CMC: DPDK CMC log saved to: " << local_dpdk_log << std::endl;
    }
    else
    {
        ErrorPrinter::warn("SSH", "CMC: Failed to fetch DPDK CMC log (file may not exist)");
    }

    // Fetch MMMS files dumped during the handover (one file per "start"
    // packet). The directory may not exist if dpdk_cmc never reached the
    // MMMS phase or the peer never responded — treat absence as benign.
    {
        std::string mmms_local_dir = LogPaths::CMC() + "/mmms_logs";
        std::string check_out;
        g_ssh_deployer_server.execute(
            "test -d /tmp/mmms_logs && ls -A /tmp/mmms_logs | head -1",
            &check_out, false, true);
        if (!check_out.empty())
        {
            DEBUG_LOG("CMC: Fetching MMMS files from server...");
            if (g_ssh_deployer_server.fetchDirectory("/tmp/mmms_logs", mmms_local_dir))
            {
                std::cout << "CMC: MMMS files saved to: " << mmms_local_dir << std::endl;
            }
            else
            {
                ErrorPrinter::warn("SSH", "CMC: Failed to fetch MMMS files directory");
            }
        }
        else
        {
            DEBUG_LOG("CMC: No MMMS files on server (skipping fetch)");
        }
    }

    if (!g_DeviceManager.enableOutput(PSUG300, false))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to disable output on PSU G300!");
        shutdown.executeShutdown();
        return false;
    }

    // Verify PSU output is actually off, retry if not
    for (int retry = 0; retry < 3; retry++)
    {
        usleep(500000); // 500ms wait before checking
        if (!g_DeviceManager.isOutputEnabled(PSUG300))
        {
            std::cout << "CMC: PSU G300 output verified OFF." << std::endl;
            break;
        }
        ErrorPrinter::warn("PSU", "CMC: PSU G300 still ON, retry " + std::to_string(retry + 1) + "/3...");
        g_DeviceManager.enableOutput(PSUG300, false);
    }
    shutdown.unregisterPsuOutputEnabled(PSUG300);

    // Record Power Off Time when PSU output is disabled
    g_ReportManager.recordPowerOffTime();

    if (!g_DeviceManager.disconnect(PSUG300))
    {
        ErrorPrinter::error("PSU", "CMC: Failed to disconnect PSU G300!");
        shutdown.executeShutdown();
        return false;
    }
    shutdown.unregisterPsuConnected(PSUG300);

    std::cout << "CMC: PSU configured successfully." << std::endl;
    return true;
}