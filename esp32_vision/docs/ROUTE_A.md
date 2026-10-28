# Route A: getting Person/Bicycle/Car quickly with the official ESP-DL YOLO11n COCO model

**Status: code, model and builds are in place; on-device recognition, buzzer, FPS and peak memory remain `HARDWARE TEST PENDING` and must be run by the user.**

## Goal and boundaries

- Goal: embed the official ESP-DL model-zoo YOLO11n COCO 320 INT8 artifact without training or self-quantization, gaining **real Person / Bicycle / Car** detection.
- Traffic Cone: COCO-80 has no such category, so this route **does not provide** cone detection. The manual 2-beep test remains. **Traffic Cone is an extension currently in progress**; automatic cone alerts will only become possible after a self-trained four-class model is provided (see [TRAINING.md](TRAINING.md)).
- No class renaming: motorcycle, truck and other categories are never passed off as Car; the other 77 COCO categories are dropped in firmware.

## Model and evidence chain

| Item | Value |
| --- | --- |
| File | `models/coco_detect_yolo11n_320_s8_v1.espdl` |
| Size | 2,858,512 bytes |
| SHA256 | `69a2125826430a46dca7edeb9193324da12758be7bcfa34d862ce31f34152986` |
| Source | [ESP-DL `models/coco_detect`](https://github.com/espressif/esp-dl/tree/master/models/coco_detect) (`models/s3/`) |
| Manifest | `models/yolo11n_coco.json` (enforced at build time) |
| License | MIT as marked by the ESP-DL repository; confirm upstream Ultralytics YOLO11 terms before redistribution |

At build time `tools/verify_model.py --yolo11` verifies the EDL2 magic, size/SHA256, target=esp32s3, exact COCO-80 order, deployed class mapping (Person=0/Bicycle=1/Car=2), preprocessing contract (mean 0 / std 255 / letterbox 114), and source/license fields. Any mismatch is a fatal CMake error; nothing is silently used.

At boot the firmware also verifies rodata size/16-byte alignment/SHA256, the `INT8 [1,320,320,3]` input, and the three-scale `score[1,40/20/10,40/20/10,80]` and `box[1,40/20/10,40/20/10,64]` outputs. Any mismatch means `MODEL NOT READY` with automatic alerts disabled.

## Class mapping and beeps

| COCO index | COCO name | Firmware ClassId | Beeps |
| --- | --- | --- | --- |
| 0 | person | Person | 1 |
| 1 | bicycle | Bicycle | 3 |
| 2 | car | Car | 4 |
| other 77 | — | dropped | none |

Traffic Cone (ClassId 1) has no COCO source, so the web UI shows `THREE_CLASS: PERSON/BICYCLE/CAR; TRAFFIC CONE NOT DEPLOYED`, `deployed_classes` has three entries, and `cone_model_deployed=false`.

## Performance expectations (official data, not measured on this board)

| Stage | Official ESP32-S3 reference |
| --- | --- |
| Preprocess | 15.1 ms |
| Model inference | 961.5 ms |
| Postprocess | 15.2 ms |
| Total | ≈ 992 ms ≈ **1 FPS** |

With the default `confirm_frames=2`, a person entering the frame produces a beep after about 2–3 seconds; a fast passer-by may be sampled only once and missed. This is the known cost of Route A in exchange for zero training cost. For 5–10 FPS, train or self-quantize a smaller model (160 input or ESPDet).

## Partition change (one time, explicit approval required)

- Application partition: starts at `0x10000`, grown from `0x3f0000` to **`0x6f0000`** (app ends at `0x700000`, leaving 1MB of the 8MB flash).
- `nvs` (`0x9000`) and `phy_init` (`0xf000`) offsets/sizes are unchanged, and the application start address is unchanged.
- On the first flash of the new layout, `tools/flash_app.py` detects that the device partition table differs from the build and **refuses**. After reviewing `partitions.csv`, add the flag explicitly:

```sh
source tools/env.sh
bash tools/build.sh
bash tools/flash.sh /dev/cu.usbmodem5C930528211 --approve-layout-change
```

The script allows only "grow the application partition in place": every other partition offset/size must match, the new app must stay within 8MB, chip/MAC/flash capacity are verified before writing, and the partition table is read back and compared after writing. **It never writes the bootloader, calls erase-flash or erases NVS.** Later application-only flashes do not need the flag.

## Memory risk (the first thing to confirm on hardware)

- Model parameters (2.86MB) stay in flash (`param_copy=false`).
- ESP-DL's official notes say the YOLO11n memory planner needs about **6MB PSRAM**; this board has 8MB and must also hold two camera FBs (~300KB), preview staging/JPEG slots (~280KB) and WiFi/LwIP.
- Model initialization runs synchronously before the preview task and WiFi; the log prints SRAM/PSRAM headroom after `before constructor`, `after constructor` and `ready`. If the arena allocation fails, firmware reports `MODEL NOT READY` and disables automatic alerts instead of silently degrading.
- If PSRAM OOM appears on hardware, possible mitigations are: fewer camera FBs (`fb_count=1`/lower resolution), removing the internal reserve, or switching to a smaller model.

## On-device acceptance checklist (user)

1. Boot log: `MODEL family=yolo11 ... PASS`, input `[1,320,320,3]`, six output shapes, `Detector loaded`; `/api/status` shows `model_family=yolo11`, `three_class_available=true`, `model_status=THREE_CLASS...`.
2. In the web preview: person → 1 beep; bicycle → 3 beeps; car → 4 beeps; traffic cone → silent (manual test still 2 beeps).
3. Record `ai_fps`, `inference_ms` mean/P95, `psram_free`/`psram_largest` and `stack_free_bytes`; confirm they are in the same order of magnitude as the official 1 FPS.
4. Run continuously for at least 1 hour and watch `errors`, reset reason, watchdog resets and whether PSRAM keeps declining.
5. Check false positives: the COCO model ignores motorcycles/trucks, but may still misclassify distant objects as person/car; record scene and threshold.
6. Regression: rebuild with `VISION_MODEL_BASELINE` and verify single-class Person still works; `tools/restore_stable.sh` must still restore the stable application (the partition table is the new layout, and the stable app still boots).

## Rollback

- Application rollback: `bash tools/restore_stable.sh /dev/cu.usbmodem5C930528211` (writes only the application; the stable build does not depend on the larger partition).
- Model switch: `idf.py menuconfig` → Vision system → Embedded detection model → choose baseline / four-class; rebuild and flash.
- Once the partition table has grown it does not need to be reverted (old firmware works in the larger app partition); the original 8MB layout is recorded in `stable_firmware/partition-table.bin`.

## Known limitations

- 1 FPS and 2–3 seconds of alert latency; fast targets may be missed.
- No Traffic Cone; COCO truck/motorcycle are ignored rather than merged into Car/Bicycle.
- The official postprocess NMS is class-agnostic, so overlapping person/car targets may suppress each other. If needed, replace the filtering before the YOLO11 branch's result filter with per-class NMS in `ModelManager`.
- Inference runs on one core (CPU1); the dual-core profile (`CONFIG_VISION_DL_DUAL_CORE`) has not been validated for YOLO11 and the worker threads contend with capture/network on CPU0.
- All accuracy/latency numbers come from ESP-DL's official S3 reference, not from this board.
