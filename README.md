# ESP32-S3 Vision Alert

On-device vision alert firmware for the **Freenove ESP32-S3 WROOM** (8MB flash,
8MB PSRAM, GC0308 camera). It runs real INT8 neural-network inference on the
board — no cloud or PC inference — and drives a graded buzzer plus a WiFi
dashboard.

**Current default (Route A):** the official ESP-DL **YOLO11n COCO 320 INT8**
model, with real **Person / Bicycle / Car** detection. **Traffic Cone is an
extension still in progress**: no cone weights are embedded yet. Official S3
reference latency is about 961 ms/frame (~1 FPS), and it has not been measured
on this board.

> This is a research visual-assistance project. It is not a navigation-safety
> product; image boxes and area changes are not distance measurements. All
> on-device behavior remains **hardware test pending** and must be verified by
> the user.

## Features

- Bounded FreeRTOS pipeline: camera → capture → inference → tracking, with
  single-owner frame-buffer handoff and no unbounded queues.
- Real INT8 detection with build-time and boot-time model provenance checks
  (EDL2, size, SHA256, tensor contracts) and explicit failure states.
- Lightweight 24-track multi-target tracking, ROI zones, entry/exit events and
  an experimental risk score (not distance).
- Graded buzzer alerts (1/2/3/4 beeps by class), priority ranking, cooldown,
  mute and an independent manual hardware test.
- WiFi AP+STA dashboard with same-frame JPEG and boxes, latency percentiles
  (P50/P95/P99), FPS, memory/stack health and CSV export. No CDN dependency.
- Guarded application-only flashing: chip/MAC/flash checks, partition-table
  readback, safe layout-upgrade path, stable-firmware restore.
- Host test suite with AddressSanitizer/UBSan plus Python checks for model,
  partition and flash guards.

## Repository layout

| Path | Contents |
| --- | --- |
| `esp32_vision/` | Main ESP-IDF firmware: route A model, tracking, dashboard, training tools |
| `person_beep/` | Original stable single-class Person firmware (no WiFi) |
| `stable_firmware/` | Recoverable stable application, bootloader, partition table and checksums |
| `ESP32_USB_Camera.ino` | Original Arduino buzzer sketch (comments in English, code unchanged) |

## Quick start

```sh
cd esp32_vision
bash tools/test.sh       # host tests: C++ algorithms + Python guards
bash tools/build.sh      # ESP-IDF 5.4.2 build
bash tools/flash.sh /dev/cu.usbmodemXXXX   # application-only flash
bash tools/monitor.sh /dev/cu.usbmodemXXXX
```

The Route A firmware needs a larger application partition (`0x6f0000`). If the
board still has the old layout, the first flash requires an explicit
`--approve-layout-change`; see [Route A notes](esp32_vision/docs/ROUTE_A.md).
Detailed setup, safety model, alert rules and limitations are in
[esp32_vision/README.md](esp32_vision/README.md).

## Documentation

- [Firmware README](esp32_vision/README.md)
- [Architecture and scheduling](esp32_vision/docs/ARCHITECTURE.md)
- [Route A: YOLO11n COCO three-class model](esp32_vision/docs/ROUTE_A.md)
- [Four-class training, quantization and deployment](esp32_vision/docs/TRAINING.md)
- [On-device test checklist](esp32_vision/docs/USER_TESTS.md)
- [Delivery record](esp32_vision/docs/DELIVERY.md)

## Status

- Route A firmware: build PASS (pipeline and dual-core), **not hardware-tested**.
- Traffic Cone: extension in progress; requires authorized four-class data,
  real training and on-device validation.
- No real FPS, accuracy, thermal or long-run stability claim is made here.

## License

Project code is MIT; see [LICENSE](LICENSE). Third-party components, model
artifacts and datasets keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
