#include "FlickerDetectionVelocity.h"
#include "FlickerDetectionDvi.h"
#include "DviManager.h"
#include "DriverManager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <string>
#include <optional>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <cstdlib>
#include <atomic>
#include <csignal>
#include "Globals.h"
#include "ErrorUtils.h"
#include <opencv2/core/ocl.hpp>
#include <opencv2/opencv.hpp>

std::atomic<bool> stopRequested{false};

static std::atomic<bool> g_shutdown_signal{false};
static std::atomic<bool> g_systems_stopped{false};

static void shutdown_signal_handler(int /*sig*/)
{
    g_shutdown_signal = true;
    stopRequested = true;
}

static void stopAllSystemsOnce()
{
    bool expected = false;
    if (!g_systems_stopped.compare_exchange_strong(expected, true))
        return;
    fprintf(stderr, "[shutdown] stopAllSystemsOnce: stopFlickerDetection start\n");
    driver_manager.stopFlickerDetection();
    fprintf(stderr, "[shutdown] stopAllSystemsOnce: stopFlickerDetection done\n");
    dvi_manager.stop(2);
    fprintf(stderr, "[shutdown] stopAllSystemsOnce: dvi_manager.stop done\n");
    try
    {
        cv::destroyAllWindows();
        for (int i = 0; i < 10; ++i)
            cv::waitKey(10);
    }
    catch (...)
    {
    }
    fprintf(stderr, "[shutdown] stopAllSystemsOnce: GUI windows destroyed\n");
}

/* Velocity Flicker Detection Thread */
void flickerDetectionTask(int card1Input, int channel1Input,
                          std::optional<Card> card2, std::optional<Channel> channel2, bool loopback_test)
{
    try
    {
        if (card2.has_value() && channel2.has_value())
        {
            startFlickerDetection(
                static_cast<Card>(card1Input),
                static_cast<Channel>(channel1Input),
                card2,
                channel2,
                loopback_test);
        }
        else
        {
            startFlickerDetection(
                static_cast<Card>(card1Input),
                static_cast<Channel>(channel1Input),
                std::nullopt,
                std::nullopt,
                loopback_test);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Velocity Flicker Detection Error: " << e.what() << std::endl;
    }
}

/* DVI Flicker Detection Thread */
void flickerDetectionDviTask(int channel)
{
    rc = startFlickerDetectionDvi(channel);
    checkReturnCode(rc, "DVI Flicker Detection failed.");
}

/* Command Interface */
void processCommands()
{
    std::string command;
    while (true)
    {
        std::cout << "\n🔹🔹🔹🔹 COMMANDS 🔹🔹🔹🔹\n";
        std::cout << "1. stats          - Show statistics for all\n";
        std::cout << "2. stop all       - Stop all flicker detection\n";
        std::cout << "3. stop card      - Stop specific Velocity card/channel\n";
        std::cout << "4. restart card   - Restart specific Velocity card/channel\n";
        std::cout << "5. stop dvi       - Stop DVI detection\n";
        std::cout << "6. restart dvi    - Restart DVI detection\n";
        std::cout << "7. exit           - Exit program\n";
        std::cout << "Command: ";

        std::getline(std::cin, command);

        // Trim spaces
        command.erase(0, command.find_first_not_of(" \t\n\r"));
        command.erase(command.find_last_not_of(" \t\n\r") + 1);

        if (command == "stats" || command == "1")
        {
            rc = driver_manager.printStatistics();
            checkReturnCode(rc, "Print Statistics Failed.");
            rc = dvi_manager.printStatistics();
            checkReturnCode(rc, "DVI Manager print statistics failed.");
        }
        else if (command == "stop all" || command == "2")
        {
            std::cout << "Stopping all systems...\n";
            stopRequested = true;
            stopAllSystemsOnce();
            break;
        }
        else if (command == "stop card" || command == "3")
        {
            std::cout << "Which Card (0: CARD_1, 1: CARD_2, 2: CARD_BOTH): ";
            std::getline(std::cin, command);
            Card card = static_cast<Card>(std::stoi(command));

            std::cout << "Which Channel (0: CH_1, 1: CH_2, 2: CH_BOTH): ";
            std::getline(std::cin, command);
            Channel channel = static_cast<Channel>(std::stoi(command));

            driver_manager.stopCard(card, channel);
        }
        else if (command == "restart card" || command == "4")
        {
            std::cout << "Which Card (0: CARD_1, 1: CARD_2, 2: CARD_BOTH): ";
            std::getline(std::cin, command);
            Card card = static_cast<Card>(std::stoi(command));

            std::cout << "Which Channel (0: CH_1, 1: CH_2, 2: CH_BOTH): ";
            std::getline(std::cin, command);
            Channel channel = static_cast<Channel>(std::stoi(command));

            driver_manager.restartCard(card, channel);
        }
        else if (command == "stop dvi" || command == "5")
        {
            std::cout << "Which DVI Channel to stop (0/1/2): ";
            std::getline(std::cin, command);
            int ch = std::stoi(command);
            cout << "select channel " << ch <<endl;
            rc = dvi_manager.stop(ch);
            checkReturnCode(rc, "DVI Manager stop failed.");
        }
        else if (command == "restart dvi" || command == "6")
        {
            std::cout << "Which DVI Channel to start (0/1/2): ";
            std::getline(std::cin, command);
            int ch = std::stoi(command);
            rc = dvi_manager.start(ch);
            checkReturnCode(rc, "DVI Manager start failed.");
        }
        else if (command == "exit" || command == "7")
        {
            std::cout << "Exiting, stopping all systems...\n";
            stopRequested = true;
            stopAllSystemsOnce();
            break;
        }
        else
        {
            std::cout << "❌ Invalid Command.\n";
        }
    }
}

struct CliConfig
{
    bool provided = false;
    int mode = 0;            // 1=velocity, 2=dvi, 3=both
    int card1 = 0;
    int channel1 = 0;
    bool useCard2 = false;
    int card2 = 0;
    int channel2 = 0;
    int dviChannel = 0;
    bool loopback = false;
    bool noCommands = false;
    std::string outputDir;
};

static bool parseIntArg(const std::string& s, int& out)
{
    try { out = std::stoi(s); return true; } catch (...) { return false; }
}

static CliConfig parseArgs(int argc, char** argv)
{
    CliConfig cfg;
    bool sawMode = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&](int& v) -> bool {
            if (i + 1 >= argc) return false;
            return parseIntArg(argv[++i], v);
        };
        if (a == "--mode" && next(cfg.mode)) { sawMode = true; }
        else if (a == "--card1") next(cfg.card1);
        else if (a == "--channel1") next(cfg.channel1);
        else if (a == "--card2") { cfg.useCard2 = true; next(cfg.card2); }
        else if (a == "--channel2") { cfg.useCard2 = true; next(cfg.channel2); }
        else if (a == "--dvi-channel") next(cfg.dviChannel);
        else if (a == "--loopback") cfg.loopback = true;
        else if (a == "--no-commands") cfg.noCommands = true;
        else if (a == "--output-dir")
        {
            if (i + 1 < argc) cfg.outputDir = argv[++i];
        }
        else
        {
            std::cerr << "Unknown argument: " << a << std::endl;
        }
    }
    cfg.provided = sawMode;
    return cfg;
}

int main(int argc, char** argv)
{
    std::cout << cv::getBuildInformation() << std::endl;

    cv::setNumThreads(1);
    cv::ocl::setUseOpenCL(false);

    CliConfig cli = parseArgs(argc, argv);

    if (!cli.outputDir.empty())
    {
        directory_manager.setBaseOutputDir(cli.outputDir);
    }

    struct sigaction sa{};
    sa.sa_handler = shutdown_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    std::thread shutdown_watcher([]() {
        while (!g_shutdown_signal) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        stopAllSystemsOnce();
    });

    std::thread velocity_thread;
    std::thread dvi_thread;

    if (cli.provided)
    {
        if (cli.mode == 1)
        {
            std::optional<Card> card2;
            std::optional<Channel> channel2;
            if (cli.useCard2)
            {
                card2 = static_cast<Card>(cli.card2);
                channel2 = static_cast<Channel>(cli.channel2);
            }
            loopbackTestMode = cli.loopback;
            velocity_thread = std::thread(flickerDetectionTask, cli.card1, cli.channel1, card2, channel2, loopbackTestMode);
        }
        else if (cli.mode == 2)
        {
            dvi_thread = std::thread(flickerDetectionDviTask, cli.dviChannel);
        }
        else if (cli.mode == 3)
        {
            std::optional<Card> card2;
            std::optional<Channel> channel2;
            if (cli.useCard2)
            {
                card2 = static_cast<Card>(cli.card2);
                channel2 = static_cast<Channel>(cli.channel2);
            }
            loopbackTestMode = cli.loopback;
            velocity_thread = std::thread(flickerDetectionTask, cli.card1, cli.channel1, card2, channel2, loopbackTestMode);
            dvi_thread = std::thread(flickerDetectionDviTask, cli.dviChannel);
        }
        else
        {
            std::cerr << "Invalid --mode value (expected 1/2/3)\n";
            return 1;
        }

        std::thread command_thread;
        if (!cli.noCommands)
        {
            command_thread = std::thread(processCommands);
        }

        fprintf(stderr, "[shutdown] joining velocity_thread...\n");
        if (velocity_thread.joinable()) velocity_thread.join();
        fprintf(stderr, "[shutdown] velocity_thread joined; joining dvi_thread...\n");
        if (dvi_thread.joinable()) dvi_thread.join();
        fprintf(stderr, "[shutdown] dvi_thread joined; joining command_thread...\n");
        if (command_thread.joinable()) command_thread.join();
        fprintf(stderr, "[shutdown] command_thread joined; releasing watcher...\n");
        g_shutdown_signal = true;
        if (shutdown_watcher.joinable()) shutdown_watcher.join();
        fprintf(stderr, "[shutdown] watcher joined.\n");
        std::cout << "Program terminated successfully.\n";
        return 0;
    }

    std::string modeSelection;
    std::cout << "🟢 Choose Flicker Detection Mode:\n";
    std::cout << "1 - Velocity\n";
    std::cout << "2 - DVI\n";
    std::cout << "3 - Both\n";
    std::cout << "Selection: ";
    std::getline(std::cin, modeSelection);

    if (modeSelection == "1" || modeSelection == "velocity")
    {
        int card1Input, channel1Input;
        std::optional<Card> card2;
        std::optional<Channel> channel2;

        std::cout << "Card1 (0: CARD_1, 1: CARD_2, 2: CARD_BOTH): ";
        std::cin >> card1Input;
        std::cout << "Channel1 (0: CH_1, 1: CH_2, 2: CH_BOTH): ";
        std::cin >> channel1Input;
        std::cin.ignore();

        if (card1Input != static_cast<int>(Card::CARD_BOTH))
        {
            std::string secondCardChoice;
            std::cout << "Use second card? (y/n): ";
            std::getline(std::cin, secondCardChoice);

            if (secondCardChoice == "y" || secondCardChoice == "Y")
            {
                int card2Input, channel2Input;
                std::cout << "Card2 (0: CARD_1, 1: CARD_2, 2: CARD_BOTH): ";
                std::cin >> card2Input;
                std::cout << "Channel2 (0: CH_1, 1: CH_2, 2: CH_BOTH): ";
                std::cin >> channel2Input;
                std::cin.ignore();

                card2 = static_cast<Card>(card2Input);
                channel2 = static_cast<Channel>(channel2Input);
            }
        }

        std::cout << "Do you want to loopback test ? (y/n): ";
        std::string loopbackChoice;
        std::getline(std::cin, loopbackChoice);
        loopbackTestMode = (loopbackChoice == "y" || loopbackChoice == "Y");

        velocity_thread = std::thread(flickerDetectionTask, card1Input, channel1Input, card2, channel2, loopbackTestMode);
    }
    else if (modeSelection == "2" || modeSelection == "dvi")
    {
        int dviChannel;
        std::cout << "DVI Channel (0: CH1 /dev/video0, 1: CH2 /dev/video1, 2: both): ";
        std::cin >> dviChannel;
        std::cin.ignore();

        dvi_thread = std::thread(flickerDetectionDviTask, dviChannel);
    }
    else if (modeSelection == "3" || modeSelection == "both")
    {
        int card1Input, channel1Input, dviChannel;
        std::optional<Card> card2;
        std::optional<Channel> channel2;

        std::cout << "\nVelocity Configuration:\n";
        std::cout << "Card1 (0: CARD_1, 1: CARD_2, 2: CARD_BOTH): ";
        std::cin >> card1Input;
        std::cout << "Channel1 (0: CH_1, 1: CH_2, 2: CH_BOTH): ";
        std::cin >> channel1Input;
        std::cin.ignore();

        if (card1Input != static_cast<int>(Card::CARD_BOTH))
        {
            std::string secondCardChoice;
            std::cout << "Use second card? (y/n): ";
            std::getline(std::cin, secondCardChoice);

            if (secondCardChoice == "y" || secondCardChoice == "Y")
            {
                int card2Input, channel2Input;
                std::cout << "Card2 (0: CARD_1, 1: CARD_2, 2: CARD_BOTH): ";
                std::cin >> card2Input;
                std::cout << "Channel2 (0: CH_1, 1: CH_2, 2: CH_BOTH): ";
                std::cin >> channel2Input;
                std::cin.ignore();

                card2 = static_cast<Card>(card2Input);
                channel2 = static_cast<Channel>(channel2Input);
            }
        }

        std::cout << "Do you want to loopback test ? (y/n): ";
        std::string loopbackChoice;
        std::getline(std::cin, loopbackChoice);
        loopbackTestMode = (loopbackChoice == "y" || loopbackChoice == "Y");

        std::cout << "\nDVI Configuration:\n";
        std::cout << "DVI Channel (0: CH1 /dev/video0, 1: CH2 /dev/video1, 2: both): ";
        std::cin >> dviChannel;
        std::cin.ignore();

        velocity_thread = std::thread(flickerDetectionTask, card1Input, channel1Input, card2, channel2, loopbackTestMode);
        dvi_thread = std::thread(flickerDetectionDviTask, dviChannel);
    }
    else
    {
        std::cerr << "❌ Invalid mode selection.\n";
        return 1;
    }

    // Start commands in separate thread
    std::thread command_thread(processCommands);

    // Wait for all threads
    fprintf(stderr, "[shutdown] joining velocity_thread...\n");
    if (velocity_thread.joinable())
        velocity_thread.join();
    fprintf(stderr, "[shutdown] velocity_thread joined; joining dvi_thread...\n");
    if (dvi_thread.joinable())
        dvi_thread.join();
    fprintf(stderr, "[shutdown] dvi_thread joined; joining command_thread...\n");
    if (command_thread.joinable())
        command_thread.join();
    fprintf(stderr, "[shutdown] command_thread joined; releasing watcher...\n");

    g_shutdown_signal = true;
    if (shutdown_watcher.joinable()) shutdown_watcher.join();
    fprintf(stderr, "[shutdown] watcher joined.\n");

    std::cout << "✅ Program terminated successfully.\n";
    return 0;
}