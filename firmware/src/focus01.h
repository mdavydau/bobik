// focus01 animation for Adafruit SSD1306
// Generated from video - 65 frames @ 8fps
// Format: Adafruit_GFX bitmap (MSB first)
// Display: 128x64 OLED

#ifndef FOCUS01_H
#define FOCUS01_H

#include <Arduino.h>

// Animation properties
#define FOCUS01_FRAME_COUNT 65
#define FOCUS01_FPS 8
#define FOCUS01_FRAME_DELAY 125  // ms per frame

// Frame data lives in focus01.cpp (separate translation unit for fast rebuilds)
extern const unsigned char* const focus01_frames[];

#endif
