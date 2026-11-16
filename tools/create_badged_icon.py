# Copyright (C) Explorer++ Project
# SPDX-License-Identifier: GPL-3.0-only

import argparse
from pathlib import Path

from PIL import Image, ImageFilter


SIZES = (16, 20, 24, 32, 40, 48, 64, 256)


def compose(folder, original, size):
    canvas = folder.ico.getimage((size, size)).convert("RGBA")
    badge_size = max(7, round(size * 0.31))
    available = sorted(width for width, height in original.ico.sizes() if width == height)
    selected = next((width for width in available if width >= badge_size), available[-1])
    badge = original.ico.getimage((selected, selected)).convert("RGBA")
    badge = badge.crop(badge.getbbox())
    badge.thumbnail((badge_size, badge_size), Image.Resampling.LANCZOS)
    stroke = max(1, round(size / 128))
    padding = stroke + max(1, round(size / 128))
    layer = Image.new("RGBA", (badge.width + padding * 2, badge.height + padding * 2))
    layer.alpha_composite(badge, (padding, padding))
    outline_alpha = layer.getchannel("A").filter(ImageFilter.MaxFilter(stroke * 2 + 1))
    outline = Image.new("RGBA", layer.size, (255, 255, 255, 255))
    outline.putalpha(outline_alpha)
    outline.alpha_composite(layer)
    inset = max(0, round(size / 128))
    canvas.alpha_composite(outline, (size - layer.width - inset, size - layer.height - inset))
    return canvas


def main():
    parser = argparse.ArgumentParser(description="Add the original Explorer++ badge to a folder icon.")
    parser.add_argument("--folder", required=True, type=Path, help="Folder ICO with all eight output sizes")
    parser.add_argument("--badge", type=Path, default=Path(__file__).parent / "icons" / "explorerplusplus-original.ico")
    parser.add_argument("--output", required=True, type=Path, help="Destination ICO")
    parser.add_argument("--preview", type=Path, help="Optional 256px PNG preview")
    args = parser.parse_args()
    with Image.open(args.folder) as folder, Image.open(args.badge) as original:
        if not {(size, size) for size in SIZES}.issubset(folder.ico.sizes()):
            parser.error("The folder ICO must contain 16, 20, 24, 32, 40, 48, 64, and 256px frames.")
        frames = {size: compose(folder, original, size) for size in SIZES}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    frames[256].save(
        args.output,
        format="ICO",
        bitmap_format="bmp",
        sizes=[(size, size) for size in SIZES],
        append_images=[frames[size] for size in SIZES if size != 256],
    )
    with Image.open(args.output) as saved:
        if saved.ico.sizes() != {(size, size) for size in SIZES}:
            raise RuntimeError("Saved icon sizes do not match the rendered frames.")
        for size in SIZES:
            if saved.ico.getimage((size, size)).convert("RGBA").tobytes() != frames[size].tobytes():
                raise RuntimeError(f"Saved {size}px frame differs from the rendered image.")
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        frames[256].save(args.preview)
    print(f"Created {args.output} with {len(SIZES)} independently rendered sizes")


if __name__ == "__main__":
    main()
