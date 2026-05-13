#ifndef CUDA_MANAGER_H
#define CUDA_MANAGER_H

#include <iostream>
#include <opencv2/core/cuda.hpp>

class CudaManager {
public:
    // CUDA desteğini kontrol eder ve cihaz bilgilerini yazdırır
    uint8_t checkCudaSupport();
};

#endif // CUDA_MANAGER_H
