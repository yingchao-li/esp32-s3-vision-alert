# ESP32 Vision — Freenove ESP32-S3

**Current default firmware (Route A): the official ESP-DL YOLO11n COCO 320 INT8 model, with real support for Person / Bicycle / Car. Traffic Cone is an extension still in progress: no cone weights are embedded yet, and the web UI keeps reporting it as not deployed.** This is the fastest path to three classes using an official off-the-shelf model. The model file, SHA256 and class mapping live under `models/`; design and acceptance notes are in [Route A notes](docs/ROUTE_A.md).

The three-class detection comes from official COCO weights, not from training performed by this project. Official S3 reference latency is about 961 ms/frame (~1 FPS); it has not been measured on this board. The web UI and JSON report `THREE_CLASS: PERSON/BICYCLE/CAR; TRAFFIC CONE NOT DEPLOYED`. Manual four-class buzzer tests do not create AI targets and do not count toward detection FPS. Build/flash status is in the [delivery record](docs/DELIVERY.md); every on-device behavior remains **HARDWARE TEST PENDING** and must be run by you.

## Project, stable version and hardware

- Git development branch: `feature/four-class-vision`; stable tag: `stable-person-beep`.
- The original `../person_beep` firmware behavior and `../ESP32_USB_Camera.ino` are unchanged; only the sketch's comments were translated to English.
- `../stable_firmware` keeps the recoverable stable application, bootloader, partition table and SHA256.
- esptool metadata read from this device: ESP32-S3 rev v0.2, **8MB Flash, 8MB embedded PSRAM**, target MAC `44:b1:76:c7:ed:58`, serial port `/dev/cu.usbmodem5C930528211`.
- Freenove camera GPIOs are kept as-is: XCLK15, PCLK13, SDA4, SCL5, VSYNC6, HREF7, D0..D7=11/9/8/10/12/18/17/16, PWDN/RESET=-1.
- The previous stable build read the sensor as **GC0308 / 0x009B**, not OV3660. Camera functionality was not re-tested for you in this round. Boot reads the PID automatically, and the known OV3660 configuration is kept in code.
- Buzzer is fixed to **GPIO14, HIGH = beep, LOW = silent**, using the 5V TMB12A05, 1kΩ resistor, S8050 transistor and common ground. Keep 5V connected.
- No overclocking: 240MHz CPU, 80MHz OPI PSRAM, DIO 80MHz Flash, 10MHz XCLK, RGB565 QVGA, two camera frame buffers.

## The Model=false fix

On-device boot logs pinpointed a 32KB allocation failure for `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_SIMD`. The fix uses a PSRAM activation arena and loads the model synchronously before WiFi and worker tasks are created, printing the native load return value, memory-planner boolean result, SHA256, memory capabilities and tensor information at each stage. If the model fails, the independent software JPEG preview still works, automatic alerts are disabled, and frames are no longer pushed into an AI queue with no consumer.

The Route A YOLO11n model has 2.86MB of parameters; they stay in flash at build time with `param_copy=false` because the 8MB PSRAM must also hold the ~6MB activation arena, two camera frame buffers, preview staging and WiFi buffers. Boot logs print SRAM/PSRAM headroom at each step. If the activation arena fails to allocate, firmware reports `MODEL NOT READY` and disables alerts instead of silently degrading; this is the main memory risk to verify on this board.

The actual sensor is **GC0308 / 0x009b**, which has no native JPEG; the existing RGB565 320×240, two PSRAM frame buffers and software JPEG path are kept. `camera_ok` does not prove image content is correct; JSON adds `camera_valid_frames` (cumulative frames that passed format/length/timestamp checks) and the encoding method.

## Editing, building and safe flashing

Open the whole `esp32_vision` folder in VS Code. **⌘⇧B** builds; Terminal → Run Task provides Build, dual-core profile build, APP ONLY flash, serial monitor, host tests and stable restore.
Actual toolchain: **ESP-IDF 5.4.2 / ESP-DL 3.3.14 / esp32-camera 2.1.3**, with all dependencies pinned in `dependencies.lock`. This is a native C++ ESP-IDF project.

```sh
cd esp32_vision
bash tools/test.sh
bash tools/build.sh
bash tools/flash.sh /dev/cu.usbmodem5C930528211
# Run the serial monitor yourself; exit with Ctrl+]
bash tools/monitor.sh /dev/cu.usbmodem5C930528211
```

**Use this project's `tools/flash.sh`; do not treat the generic `idf.py flash` from build logs as the recommended flashing flow.**
The script verifies chip/MAC/Flash size, reads back and compares the real partition table, then writes only the **application at 0x10000**. It never writes the bootloader, never calls erase-flash and never erases NVS. Flash page erases happen only in sectors covered by the application image.
Route A enlarged the application partition from `0x3f0000` to **`0x6f0000`** (nvs/phy/app offsets unchanged, leaving 1MB of the 8MB flash). If the device still has the old partition table, the script refuses; after reviewing `partitions.csv`, add `--approve-layout-change` to write the **0x8000 partition table once** (a single sector, not touching the NVS at 0x9000) and then the application. Only the "grow the application partition in place" layout change is allowed. Higher baud rates previously caused corruption on this serial link, so 115200 is used.

Back to the stable version:

```sh
bash tools/restore_stable.sh /dev/cu.usbmodem5C930528211
```

First-time IDF setup on another Mac: `brew install python@3.13 cmake`, then `bash tools/setup_idf.sh`. Training uses a separate Python 3.10 environment and does not pollute the IDF Python environment.

## Current model and class mapping

| ID | Target | Weight in the Route A firmware | Beeps per sequence |
| --- | --- | --- | --- |
| 0 | Person | **Yes: YOLO11n COCO 320 INT8 (COCO 0)** | 1 beep |
| 1 | Traffic Cone | **Not deployed (no such COCO category)** | Manual test only, 2 beeps |
| 2 | Bicycle | **Yes: YOLO11n COCO 320 INT8 (COCO 1)** | 3 beeps |
| 3 | Car | **Yes: YOLO11n COCO 320 INT8 (COCO 2)** | 4 beeps |

**Traffic Cone is an extension currently in progress.** The detection/training/deployment path for it is implemented (`VISION_FOUR_CLASS_MODEL`, the nuImages converter and the ESPDet training/quantization/deploy tools), but it requires authorized four-class data, real training and on-device validation. Until then, automatic cone alerts are unavailable; only the manual 2-beep hardware test exists.

The model itself, its size, SHA256, class mapping and preprocessing contract are in `models/yolo11n_coco.json`; both build and boot verify them. The other 77 COCO categories (including motorcycle/truck) are dropped and are never renamed to Bicycle/Car. The YOLO11 backend uses the official `yolo11PostProcessor` (DFL box with 64 channels, score with 80 channels, official top-k NMS); that NMS is **class-agnostic**, so overlapping person/car targets may suppress each other.
[Model training and data notes](docs/TRAINING.md) provide the four-class training, ONNX checks, quantization, validation and deployment commands. There is no `four_class.espdl` yet, and enabling the four-class build fails explicitly. `CONFIG_VISION_MODEL_SOURCE` switches between the single-class PicoDet, YOLO11 COCO and four-class ESPDet backends; exactly one real model is embedded at a time.

## Alert rules

- Default HIGH **150ms** per beep, LOW **150ms** between beeps, and **at least 3000ms between sequence starts**.
- Car > Bicycle > Cone > Person; the web UI lets you edit the priority ranking (four distinct values 0–3). Only one mode is active at a time.
- The RMT peripheral plays the whole waveform, scheduled by a dedicated 5ms control task. Class changes or brief missed detections do not interrupt a started sequence.
- Muting, disabling AI, model/camera faults, stale results or a TX timeout can abort an automatic alert; the hardware end level is always LOW.
- Consecutive-frame confirmation defaults to 2 frames; a confirmed target that briefly disappears keeps alert eligibility for at most 600ms to avoid jitter. Track IDs are kept for at most 2 seconds, after which a reappearing target gets a new ID.
- Setting ranges are bounded; the sequence interval must also exceed the longest four-beep sequence plus 150ms to prevent parameter-induced continuous alerts. The manual test queue has length 1, is subject to mute and cooldown, and never counts as AI data.

## Modes and performance monitoring

All three modes run the real model; FPS is never inflated by skipping the network. The three Route A modes share the same **320×320 YOLO11n COCO model**, and the camera is always 320×240.

| Mode | Postprocess threshold / max targets | JPEG preview | Trade-off |
| --- | --- | --- | --- |
| Accuracy | YOLO11 fixed NMS 0.70 / 24 | ≤2fps, quality 75 | More dense targets and a sharper preview |
| Balanced | Same / 16 | ≤1fps, quality 60 | Default resource compromise |
| Performance | Same / 8 | Disabled | Less output and preview encoding work |

The single-class PicoDet and four-class ESPDet backends still use per-class NMS at 0.60/0.50/0.40. Modes have not been compared on hardware; no specific FPS or accuracy gain is promised. The official S3 reference for YOLO11n 320 is about 961ms (~1 FPS), with end-to-end alert latency of about 2–3 seconds; trust `/api/status` on this board for real numbers. The four-class training tools can export 160/224/320 inputs, but a firmware build embeds exactly one validated model and the fixed input size cannot be changed from the web UI.

Capture / Preprocess / Inference / Postprocess / Tracking / End-to-End / JPEG encoding latencies are monitored, reporting mean, P50, P95, P99 and Max over the latest **128 valid samples**. Changing the configuration clears the latency window so different modes are not mixed.
AI FPS is **completed neural-network inferences divided by real elapsed time**, updated every 2 seconds; preview FPS is not AI FPS. End-to-End runs from the sensor frame timestamp to tracking/risk completion.
Application queue and stale-frame drops are counted separately from preview resource drops; when driver-internal drops cannot be measured reliably the value is null, and no total sensor drop count is invented. CPU usage is also null.
The page also reports SRAM/PSRAM, minimum free SRAM, largest contiguous PSRAM block, task stack watermarks, uptime, errors, reset reason, and whether the previous boot was caused by the watchdog. IDF's own watchdog logs are kept.

## WiFi dashboard

1. After power-up, connect a phone/computer to the **`ESP32-Vision-xxxx`** AP; the initial WPA2 password is **`vision-setup`**.
2. Open **http://192.168.4.1**. The page has no CDN or external framework dependency, so losing internet does not affect on-board AI.
3. You can enter the SSID/password of the same router on the page. The AP remains available and the page shows the STA IP. Credentials are written to the `vision` namespace only when "save" is checked.
4. View the same-frame JPEG/boxes, class, ID, confidence, risk, performance and buzzer status; configure AI, mute, threshold, mode, ROI and buzzer parameters.
5. The **four-class buzzer buttons are manual hardware tests only** and do not prove recognition of any missing class.

Preview is capped at 2fps, with exactly two 64KB JPEG slots and one 153600B RGB staging buffer; HTTP reference counting protects slots being read. Slow clients cause preview drops, never blocking AI or holding the camera's raw FB. Metadata and JPEG are delivered in one response packet with the same frame ID.
Reconnects are throttled to 15 seconds and automatic recognition never waits for the network. If NVS initialization fails, RAM configuration is used and no automatic erase repair is performed. The password can be changed with `idf.py menuconfig`; use it on a trusted lab network.

## User tests and logs

The [on-device checklist](docs/USER_TESTS.md) covers idle, real person, multiple targets, WiFi, maximum throughput, four buzzer patterns and long-run steps. **These tests were not executed for you.**
```sh
# Start it yourself; this tool only records status and never changes settings or starts a stress load
python3 tools/collect_metrics.py --url http://192.168.4.1 --seconds 300 --output metrics.jsonl
```

The page offers `/api/performance.csv` and `/api/status` exports. ROI risk is based on normalized image position, area change and motion trend; it is **not meters or true distance**. This is a research visual-assistance project and provides no blind-navigation safety guarantee.

See [task/cache/scheduling notes](docs/ARCHITECTURE.md) for more design detail. The code has no unbounded queues, no runtime server inference and no fabricated detections. Long-term reliability, temperature, real FPS, accuracy and network behavior still need your verification.
