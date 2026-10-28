#pragma once
#include "alarm_policy.hpp"

// Called first, before allocations, camera init or model loading.
bool buzzer_init();
void buzzer_publish(const person_beep::Observation &observation);
