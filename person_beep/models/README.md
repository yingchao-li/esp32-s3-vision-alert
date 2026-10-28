# Real model provenance

- File: `pedestrian_detect_pico_s8_v1.espdl`, 435,328 bytes.
- Type: PicoDet, INT8, input NHWC `[1,224,224,3]`, single class `0 = person`.
- Source: [Espressif ESP-DL v3.3.6 ESP32-S3 model](https://github.com/espressif/esp-dl/blob/v3.3.6/models/pedestrian_detect/models/s3/pedestrian_detect_pico_s8_v1.espdl).
- [Raw binary download](https://raw.githubusercontent.com/espressif/esp-dl/v3.3.6/models/pedestrian_detect/models/s3/pedestrian_detect_pico_s8_v1.espdl).
- [Official model notes](https://github.com/espressif/esp-dl/blob/v3.3.6/models/pedestrian_detect/README.md).
- Upstream tag commit: `a1b4e53c591fe2ba3e134d0a6b1691ba25879d87`.
- The same-version pedestrian_detect component is `0.3.2`; this project reuses its preprocessing, DFL anchors and Pico postprocess configuration, adds validation and error checks, and embeds a single `.espdl` directly with no multi-model packaging or conversion.
- The model comes from upstream `models/s3`, not the P4 version. License in the same directory: `LICENSE.esp-dl` (upstream MIT).

Verification (from the project root):

```sh
shasum -a 256 -c models/SHA256SUMS
```

SHA256: `3eb5827e12322728fe99ea9c86d71ea38aa5a5b70064abf7d75bc90140b74015`.
This value is verified both at build time and on board boot. Alerts are not enabled if verification fails.

Inference runs entirely on the S3: RGB565BE camera frame → ESP-DL resize/quantize → neural network → three-scale DFL decode/NMS → person box/confidence.
The model recognizes person only and cannot guarantee correctness for all poses, occlusions, distances or lighting; this project defaults to threshold 0.80 (upstream 0.70), NMS 0.50, at most 10 returned boxes.
