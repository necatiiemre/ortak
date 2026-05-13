#ifndef FLICKER_DETECTION_CONFIG_H
#define FLICKER_DETECTION_CONFIG_H

namespace FlickerDetectionConfig
{
    // Mode: 1=Velocity, 2=DVI, 3=Both
    // Card values  : 0=CARD_1, 1=CARD_2, 2=CARD_BOTH
    // Channel values: 0=CH_1,   1=CH_2,   2=CH_BOTH
    // DVI channel   : 0=/dev/video0, 1=/dev/video1, 2=both

    namespace Cmc
    {
        constexpr int  MODE             = 3;     // Both (Velocity + DVI)
        constexpr int  CARD_1           = 0;     // CARD_1
        constexpr int  CHANNEL_1        = 2;     // CH_BOTH
        constexpr bool USE_SECOND_CARD  = false;
        constexpr int  CARD_2           = 0;
        constexpr int  CHANNEL_2        = 0;
        constexpr int  DVI_CHANNEL      = 2;     // both
        constexpr bool LOOPBACK         = false;
    }

    namespace Mmc
    {
        constexpr int  MODE             = 3;     // Both (Velocity + DVI)
        constexpr int  CARD_1           = 0;     // CARD_1
        constexpr int  CHANNEL_1        = 2;     // CH_BOTH
        constexpr bool USE_SECOND_CARD  = false;
        constexpr int  CARD_2           = 0;
        constexpr int  CHANNEL_2        = 0;
        constexpr int  DVI_CHANNEL      = 2;     // both
        constexpr bool LOOPBACK         = false;
    }
}

#endif // FLICKER_DETECTION_CONFIG_H