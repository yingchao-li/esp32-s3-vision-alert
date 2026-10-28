#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vision {
constexpr size_t kMaxObjects = 24;
enum class ClassId : uint8_t { Person = 0, Cone = 1, Bicycle = 2, Car = 3, None = 255 };
inline bool valid_class(ClassId c) { return static_cast<unsigned>(c) < 4; }
inline const char *class_name(ClassId c) {
    constexpr const char *names[] = {"Person", "Traffic Cone", "Bicycle", "Car"};
    return valid_class(c) ? names[static_cast<unsigned>(c)] : "None";
}
// COCO-80 category indices used by the official ESP-DL YOLO11n model. Only
// person(0), bicycle(1) and car(2) exist in the alarm vocabulary; the other 77
// COCO categories (including motorcycle/truck, which are NOT renamed to Car or
// Bicycle) are ignored. Traffic Cone has no COCO category and stays undeployed.
inline ClassId coco_to_class(int category) {
    switch (category) {
    case 0:
        return ClassId::Person;
    case 1:
        return ClassId::Bicycle;
    case 2:
        return ClassId::Car;
    default:
        return ClassId::None;
    }
}
enum class Mode : uint8_t { Accuracy = 0, Balanced = 1, Performance = 2 };
struct Box {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float width() const { return std::max(0.f, x2 - x1); }
    float height() const { return std::max(0.f, y2 - y1); }
    float area() const { return width() * height(); }
    float cx() const { return (x1 + x2) * .5f; }
    float cy() const { return (y1 + y2) * .5f; }
    bool valid() const {
        return std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2) && std::isfinite(y2) && x1 >= 0 &&
               y1 >= 0 && x2 <= 1 && y2 <= 1 && width() > 0 && height() > 0;
    }
};
inline float iou(const Box &a, const Box &b) {
    float overlap = std::max(0.f, std::min(a.x2, b.x2) - std::max(a.x1, b.x1)) *
                    std::max(0.f, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
    float total = a.area() + b.area() - overlap;
    return total > 0 ? overlap / total : 0;
}
struct Detection {
    ClassId cls = ClassId::None;
    float score = 0;
    Box box;
};
struct Track {
    uint32_t id = 0;
    ClassId cls = ClassId::None;
    float score = 0;
    Box box;
    float vx = 0, vy = 0, area_rate = 0;
    int64_t seen_us = 0;
    uint16_t hits = 0;
    bool matched = false, confirmed = false;
    int8_t zone = -1, event = 0; // -1=left ROI/zone, +1=entered; 0=no transition
    uint8_t risk = 0;
};
struct FrameResult {
    uint32_t frame_id = 0;
    int64_t captured_us = 0, completed_us = 0;
    float inference_ms = 0;
    uint16_t width = 320, height = 240;
    uint8_t count = 0;
    std::array<Track, kMaxObjects> tracks{};
};
struct Settings {
    bool ai_enabled = true, buzzer_enabled = true, stream_enabled = true;
    Mode mode = Mode::Balanced;
    float confidence = .80f;
    uint32_t beep_ms = 150, gap_ms = 150, alert_interval_ms = 3000;
    float roi_top = .25f, roi_bottom = 1.f, left_boundary = 1.f / 3, right_boundary = 2.f / 3;
    float danger_y = .72f;
    uint16_t confirm_frames = 2;
    std::array<uint8_t, 4> priority{{0, 1, 2, 3}};
};
inline bool valid_settings(const Settings &s) {
    if (static_cast<unsigned>(s.mode) > 2 || !std::isfinite(s.confidence) || s.confidence < .05f ||
        s.confidence > .99f || s.beep_ms < 30 || s.beep_ms > 500 || s.gap_ms < 30 || s.gap_ms > 500 ||
        s.alert_interval_ms < 1000 || s.alert_interval_ms > 60000 || s.confirm_frames < 1 ||
        s.confirm_frames > 10)
        return false;
    if (s.alert_interval_ms < 4 * s.beep_ms + 3 * s.gap_ms + 150)
        return false;
    if (!std::isfinite(s.roi_top) || !std::isfinite(s.roi_bottom) || !std::isfinite(s.left_boundary) ||
        !std::isfinite(s.right_boundary) || !std::isfinite(s.danger_y) || s.roi_top < 0 || s.roi_bottom > 1 ||
        s.roi_top >= s.roi_bottom || s.left_boundary <= 0 || s.right_boundary >= 1 ||
        s.left_boundary >= s.right_boundary || s.danger_y < s.roi_top || s.danger_y > s.roi_bottom)
        return false;
    unsigned bits = 0;
    for (auto p : s.priority) {
        if (p > 3)
            return false;
        bits |= 1u << p;
    }
    return bits == 15;
}
inline int preview_fps(Mode m) { return m == Mode::Accuracy ? 2 : m == Mode::Balanced ? 1 : 0; }
inline int preview_quality(Mode m) { return m == Mode::Accuracy ? 75 : 60; }
} // namespace vision
