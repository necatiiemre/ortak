#include <cmath>
#include <vector>

// Generates a 2D Gaussian window used for local filtering (e.g., in SSIM calculations)
// @param windowSize: size of the Gaussian kernel (e.g., 11 for an 11x11 window)
// @return a 2D vector containing the normalized Gaussian weights
std::vector<std::vector<float>> createGaussianWindow(int windowSize)
{
    // Create a 1D Gaussian vector of the specified size
    std::vector<float> gauss(windowSize);
    float sigma = 1.5;           // Standard deviation for the Gaussian distribution
    int center = windowSize / 2; // Center index of the Gaussian
    float sum = 0.0;             // Sum for normalization

    // Calculate the values of the 1D Gaussian vector
    for (int i = 0; i < windowSize; ++i)
    {
        gauss[i] = std::exp(-0.5 * std::pow((i - center) / sigma, 2));
        sum += gauss[i];
    }

    // Normalize the 1D Gaussian so that the sum of all elements is 1
    for (int i = 0; i < windowSize; ++i)
    {
        gauss[i] /= sum;
    }

    // Create a 2D Gaussian window by taking the outer product of the 1D vector with itself
    std::vector<std::vector<float>> window(windowSize, std::vector<float>(windowSize, 0.0f));
    for (int i = 0; i < windowSize; ++i)
    {
        for (int j = 0; j < windowSize; ++j)
        {
            window[i][j] = gauss[i] * gauss[j];
        }
    }

    return window; // Return the resulting 2D Gaussian window
}
