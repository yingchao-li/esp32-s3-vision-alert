# Model files and real capabilities

This directory contains two **real, buildable** INT8 models; `CONFIG_VISION_MODEL_SOURCE` selects exactly one to embed:

## 1. Route A (default): YOLO11n COCO 320 — Person / Bicycle / Car

- File: `coco_detect_yolo11n_320_s8_v1.espdl`, **2,858,512 bytes**, SHA256 `69a2125826430a46dca7edeb9193324da12758be7bcfa34d862ce31f34152986`.
- Source: [ESP-DL model zoo `coco_detect`](https://github.com/espressif/esp-dl/tree/master/models/coco_detect), the artifact shipped with ESP-DL 3.3.14 (MIT, see that directory's LICENSE; upstream Ultralytics YOLO11 terms apply on redistribution).
- Input: INT8 NHWC `[1,320,320,3]`; preprocessing is RGB `/255` (mean 0 / std 255) with letterbox padding 114.
- Output per scale: `scoreN: [1,S/stride,S/stride,80]`, `boxN: [1,S/stride,S/stride,64]` (DFL reg_max=16), strides 8/16/32, decoded by the official ESP-DL `yolo11PostProcessor`.
- Only COCO categories 0/1/2 are mapped to Person/Bicycle/Car; the other 77 categories (including motorcycle/truck) are dropped and never renamed. **Traffic Cone has no COCO category and is still an extension in progress: it is not deployed.**
- Official S3 reference latency: 15.1ms preprocess + 961.5ms model + 15.2ms postprocess ≈ **1 FPS** (not measured on this board).
- Because the model is 2.86MB, the application partition was enlarged from `0x3f0000` to `0x6f0000`; the first flash needs `--approve-layout-change`. See the [Route A notes](../docs/ROUTE_A.md).

## 2. Single-class baseline: PicoDet pedestrian 224 — Person only

- File: `pedestrian_detect_pico_s8_v1.espdl`, **435,328 bytes**.
- [Upstream Espressif ESP-DL v3.3.6 S3 model](https://github.com/espressif/esp-dl/blob/v3.3.6/models/pedestrian_detect/models/s3/pedestrian_detect_pico_s8_v1.espdl)
- SHA256: `3eb5827e12322728fe99ea9c86d71ea38aa5a5b70064abf7d75bc90140b74015`
- Input: INT8 `[1,224,224,3]` NHWC; single class 0=Person; `score0/1/2` with last dim 1, `bbox0/1/2` with last dim 32 (DFL), decoded by `PicoPostprocessor`.
- Smallest image and fastest inference; cannot output bicycle, car or traffic cone.
- MIT license, see `LICENSE.esp-dl`.

## 3. Four-class model (optional, waiting for authorized data)

- Target format: INT8 NHWC `[1,S,S,3]`, S=160/224/320, RGB `/255`, letterbox padding 114.
- Output per scale: `scoreN: [1,S/stride,S/stride,4]`, `boxN: [1,S/stride,S/stride,4]`, strides 8/16/32.
- Class order must be exactly `[Person, Traffic Cone, Bicycle, Car]`; `box` holds four edge regressions, score holds logits, decoded with sigmoid plus per-class NMS.
- Requires real training/quantization reports and SHA256. See the [training notes](../docs/TRAINING.md); installed by `tools/deploy_model.py` and re-verified at build time.
- Enabling `CONFIG_VISION_FOUR_CLASS_MODEL` without the four-class artifact fails the build; no single-class model is silently passed off as four-class.

**Traffic Cone remains an extension in progress. There is no `four_class.espdl`, no invented download link and no placeholder model.** The web UI's `model_status` reports the actual deployed class set. Rebuild after switching models; the flash script never enlarges the partition automatically. Route A's partition growth is a one-time operation that requires explicit approval.
