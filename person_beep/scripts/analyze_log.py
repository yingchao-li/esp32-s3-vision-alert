#!/usr/bin/env python3
"""Summarize measured logs; never generate or substitute detection results."""
import argparse
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("log", type=Path)
args = p.parse_args()
text = args.log.read_text(errors="replace")
latency = [float(x) for x in re.findall(r"Inference: ([\d.]+) ms", text)]
scores = [float(x) for x in re.findall(r"person, confidence=([\d.]+)", text)]
starts, cooldowns, errors = [], [], []
last_low = None
last_low_us = None
pad_edges = []
for line in text.splitlines():
    pad = re.search(r"BUZZER_PIN: GPIO14 actual pad=(\d), t=(\d+) us", line)
    if pad:
        pad_edges.append((int(pad[2]), int(pad[1])))
    m = re.search(r"\((\d+)\) BUZZER: (.*)", line)
    if not m:
        continue
    now, event = int(m[1]), m[2]
    monotonic = re.search(r"t=(\d+) us", event)
    now_us = int(monotonic[1]) if monotonic else None
    if "LOW, cooldown" in event or "Stopped:" in event:
        last_low = now
        last_low_us = now_us
    if "Beep Beep!" in event:
        starts.append(now)
        if last_low is not None:
            cooldowns.append((now_us - last_low_us) / 1000
                             if now_us is not None and last_low_us is not None
                             else now - last_low)
            if cooldowns[-1] < 2000:
                errors.append(line)
high_pulses = [(b[0] - a[0]) / 1000 for a, b in zip(pad_edges, pad_edges[1:])
               if a[1] == 1 and b[1] == 0]
low_periods = [(b[0] - a[0]) / 1000 for a, b in zip(pad_edges, pad_edges[1:])
               if a[1] == 0 and b[1] == 1]
summary = {
    "source": str(args.log), "frames": len(latency),
    "inference_ms": {"mean": round(statistics.mean(latency), 2),
                     "min": min(latency), "max": max(latency)} if latency else None,
    "person_boxes": len(scores),
    "confidence_range": [min(scores), max(scores)] if scores else None,
    "negative_frames": text.count("No person above threshold"),
    "alarm_sequences": len(starts),
    "min_observed_low_cooldown_ms": min(cooldowns) if cooldowns else None,
    "cooldown_violations": errors,
    "gpio14_readback": {
        "edges": len(pad_edges),
        "high_pulse_ms": high_pulses,
        "low_period_ms": low_periods,
        "note": "5ms polling of actual GPIO pad, includes any cancelled/truncated sequence",
    },
    "safe_idle": "SAFE IDLE" in text,
    "panic": bool(re.search(r"Guru Meditation|panic'ed|assert failed|Stack canary|Brownout", text)),
    "hardware_lines": re.findall(r"^.*HARDWARE:.*$", text, re.MULTILINE),
    "sensor_lines": re.findall(r"^.*CAMERA: Sensor=.*$", text, re.MULTILINE),
}
print(json.dumps(summary, indent=2, ensure_ascii=False))
if errors or summary["panic"]:
    raise SystemExit(1)
