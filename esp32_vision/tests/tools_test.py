import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from convert_nuimages import class_id,yolo_box,convert,CLASSES
from verify_model import verify,COCO_CLASSES
from flash_app import app_region,validate_image,layout_upgrade_ok
from instrument_model import instrument

def partition_table(items):
    blob=bytearray()
    for kind,subtype,address,size,label in items:
        blob+=struct.pack("<HBBII16sI",0x50AA,kind,subtype,address,size,label.encode().ljust(16,b"\0"),0)
    return bytes(blob)

class ModelDiagnosticsTests(unittest.TestCase):
    def test_changed_dependency_is_rejected(self):
        with self.assertRaises(ValueError):
            instrument(b"a different upstream ESP-DL source")

    def test_real_dependency_diagnostics_and_allocation_guard(self):
        source=ROOT/"managed_components/espressif__esp-dl/dl/model/src/dl_model_base.cpp"
        if not source.is_file():
            self.skipTest("Build once to fetch pinned ESP-DL source")
        original=source.read_bytes()
        patched=instrument(original)
        self.assertIn('esp_err_to_name(load_result)',patched)
        self.assertIn('const bool allocated = memory_manager->alloc',patched)
        guard=patched.index('if (!allocated)')
        tensors=patched.index('// get the TensorBase* of inputs and outputs',guard)
        self.assertIn('m_inputs.clear();',patched[guard:tensors])
        self.assertIn('m_outputs.clear();',patched[guard:tensors])
        self.assertIn('delete memory_manager;',patched[guard:tensors])
        self.assertIn('return;',patched[guard:tensors])
        self.assertEqual(source.read_bytes(),original)

class DatasetTests(unittest.TestCase):
    def test_exact_class_mapping(self):
        self.assertEqual(class_id("human.pedestrian.adult"),0)
        self.assertEqual(class_id("movable_object.trafficcone"),1)
        self.assertEqual(class_id("vehicle.bicycle"),2)
        self.assertEqual(class_id("vehicle.car"),3)
        for unsupported in ["traffic light","vehicle.motorcycle","human.pedestrian.stroller","movable_object.barrier"]:
            self.assertIsNone(class_id(unsupported))
    def test_boxes(self):
        self.assertEqual(yolo_box([0,0,200,100],200,100),(.5,.5,1.,1.))
        self.assertEqual(yolo_box([-10,0,250,100],200,100),(.5,.5,1.,1.))
        for box in [[10,10,0,0],[0,0,0,0],[math.nan,0,100,100],[0,0,math.inf,100]]:
            with self.assertRaises(ValueError):yolo_box(box,200,100)
    def test_annotation_conversion_fixture_only(self):
        # These tiny JSON fixtures validate conversion logic only. They are not
        # images or an AI model and are never included in runtime firmware.
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/"source";version=root/"v1.0-train";version.mkdir(parents=True)
            (root/"sample.jpg").touch()
            categories=[{"token":str(i),"name":name} for i,name in enumerate([
                "human.pedestrian.adult","movable_object.trafficcone","vehicle.bicycle","vehicle.car"])]
            samples=[{"token":"aabb","filename":"sample.jpg","width":200,"height":100,"is_key_frame":True}]
            annotations=[{"sample_data_token":"aabb","category_token":str(i),"bbox":[0,0,100,100]} for i in range(4)]
            for name,data in [("category",categories),("sample_data",samples),("object_ann",annotations)]:
                (version/(name+".json")).write_text(json.dumps(data))
            out=Path(temp)/"converted";summary=convert(root,out,"v1.0-train","train")
            self.assertEqual(summary["images"],1)
            self.assertEqual(summary["boxes"],{name:1 for name in CLASSES})
            rows=(out/"labels/train/aabb.txt").read_text().splitlines()
            self.assertEqual([r.split()[0] for r in rows],["0","1","2","3"])

class ModelTests(unittest.TestCase):
    def test_real_baseline(self):
        spec,model=verify(ROOT/"models/person_baseline.json")
        self.assertEqual(spec["classes"],["Person"])
        self.assertEqual(model.stat().st_size,435328)
    def test_baseline_cannot_be_claimed_as_four_classes(self):
        with self.assertRaises(ValueError):verify(ROOT/"models/person_baseline.json",True)
    def test_real_yolo11_three_class(self):
        spec,model=verify(ROOT/"models/yolo11n_coco.json",yolo11=True)
        self.assertEqual(spec["family"],"yolo11")
        self.assertEqual(spec["classes"],COCO_CLASSES)
        self.assertEqual(spec["deployed_classes"],["Person","Bicycle","Car"])
        self.assertEqual(model.stat().st_size,2858512)
    def test_yolo11_cannot_be_claimed_as_four_classes(self):
        with self.assertRaises(ValueError):verify(ROOT/"models/yolo11n_coco.json",True)
    def test_baseline_cannot_be_claimed_as_yolo11(self):
        with self.assertRaises(ValueError):verify(ROOT/"models/person_baseline.json",yolo11=True)
    def test_corruption_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp=Path(temp);original=ROOT/"models/person_baseline.json";spec=json.loads(original.read_text())
            damaged=bytearray((original.parent/spec["file"]).read_bytes());damaged[-1]^=1
            (temp/spec["file"]).write_bytes(damaged);(temp/"model.json").write_text(json.dumps(spec))
            with self.assertRaises(ValueError):verify(temp/"model.json")
    def test_yolo11_corruption_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp=Path(temp);original=ROOT/"models/yolo11n_coco.json";spec=json.loads(original.read_text())
            damaged=bytearray((original.parent/spec["file"]).read_bytes());damaged[-1]^=1
            (temp/spec["file"]).write_bytes(damaged);(temp/"model.json").write_text(json.dumps(spec))
            with self.assertRaises(ValueError):verify(temp/"model.json",yolo11=True)
    def test_missing_four_class_artifact_blocks_build(self):
        with self.assertRaises(FileNotFoundError):verify(ROOT/"models/four_class.json",True)

class FlashTests(unittest.TestCase):
    def test_real_stable_table_and_binary_fit(self):
        stable=ROOT.parent/"stable_firmware"
        address,size=app_region((stable/"partition-table.bin").read_bytes())
        self.assertEqual((address,size),(0x10000,0x3f0000))
        validate_image((stable/"person_beep.bin").read_bytes(),size)
    def test_route_a_layout_and_safe_upgrade(self):
        stable=(ROOT.parent/"stable_firmware/partition-table.bin").read_bytes()
        route_a=partition_table([(1,2,0x9000,0x6000,"nvs"),(1,1,0xf000,0x1000,"phy_init"),
                                 (0,0,0x10000,0x6f0000,"factory")])
        self.assertEqual(app_region(route_a),(0x10000,0x6f0000))
        self.assertTrue(layout_upgrade_ok(stable,route_a))
        self.assertFalse(layout_upgrade_ok(route_a,stable))
        moved_nvs=partition_table([(1,2,0x9000,0x5000,"nvs"),(1,1,0xf000,0x1000,"phy_init"),
                                   (0,0,0x10000,0x6f0000,"factory")])
        self.assertFalse(layout_upgrade_ok(stable,moved_nvs))
        too_big=partition_table([(1,2,0x9000,0x6000,"nvs"),(1,1,0xf000,0x1000,"phy_init"),
                                 (0,0,0x10000,0x7f1000,"factory")])
        self.assertFalse(layout_upgrade_ok(stable,too_big))
    def test_modified_partition_layout_is_rejected(self):
        stable=ROOT.parent/"stable_firmware/partition-table.bin"
        table=bytearray(stable.read_bytes());struct.pack_into("<I",table,2*32+8,0x7f0000)
        with self.assertRaises(ValueError):app_region(table)
    def test_wrong_chip_or_oversized_image_rejected(self):
        stable=bytearray((ROOT.parent/"stable_firmware/person_beep.bin").read_bytes())
        with self.assertRaises(ValueError):validate_image(stable,4096)
        struct.pack_into("<H",stable,12,0)
        with self.assertRaises(ValueError):validate_image(stable,0x3f0000)

if __name__=="__main__":unittest.main()
