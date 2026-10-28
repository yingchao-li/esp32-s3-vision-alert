# Software architecture, ownership and scheduling

## Tasks

| Task | Core | Priority | Stack bytes | Responsibility |
| --- | --- | --- | --- | --- |
| Camera driver cam_task | 0 | driver default 23 | driver-configured 4096 | DMA/frame production; original configuration kept |
| capture | 0 | 4 | 4096 | Acquire latest frame, validate, bounded queue |
| inference | 1 | 5 | 16384 | Sole owner of model/preprocess/postprocess |
| tracking | 0 | 6 | 8192 | Fixed-capacity matching, ROI, result publication, FB return |
| buzzer | 0 | 12 | 4096 | 5ms state machine, RMT hardware waveform |
| httpd | 0 | 2 | 10240 | JSON, same-frame snapshot, control |
| preview | 0 | 1 | 8192 | Rate-limited software JPEG encoding |
| perf | 0 | 1 | 10240 | 2s statistics, 10s logging, throttled reconnect |

WiFi and the network stack use IDF's own tasks. Tasks are not spread evenly across both cores; inference is concentrated on CPU1 while peripheral/network/cache work is scheduled on CPU0. Tracking runs at a higher priority than the inference worker so the scarce FB is released earlier; the RMT control task is higher still, and the driver/WiFi default priorities are preserved.

## Two buildable profiles

```sh
bash tools/build_profile.sh pipeline  # default build/esp32_vision.bin
bash tools/build_profile.sh dual      # build-dual/esp32_vision.bin
```

The default is `RUNTIME_MODE_SINGLE_CORE`, with the model running on CPU1.
The optional `CONFIG_VISION_DL_DUAL_CORE=y` uses `RUNTIME_MODE_MULTI_CORE`. ESP-DL 3.3.14's `dl/module/src/dl_module_base.cpp` was analyzed: the first split operator creates `dl_mc0/dl_mc1`, pinned to CPU0/CPU1 respectively, each with a 2048B static stack and task priority inherited from the caller (currently 5). A serial dispatch mutex and completion semaphore manage the operator tasks; only one AI task accesses the model. Whether Conv/Pool/Resize/LUT actually split depends on the implementation, so not every layer runs in parallel. CPU0 network/capture work and the worker threads contend, so dual-core is not assumed to be faster. The web UI reports the configuration, and monitoring registers the DL worker stack watermarks.

The dual firmware must also be flashed through the safe application-only flasher:

```sh
source tools/env.sh
python tools/flash_app.py --port /dev/cu.usbmodem5C930528211 --image build-dual/esp32_vision.bin
```

## Raw frame ownership

```text
camera driver -> capture task -> [capture_queue, length 1]
              -> inference task -> [tracking_queue, length 1]
              -> tracking task -> esp_camera_fb_return()
```

- Only the current owner may return an FB. Queue payloads are statically asserted to be trivially copyable; results never contain `std::list`, model tensors or other dangling references.
- When a queue is full the producer tries to pop and return the old element; if the send still fails, the new element is returned. FreeRTOS queue operations decide the single owner; the code does not assume the consumer is not concurrently taking a frame.
- When AI is disabled or the model is not ready, capture produces an optional preview with an empty target result and returns the FB immediately; those frames do not count as AI inferences.
- A configuration epoch increments on every setting change; pending packets from an older configuration are released instead of mixing thresholds/ROI results.
- The model is initialized synchronously in `app_main` and AI/tracking tasks start only after success. On failure the allocated model object is released, AI/tracking tasks are not created, and preview plus HTTP still initialize. After five consecutive camera failures the camera latches off, automatic alerts are disabled, and hardware is not repeatedly restarted.

## Preview and frame synchronization

RGB565 is copied once into a separate PSRAM staging buffer only when the preview budget allows. The normal AI path never copies a full RGB image.
There is one staging buffer plus two fixed 64KB JPEG slots; bounded callback writes check for overflow. The encoder task only writes a slot that is not current and has refs=0. Publishing updates the JPEG, FrameResult and frame ID together.
HTTP uses lease reference counting and releases it in an RAII destructor, including on disconnect or serialization failure. HTTP allows at most 3 sockets with a 3-second send timeout, so network throughput pressure is never pushed onto camera FBs.

`/api/frame` format: 4-byte little-endian metadata length → UTF-8 JSON → JPEG. The client draws the image and boxes from the same response, avoiding boxes overlaid on another frame. Performance mode disables preview; if encoding fails or resources are busy, only the preview is dropped.

## Tracking, risk and limits

24 fixed tracks with global greedy matching per class, IoU plus a gated center distance, constant-velocity prediction and exponential smoothing of velocity/box; no matrix inversion. This is not a full Kalman filter and not appearance ReID.
Confirmation defaults to 2 consecutive frames. IDs are kept for 2 seconds of short occlusion; alert eligibility is kept for only 600ms. Two people crossing, heavy occlusion or fast camera motion can swap IDs. A model configuration change clears track state and never reuses previous IDs.
ROI determines left/center/right from the box bottom center and records entry/exit; risk uses the bottom position, central zone and normalized area change, and never infers real distance.
The four-class ESPDet decoder caps candidates at 32 strongest per class before per-class NMS so pedestrians and bicycles do not suppress each other; at most 24 targets are output.

## Model backends and class mapping

`CONFIG_VISION_MODEL_SOURCE` selects one of three; exactly one real model is embedded at a time. At build time `tools/verify_model.py` verifies EDL2/SHA/tensor contracts and generates `model_spec.hpp`:

- `VISION_MODEL_BASELINE`: PicoDet 224, single-class Person, `PicoPostprocessor` (bbox 32-channel DFL).
- `VISION_MODEL_YOLO11_COCO` (Route A, default): official ESP-DL YOLO11n COCO 320, 2.86MB, `yolo11PostProcessor` (80-channel score, 64-channel DFL box, strides 8/16/32). Only COCO 0/1/2 map to Person/Bicycle/Car; the other 77 categories are dropped, and Traffic Cone has no COCO category, so it remains an extension in progress and is not deployed. The official top-k NMS is class-agnostic, so overlapping targets may suppress each other.
- `VISION_FOUR_CLASS_MODEL`: self-trained ESPDet four-class, 4-channel score/box plus per-class NMS, requiring `models/four_class.json` and a validation report.

The YOLO11 backend keeps parameters in flash (`param_copy=false`) because the 8MB PSRAM must hold the ~6MB activation arena plus camera/preview buffers. Model initialization still completes before WiFi and worker tasks; on failure automatic alerts are disabled but preview is kept. The Route A application partition is `0x6f0000`; the first flash requires an explicit `--approve-layout-change`, and only "grow the application partition in place" is allowed.

## Concurrency and errors

Settings, health and results are copied in short critical sections; percentile sorting happens outside the task mutex. Monitoring copies the handle table before reading stack watermarks, and a generation counter prevents old statistics from overwriting newer state after registration or configuration changes.
The model finishes loading, memory planning, tensor checks and pre/postprocessor initialization synchronously before network and worker tasks start; afterwards only the inference task modifies parameters and runs the model. The buzzer callback only updates a completion flag inside a critical section, and never allocates, logs or waits on a queue in the ISR.
Tasks that fail are not deleted after monitoring registration, avoiding `uxTaskGetStackHighWaterMark()` on a freed TCB.
Automatic alerts require camera/model health, AI enabled and a frame result no older than 2 seconds. Mute/faults may abort; a class change may not interrupt a started sequence. The hardware wave returns to LOW automatically when it expires.

## Timing and stability

Software memory policy: keep a 64KB internal-RAM reserve, lower the large-allocation threshold from 16KB to 4KB, prefer PSRAM for migratable WiFi/LwIP buffers; the model activation arena is in PSRAM (internal arena budget 0). The original fixed 32KB internal arena failed on hardware due to missing SIMD capability; a generic internal largest-block value must not be treated as the SIMD internal largest block. Route A additionally requires model parameters not to be copied into PSRAM (`param_copy=false`; 2.86MB stays in flash) because the official YOLO11n memory planner needs about 6MB of PSRAM. Model initialization and task creation complete synchronously in the main task with an 8KB stack, and inference has its own 16KB stack. These are static resource budgets, not measured peak-memory results.

All timings use `esp_timer_get_time()`; log-printed timings are never presented as inference time. FPS uses completed counts and real monotonic time. Each metric samples its own 128-sample window; different metrics may span different wall-clock intervals.
Preview throttling, disabled AI and application frame drops are never counted as neural-network throughput gains. Driver/sensor loss is not reliably exposed and returns null; CPU usage has no reliable measurement and returns null.
IDF's idle watchdog and 10-second timeout are kept, and tasks yield through queues/notifications/1ms periods rather than masking blocking with an infinite watchdog feed. System logs and the next boot's reset reason support WDT diagnosis; no physical fault-injection or long-duration stress test has been done.
