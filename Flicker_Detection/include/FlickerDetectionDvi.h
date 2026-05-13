#ifndef FLICKER_DETECTION_DVI_H
#define FLICKER_DETECTION_DVI_H

#include "DviManager.h"

// Starts flicker detection for DVI input on the specified channel
// Checks CUDA support, initializes DVI capture, and monitors activity until stopped
// @param channel: index of the DVI channel to monitor (0 = Ch1, 1 = Ch2, 2 = both)
// @return void
uint8_t startFlickerDetectionDvi(int channel);

#endif