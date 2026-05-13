// reduce_kernel.cu
#include <cuda_runtime.h>

__global__ void reduceKernel(const float* data, float* result, int total)
{
    extern __shared__ float sdata[];
    int tid = threadIdx.x;
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    // Load global data into shared memory (sdata)
    sdata[tid] = (i < total) ? data[i] : 0.0f;
    __syncthreads();

    // Reduce within block
    for (int s = blockDim.x / 2; s > 0; s >>= 1)
    {
        if (tid < s)
            sdata[tid] += sdata[tid + s];
        __syncthreads();
    }

    // Add the result of this block to global result atomically
    if (tid == 0)
        atomicAdd(result, sdata[0]);
}

// Wrapper function for launching the kernel
extern "C" void launchReduceKernel(const float* data, float* result, int total, cudaStream_t stream)
{
    const int threads = 256;
    const int blocks = (total + threads - 1) / threads;
    size_t sharedMemSize = threads * sizeof(float);

    reduceKernel<<<blocks, threads, sharedMemSize, stream>>>(data, result, total);
}
