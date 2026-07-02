// love01 animation for Adafruit SSD1306
// Generated from video - 65 frames @ 8fps
// Format: Adafruit_GFX bitmap (MSB first)
// Display: 128x64 OLED

#ifndef LOVE01_H
#define LOVE01_H

#include <Arduino.h>

// Animation properties
#define LOVE01_FRAME_COUNT 65
#define LOVE01_FPS 8
#define LOVE01_FRAME_DELAY 125  // ms per frame

// Frame data lives in love01.cpp (separate translation unit for fast rebuilds)
extern const unsigned char* const love01_frames[];

#endif
