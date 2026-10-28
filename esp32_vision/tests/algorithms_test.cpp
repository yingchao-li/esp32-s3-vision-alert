#include "alarm_policy.hpp"
#include "detection_filter.hpp"
#include "object_tracker.hpp"
#include "obstacle_analyzer.hpp"
#include "stats.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
#include <set>
using namespace vision;

void alarms() {
    Settings s;
    assert(valid_settings(s));
    AlarmPolicy p;
    assert(!p.may_start(ClassId::None, 100, 100, true, s));
    assert(!p.may_start(ClassId::Car, 100, 100, false, s));
    assert(!p.may_start(ClassId::Car, 100, 101, true, s));
    assert(!p.may_start(ClassId::Car, 2000101, 100, true, s));
    assert(p.may_start(ClassId::Person, 100, 100, true, s));
    p.started(ClassId::Car, 100, s.alert_interval_ms);
    assert(!p.may_start(ClassId::Person, 200, 200, true, s));
    assert(p.active() == ClassId::Car);
    p.stopped();
    assert(!p.ready(3000099));
    assert(p.ready(3000100));
    s.buzzer_enabled = false;
    assert(!p.may_start(ClassId::Car, 3000100, 3000100, true, s));
    s.buzzer_enabled = true;
    s.ai_enabled = false;
    assert(!p.may_start(ClassId::Car, 3000100, 3000100, true, s));
    for (unsigned c = 0; c < 4; ++c) {
        auto cls = static_cast<ClassId>(c);
        assert(AlarmPolicy::count(cls) == c + 1);
        Pulse pulses[128];
        size_t n = waveform(cls, 150, 150, pulses, 128);
        assert(n > 0);
        unsigned high = 0, low = 0, starts = 0;
        int prior = 0;
        for (size_t i = 0; i < n; ++i) {
            assert(pulses[i].duration > 0 && pulses[i].duration <= 32767);
            if (pulses[i].level) {
                high += pulses[i].duration;
                if (!prior)
                    ++starts;
            } else
                low += pulses[i].duration;
            prior = pulses[i].level;
        }
        assert(starts == c + 1);
        assert(high == (c + 1) * 150000);
        assert(low == c * 150000 + 1);
        assert(prior == 0);
        assert(waveform(cls, 150, 150, pulses, 1) == 0);
    }
    Pulse longest[128];
    assert(waveform(ClassId::Car, 500, 500, longest, 128) <= 128);
    s = Settings{};
    s.beep_ms = 500;
    s.gap_ms = 500;
    assert(!valid_settings(s));
    s = Settings{};
    s.priority = {0, 0, 1, 2};
    assert(!valid_settings(s));
    s = Settings{};
    s.confidence = std::numeric_limits<float>::quiet_NaN();
    assert(!valid_settings(s));
    s = Settings{};
    s.left_boundary = .8;
    s.right_boundary = .2;
    assert(!valid_settings(s));
    s = Settings{};
    s.roi_top = .9;
    s.danger_y = .5;
    assert(!valid_settings(s));
    puts("PASS alarm: 1/2/3/4 counts, bounded pulses, start intervals, no re-entry, mute/fault/freshness, "
         "settings bounds");
}

void tracking() {
    ObjectTracker tracker;
    FrameResult f;
    Detection d[] = {{ClassId::Person, .9f, {.1f, .2f, .3f, .8f}},
                     {ClassId::Person, .95f, {.6f, .2f, .8f, .8f}},
                     {ClassId::Car, .85f, {.1f, .2f, .3f, .8f}}};
    tracker.update(d, 3, 1000000, 2, f);
    assert(f.count == 3);
    std::set<uint32_t> ids;
    for (unsigned i = 0; i < f.count; ++i) {
        ids.insert(f.tracks[i].id);
        assert(!f.tracks[i].confirmed);
    }
    assert(ids.size() == 3);
    uint32_t first = f.tracks[0].id, second = f.tracks[1].id;
    std::swap(d[0], d[1]);
    d[0].box.x1 += .02f;
    d[0].box.x2 += .02f;
    tracker.update(d, 3, 1150000, 2, f);
    assert(f.count == 3);
    assert(f.tracks[0].id == first && f.tracks[1].id == second);
    for (unsigned i = 0; i < f.count; ++i)
        assert(f.tracks[i].confirmed);
    tracker.update(nullptr, 0, 1450000, 2, f);
    assert(f.count == 3);
    tracker.update(d, 3, 1600000, 2, f);
    assert(f.tracks[0].id == first);
    tracker.update(nullptr, 0, 4000000, 2, f);
    assert(f.count == 0);
    tracker.update(d, 3, 4100000, 2, f);
    assert(f.count == 3 && f.tracks[0].id != first);
    tracker.reset();
    tracker.update(d, 3, 4200000, 2, f);
    assert(f.tracks[0].id > first);
    ObjectTracker tentative;
    tentative.update(d, 1, 1000000, 2, f);
    tentative.update(nullptr, 0, 1100000, 2, f);
    tentative.update(d, 1, 1200000, 2, f);
    assert(!f.tracks[0].confirmed);
    Detection many[32];
    for (auto &x : many)
        x = {ClassId::Bicycle, .8f, {.1f, .1f, .2f, .5f}};
    tracker.reset();
    tracker.update(many, 32, 5000000, 1, f);
    assert(f.count <= kMaxObjects);
    puts("PASS tracking: multiple instances, class gating, order-independent IDs, occlusion/reappearance, "
         "tentative reset, bounded capacity");
}

void regions() {
    ObstacleAnalyzer a;
    Settings s;
    FrameResult f;
    f.captured_us = 1000000;
    f.count = 4;
    for (unsigned c = 0; c < 4; ++c) {
        auto &t = f.tracks[c];
        t.id = c + 1;
        t.cls = static_cast<ClassId>(c);
        t.box = {.4f, .3f, .6f, .9f};
        t.confirmed = true;
        t.seen_us = f.captured_us;
        t.matched = true;
    }
    assert(a.analyze(f, s) == ClassId::Car);
    for (unsigned i = 0; i < 4; ++i) {
        assert(f.tracks[i].zone == 1);
        assert(f.tracks[i].event == 1);
        assert(f.tracks[i].risk == 3);
    }
    assert(a.analyze(f, s) == ClassId::Car);
    assert(f.tracks[0].event == 0);
    f.tracks[0].box = {.05f, .3f, .20f, .9f};
    a.analyze(f, s);
    assert(f.tracks[0].zone == 0);
    f.tracks[0].box = {.80f, .3f, .95f, .9f};
    a.analyze(f, s);
    assert(f.tracks[0].zone == 2);
    s.priority = {3, 2, 1, 0};
    assert(a.analyze(f, s) == ClassId::Person);
    for (auto &t : f.tracks)
        t.box = {.05f, .1f, .2f, .2f};
    assert(a.analyze(f, s) == ClassId::None);
    assert(f.tracks[0].event == -1);
    f.captured_us += 700000;
    for (auto &t : f.tracks)
        t.box = {.4f, .3f, .6f, .9f};
    assert(a.analyze(f, s) == ClassId::None);
    puts("PASS ROI: left/center/right, entry/exit, confirmation, short hold expiry, risk, configurable class "
         "priority");
}

void filtering() {
    Detection d[] = {{ClassId::Person, .9f, {.1f, .1f, .5f, .9f}},
                     {ClassId::Person, .8f, {.1f, .1f, .5f, .9f}},
                     {ClassId::Bicycle, .85f, {.1f, .1f, .5f, .9f}}};
    Detection out[24];
    size_t n = class_aware_nms(d, 3, out, 24, .5f);
    assert(n == 2 && out[0].cls == ClassId::Person && out[1].cls == ClassId::Bicycle);
    CandidatePool pool;
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 40; ++i)
            pool.add({static_cast<ClassId>(c), i / 40.f, {.1f, .1f, .3f, .3f}});
    assert(pool.compact() == 128);
    for (auto c : pool.counts)
        assert(c == 32);
    assert(mode_limit(Mode::Accuracy) > mode_limit(Mode::Performance));
    WindowStats<128> stats;
    for (int i = 1; i <= 128; ++i)
        stats.add(i);
    auto q = stats.summarize();
    assert(q.mean == 64.5f && q.p50 == 64 && q.p95 == 122 && q.p99 == 127 && q.max == 128);
    stats.add(std::numeric_limits<float>::quiet_NaN());
    assert(stats.summarize().count == 128);
    stats.add(129);
    assert(stats.summarize().mean == 65.5f);
    puts("PASS class-aware NMS/candidate bounds and real sample-window mean/P50/P95/P99/max");
}
void coco_mapping() {
    // Route A: the official YOLO11n COCO model exposes 80 categories; only
    // person/bicycle/car are mapped into the four-class alarm vocabulary. The
    // beep count follows the ClassId (Person=1, Bicycle=3, Car=4); cone is not
    // produced by this model and remains manual-test only.
    assert(coco_to_class(0) == ClassId::Person);
    assert(coco_to_class(1) == ClassId::Bicycle);
    assert(coco_to_class(2) == ClassId::Car);
    for (int unsupported : {3, 5, 7, 9, 79, -1, 80})
        assert(coco_to_class(unsupported) == ClassId::None);
    assert(AlarmPolicy::count(coco_to_class(0)) == 1);
    assert(AlarmPolicy::count(coco_to_class(1)) == 3);
    assert(AlarmPolicy::count(coco_to_class(2)) == 4);
    assert(AlarmPolicy::count(coco_to_class(3)) == 0);
    puts("PASS COCO-80 mapping: person/bicycle/car -> 1/3/4 beeps; unsupported categories ignored, "
         "no relabeling of motorcycle/truck/cone");
}

int main() {
    alarms();
    tracking();
    regions();
    filtering();
    coco_mapping();
}
