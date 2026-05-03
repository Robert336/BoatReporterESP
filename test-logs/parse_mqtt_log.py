#!/usr/bin/env python3
import re
import os
from datetime import datetime
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import matplotlib.patches as mpatches
import numpy as np
import pandas as pd

LOG_PATH = os.path.join(os.path.dirname(__file__), "mqtt_log.txt")
OUT_DIR = os.path.dirname(__file__)

TS_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})")
MV_RE = re.compile(r"millivolts reading = ([\d.]+) mV")
STATUS_RE = re.compile(r"\[STATUS\] State=(\w+), WaterLevel=([\d.-]+) cm, SensorError=(\d+), EmergencyConditions=(\d+)")
HEAP_RE = re.compile(r"\[HEAP\] Free=(\d+), MinFree=(\d+), MaxBlock=(\d+)")
EVENT_RE = re.compile(r"\[EVENT\] (.+)")
STATE_RE = re.compile(r"\[STATE\] Transitioning from (\w+) to (\w+)")

mv_rows, status_rows, heap_rows, events, state_changes = [], [], [], [], []

with open(LOG_PATH) as f:
    for line in f:
        m = TS_RE.match(line)
        if not m:
            continue
        ts = datetime.fromisoformat(m.group(1))

        if mm := MV_RE.search(line):
            mv_rows.append((ts, float(mm.group(1))))
        elif mm := STATUS_RE.search(line):
            status_rows.append((ts, mm.group(1), float(mm.group(2)), int(mm.group(3)), int(mm.group(4))))
        elif mm := HEAP_RE.search(line):
            heap_rows.append((ts, int(mm.group(1)), int(mm.group(2)), int(mm.group(3))))
        elif mm := EVENT_RE.search(line):
            events.append((ts, mm.group(1)))
        elif mm := STATE_RE.search(line):
            state_changes.append((ts, mm.group(1), mm.group(2)))

mv_df = pd.DataFrame(mv_rows, columns=["ts", "mv"]).set_index("ts")
status_df = pd.DataFrame(status_rows, columns=["ts", "state", "water_level", "sensor_error", "emergency"]).set_index("ts")
heap_df = pd.DataFrame(heap_rows, columns=["ts", "free", "min_free", "max_block"]).set_index("ts")

# resample noisy ADC readings to 30s mean
mv_30s = mv_df["mv"].resample("30s").mean().dropna()

STYLE = {
    "figure.facecolor": "#0d1117",
    "axes.facecolor": "#161b22",
    "axes.edgecolor": "#30363d",
    "axes.labelcolor": "#c9d1d9",
    "axes.titlecolor": "#e6edf3",
    "xtick.color": "#8b949e",
    "ytick.color": "#8b949e",
    "grid.color": "#21262d",
    "grid.linewidth": 0.8,
    "text.color": "#c9d1d9",
    "legend.facecolor": "#161b22",
    "legend.edgecolor": "#30363d",
}
plt.rcParams.update(STYLE)

DATE_FMT = mdates.DateFormatter("%H:%M")

def add_state_bands(ax, state_changes, ymin, ymax):
    """Shade EMERGENCY periods."""
    in_emergency = False
    start = None
    for ts, frm, to in state_changes:
        if to == "EMERGENCY":
            in_emergency = True
            start = ts
        elif to == "NORMAL" and in_emergency:
            ax.axvspan(start, ts, color="#f85149", alpha=0.08, zorder=0)
            in_emergency = False
    if in_emergency and start:
        ax.axvspan(start, status_df.index[-1], color="#f85149", alpha=0.08, zorder=0)


# ── Figure 1: Water Level ────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(13, 4.5))
ax.plot(status_df.index, status_df["water_level"], color="#58a6ff", linewidth=1.2, label="Water Level")
ax.axhline(20, color="#ffa657", linewidth=1, linestyle="--", label="Tier 1 threshold (20 cm)")
ax.axhline(40, color="#f85149", linewidth=1, linestyle="--", label="Tier 2 threshold (40 cm)")
add_state_bands(ax, state_changes, 0, status_df["water_level"].max())

# annotate peak
peak_ts = status_df["water_level"].idxmax()
peak_val = status_df["water_level"].max()
ax.annotate(f"Peak: {peak_val:.1f} cm", xy=(peak_ts, peak_val),
            xytext=(15, -20), textcoords="offset points",
            color="#ffa657", fontsize=8,
            arrowprops=dict(arrowstyle="->", color="#ffa657", lw=0.8))

ax.set_title("Water Level Over Time", fontsize=13, fontweight="bold", pad=10)
ax.set_ylabel("Water Level (cm)")
ax.xaxis.set_major_formatter(DATE_FMT)
ax.grid(True, axis="both")
ax.legend(fontsize=8)
fig.autofmt_xdate()
fig.tight_layout()
fig.savefig(os.path.join(OUT_DIR, "plot_water_level.png"), dpi=150, bbox_inches="tight")
plt.close()
print("saved plot_water_level.png")


# ── Figure 2: Raw Sensor Voltage ─────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(13, 4))
ax.plot(mv_30s.index, mv_30s.values, color="#3fb950", linewidth=1.0, label="mV (30s mean)")
add_state_bands(ax, state_changes, mv_30s.min(), mv_30s.max())
ax.set_title("Pressure Sensor Voltage (30 s Rolling Mean)", fontsize=13, fontweight="bold", pad=10)
ax.set_ylabel("Millivolts (mV)")
ax.xaxis.set_major_formatter(DATE_FMT)
ax.grid(True, axis="both")
ax.legend(fontsize=8)
fig.autofmt_xdate()
fig.tight_layout()
fig.savefig(os.path.join(OUT_DIR, "plot_sensor_voltage.png"), dpi=150, bbox_inches="tight")
plt.close()
print("saved plot_sensor_voltage.png")


# ── Figure 3: Heap Memory ────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(13, 4))
ax.plot(heap_df.index, heap_df["free"] / 1024, color="#58a6ff", linewidth=1.0, label="Free heap")
ax.plot(heap_df.index, heap_df["min_free"] / 1024, color="#ffa657", linewidth=1.0, linestyle="--", label="Min free (low-water mark)")
ax.plot(heap_df.index, heap_df["max_block"] / 1024, color="#3fb950", linewidth=1.0, linestyle=":", label="Max contiguous block")
add_state_bands(ax, state_changes, 0, heap_df["free"].max() / 1024)
ax.set_title("ESP32 Heap Memory Over Time", fontsize=13, fontweight="bold", pad=10)
ax.set_ylabel("Memory (KB)")
ax.xaxis.set_major_formatter(DATE_FMT)
ax.grid(True, axis="both")
ax.legend(fontsize=8)
fig.autofmt_xdate()
fig.tight_layout()
fig.savefig(os.path.join(OUT_DIR, "plot_heap.png"), dpi=150, bbox_inches="tight")
plt.close()
print("saved plot_heap.png")


# ── Figure 4: Event Timeline ─────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(13, 3.5))
ax.set_xlim(status_df.index[0], status_df.index[-1])
ax.set_ylim(-0.5, 2.5)
ax.set_yticks([0, 1, 2])
ax.set_yticklabels(["State", "Tier 1", "Tier 2"], fontsize=9)

# state bands
in_emergency = False
start = None
for ts, frm, to in state_changes:
    if to == "EMERGENCY":
        in_emergency = True
        start = ts
        ax.axvline(ts, color="#f85149", linewidth=1.2, alpha=0.7, ymax=0.45)
    elif to == "NORMAL" and in_emergency:
        ax.axvline(ts, color="#3fb950", linewidth=1.2, alpha=0.7, ymax=0.45)
        ax.axvspan(start, ts, ymin=0, ymax=0.45, color="#f85149", alpha=0.15)
        in_emergency = False

TIER_COLORS = {
    "Tier 1 Emergency conditions detected": ("#ffa657", 1, "^"),
    "Tier 1 Emergency conditions cleared":  ("#ffa657", 1, "v"),
    "Tier 2 URGENT Emergency conditions detected": ("#f85149", 2, "^"),
    "Tier 2 URGENT Emergency conditions cleared":  ("#f85149", 2, "v"),
}
for ts, desc in events:
    for key, (color, y, marker) in TIER_COLORS.items():
        if key in desc:
            ax.scatter(ts, y, color=color, marker=marker, s=60, zorder=5)

ax.xaxis.set_major_formatter(DATE_FMT)
ax.grid(True, axis="x")
ax.set_title("Event Timeline", fontsize=13, fontweight="bold", pad=10)

legend_handles = [
    mpatches.Patch(color="#f85149", alpha=0.5, label="EMERGENCY state"),
    plt.Line2D([0], [0], color="#f85149", marker="^", linestyle="None", markersize=7, label="Tier 2 detected"),
    plt.Line2D([0], [0], color="#f85149", marker="v", linestyle="None", markersize=7, label="Tier 2 cleared"),
    plt.Line2D([0], [0], color="#ffa657", marker="^", linestyle="None", markersize=7, label="Tier 1 detected"),
    plt.Line2D([0], [0], color="#ffa657", marker="v", linestyle="None", markersize=7, label="Tier 1 cleared"),
]
ax.legend(handles=legend_handles, fontsize=8, loc="upper right")
fig.autofmt_xdate()
fig.tight_layout()
fig.savefig(os.path.join(OUT_DIR, "plot_events.png"), dpi=150, bbox_inches="tight")
plt.close()
print("saved plot_events.png")


# ── Stats for markdown ────────────────────────────────────────────────────────
duration = status_df.index[-1] - status_df.index[0]
peak_level = status_df["water_level"].max()
peak_ts_str = status_df["water_level"].idxmax().strftime("%H:%M:%S")
num_tier1 = sum(1 for _, d in events if "Tier 1 Emergency conditions detected" in d)
num_tier2 = sum(1 for _, d in events if "Tier 2 URGENT Emergency conditions detected" in d)
mean_free_kb = heap_df["free"].mean() / 1024
min_free_kb = heap_df["min_free"].min() / 1024
total_mv_readings = len(mv_rows)

print(f"\nDuration: {duration}")
print(f"Peak water level: {peak_level:.2f} cm at {peak_ts_str}")
print(f"Tier 1 events: {num_tier1}, Tier 2 events: {num_tier2}")
print(f"Avg free heap: {mean_free_kb:.1f} KB, Min free heap: {min_free_kb:.1f} KB")
print(f"Total ADC readings: {total_mv_readings:,}")
