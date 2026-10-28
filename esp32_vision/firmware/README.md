# Generated real firmware (Route A)

- `esp32_vision-pipeline.bin`: default single-core pipeline, embedding the 2.86MB YOLO11n COCO 320 model (Person/Bicycle/Car). **Built, not flashed.**
- `esp32_vision-dual.bin`: ESP-DL internal dual-core profile with the same model. **Built, not flashed.**
- `partition-table-reference.bin`: **new Route A layout** (app `0x10000` / `0x6f0000`), for comparison only, **not a flash target**.
- SHA256 is in `SHA256SUMS`; build details and limitations are in the [Route A notes](../docs/ROUTE_A.md).

| Firmware | Size | App partition | Headroom |
| --- | ---: | ---: | ---: |
| pipeline | 4,817,936 B | 7,274,496 B (`0x6f0000`) | 2,456,560 B (34%) |
| dual | 4,818,032 B | 7,274,496 B (`0x6f0000`) | 2,456,464 B (34%) |

**First flash**: if the device still has the old `0x3f0000` partition table, the script refuses; after reviewing the layout, run once:

```sh
source tools/env.sh
bash tools/flash.sh /dev/cu.usbmodem5C930528211 --approve-layout-change
```

The script verifies chip/MAC/8MB flash, reads back the partition table, allows only "grow the application partition in place", writes the 0x8000 partition table once (leaving NVS untouched), then writes the application. Later flashes no longer need that flag.

```sh
# Update the application only
bash tools/flash.sh /dev/cu.usbmodem5C930528211
# Restore the stable application (the new layout stays; the stable app still runs)
bash tools/restore_stable.sh /dev/cu.usbmodem5C930528211
```

No flashing was performed in this round, and no on-device recognition, buzzer, FPS, memory or stability test was run; hardware behavior beyond the web UI, model and partition remains for the user to verify. Traffic Cone is still not deployed (extension in progress).
