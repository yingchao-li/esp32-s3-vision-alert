#!/usr/bin/env python3
"""Build-time artifact/provenance checks. Never creates dummy model weights."""
import argparse
import hashlib
import json
import math
from pathlib import Path

CLASSES = ["Person", "Traffic Cone", "Bicycle", "Car"]

# Exact COCO-80 order used by the Ultralytics YOLO11n export and by ESP-DL's
# coco_detect model. The C++ side maps indices 0/1/2 (person/bicycle/car) into
# the four-class alarm vocabulary; every other COCO category is ignored.
COCO_CLASSES = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair",
    "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
    "toothbrush",
]

# Only these COCO categories are exposed by the firmware and by the buzzer
# mapping (Person=1 beep, Bicycle=3 beeps, Car=4 beeps). Traffic Cone has no
# COCO category and stays undeployed until a custom model is provided.
DEPLOYED_COCO = ["Person", "Bicycle", "Car"]
COCO_INDICES = {"Person": 0, "Bicycle": 1, "Car": 2}
YOLO_PREPROCESSING = {"mean": [0, 0, 0], "std": [255, 255, 255], "letterbox": 114}


def verify(manifest, four=False, yolo11=False):
    if four and yolo11:
        raise ValueError("Select either the four-class model or the COCO YOLO11 model")
    path = Path(manifest).resolve()
    spec = json.loads(path.read_text())
    model = (path.parent / spec["file"]).resolve()
    if model.parent != path.parent:
        raise ValueError("Model must reside in manifest directory")
    blob = model.read_bytes()
    if blob[:4] != b"EDL2" or len(blob) < 1024:
        raise ValueError("Expected a real ESP-DL EDL2 artifact")
    if len(blob) != spec["bytes"] or hashlib.sha256(blob).hexdigest() != spec["sha256"]:
        raise ValueError("Model size/SHA256 mismatch")
    if spec["target"] != "esp32s3" or spec["training_completed"] is not True:
        raise ValueError("Model is untrained or not targeted to ESP32-S3")
    if any(not isinstance(spec[k], int) or not 32 <= spec[k] <= 640 or spec[k] % 32 for k in ["width", "height"]):
        raise ValueError("Expected 32-aligned input <=640")
    if four:
        if spec["family"] != "espdet" or spec["classes"] != CLASSES:
            raise ValueError("Four-class ESPDet with exact documented label order required")
        if not spec.get("validation_report") or not spec.get("dataset_license"):
            raise ValueError("Missing validation report/dataset license provenance")
        report_path = (path.parent / spec["validation_report"]).resolve()
        if report_path.parent != path.parent:
            raise ValueError("Validation report must reside beside model")
        report = json.loads(report_path.read_text())
        ap = report.get("per_class_ap50", [])
        overall = report.get("fp32_map50", 0)
        if len(ap) != 4 or any(not math.isfinite(x) or x < .15 for x in ap) or not math.isfinite(overall) or overall < .25:
            raise ValueError("Four-class FP32 validation quality gate failed")
        comparisons = report.get("quantization", [])
        if not comparisons or any(not math.isfinite(x["cosine"]) or x["cosine"] < .90 for x in comparisons):
            raise ValueError("Quantization similarity report failed/missing")
    elif yolo11:
        if spec["family"] != "yolo11" or spec["width"] != spec["height"]:
            raise ValueError("Expected the official square YOLO11 COCO artifact")
        if spec["classes"] != COCO_CLASSES:
            raise ValueError("Exact COCO-80 label order required")
        if spec["deployed_classes"] != DEPLOYED_COCO or spec.get("class_indices") != COCO_INDICES:
            raise ValueError("Deployed class mapping must be Person=0, Bicycle=1, Car=2")
        if spec.get("preprocessing") != YOLO_PREPROCESSING:
            raise ValueError("Firmware preprocessing contract (mean 0, std 255, letterbox 114) mismatch")
        if not spec.get("source") or not spec.get("license"):
            raise ValueError("Missing model source/license provenance")
    elif spec["family"] != "pico" or spec["classes"] != ["Person"]:
        raise ValueError("Baseline must be the real person model")
    return spec, model


def header(spec, four=False, yolo11=False):
    return (
        "#pragma once\n"
        "#include <cstddef>\n"
        "namespace vision {\n"
        f"inline constexpr bool kFourClassModel = {str(four).lower()};\n"
        f"inline constexpr bool kYolo11CocoModel = {str(yolo11).lower()};\n"
        f"inline constexpr const char *kModelFamily = {json.dumps(spec['family'])};\n"
        f"inline constexpr const char *kModelName = {json.dumps(spec['name'])};\n"
        f"inline constexpr const char *kModelSha256 = {json.dumps(spec['sha256'])};\n"
        f"inline constexpr size_t kModelBytes = {spec['bytes']};\n"
        f"inline constexpr int kModelWidth = {spec['width']}, kModelHeight = {spec['height']};\n"
        f"inline constexpr int kModelClassCount = {len(spec['classes'])};\n"
        "}\n"
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument("manifest", type=Path)
    p.add_argument("--four", action="store_true")
    p.add_argument("--yolo11", action="store_true")
    p.add_argument("--header", type=Path)
    args = p.parse_args()
    spec, model = verify(args.manifest, args.four, args.yolo11)
    if args.header:
        args.header.write_text(header(spec, args.four, args.yolo11))
    print(model)


if __name__ == "__main__":
    main()
