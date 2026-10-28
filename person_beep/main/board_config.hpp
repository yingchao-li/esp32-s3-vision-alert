#pragma once

// Freenove ESP32-S3 WROOM: official camera connector mapping.
// Freenove's example calls this pin map ESP32S3_EYE; the board is Freenove.
namespace board {
constexpr int kBuzzer = 14;  // Active HIGH, S8050 + 1k base resistor.
constexpr int kPwdn = -1;
constexpr int kReset = -1;
constexpr int kXclk = 15;
constexpr int kSda = 4;
constexpr int kScl = 5;
constexpr int kD0 = 11;
constexpr int kD1 = 9;
constexpr int kD2 = 8;
constexpr int kD3 = 10;
constexpr int kD4 = 12;
constexpr int kD5 = 18;
constexpr int kD6 = 17;
constexpr int kD7 = 16;
constexpr int kVsync = 6;
constexpr int kHref = 7;
constexpr int kPclk = 13;
static_assert(kBuzzer != kXclk && kBuzzer != kSda && kBuzzer != kScl &&
              kBuzzer != kD0 && kBuzzer != kD1 && kBuzzer != kD2 &&
              kBuzzer != kD3 && kBuzzer != kD4 && kBuzzer != kD5 &&
              kBuzzer != kD6 && kBuzzer != kD7 && kBuzzer != kVsync &&
              kBuzzer != kHref && kBuzzer != kPclk, "Buzzer/camera pin conflict");
}
