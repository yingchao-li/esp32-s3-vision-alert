# On-device test checklist

This checklist is **executed by the user**. The session that produced this build only performed metadata reads, host checks/builds and flashing preparation; it did not run camera recognition, buzzer, network/FPS or long-duration hardware tests.
All items below start as **HARDWARE TEST PENDING**.

## 0. First boot / web UI

- Confirm the buzzer 5V supply, common ground, S8050 and GPIO14 wiring are unchanged.
- After RESET, check the serial log: Flash/PSRAM, sensor PID, model capability and error reason.
- Connect to `ESP32-Vision-xxxx` with password `vision-setup`, then open `http://192.168.4.1`.
- The page should show **THREE_CLASS: PERSON/BICYCLE/CAR; TRAFFIC CONE NOT DEPLOYED** (Route A); `model_family` is `yolo11`, `three_class_available=true`, `cone_model_deployed=false`. A class that is not deployed must never be presented as recognized.
- If the model/camera enters an error state, record `last_error`, the full boot log and SRAM/PSRAM; automatic alerts must stay disabled.

## 1. Idle

- Use the "Idle" preset: AI off, muted. Confirm AI FPS eventually reaches 0 while preview and network control still work.
- Enable AI and keep an empty scene; check mute behavior and false-positive rate.
- Verify enable/disable/mute take effect in the web UI; if needed, save the configuration and reboot to confirm persistence.

## 2. Continuous Person

- Use the "Continuous person detection" preset; walk fully into and then out of the frame.
- Check confidence, box, ID and frame timestamp.
- Person should produce 1 beep per sequence; sequence starts are at least 3000ms apart; a brief missed detection must not cut off a started beep or cause an alert storm.
- After leaving, muting or disabling AI, confirm the output is LOW.

## 3. Four buzzer patterns (explicitly non-AI manual tests)

- AI is not required. With the buzzer enabled, click the Person/Cone/Bicycle/Car manual test buttons in turn.
- Verify 1/2/3/4 beeps, 150ms beep length and 150ms gaps.
- Wait for cooldown; check that busy/muted requests are rejected or delayed and never queue unboundedly.
- Verify manual tests report `test_mode=true` and do not add AI detections/inferences.
- Changing class/priority during playback must not splice sounds; mute must stop it immediately.

## 3b. Three-class automatic detection (new in Route A)

- Bring a real person, bicycle and car into the ROI one at a time: Person=1 beep, Bicycle=3 beeps, Car=4 beeps; verify the web UI `class_name` matches the box.
- When several targets are present, check the default priority Car>Bicycle>Cone>Person. The official NMS is class-agnostic, so overlapping targets may keep only one; record actual behavior.
- Motorcycles/trucks/buses must **not** be reported as Bicycle/Car; a traffic cone must **not** trigger automatically (manual test only).
- The official S3 reference is about 1 FPS with 2–3 seconds end-to-end; record real `ai_fps`, `inference_ms` and `psram_free`. Missing a fast-moving target is a known limitation.
- Record COCO-model false positives (distant small objects, people in screens/posters) and adjust confidence if needed (default 0.35, editable in the web UI).

## 4. Multiple targets and tracking

- Now test a mix of people/bicycles/cars: observe ID stability, short occlusion, leave-and-re-enter, and left/center/right transitions.
- Check the confirmation threshold; a brief false positive must not immediately become a confirmed risk target.
- After more than 2 seconds of absence a new ID is allowed; record ID swaps during heavy occlusion/crossing. Do not assume appearance ReID.
- **Traffic Cone AI testing is still BLOCKED**: the COCO model has no Traffic Cone category. It needs a trained four-class model plus a rebuild/reflash. Manual beeps or web labels cannot substitute for the model.
- Once the four-class model is installed, test real traffic cones and check Car>Bicycle>Cone>Person when all appear together.

## 5. ROI and experimental risk

- Change the top/bottom ROI, left/right boundaries and `danger_y`; place targets left/center/right and outside the ROI.
- Check entry/exit events, consecutive confirmation, risk level and area/motion trends.
- Do not interpret normalized boxes, area growth or risk values as meters or reliable true distance.

## 6. Real FPS and dual-core comparison

- Start from the same camera position/lighting/targets and wait for the statistics window to form after each setting change.
- Compare Accuracy / Balanced / Performance. In Route A all three share the same 320×320 YOLO11n model; only postprocess target capacity and preview budget differ (YOLO11 NMS is fixed at 0.70).
- Export mean/P50/P95/P99/max for capture/pre/inference/post/tracking/end-to-end, plus AI FPS, application drops, RAM and stack.
- Compare the default pipeline firmware with `build-dual/esp32_vision.bin` under otherwise identical conditions.
- Do not use browser refresh rate as AI FPS. Fields being null when no reliable CPU/driver-loss measurement exists is expected.

## 7. WiFi + preview load

- Use the "Camera + WiFi" preset; check image, boxes and frame ID synchronization.
- Open multiple browsers, a slow network, disconnect and reconnect the router; observe preview drops and memory, and confirm AI/buzzer do not depend on the network.
- Preview must be disabled in Performance mode to avoid encoding load; it should recover after leaving that mode.
- If the page cannot connect, provide the serial log; do not erase NVS as a first step.

## 8. High-frequency and long-run logging

- The "High-frequency manual buzzer" preset sets AI off, 100ms beep/gap and 1000ms sequence start interval; you still submit each test manually.
- The "Long-run logging" preset does not claim a duration for you; decide how long to run and watch temperature, power, false positives, memory decline and resets.
- Recorder example:

```sh
python3 tools/collect_metrics.py --url http://192.168.4.1 --seconds 300 --output test-5min.jsonl
# Adjust --seconds if you decide to run longer; none of these commands was executed by the assistant.
```

When reporting back, include firmware SHA/version, mode, scheduling profile, embedded model, scene, real duration, serial log, JSONL/CSV and reproduction steps. A successful build or flash does not mean any of the above passed.

## Recovery

```sh
bash tools/restore_stable.sh /dev/cu.usbmodem5C930528211
```

It also verifies the current partition and restores only the stable application, without changing the partition table. In Route A the partition table is already the `0x6f0000` layout, and the stable application still runs in that larger partition. Do not use erase-flash as the first troubleshooting step.
