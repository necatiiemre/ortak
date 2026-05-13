#include "CudaManager.h"
#include "DebugLog.h"
#include "ErrorUtils.h"

using cv::cuda::getCudaEnabledDeviceCount;
using cv::cuda::getDevice;
using cv::cuda::DeviceInfo;

namespace {
    void cudaLog(const std::string &label, const std::string &value) {
        LOG_INFO(label << ": " << value);
    }

    void cudaLog(const std::string &label, int value) {
        LOG_INFO(label << ": " << value);
    }

    void printCudaDeviceInfo(int device_id, const DeviceInfo &info) {
        LOG_INFO("CUDA is supported on this system!");
        LOG_INFO("CUDA Device Info:");

        cudaLog("Device ID", device_id);
        cudaLog("Device Name", info.name());
        cudaLog("Compute Capability", std::to_string(info.majorVersion()) + "." + std::to_string(info.minorVersion()));
        cudaLog("MultiProcessor Count", info.multiProcessorCount());
        cudaLog("Total Memory (MB)", static_cast<int>(info.totalGlobalMem() / (1024 * 1024)));
    }
}

// Checks whether CUDA is supported and available on the current system
// Prints device information such as name, compute capability, memory, and multiprocessor count
// Useful for verifying GPU readiness before using CUDA-accelerated functions
// @return void
uint8_t CudaManager::checkCudaSupport()
{
    try
    {
        if (getCudaEnabledDeviceCount() > 0)
        {
            int device_id = getDevice();
            DeviceInfo info(device_id);

            printCudaDeviceInfo(device_id, info);

            return CODE_SUCCESS;
        }
        else
        {
            LOG_ERROR("No CUDA-enabled devices found.");
            return CODE_CUDA_NOT_SUPPORTED;
        }

    }
    catch (const std::exception &e)
    {
        LOG_ERROR("CUDA is not supported on this system! Reason: " << e.what());
        throw;
    }
}
