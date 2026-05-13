#include "FlickerDetectionRunner.h"
#include "FlickerDetectionConfig.h"
#include "SSHDeployer.h"
#include "ErrorPrinter.h"
#include "Utils.h"

#include <vector>
#include <string>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

FlickerDetectionRunner::FlickerDetectionRunner() : m_pid(-1) {}

FlickerDetectionRunner::~FlickerDetectionRunner()
{
    if (isRunning())
    {
        stop();
    }
}

bool FlickerDetectionRunner::isRunning() const
{
    if (m_pid <= 0) return false;
    return ::kill(m_pid, 0) == 0;
}

bool FlickerDetectionRunner::startForCmc(const std::string& log_path,
                                         const std::string& output_dir)
{
    UnitParams p{
        "CMC",
        FlickerDetectionConfig::Cmc::MODE,
        FlickerDetectionConfig::Cmc::CARD_1,
        FlickerDetectionConfig::Cmc::CHANNEL_1,
        FlickerDetectionConfig::Cmc::USE_SECOND_CARD,
        FlickerDetectionConfig::Cmc::CARD_2,
        FlickerDetectionConfig::Cmc::CHANNEL_2,
        FlickerDetectionConfig::Cmc::DVI_CHANNEL,
        FlickerDetectionConfig::Cmc::LOOPBACK
    };
    return startWithParams(p, log_path, output_dir);
}

bool FlickerDetectionRunner::startForMmc(const std::string& log_path,
                                         const std::string& output_dir)
{
    UnitParams p{
        "MMC",
        FlickerDetectionConfig::Mmc::MODE,
        FlickerDetectionConfig::Mmc::CARD_1,
        FlickerDetectionConfig::Mmc::CHANNEL_1,
        FlickerDetectionConfig::Mmc::USE_SECOND_CARD,
        FlickerDetectionConfig::Mmc::CARD_2,
        FlickerDetectionConfig::Mmc::CHANNEL_2,
        FlickerDetectionConfig::Mmc::DVI_CHANNEL,
        FlickerDetectionConfig::Mmc::LOOPBACK
    };
    return startWithParams(p, log_path, output_dir);
}

bool FlickerDetectionRunner::startWithParams(const UnitParams& p,
                                             const std::string& log_path,
                                             const std::string& output_dir)
{
    if (isRunning())
    {
        ErrorPrinter::warn("FLICKER", "FlickerDetection already running (pid="
                           + std::to_string(m_pid) + ")");
        return true;
    }

    std::string binary = SSHDeployer::getPrebuiltRoot()
                       + "/Flicker_Detection/FlickerDetection";

    if (!std::filesystem::exists(binary))
    {
        ErrorPrinter::error("FLICKER",
            "FlickerDetection binary not found at: " + binary
            + " (run prepare_release.sh flicker)");
        return false;
    }

    std::vector<std::string> args;
    args.push_back(binary);
    args.push_back("--mode");
    args.push_back(std::to_string(p.mode));

    if (p.mode == 1 || p.mode == 3)
    {
        args.push_back("--card1");
        args.push_back(std::to_string(p.card1));
        args.push_back("--channel1");
        args.push_back(std::to_string(p.channel1));

        if (p.use_second_card)
        {
            args.push_back("--card2");
            args.push_back(std::to_string(p.card2));
            args.push_back("--channel2");
            args.push_back(std::to_string(p.channel2));
        }

        if (p.loopback)
            args.push_back("--loopback");
    }

    if (p.mode == 2 || p.mode == 3)
    {
        args.push_back("--dvi-channel");
        args.push_back(std::to_string(p.dvi_channel));
    }

    if (!output_dir.empty())
    {
        args.push_back("--output-dir");
        args.push_back(output_dir);
    }

    args.push_back("--no-commands");

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    try {
        std::filesystem::create_directories(
            std::filesystem::path(log_path).parent_path());
        if (!output_dir.empty())
            std::filesystem::create_directories(output_dir);
    } catch (const std::exception& e) {
        ErrorPrinter::warn("FLICKER",
            std::string("Could not create flicker dirs: ") + e.what());
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
        ErrorPrinter::error("FLICKER",
            std::string("fork() failed: ") + std::strerror(errno));
        return false;
    }

    if (pid == 0)
    {
        int log_fd = ::open(log_path.c_str(),
                            O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd >= 0)
        {
            ::dup2(log_fd, STDOUT_FILENO);
            ::dup2(log_fd, STDERR_FILENO);
            ::close(log_fd);
        }
        int devnull = ::open("/dev/null", O_RDONLY);
        if (devnull >= 0)
        {
            ::dup2(devnull, STDIN_FILENO);
            ::close(devnull);
        }

        ::setpgid(0, 0);

        ::execv(binary.c_str(), argv.data());
        std::cerr << "execv failed: " << std::strerror(errno) << std::endl;
        _exit(127);
    }

    m_pid = pid;
    DEBUG_LOG("FLICKER: FlickerDetection started for " << p.label
              << ", pid=" << m_pid
              << " log=" << log_path
              << " output=" << output_dir);
    return true;
}

void FlickerDetectionRunner::stop()
{
    if (m_pid <= 0) return;

    if (::kill(m_pid, 0) != 0)
    {
        m_pid = -1;
        return;
    }

    DEBUG_LOG("FLICKER: Sending SIGTERM to FlickerDetection (pid="
              << m_pid << ")");
    ::kill(m_pid, SIGTERM);

    // Wait up to 10s for graceful exit
    for (int i = 0; i < 100; ++i)
    {
        int status = 0;
        pid_t r = ::waitpid(m_pid, &status, WNOHANG);
        if (r == m_pid)
        {
            DEBUG_LOG("FLICKER: FlickerDetection exited cleanly");
            m_pid = -1;
            return;
        }
        if (r < 0 && errno == ECHILD)
        {
            m_pid = -1;
            return;
        }
        usleep(100 * 1000);
    }

    ErrorPrinter::warn("FLICKER",
        "FlickerDetection did not exit after SIGTERM, sending SIGKILL");
    ::kill(m_pid, SIGKILL);
    int status = 0;
    ::waitpid(m_pid, &status, 0);
    m_pid = -1;
}