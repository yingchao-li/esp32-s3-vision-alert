# Delivery record — 2026-10-28

> **Route A increment (later the same day)**: the default model switched from the single-class PicoDet to the official ESP-DL YOLO11n COCO 320, with real Person/Bicycle/Car support; Traffic Cone is still not deployed (extension in progress). The application partition grew to `0x6f0000`, and the first flash requires `--approve-layout-change`. The content below describes the single-class baseline delivery that preceded Route A; for current model/partition/performance status see [ROUTE_A.md](ROUTE_A.md).

**The system code is implemented, both scheduling profiles BUILD PASS, and the default profile FLASH PASS. The real four-class model remains PARTIAL/BLOCKED: the user confirmed there is no data or weight, so the system and training tools were delivered first. Missing classes were never packaged as usable recognition.**

## Results of this Model=false fix

**IMPLEMENTED · BUILD PASS (both profiles) · FLASH PASS (pipeline) · model boot initialization PASS. Image content, real-person detection, alerting, network functions, FPS and long-run behavior remain HARDWARE TEST PENDING for the user.**

The actual error chain in the pre-fix boot log was:

```text
esp-dl: Input cap=0x804 can not callocate with MALLOC_CAP_SIMD
ModelContext: Failed to alloc 32.00KB internal RAM, largest available internal RAM block size 62.00KB
MemoryManagerGreedy: root_alloc failed
```

The root cause was that the fixed 32KB activation arena requires contiguous `INTERNAL | 8BIT | SIMD` memory, while the original preflight only checked total generic internal free memory. ESP-DL 3.3.14's aligned allocator adds `MALLOC_CAP_SIMD`; IDF 5.4.2's 64KB DMA-reserved pool does not carry that capability flag, so a generic 62KB internal largest block does not prove SIMD memory is sufficient. The new log shows a generic internal largest block of 63,488B and a SIMD internal largest block of 21,504B after loading the graph/parameters. It was not a missing model partition or a wrong model file.

The supported `max_internal_size=0` is now used (activation arena in PSRAM, parameters copied to PSRAM), keeping SIMD-optimized operators. The model is initialized synchronously before WiFi, preview and pipeline worker tasks start, removing initialization resource races/early access. On failure the exact stage is preserved, AI/tracking consumers are not started, capture goes straight to an empty-target preview and returns the FB, automatic alerts are disabled, and frames are not continuously pushed into a consumer-less queue. GPIO14, the original camera pins, the partition table and NVS were not modified.

Diagnostics were added to a build-directory copy of the pinned ESP-DL source: it prints the real `load` return code and the memory planner's boolean result, and does not expose tensors without storage when allocation fails. The original `managed_components` is not modified; a source SHA mismatch rejects the patch. Two corresponding host tests pass. The memory planner allocator returns bool/pointer, not a native `esp_err_t`; no error code was invented for it.

After the fix only the following boot initialization results were read before closing the serial port; no preview image was captured, no buzzer test was triggered and no FPS was collected:

```text
Detected GC0308 camera
MODEL: init/verify: SHA256=3eb5827e12322728fe99ea9c86d71ea38aa5a5b70064abf7d75bc90140b74015 PASS
dl::Model: init/load returned ESP_OK (0x0)
dl::Model: init/arena allocated=true; internal budget=0 bytes
MODEL: init/graph: inputs=1 outputs=6
MODEL: Person detector loaded
```

The input is INT8 `[1,224,224,3]`; all three scales' score/bbox output shapes and non-empty data storage were checked. Raw logs: [before fix](logs/model-init-before-fix.log), [after fix](logs/model-init-after-fix.log). The real model file `pedestrian_detect_pico_s8_v1.espdl` is 435,328B and lives in application rodata; in the ELF it occupies `0x3c1ad9d0..0x3c217e50` and is byte-identical to the original model file, as proven in [model deployment proof](logs/model-deployment-proof.json). No separate `pedestrian_det` partition or SD card is used; the original v3.3.6 model has the same SHA as the official v3.3.14 file of the same name.

The real sensor is **GC0308 / PID 0x009b**, which has no native JPEG. RGB565, 320×240, 153,600B per FB, two PSRAM FBs, GRAB_LATEST, 10MHz XCLK are kept, and JPEG is software-encoded with `fmt2jpg_cb`. The API adds sensor name, capture format, FB configuration and a count of frames that passed format/timestamp checks; successful initialization does not mean the image content is correct. Performance impact has not been measured.

User retest: connect to the device AP, open `http://192.168.4.1` and force refresh; Balanced mode with preview enabled. First disable AI and apply to RAM to check the real image and increasing valid frame count, then enable AI to check Model=true, Person box/confidence/timing, and run empty→person→empty. The current four-class interaction convention is still **Person=1 beep, Cone=2 beeps, Bicycle=3 beeps, Car=4 beeps**; to check Beep-Beep separately, click the Cone·2-beep manual test, which does not mean traffic-cone recognition. The buzzer 5V and common ground must follow the verified wiring; defaults are 150ms beep / 150ms gap / 3000ms sequence start interval, and a Person box must be confirmed consecutively inside the ROI. Only the real Person weight is currently available.

## Feature status

| Feature | Code / build / flash status | On-device status / limits |
| --- | --- | --- |
| Real INT8 Person detection | IMPLEMENTED · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING (new firmware/engine) |
| Real Traffic Cone/Bicycle/Car weights | PARTIAL / BLOCKED | No data, no trained weights; these three cannot be recognized automatically |
| Four-class enum/labels/model validation/ESPDet decode | IMPLEMENTED · backend C++ BUILD PASS | No four-class-weight firmware yet; four-class target tests blocked |
| Four-class training/export/quantization/deploy tools | IMPLEMENTED; dependencies and imports PASS | End-to-end training/quantization PARTIAL, not run; needs authorized data |
| Three modes and latency/FPS/memory monitoring | IMPLEMENTED · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING, no real FPS numbers |
| FreeRTOS queues/frame ownership/pipeline | IMPLEMENTED · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING |
| ESP-DL internal dual-core profile | IMPLEMENTED · BUILD PASS | Not flashed; HARDWARE TEST PENDING |
| Multi-target tracking/short occlusion/constant-velocity smoothing | IMPLEMENTED · UNIT TESTS PASS · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING; only the Person model can be tested today |
| ROI/three zones/confirmation/experimental risk | IMPLEMENTED · UNIT TESTS PASS · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING; no true distance |
| 1/2/3/4 beeps/priority/cooldown/mute/independent test | IMPLEMENTED · host waveform logic PASS · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING; automatic three-class alerts limited by missing model |
| WiFi AP+STA / dashboard / same-frame boxes | IMPLEMENTED · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING, device web UI not accessed |
| Uptime/stack/WDT reason/errors/CSV/log tools | IMPLEMENTED · BUILD PASS · FLASH PASS | HARDWARE TEST PENDING; no long-run stability report |

## Actual build results

Toolchain: ESP-IDF **5.4.2**, ESP-DL **3.3.14**, esp32-camera **2.1.3**, Xtensa GCC **14.2.0 / esp-14.2.0_20241119**.
Dependencies are pinned in `dependencies.lock`. Both final build logs completed successfully with no application/ESP-DL compile warnings/errors; CMake's old-minimum-version warning for mbedtls is a third-party build notice.

| Firmware | bytes | Original app partition | Headroom | Flash |
| --- | ---: | ---: | ---: | --- |
| Pipeline | **2,388,064** | 4,128,768 (`0x3f0000`) | **1,740,704 / 42.2%** | **FLASH PASS** |
| ESP-DL Dual | **2,388,160** | same | **1,740,608 / 42.2%** | NOT FLASHED |

Pipeline SHA256: `729ea7b2fd5190ec7f993c4d492a882bdd92059ef50b3b3a3ce38be8e4393c70`.
Dual SHA256: `f31c77a5941556ae9af98bc81a39ff9c6f12a2bd244b5c0dd216992ec976072a`.

Build artifacts were copied to `firmware/`, checksums in `firmware/SHA256SUMS`. Raw logs are `logs/build-model-fix.log` and `logs/build-dual-model-fix.log`; machine-readable summary is `logs/build-report.json`.
Static image size and linked-RAM budget are not runtime peak memory. Migratable WiFi/LwIP buffers preferring PSRAM, the 4KB large-allocation threshold, 64KB internal RAM reserve and the PSRAM activation arena (internal budget 0) all need your runtime monitoring.

## Actual flash result

- Connected and verified ESP32-S3 rev v0.2, MAC **44:b1:76:c7:ed:58**, Flash **8MB**, eFuse reports embedded PSRAM **8MB**.
- Actual port **/dev/cu.usbmodem5C930528211**, esptool **4.12.0**, 115200 baud.
- Read back the real partition table at 0x8000 and matched it with the build reference; the application is still at **0x10000**, size **0x3f0000**.
- Wrote the application only; esptool returned success and reported **Hash of data verified**.
- Did not write the bootloader/partition table, did not call erase-flash, did not erase NVS/data partitions, and did not enlarge any partition without authorization.
- Details in `logs/flash-model-fix.log` and `logs/flash-result.json`. The last flashed image matches the SHA256 of `firmware/esp32_vision-pipeline.bin`.

**FLASH PASS does not mean the features pass.** This round collected no camera recognition, buzzer sound, web connectivity, real FPS, temperature or long-run hardware data.

## Host checks performed

- Four C++ algorithm groups: alert waveform/time gates/error states, tracking and occlusion lifecycle, ROI/priority, per-class NMS/candidate capacity/percentiles.
- **AddressSanitizer + UndefinedBehaviorSanitizer** enabled, no diagnostics.
- **12 Python unittests PASS**: class mapping, box conversion, JSON fixtures for the converter only, real model validation, rejection of corrupted/missing models, chip/partition/image-size guards, pinned ESP-DL diagnostic patch and allocation-failure guard.
- Clang static analysis covered the pure tracking/ROI/algorithm test files with no diagnostics; FreeRTOS/cache lifetimes additionally received static code review and a full cross-compile, without claiming host verification of hardware concurrency.
- `node --check` front-end JS syntax, Python `py_compile`, Bash syntax and VS Code JSON checks passed.
- Training environment `pip check`, Train/Export/ESP-PPQ/TorchExecutor imports passed; MPS is available. No fake model was generated from untrained weights.

## Preservation and recovery

The original stable source is unchanged, Git tag `stable-person-beep`, development branch `feature/four-class-vision`.
The recoverable original firmware is in the repository root's `stable_firmware/`, with SHA256. `tools/restore_stable.sh` also verifies partition/MAC and restores only the stable application.

## Your next steps

Run the on-device acceptance in [USER_TESTS.md](USER_TESTS.md); start with Person, the four independent buzzer patterns, tracking/ROI, the web UI and statistics. Real three-class recognition requires authorized data and real training artifacts first, per [TRAINING.md](TRAINING.md).
First compare the two scheduling profiles and three modes for real P95/P99/FPS/stack/memory, then decide whether to adjust the JPEG budget, internal arena or model size. Fix bottlenecks seen in real logs; do not use the model's official reference FPS as this board's data.

## Current technical limitations

- Missing weights for three classes is a known feature gap, not something threshold tuning can fix.
- The current modes share a 224-input model; Performance mode mainly reduces postprocess capacity and preview load and does not claim the network itself got smaller or that frames are skipped.
- CPU usage and sensor/driver-internal frame drops have no reliable measurement and return null; application/preview drops are counted separately.
- Preview is capped at 2fps; camera exposure, bandwidth, wireless traffic, PSRAM contention, temperature and other physical factors are unverified.
- 24 lightweight tracks; long occlusion/crossing may swap IDs. Not ReID, not a reliable distance estimate and not a navigation-safety product.
- No 1-hour, 8-hour or 24-hour validation, and no claim of real FPS or four-class accuracy.
