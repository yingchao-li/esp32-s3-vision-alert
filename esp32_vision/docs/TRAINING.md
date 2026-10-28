# Four-class real model: data, training, quantization and deployment

**Current status: PARTIAL. There are no four-class weights, and no training/quantization or on-device four-class detection has been performed.** The user confirmed there is no ready data, so the system and executable tools were delivered first.
The only real model shipped recently was the Person model, with its SHA256 recorded in `models/person_baseline.json`. It was never relabeled or claimed to detect traffic cones, bicycles or cars. Route A (default) uses the official COCO YOLO11n for Person/Bicycle/Car; Traffic Cone remains an extension in progress until this four-class path is completed.

## Verified technical route

- Lightweight architecture: [Espressif ESP-Detection / ESPDet-Pico](https://github.com/espressif/esp-detection), pinned to commit `28de5902a357d99dd9a5db0f52905255641c5a78`.
- Quantizer: ESP-PPQ 1.3.0, pinned to commit `55bb56b22dca5186876a44ee740d71c25bb42ffc`, targeting `esp32s3` INT8.
- Inference engine: ESP-DL 3.3.14, using S3-optimized operators; no PC/internet runtime inference.
- Model export input is fixed to one of 160, 224 or 320 square sizes. The runtime cannot arbitrarily rewrite the model input shape.
- ESP-Detection/Ultralytics is AGPL-3.0-family software; ESP-DL and the baseline model licenses are provided in `models/LICENSE.esp-dl`. Follow the relevant upstream licenses when training or distributing.

Standard COCO has no traffic cone, and Open Images' 600-box categories have no Traffic Cone either; traffic lights/signs must not be renamed.
This project uses [official nuImages data](https://www.nuscenes.org/nuimages), which does have four-class 2D boxes; the official [annotation tutorial](https://www.nuscenes.org/tutorials/nuimages_tutorial.html) shows `movable_object.trafficcone`. **nuImages is non-commercial only; you must obtain the data and accept the official terms yourself. Commercial use requires a separate license.** No account registration, terms acceptance or full dataset download was performed on your behalf.

| Target ID | nuImages category |
| --- | --- |
| 0 Person | human.pedestrian.adult / child / construction_worker / police_officer |
| 1 Traffic Cone | movable_object.trafficcone |
| 2 Bicycle | vehicle.bicycle |
| 3 Car | vehicle.car |

Barriers, traffic signs, strollers and motorcycles are not swapped into the target classes. Only annotated keyframes are used; keyframes without targets serve as empty-label negative samples. The official train/val split is kept so adjacent video frames are not randomly split into train/val and cause validation leakage.

## Environment and checks already performed

A separate training environment was installed and `pip check`, Train/Export/ESP-PPQ/TorchExecutor import checks were run:

- Python 3.10, torch 2.2.2, torchvision 0.17.2, torchaudio 2.2.2.
- ultralytics 8.3.112, onnx 1.17.0, onnxruntime 1.19.2, onnxsim 0.4.36, numpy 1.26.4.
- Mac MPS is available, but **no training, model inference performance or accuracy test was run**.
- The full install list is in `docs/logs/training-environment.freeze.txt`, with the install log in `training-setup.log`.

Reinstall commands:

```sh
brew install python@3.10
bash tools/prepare_training_env.sh
```

## Full flow once you provide data

1. Download the authorized nuImages train/val images and metadata in the official way, arranged as:

```text
/path/to/nuimages/
  v1.0-train/category.json, sample_data.json, object_ann.json, ...
  v1.0-val/category.json, sample_data.json, object_ann.json, ...
  samples/...
```

2. Convert (replace paths with your real authorized data path):

```sh
tools/.training-env/bin/python tools/convert_nuimages.py \
  --dataroot /path/to/nuimages --output "$PWD/data/four_class" \
  --license-acknowledged
```

The converter checks classes, dimensions, finite values and box ranges, and produces standard YOLO labels, image symlinks, a JSON-compatible YAML data config, class counts and license provenance. If either train or val split misses any of the four classes, it fails instead of silently training an empty class.

3. Real training → validation → export → INT8 quantization:

```sh
tools/.training-env/bin/python tools/train_four_class.py \
  --data "$PWD/data/four_class/data.yaml" \
  --output "$PWD/training_runs/four_224" \
  --epochs 120 --batch 8 --size 224 --device mps
```

Use `--device cpu` for CPU or the appropriate CUDA index. Training time depends on data size and resources; 120 epochs is not a quality guarantee.
The tool actually calls the official architecture for training and requires a real `best.pt`; validation must reach overall FP32 mAP50 of at least 0.25 (default) and per-class AP50 of at least 0.15, otherwise it stops and cannot print a "deployment complete" checklist.
It exports a static batch=1 ONNX graph, checks graph validity, an operator whitelist, the six output names and NCHW `[1,4,H/stride,W/stride]`. It calibrates with real training samples using `/255` plus RGB letterbox 114, targeting S3 INT8.
After quantization it compares the six FP32/quantized outputs on 8 calibration images and records MAE/cosine similarity, with a minimum cosine of 0.90. This check is not post-quantization detection mAP and does not replace target-board validation.

4. On success it outputs `best.pt`, `four_class.espdl`, `four_class.json`, `validation.json` and `quantization.log`. No deployment claim is generated without successful training and validation.

```sh
python3.13 tools/deploy_model.py training_runs/four_224/four_class.json
source tools/env.sh
idf.py menuconfig
# Vision system -> Embed a trained, validated four-class ESPDet model
bash tools/build.sh
bash tools/flash.sh /dev/cu.usbmodem5C930528211
```

The deployer re-verifies the EDL2 header, size, SHA256, target, exact four-class order and validation results. An existing custom model is preserved unless `--replace` is passed explicitly. Enabling four-class with a missing model/report fails at the CMake stage.
On the target board it then checks the NHWC INT8 input and the four-class score / four-channel box tensors; an allocation failure or format mismatch disables alerts. **Real operator execution, peak memory, four-class accuracy and latency are still for you to test on hardware.**

## Explicitly incomplete

- No four-class training data, so no training, validation-set evaluation, quantized artifact or on-device four-class detection has been completed.
- The training CLI and dependencies passed import/logic checks, and the converter has host unit tests; this is not the same as successful end-to-end training.
- No download link is provided for a non-existent four-class model. After authorized data is available, produce a real model with the tools above; if conversion/training errors occur, provide the full log so they can be fixed.
- Flash partitions and runtime memory must be confirmed by a real build and target-board logs. File size is not proof of SRAM/PSRAM peak usage.
