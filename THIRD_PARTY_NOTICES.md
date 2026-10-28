# Third-party notices

This repository contains project code plus third-party components, model
artifacts and datasets. The project code is licensed under the MIT license in
[LICENSE](LICENSE). The following components keep their own licenses; check the
linked directories/files for the full texts.

## Frameworks and drivers

| Component | License | Location |
| --- | --- | --- |
| ESP-IDF 5.4.2 | Apache-2.0 | Toolchain dependency, not vendored in this repository |
| ESP-DL 3.3.14 | MIT | `esp32_vision/managed_components/espressif__esp-dl/LICENSE` |
| esp32-camera 2.1.3 | Apache-2.0 | `esp32_vision/managed_components/espressif__esp32-camera/LICENSE` |
| esp_jpeg / esp_new_jpeg | Apache-2.0 | `esp32_vision/managed_components/espressif__esp_jpeg/LICENSE`, `espressif__esp_new_jpeg/LICENSE` |
| dl_fft | Apache-2.0 | `esp32_vision/managed_components/espressif__dl_fft/` |
| cJSON | MIT | Bundled with ESP-IDF |

These directories are normally downloaded by the ESP-IDF component manager and
are not committed; the licenses above apply to the fetched versions.

## Models

- **Person baseline**: Espressif PicoDet pedestrian S8 v1, distributed by
  ESP-DL under MIT terms. License text: `esp32_vision/models/LICENSE.esp-dl`.
- **Route A YOLO11n COCO 320**: the official ESP-DL `coco_detect` artifact.
  ESP-DL's model component marks it MIT. YOLO11 was originally developed by
  Ultralytics; review Ultralytics' terms before redistributing the weights.
- The optional four-class model is produced from user-provided, authorized data
  by the tools in `esp32_vision/tools/`; its license depends on the training
  data and must be documented in its manifest.

## Training data and tooling

- **nuImages** (optional four-class training) is published by Motional under
  non-commercial terms. No nuImages images, annotations or derived weights are
  included in this repository.
- **ESP-Detection / Ultralytics** training code is AGPL-3.0-family software.
  It is used only by the optional training tools and is not linked into the
  shipped firmware.

## Pin reference

- The Freenove camera pin mapping is referenced from Freenove's public board
  examples; see [person_beep/docs/freenove_camera_pins.reference.h](person_beep/docs/freenove_camera_pins.reference.h)
  for the source URL and notes.

If you redistribute firmware images or model files from this repository, keep
the corresponding license notices with them.
