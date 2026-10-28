#include "alarm_policy.hpp"
#include <cassert>
#include <limits>
#include <cstdio>

int main() {
    using namespace person_beep;
    AlarmPolicy p;
    assert(!p.can_start({}, 100000));
    assert(!p.can_start({true, false, 100000}, 100000));
    assert(!p.can_start({false, true, 100000}, 100000));
    assert(!p.can_start({true, true, 0}, 100000));
    assert(!p.can_start({true, true, 100001}, 100000));
    assert(!p.can_start({true, true, 100000}, 1600001));
    assert(p.can_start({true, true, 100000}, 100000));
    p.started();
    assert(!p.can_start({true, true, 600000}, 700000));
    // Hardware completion at 700010us, including terminal LOW symbol.
    p.stopped(700010);
    assert(!p.can_start({true, true, 2600000}, 2700009));
    assert(p.can_start({true, true, 2600000}, 2700010));
    assert(!p.can_start({true, false, 2700010}, 2700010));
    p.started();
    p.stopped(2800000);  // Cancelled sequence also needs full cooldown.
    assert(!p.can_start({true, true, 4700000}, 4799999));
    assert(p.can_start({true, true, 4700000}, 4800000));
    assert(accepted_person(0, 0.8f, true));
    assert(!accepted_person(0, 0.799f, true));
    assert(!accepted_person(1, 0.9f, true));
    assert(!accepted_person(0, 0.9f, false));
    assert(!accepted_person(0, std::numeric_limits<float>::quiet_NaN(), true));
    assert(!accepted_person(0, std::numeric_limits<float>::infinity(), true));
    assert(!accepted_person(0, 1.1f, true));
    puts("PASS: fail-closed observations, confidence, freshness, sequence/cooldown/cancel boundaries");
}
