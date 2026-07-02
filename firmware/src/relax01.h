// relax01 animation for Adafruit SSD1306
// Generated from video - 65 frames @ 8fps
// Format: Adafruit_GFX bitmap (MSB first)
// Display: 128x64 OLED

#ifndef RELAX01_H
#define RELAX01_H

#include <Arduino.h>

// Animation properties
#define RELAX01_FRAME_COUNT 65
#define RELAX01_FPS 8
#define RELAX01_FRAME_DELAY 125  // ms per frame

// Frame data lives in relax01.cpp (separate translation unit for fast rebuilds)
extern const unsigned char* const relax01_frames[];

#endif
