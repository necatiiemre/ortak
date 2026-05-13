cmake_minimum_required(VERSION 3.18)

project(FlickerDetection LANGUAGES CXX CUDA)

# Varsayılan Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CUDA compute capability
set(CMAKE_CUDA_ARCHITECTURES 75 86)

# OpenCV ve Threads bulunur
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)  # <-- pthread desteği için eklendi

# CUDA kernel ayrı static library
add_library(ssim_kernels STATIC src/ssim_kernel.cu)

set_target_properties(ssim_kernels PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

# Ana kaynak dosyalar
set(SOURCES
    src/main.cpp
    src/ssim_gpu.cpp
    src/GaussianWindow.cpp
    src/FlickerDetectionVelocity.cpp
    src/FlickerDetectionDvi.cpp
    src/DirectoryManager.cpp
    src/CudaManager.cpp
    src/DriverManager.cpp
    src/ImageProcessor.cpp
    src/DviManager.cpp
    src/Globals.cpp
    src/ErrorUtils.cpp
    src/VideoUtils.cpp
    src/Helpers.cpp
)

# Header dosyalar
set(HEADERS
    include/Resolution.h
    include/ssim_gpu.h
    include/FlickerDetectionVelocity.h
    include/FlickerDetectionDvi.h
    include/DirectoryManager.h
    include/CudaManager.h
    include/DriverManager.h
    include/ImageProcessor.h
    include/DviManager.h
    include/Globals.h
    include/DebugLog.h
    include/ImageDisplayTimeout.h
    include/ErrorUtils.h
    include/DirectoryException.h
    include/VideoUtils.h
    include/Helpers.h
)

# Executable
add_executable(FlickerDetection ${SOURCES} ${HEADERS})

# Dahil dizinleri
target_include_directories(FlickerDetection PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    include
    /usr/local/include/velocity/
    /usr/local/cuda/include
)

# C++ OPTIMIZATION FLAGS
target_compile_options(FlickerDetection PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:
        $<$<CONFIG:RELEASE>:
            -O3
            -Ofast
            -march=native
            -funroll-loops
            -ffast-math
            -DNDEBUG
        >
        $<$<CONFIG:DEBUG>:
            -O0
            -g
            -Wall
            -Wextra
        >
    >
)

# CUDA OPTIMIZATION FLAGS
target_compile_options(ssim_kernels PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        $<$<CONFIG:RELEASE>:
            -O3
            --use_fast_math
            --relocatable-device-code=true
            --generate-line-info
        >
        $<$<CONFIG:DEBUG>:
            -O0
            -G
            -g
        >
    >
)

# CUDA properties
set_target_properties(FlickerDetection PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

# C++17 garantisi
target_compile_features(FlickerDetection PRIVATE cxx_std_17)

# Kütüphaneler
target_link_libraries(FlickerDetection
    ${OpenCV_LIBS}
    grtv_api
    ssim_kernels
    Threads::Threads     # <-- pthread için gerekli link
)

# # Debug tanımı
# target_compile_definitions(FlickerDetection PRIVATE
#     $<$<CONFIG:DEBUG>:DEBUG>
# )
