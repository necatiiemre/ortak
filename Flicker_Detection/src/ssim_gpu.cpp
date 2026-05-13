

#include "ssim_gpu.h"
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafilters.hpp>
#include <opencv2/core/cuda_stream_accessor.hpp>
#include <cuda_runtime.h>
#include <cmath>

// CUDA kernel launcher (cu dosyasında tanımlı optimize versiyon)
extern "C" void launchReduceKernel(const float *data, float *result, int total, cudaStream_t stream);

// Thread-local veri yapısı
struct ThreadLocalSSIMData
{
    cv::cuda::GpuMat d_img1, d_img2, d_img1f, d_img2f;
    cv::cuda::GpuMat mu1, mu2, mu1_sq, mu2_sq, mu1_mu2;
    cv::cuda::GpuMat img1_sq, img2_sq, img1_img2;
    cv::cuda::GpuMat sigma1_sq, sigma2_sq, sigma12;
    cv::cuda::GpuMat numerator1, numerator2, denominator1, denominator2, ssim_map;

    cv::Ptr<cv::cuda::Filter> filter;
    int filterWindowSize = -1;
    cv::cuda::Stream stream;

    float *d_result = nullptr;

    ~ThreadLocalSSIMData()
    {
        if (d_result)
        {
            cudaFree(d_result);
            d_result = nullptr;
        }
    }
};

// Gaussian filtre oluştur
cv::Ptr<cv::cuda::Filter> getGaussianFilter(int windowSize)
{
    return cv::cuda::createGaussianFilter(CV_32F, -1, cv::Size(windowSize, windowSize), 1.5);
}

// SSIM hesaplama fonksiyonu
float ssimGPU(const cv::Mat &img1, const cv::Mat &img2, int windowSize, float maxVal)
{
    thread_local ThreadLocalSSIMData data;
    auto &stream = data.stream;
    cudaStream_t raw_stream = cv::cuda::StreamAccessor::getStream(stream);

    if (!data.filter || data.filterWindowSize != windowSize)
    {
        data.filter = getGaussianFilter(windowSize);
        data.filterWindowSize = windowSize;
    }

    data.d_img1.upload(img1, stream);
    data.d_img2.upload(img2, stream);
    data.d_img1.convertTo(data.d_img1f, CV_32F, 1.0, 0.0, stream);
    data.d_img2.convertTo(data.d_img2f, CV_32F, 1.0, 0.0, stream);

    data.filter->apply(data.d_img1f, data.mu1, stream);
    data.filter->apply(data.d_img2f, data.mu2, stream);

    cv::cuda::multiply(data.mu1, data.mu1, data.mu1_sq, 1, -1, stream);
    cv::cuda::multiply(data.mu2, data.mu2, data.mu2_sq, 1, -1, stream);
    cv::cuda::multiply(data.mu1, data.mu2, data.mu1_mu2, 1, -1, stream);

    cv::cuda::multiply(data.d_img1f, data.d_img1f, data.img1_sq, 1, -1, stream);
    cv::cuda::multiply(data.d_img2f, data.d_img2f, data.img2_sq, 1, -1, stream);
    cv::cuda::multiply(data.d_img1f, data.d_img2f, data.img1_img2, 1, -1, stream);

    data.filter->apply(data.img1_sq, data.sigma1_sq, stream);
    data.filter->apply(data.img2_sq, data.sigma2_sq, stream);
    data.filter->apply(data.img1_img2, data.sigma12, stream);

    cv::cuda::subtract(data.sigma1_sq, data.mu1_sq, data.sigma1_sq, cv::noArray(), -1, stream);
    cv::cuda::subtract(data.sigma2_sq, data.mu2_sq, data.sigma2_sq, cv::noArray(), -1, stream);
    cv::cuda::subtract(data.sigma12, data.mu1_mu2, data.sigma12, cv::noArray(), -1, stream);

    const float C1 = std::pow(0.01f * maxVal, 2);
    const float C2 = std::pow(0.03f * maxVal, 2);

    cv::cuda::multiply(data.mu1_mu2, 2.0, data.numerator1, 1, -1, stream);
    cv::cuda::add(data.numerator1, C1, data.numerator1, cv::noArray(), -1, stream);

    cv::cuda::multiply(data.sigma12, 2.0, data.numerator2, 1, -1, stream);
    cv::cuda::add(data.numerator2, C2, data.numerator2, cv::noArray(), -1, stream);

    cv::cuda::add(data.mu1_sq, data.mu2_sq, data.denominator1, cv::noArray(), -1, stream);
    cv::cuda::add(data.denominator1, C1, data.denominator1, cv::noArray(), -1, stream);

    cv::cuda::add(data.sigma1_sq, data.sigma2_sq, data.denominator2, cv::noArray(), -1, stream);
    cv::cuda::add(data.denominator2, C2, data.denominator2, cv::noArray(), -1, stream);

    cv::cuda::multiply(data.numerator1, data.numerator2, data.numerator1, 1, -1, stream);
    cv::cuda::multiply(data.denominator1, data.denominator2, data.denominator1, 1, -1, stream);
    cv::cuda::divide(data.numerator1, data.denominator1, data.ssim_map, 1, -1, stream);

    const int total = data.ssim_map.rows * data.ssim_map.cols;
    if (total == 0)
    {
        stream.waitForCompletion();
        return 0.0f;
    }

    if (!data.d_result)
        cudaMalloc(&data.d_result, sizeof(float));
    cudaMemsetAsync(data.d_result, 0, sizeof(float), raw_stream);

    launchReduceKernel((const float *)data.ssim_map.ptr<float>(), data.d_result, total, raw_stream);

    float h_result = 0.0f;
    cudaMemcpyAsync(&h_result, data.d_result, sizeof(float), cudaMemcpyDeviceToHost, raw_stream);
    stream.waitForCompletion();

    return h_result / total;
}
