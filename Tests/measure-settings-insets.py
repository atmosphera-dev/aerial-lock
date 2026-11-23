#!/usr/bin/env python3
"""Check client-frame edge insets from native captures and named QML geometry."""

import argparse
import json
from pathlib import Path

from PIL import Image


EDGE = 24


def distance(left, right):
    return max(abs(a - b) for a, b in zip(left, right))


def edge_pixels(image, left, right, excluded=None):
    """Count non-background pixels in the four strips, ignoring LCD fringes."""
    px = image.load()
    background = px[left + 1, 1]
    height = image.height
    count = 0
    for y in range(height):
        outer = y < EDGE or y >= height - EDGE
        xs = range(left, right) if outer else (*range(left, left + EDGE), *range(right - EDGE, right))
        for x in xs:
            if excluded:
                bx, by, bw, bh = excluded
                if bx <= x < bx + bw and by <= y < by + bh:
                    continue
            pixel = px[x, y]
            delta = distance(pixel, background)
            if delta <= 3:
                continue
            fringe = delta < 96 and (max(pixel) - min(pixel) > 12 or delta < 16)
            if fringe and any(left <= n < right and distance(px[n, y], background) > delta for n in (x - 1, x + 1)):
                continue
            count += 1
    return count


def frame(row, directory, reference):
    image = Image.open(directory / row["image"]).convert("RGB")
    named = json.loads((directory / row["geometry"]).read_text())
    errors = []

    def check(condition, label):
        if not condition:
            errors.append(label)

    def rect(name):
        for item in named:
            if item["name"] == name:
                return tuple(item[key] for key in ("x", "y", "width", "height"))
        raise ValueError(f"missing {name}")

    def equal(actual, target):
        return abs(actual - target) <= 0.25

    width, height = image.size
    check((width, height) == (row["width"], row["height"]), "client size")
    if reference:
        with Image.open(reference) as approved:
            check(approved.size == image.size, "reference size")
    sx, sy, sw, sh = rect("sidebar")
    x, y, w, h = rect("sidebarColumn")
    sb = [x - sx, y - sy, sx + sw - x - w, sy + sh - y - h]
    check(all(equal(gap, EDGE) for gap in sb), "sidebar column")
    for item in named:
        if item["name"].startswith("navigation-"):
            check(equal(item["x"], x) and equal(item["width"], w), item["name"])
    pane_x, pane_y, pane_w, pane_h = rect("pagePane")
    measurements = {"sidebar": sb}
    names = {item["name"] for item in named}
    bar = None
    if "pageViewport" in names:
        vx, vy, vw, vh = rect("pageViewport")
        viewport = [vy - pane_y, pane_y + pane_h - vy - vh]
        check(all(equal(gap, EDGE) for gap in viewport), "viewport edges")
        vp = next(item for item in named if item["name"] == "pageViewport")
        check(equal(vp["contentX"], 0), "horizontal scroll")
        extent = vp["contentHeight"] - vh
        if row["name"].endswith("-middle"):
            check(0 < vp["contentY"] < extent, "middle position")
        if row["name"].endswith("-end"):
            check(extent > 0 and equal(vp["contentY"], extent), "end position")
        cx, _, cw, _ = rect("pageColumn")
        column = [cx - vx, vx + vw - cx - cw]
        check(equal(column[0], EDGE) and equal(column[1], max(EDGE, vw - EDGE - 620)), "column edges")
        measurements.update(viewport=viewport, column=column, content_y=vp["contentY"], scroll_range=extent)
        if "pageScrollBar" in names and extent > 0:
            bar = rect("pageScrollBar")
            bx, by, bw, bh = bar
            gaps = [by - pane_y, pane_y + pane_h - by - bh, width - bx - bw, bx - cx - cw]
            check(equal(gaps[0], EDGE) and equal(gaps[1], EDGE) and equal(gaps[2], 8) and gaps[3] >= 8, "scrollbar lane")
            measurements["scrollbar"] = gaps

    popups = [item for item in named if item["name"].startswith("popupSurface-")]
    for popup in popups:
        px, py, pw, ph = (popup[key] for key in ("x", "y", "width", "height"))
        gaps = [px, py, width - px - pw, height - py - ph]
        check(all(gap >= EDGE - .25 for gap in gaps), popup["name"])
        measurements[popup["name"]] = gaps
    if not any(item.get("modal", False) for item in popups):
        counts = [edge_pixels(image, 0, round(sw)), edge_pixels(image, round(pane_x), width, bar)]
        check(not any(counts), "painted edge pixels")
        measurements["painted_edge_pixels"] = counts
    return {"name": row["name"], "scheme": row["scheme"], "measurements": measurements, "errors": errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    suffix = "_aerial-settings-revamp-"
    reference = {file.stem.split(suffix)[-1]: file for file in args.reference.glob("*.png")}
    expected = {key for key in reference if not key.startswith("tray-")}
    if not expected:
        parser.error("no reference window frames")
    rows = json.loads(args.manifest.read_text())["frames"]
    results, seen = [], set()
    for row in rows:
        key = row["name"] + "-" + row["scheme"]
        if key in seen:
            parser.error(f"duplicate frame: {key}")
        seen.add(key)
        results.append(frame(row, args.manifest.parent, reference.get(key)))
    missing = sorted(expected - seen)
    passed = bool(results) and not missing and all(not result["errors"] for result in results)
    args.output.write_text(json.dumps({"frames": results, "missing": missing, "pass": passed}, indent=2) + "\n")
    print(f"{'ALL-PASS' if passed else 'FAIL'}: {len(results)} frames, {len(missing)} missing")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
