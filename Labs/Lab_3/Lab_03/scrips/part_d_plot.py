#!/usr/bin/env python3
from math import ceil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path("/home/elaina/embedded_lab3")
MAX_LATENCY_US = 200
WIDTH = 1600
HEIGHT = 1040
THREAD_COLORS = ("#2E75B6", "#C55A11", "#70AD47", "#8064A2")


def read_histogram(path):
    bins = [[0] * (MAX_LATENCY_US + 1) for _ in range(4)]
    summary = {}
    for line in path.read_text().splitlines():
        if line.startswith("# Min Latencies:"):
            summary["min"] = [int(value) for value in line.split(":", 1)[1].split()]
        elif line.startswith("# Avg Latencies:"):
            summary["avg"] = [int(value) for value in line.split(":", 1)[1].split()]
        elif line.startswith("# Max Latencies:"):
            summary["max"] = [int(value) for value in line.split(":", 1)[1].split()]
        elif line.startswith("# Histogram Overflows:"):
            summary["overflows"] = [
                int(value) for value in line.split(":", 1)[1].split()
            ]
        elif line and line[0].isdigit():
            fields = [int(value) for value in line.split()]
            if len(fields) != 5:
                raise ValueError(f"Unexpected histogram row in {path}: {line}")
            latency = fields[0]
            for thread, count in enumerate(fields[1:]):
                if latency <= MAX_LATENCY_US:
                    bins[thread][latency] += count
    if set(summary) != {"min", "avg", "max", "overflows"}:
        raise ValueError(f"Missing summary values in {path}: {summary}")
    if any(len(values) != 4 for values in summary.values()):
        raise ValueError(f"Expected four thread values in {path}: {summary}")
    return bins, summary


def load_font(size):
    font_path = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    return ImageFont.truetype(str(font_path), size)


def draw_panel(draw, top, title, bins, summary, font, small_font):
    left, right = 110, WIDTH - 55
    graph_top, graph_bottom = top + 65, top + 365
    graph_width = right - left
    graph_height = graph_bottom - graph_top
    max_count = max(max(thread_bins) for thread_bins in bins)
    tick_step = max(1, ceil(max_count / 4 / 500)) * 500
    y_limit = tick_step * 4
    scale_x = graph_width / MAX_LATENCY_US

    draw.text((left, top), title, fill="#111111", font=font)
    for count in range(0, y_limit + 1, tick_step):
        y = graph_bottom - round(count / y_limit * graph_height)
        draw.line((left, y, right, y), fill="#D9D9D9", width=1)
        draw.text((left - 12, y - 10), str(count), fill="#444444",
                  font=small_font, anchor="ra")

    for latency in range(MAX_LATENCY_US + 1):
        x = left + round(latency * scale_x)
        for thread, thread_bins in enumerate(bins):
            count = thread_bins[latency]
            if count == 0:
                continue
            x_offset = round(thread * scale_x / 4)
            bar_x = x + x_offset
            bar_y = graph_bottom - round(count / y_limit * graph_height)
            draw.rectangle(
                (bar_x, bar_y, bar_x + max(1, round(scale_x / 5)), graph_bottom),
                fill=THREAD_COLORS[thread],
            )

    draw.line((left, graph_top, left, graph_bottom), fill="#222222", width=2)
    draw.line((left, graph_bottom, right, graph_bottom), fill="#222222", width=2)
    for latency in range(0, MAX_LATENCY_US + 1, 25):
        x = left + round(latency * scale_x)
        draw.line((x, graph_bottom, x, graph_bottom + 6), fill="#222222", width=1)
        draw.text((x, graph_bottom + 10), str(latency), fill="#333333",
                  font=small_font, anchor="ma")

    legend_y = graph_top + 6
    for thread, color in enumerate(THREAD_COLORS):
        x = left + thread * 145
        draw.rectangle((x, legend_y, x + 20, legend_y + 14), fill=color)
        draw.text((x + 27, legend_y - 4), f"Thread {thread}", fill="#222222",
                  font=small_font)

    overflows = ", ".join(str(value) for value in summary["overflows"])
    maxima = ", ".join(str(value) for value in summary["max"])
    draw.text(
        (left, graph_bottom + 38),
        f"Latency (us)  |  overflow samples/thread: {overflows}"
        f"  |  measured max (us): {maxima}",
        fill="#222222",
        font=small_font,
    )


def main():
    standard, standard_summary = read_histogram(
        ROOT / "logs/part_d/hist_standard.txt"
    )
    stress, stress_summary = read_histogram(ROOT / "logs/part_d/hist_stress.txt")
    image = Image.new("RGB", (WIDTH, HEIGHT), "white")
    draw = ImageDraw.Draw(image)
    font = load_font(25)
    small_font = load_font(16)
    draw.text((55, 22), "Cyclictest latency histograms (0-200 us)",
              fill="#111111", font=load_font(30))
    draw_panel(draw, 75, "Standard kernel - no synthetic stress load",
               standard, standard_summary, font, small_font)
    draw_panel(draw, 540, "Standard kernel - stress-ng CPU/I/O load, 30 seconds",
               stress, stress_summary, font, small_font)
    output = ROOT / "logs/part_d/latency_histogram.png"
    if output.exists():
        raise SystemExit(f"Refusing to overwrite {output}")
    image.save(output)
    print(output)


if __name__ == "__main__":
    main()
