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


# ── Figure 5: Zoom — Tier 1 oscillation near end of drain ───────────────────
# Window: 18:05 – 18:10 to show lead-up, oscillation cluster, and NORMAL aftermath
ZOOM_START = datetime(2026, 5, 2, 18, 5, 0)
ZOOM_END   = datetime(2026, 5, 2, 18, 10, 0)
TIER1_THRESHOLD = 20.0
TIER1_HYSTERESIS = 1.5  # illustrative only — not yet in firmware

# Tier 1 events in window
t1_detected = [ts for ts, d in events if "Tier 1 Emergency conditions detected" in d and ZOOM_START <= ts <= ZOOM_END]
t1_cleared  = [ts for ts, d in events if "Tier 1 Emergency conditions cleared"  in d and ZOOM_START <= ts <= ZOOM_END]

# State transitions in window
state_in_window = [(ts, frm, to) for ts, frm, to in state_changes if ZOOM_START <= ts <= ZOOM_END]

# Water level from STATUS in window
zoom_status = status_df[(status_df.index >= ZOOM_START) & (status_df.index <= ZOOM_END)]

fig, ax = plt.subplots(figsize=(13, 5))

# EMERGENCY state shading
in_emergency = any(
    s == "EMERGENCY" for _, s, _, _, _ in status_rows
    if datetime.fromisoformat("2026-05-02 18:05:00") > datetime.fromisoformat("2026-05-02 16:17:00")
)
# Reconstruct state at ZOOM_START from full state_changes list
current_state = "NORMAL"
emerg_start = None
for ts, frm, to in state_changes:
    if ts <= ZOOM_START:
        current_state = to
        if to == "EMERGENCY":
            emerg_start = ts
        elif to == "NORMAL":
            emerg_start = None

if current_state == "EMERGENCY" and emerg_start:
    # shade from window start until a NORMAL transition or window end
    emerg_end = ZOOM_END
    for ts, frm, to in state_in_window:
        if to == "NORMAL":
            emerg_end = ts
            break
    ax.axvspan(ZOOM_START, emerg_end, color="#f85149", alpha=0.10, zorder=0, label="EMERGENCY state")

# Water level trace
ax.plot(zoom_status.index, zoom_status["water_level"],
        color="#58a6ff", linewidth=1.4, label="Water level", zorder=3)

# Tier 1 set threshold
ax.axhline(TIER1_THRESHOLD, color="#ffa657", linewidth=1.2, linestyle="--",
           label=f"Tier 1 set threshold ({TIER1_THRESHOLD} cm)", zorder=2)

# Illustrative hysteresis clear threshold
ax.axhline(TIER1_THRESHOLD - TIER1_HYSTERESIS, color="#ffa657", linewidth=0.9,
           linestyle=":", alpha=0.6,
           label=f"Proposed clear threshold (−{TIER1_HYSTERESIS} cm hysteresis)", zorder=2)
ax.fill_between([ZOOM_START, ZOOM_END],
                TIER1_THRESHOLD - TIER1_HYSTERESIS, TIER1_THRESHOLD,
                color="#ffa657", alpha=0.04, zorder=1)

# Tier 1 event markers
for ts in t1_detected:
    ax.scatter(ts, TIER1_THRESHOLD + 0.25, color="#ffa657", marker="^", s=70, zorder=6)
for ts in t1_cleared:
    ax.scatter(ts, TIER1_THRESHOLD - 0.25, color="#ffa657", marker="v", s=70, zorder=6)

# State transition lines and annotation
for ts, frm, to in state_in_window:
    color = "#3fb950" if to == "NORMAL" else "#f85149"
    ax.axvline(ts, color=color, linewidth=1.5, linestyle="-", alpha=0.9, zorder=5)
    ax.text(ts, ax.get_ylim()[0] if ax.get_ylim()[0] > 0 else 18.5,
            f" → {to}", color=color, fontsize=7.5, va="bottom", rotation=90, zorder=6)

# Annotate the oscillation cluster
if t1_cleared:
    cluster_ts = t1_cleared[-1]  # last clear before state transition
    ax.annotate("3× oscillation\n~19.99↔20.01 cm\nin < 2 s",
                xy=(cluster_ts, TIER1_THRESHOLD),
                xytext=(25, 30), textcoords="offset points",
                color="#ffa657", fontsize=8,
                arrowprops=dict(arrowstyle="->", color="#ffa657", lw=0.9),
                bbox=dict(boxstyle="round,pad=0.3", facecolor="#161b22", edgecolor="#ffa657", alpha=0.85))

# Annotate EMERGENCY_TIMEOUT_MS bracket (last Tier 1 clear → state transition)
if t1_cleared and state_in_window:
    last_clear_ts = t1_cleared[-1]
    state_ts = state_in_window[0][0]
    y_bracket = TIER1_THRESHOLD - 1.0
    ax.annotate("", xy=(state_ts, y_bracket), xytext=(last_clear_ts, y_bracket),
                arrowprops=dict(arrowstyle="<->", color="#8b949e", lw=1.1))
    mid_ts = last_clear_ts + (state_ts - last_clear_ts) / 2
    ax.text(mid_ts, y_bracket - 0.15, "EMERGENCY_TIMEOUT_MS\n(5 s)", color="#8b949e",
            fontsize=7.5, ha="center", va="top")

ax.set_xlim(ZOOM_START, ZOOM_END)
ax.set_ylim(18.0, 21.5)
ax.set_title("Zoom: Tier 1 Oscillation at End of Drain (~18:07)", fontsize=13, fontweight="bold", pad=10)
ax.set_ylabel("Water Level (cm)")
ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
ax.xaxis.set_major_locator(mdates.MinuteLocator(interval=1))
ax.grid(True, axis="both")

legend_handles_zoom = [
    mpatches.Patch(color="#f85149", alpha=0.3, label="EMERGENCY state"),
    plt.Line2D([0], [0], color="#58a6ff", linewidth=1.4, label="Water level"),
    plt.Line2D([0], [0], color="#ffa657", linestyle="--", label=f"Tier 1 set threshold ({TIER1_THRESHOLD} cm)"),
    plt.Line2D([0], [0], color="#ffa657", linestyle=":", alpha=0.7,
               label=f"Proposed clear threshold (−{TIER1_HYSTERESIS} cm)"),
    plt.Line2D([0], [0], color="#ffa657", marker="^", linestyle="None", markersize=7, label="Tier 1 detected"),
    plt.Line2D([0], [0], color="#ffa657", marker="v", linestyle="None", markersize=7, label="Tier 1 cleared"),
    plt.Line2D([0], [0], color="#3fb950", linewidth=1.5, label="→ NORMAL transition"),
]
ax.legend(handles=legend_handles_zoom, fontsize=8, loc="upper right")
fig.autofmt_xdate()
fig.tight_layout()
fig.savefig(os.path.join(OUT_DIR, "plot_tier1_zoom.png"), dpi=150, bbox_inches="tight")
plt.close()
print("saved plot_tier1_zoom.png")


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
