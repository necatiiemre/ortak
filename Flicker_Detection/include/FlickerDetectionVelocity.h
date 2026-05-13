#ifndef FLICKER_DETECTION_VELOCITY_H
#define FLICKER_DETECTION_VELOCITY_H

#include <iostream>
#include <optional>
#include "DirectoryManager.h"

// Main entry point for flicker detection; sets up directories, checks CUDA support, initializes drivers, and starts capture
// @param card_1: first video capture card identifier
// @param channel_1: channel associated with the first card
// @param card_2: optional second capture card identifier
// @param channel_2: optional channel for the second card
// @return void
uint8_t startFlickerDetection(Card card, Channel channel,
                           std::optional<Card> card_2 = std::nullopt,
                           std::optional<Channel> channel_2 = std::nullopt, bool loopback_test = false);

#endif // FLICKER_DETECTION_VELOCITY_H
