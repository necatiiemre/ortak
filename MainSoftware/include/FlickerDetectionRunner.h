#ifndef FLICKER_DETECTION_RUNNER_H
#define FLICKER_DETECTION_RUNNER_H

#include <string>
#include <sys/types.h>

class FlickerDetectionRunner
{
public:
    FlickerDetectionRunner();
    ~FlickerDetectionRunner();

    /**
     * @brief Spawn the local FlickerDetection binary with CMC defaults.
     *        Stdout/stderr are redirected to log_path. Error frames and
     *        videos are written under output_dir.
     */
    bool startForCmc(const std::string& log_path,
                     const std::string& output_dir);

    /**
     * @brief Spawn the local FlickerDetection binary with MMC defaults.
     *        Stdout/stderr are redirected to log_path. Error frames and
     *        videos are written under output_dir.
     */
    bool startForMmc(const std::string& log_path,
                     const std::string& output_dir);

    /**
     * @brief Send SIGTERM to the running FlickerDetection process and wait
     *        until it exits (with a bounded grace period before SIGKILL).
     */
    void stop();

    bool isRunning() const;

private:
    struct UnitParams
    {
        const char* label;
        int mode;
        int card1;
        int channel1;
        bool use_second_card;
        int card2;
        int channel2;
        int dvi_channel;
        bool loopback;
    };

    bool startWithParams(const UnitParams& p,
                         const std::string& log_path,
                         const std::string& output_dir);

    pid_t m_pid;
};

#endif // FLICKER_DETECTION_RUNNER_H