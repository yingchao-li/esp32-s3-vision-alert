#!/usr/bin/env python3
"""Convert licensed local nuImages 2D boxes to exact four-class YOLO labels.

No account login, image retrieval, artificial labels or data acceptance on the
user's behalf. Download the official dataset after agreeing to its terms.
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path

CLASSES = ["Person", "Traffic Cone", "Bicycle", "Car"]
PERSON = {"human.pedestrian.adult", "human.pedestrian.child",
          "human.pedestrian.construction_worker", "human.pedestrian.police_officer"}

def class_id(name):
    if name in PERSON:
        return 0
    return {"movable_object.trafficcone": 1, "vehicle.bicycle": 2, "vehicle.car": 3}.get(name)

def yolo_box(box, width, height):
    if width <= 0 or height <= 0 or len(box) != 4:
        raise ValueError("Invalid image/bbox dimensions")
    x1, y1, x2, y2 = map(float, box)
    if not all(math.isfinite(v) for v in (x1, y1, x2, y2)):
        raise ValueError("Non-finite bounding box")
    x1, x2 = max(0, x1), min(width, x2)
    y1, y2 = max(0, y1), min(height, y2)
    if not (0 <= x1 < x2 <= width and 0 <= y1 < y2 <= height):
        raise ValueError("Degenerate or non-finite bbox")
    return ((x1+x2)/(2*width), (y1+y2)/(2*height), (x2-x1)/width, (y2-y1)/height)

def convert(root, output, version, split):
    directory = root / version
    load = lambda name: json.loads((directory / (name + ".json")).read_text())
    categories = {c["token"]: c["name"] for c in load("category")}
    annotations = defaultdict(list)
    for a in load("object_ann"):
        cid = class_id(categories[a["category_token"]])
        if cid is not None:
            annotations[a["sample_data_token"]].append((cid, a["bbox"]))
    counts = Counter()
    image_dir, label_dir = output/"images"/split, output/"labels"/split
    image_dir.mkdir(parents=True, exist_ok=True)
    label_dir.mkdir(parents=True, exist_ok=True)
    images = 0
    for record in load("sample_data"):
        if not record.get("is_key_frame", False):
            continue  # Non-key frames do not have exhaustive object labels.
        source = (root / record["filename"]).resolve()
        if not source.is_relative_to(root.resolve()) or not source.is_file():
            raise FileNotFoundError(source)
        token = record["token"]
        if not token.isalnum():
            raise ValueError("Invalid nuImages token")
        lines = []
        for cid, box in annotations[token]:
            normalized = yolo_box(box, record["width"], record["height"])
            lines.append(str(cid)+" "+" ".join(f"{v:.8f}" for v in normalized))
            counts[cid] += 1
        destination = image_dir / (token + source.suffix.lower())
        if not destination.exists():
            destination.symlink_to(source)
        elif destination.resolve() != source:
            raise FileExistsError(destination)
        (label_dir/(token+".txt")).write_text("\n".join(lines)+("\n" if lines else ""))
        images += 1
    return {"images": images, "boxes": {CLASSES[c]: counts[c] for c in range(4)}}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dataroot", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--license-acknowledged", action="store_true",
                   help="You have obtained nuImages and permission for its non-commercial terms")
    args = p.parse_args()
    if not args.license_acknowledged:
        p.error("Read https://www.nuscenes.org/nuimages and obtain authorized local data first")
    root, output = args.dataroot.resolve(), args.output.resolve()
    if output == root or root.is_relative_to(output):
        p.error("Output must not contain or replace the original dataset")
    summary = {split: convert(root, output, version, split)
               for split, version in [("train", "v1.0-train"), ("val", "v1.0-val")]}
    if any(v["boxes"][c] == 0 for v in summary.values() for c in CLASSES):
        raise ValueError("All four classes must occur in both official splits; no empty-class training")
    # JSON is a valid YAML subset, avoiding an extra dependency for preparation.
    config = {"path": str(output), "train": "images/train", "val": "images/val", "names": CLASSES}
    (output/"data.yaml").write_text(json.dumps(config, indent=2)+"\n")
    provenance = {"source": "https://www.nuscenes.org/nuimages", "dataset_license": "nuImages non-commercial terms",
                  "license_acknowledged_by_user": True, "classes": CLASSES, "splits": summary,
                  "notes": "Official train/val splits; only annotated key frames. Strollers, personal mobility and wheelchair boxes are not relabeled as pedestrians."}
    (output/"provenance.json").write_text(json.dumps(provenance, indent=2)+"\n")
    print(json.dumps(provenance, indent=2))

if __name__ == "__main__":
    main()
