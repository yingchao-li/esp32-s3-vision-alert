#!/usr/bin/env python3
"""Install validated trained artifacts locally. Does not build or flash hardware."""
import argparse
import json
from pathlib import Path
import shutil
from verify_model import verify

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest",type=Path)
    p.add_argument("--replace",action="store_true")
    args=p.parse_args();spec,model=verify(args.manifest,True)
    report=(args.manifest.resolve().parent/spec["validation_report"]).resolve()
    data=json.loads(report.read_text())
    if len(data["per_class_ap50"])!=4 or min(data["per_class_ap50"])<.15 or data["fp32_map50"]<.25:
        raise ValueError("Validation quality gate failed")
    output=Path(__file__).resolve().parents[1]/"models"
    if (output/"four_class.json").exists() and not args.replace:
        p.error("Existing custom model preserved; pass --replace to replace it explicitly")
    shutil.copy2(model,output/"four_class.espdl")
    shutil.copy2(report,output/"four_class.validation.json")
    spec["file"]="four_class.espdl";spec["validation_report"]="four_class.validation.json"
    (output/"four_class.json").write_text(json.dumps(spec,indent=2)+"\n")
    verify(output/"four_class.json",True)
    print("Artifact installed. Enable VISION_FOUR_CLASS_MODEL in idf.py menuconfig, build and run guarded flash. Target functional tests remain pending.")

if __name__=="__main__":main()
