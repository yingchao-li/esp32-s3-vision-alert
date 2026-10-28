#!/usr/bin/env python3
"""Real train -> validate -> ONNX -> INT8 ESP-DL export, never run by firmware.

Requires authorized local four-class nuImages YOLO conversion. This tool cannot
produce a deployable manifest without actual training and validation metrics.
"""
import argparse
import contextlib
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

CLASSES = ["Person", "Traffic Cone", "Bicycle", "Car"]
ESPDET_COMMIT = "28de5902a357d99dd9a5db0f52905255641c5a78"

def validate_onnx(path, size):
    import onnx
    model = onnx.load(path)
    onnx.checker.check_model(model)
    expected = {f"{prefix}{i}" for prefix in ["box", "score"] for i in range(3)}
    if {o.name for o in model.graph.output} != expected:
        raise ValueError("Unexpected ESPDet outputs; expected box0/score0 ... box2/score2")
    allowed = {"Conv", "Add", "Sub", "Mul", "Div", "Concat", "Reshape", "Transpose", "Split", "Slice",
               "Resize", "MaxPool", "AveragePool", "GlobalAveragePool", "Relu", "LeakyRelu", "Sigmoid",
               "Clip", "Identity", "Squeeze", "Unsqueeze", "Constant", "Gather", "Shape", "Cast",
               "MatMul", "Softmax", "Flatten", "BatchNormalization"}
    unknown = {n.op_type for n in model.graph.node} - allowed
    if unknown:
        raise ValueError(f"Unreviewed operators: {unknown}")
    dimensions = lambda t: [d.dim_value for d in t.type.tensor_type.shape.dim]
    if dimensions(model.graph.input[0]) != [1, 3, size, size]:
        raise ValueError("Only static NCHW batch=1 RGB input supported")
    for out in model.graph.output:
        stage = int(out.name[-1]); edge = size // (8 << stage)
        if dimensions(out) != [1, 4, edge, edge]:
            raise ValueError(f"Unexpected shape for {out.name}: {dimensions(out)}")
    return sorted({n.op_type for n in model.graph.node})

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--epochs", type=int, default=120)
    p.add_argument("--batch", type=int, default=8)
    p.add_argument("--size", type=int, choices=[160, 224, 320], default=224)
    p.add_argument("--device", default="cpu", help="cpu, mps, or a CUDA device index")
    p.add_argument("--min-map50", type=float, default=.25)
    args = p.parse_args()
    args.data = args.data.resolve()
    args.output = args.output.resolve()
    if args.epochs < 1:
        p.error("At least one real training epoch is required")
    root = Path(__file__).resolve().parents[1]
    vendor = root/"tools/vendor/esp-detection"
    import subprocess
    revision = subprocess.check_output(["git", "-C", str(vendor), "rev-parse", "HEAD"], text=True).strip()
    if revision != ESPDET_COMMIT:
        raise ValueError("Run tools/prepare_training_env.sh to install the pinned training source")
    config = json.loads(args.data.resolve().read_text())
    dataset = Path(config["path"])
    provenance = json.loads((dataset/"provenance.json").read_text())
    if config["names"] != CLASSES or provenance["classes"] != CLASSES or not provenance["license_acknowledged_by_user"]:
        raise ValueError("Four-class dataset/provenance mismatch")
    if any(v["boxes"][name] <= 0 for v in provenance["splits"].values() for name in CLASSES):
        raise ValueError("Missing class annotations")
    args.output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(vendor))
    from train import Train
    from deploy.export import Export, ESP_YOLO
    import torch
    import numpy as np
    import onnxruntime as ort
    from PIL import Image
    from torch.utils.data import Dataset, DataLoader
    from esp_ppq import QuantizationSettingFactory
    from esp_ppq.api import espdl_quantize_onnx
    from esp_ppq.executor import TorchExecutor
    import os
    previous = Path.cwd()
    try:
        os.chdir(vendor)
        result = Train(None, str(args.data.resolve()), args.size, epochs=args.epochs, batch=args.batch,
                       device=args.device, workers=0, project=str(args.output.resolve()/"training"), name="four_class",
                       close_mosaic=min(10, args.epochs), seed=42)
    finally:
        os.chdir(previous)
    weights = Path(result.save_dir)/"weights/best.pt"
    if not weights.is_file():
        raise FileNotFoundError("No trained best.pt produced")
    metrics = ESP_YOLO(str(weights)).val(data=str(args.data.resolve()), imgsz=args.size,
                                        device=args.device, batch=args.batch, workers=0)
    ap50 = list(map(float, metrics.box.ap50))
    if len(ap50) != 4 or any(not math.isfinite(x) for x in ap50) or not math.isfinite(float(metrics.box.map50)) or float(metrics.box.map50) < args.min_map50 or min(ap50) < .15:
        raise ValueError(f"Validation quality gate failed: per-class AP50={ap50}; no deploy manifest generated")
    Export(str(weights), [args.size, args.size])
    onnx = weights.with_suffix(".onnx")
    operators = validate_onnx(onnx, args.size)
    files = sorted((dataset/"images/train").glob("*"))
    if len(files) < 64:
        raise ValueError("At least 64 representative calibration images required")
    # Spread samples over the full set, without training/validation split leakage.
    files = files[::max(1,len(files)//256)][:256]
    class Calibration(Dataset):
        def __len__(self): return len(files)
        def __getitem__(self, index):
            with Image.open(files[index]) as im:
                im = im.convert("RGB")
                gain = min(args.size/im.width, args.size/im.height)
                width,height = round(im.width*gain),round(im.height*gain)
                canvas=Image.new("RGB",(args.size,args.size),(114,114,114))
                canvas.paste(im.resize((width,height),Image.Resampling.BILINEAR),((args.size-width)//2,(args.size-height)//2))
                return torch.from_numpy(np.asarray(canvas).copy()).permute(2,0,1).float()/255
    loader = DataLoader(Calibration(), batch_size=1, shuffle=False)
    artifact = args.output.resolve()/"four_class.espdl"
    setting = QuantizationSettingFactory.espdl_setting()
    setting.equalization = True
    with (args.output/"quantization.log").open("w") as log, contextlib.redirect_stdout(log):
        graph = espdl_quantize_onnx(str(onnx),str(artifact),loader,32,[1,3,args.size,args.size],
            target="esp32s3",num_of_bits=8,device="cpu",setting=setting,error_report=True,export_test_values=True)
    # Compare all six real output tensors of the FP32 and quantized graphs.
    session=ort.InferenceSession(str(onnx),providers=["CPUExecutionProvider"])
    names=[o.name for o in session.get_outputs()]
    executor=TorchExecutor(graph=graph,device="cpu")
    comparisons=[]
    for index,image in enumerate(loader):
        if index >= 8: break
        floating=session.run(names,{session.get_inputs()[0].name:image.numpy()})
        quantized=executor.forward(inputs=image,output_names=names)
        for name,a,b in zip(names,floating,quantized):
            b=b.detach().cpu().numpy()
            if a.shape!=b.shape and b.ndim==4 and a.shape==b.transpose(0,3,1,2).shape:
                b=b.transpose(0,3,1,2)
            if a.shape!=b.shape or not np.isfinite(b).all():
                raise ValueError(f"Invalid quantized output {name}")
            cosine=float(np.dot(a.ravel(),b.ravel())/(np.linalg.norm(a)*np.linalg.norm(b)+1e-12))
            comparisons.append({"tensor":name,"sample":index,"cosine":cosine,"mae":float(np.abs(a-b).mean())})
    if not comparisons or min(x["cosine"] for x in comparisons) < .90:
        raise ValueError("Quantized-vs-FP32 cosine gate failed; inspect quantization.log")
    blob=artifact.read_bytes()
    if blob[:4]!=b"EDL2": raise ValueError("Export did not produce an ESP-DL model")
    report={"fp32_map50":float(metrics.box.map50),"per_class_ap50":ap50,"quantization":comparisons,
            "operators":operators,"dataset":provenance,"input_size":args.size,
            "hardware_operator_allocation_test":"PENDING USER HARDWARE TEST",
            "memory":"Firmware graph/tensor checks and memory monitor must be reviewed on target; host similarity is not target mAP."}
    (args.output/"validation.json").write_text(json.dumps(report,indent=2)+"\n")
    manifest={"name":f"Trained ESPDet four-class INT8 {args.size}","file":artifact.name,"sha256":hashlib.sha256(blob).hexdigest(),
              "bytes":len(blob),"target":"esp32s3","family":"espdet","width":args.size,"height":args.size,
              "classes":CLASSES,"training_completed":True,"training_source_commit":ESPDET_COMMIT,
              "dataset_license":provenance["dataset_license"],"validation_report":"validation.json"}
    (args.output/"four_class.json").write_text(json.dumps(manifest,indent=2)+"\n")
    shutil.copy2(weights,args.output/"best.pt")
    print(f"Trained/validated artifact: {artifact}. Target hardware validation is still pending.")

if __name__ == "__main__":
    main()
