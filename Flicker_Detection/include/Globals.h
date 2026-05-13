#include "CudaManager.h"
#include "DirectoryManager.h"
#include "DriverManager.h"
#include "DviManager.h"
#include "FlickerDetectionDvi.h"
#include "FlickerDetectionVelocity.h"
#include "ImageProcessor.h"
#include "Resolution.h"
#include "ssim_gpu.h"
#include "ErrorUtils.h"

extern CudaManager cuda_manager;
extern DirectoryManager directory_manager;
extern DriverManager driver_manager;
extern DviManager dvi_manager;
extern ImageProcessor image_processor;

extern bool loopbackTestMode ;

extern int rc;

