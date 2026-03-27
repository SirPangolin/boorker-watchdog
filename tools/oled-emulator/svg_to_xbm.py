#!/usr/bin/env python3
"""Convert SVG to XBM C array for u8g2 OLED display.

Usage: uv run --with cairosvg --with Pillow svg_to_xbm.py <input.svg> [--size 48] [--threshold 128] [--invert]

Renders SVG at high resolution, downscales with averaging, thresholds to 1-bit,
and outputs both an ASCII pixel map and a C array for display_logo.c.
"""

import argparse
import io
import sys

try:
    import cairosvg
    from PIL import Image
except ImportError:
    print("Run with: uv run --with cairosvg --with Pillow svg_to_xbm.py ...")
    sys.exit(1)


def svg_to_bitmap(svg_path, size, threshold, invert):
    """Render SVG to monochrome bitmap."""
    # Render at 10x for better downscaling
    hi_res = size * 10
    png_data = cairosvg.svg2png(
        url=svg_path,
        output_width=hi_res,
        output_height=hi_res,
        background_color="black",
    )

    img = Image.open(io.BytesIO(png_data))
    # Downscale with high-quality resampling
    img = img.resize((size, size), Image.LANCZOS)
    # Convert to grayscale
    gray = img.convert("L")

    # Threshold to 1-bit
    pixels = []
    for y in range(size):
        row = []
        for x in range(size):
            val = gray.getpixel((x, y))
            bit = 1 if val >= threshold else 0
            if invert:
                bit = 1 - bit
            row.append(bit)
        pixels.append(row)

    return pixels


def print_ascii(pixels):
    """Print ASCII art pixel map."""
    size = len(pixels)
    # Header with column numbers every 10
    tens = "".join(str(x // 10) if x % 10 == 0 else " " for x in range(size))
    ones = "".join(str(x % 10) for x in range(size))
    print(f"     {tens}")
    print(f"     {ones}")
    print(f"     {'─' * size}")

    for y, row in enumerate(pixels):
        line = "".join("██" if p else "  " for p in row)
        # Also show compact version
        compact = "".join("#" if p else "." for p in row)
        print(f"{y:3d} |{compact}|")


def pixels_to_xbm_bytes(pixels):
    """Convert pixel grid to XBM byte array (LSB first)."""
    size = len(pixels)
    bytes_per_row = (size + 7) // 8
    result = []

    for y in range(size):
        for byte_idx in range(bytes_per_row):
            byte_val = 0
            for bit in range(8):
                x = byte_idx * 8 + bit
                if x < size and pixels[y][x]:
                    byte_val |= 1 << bit
            result.append(byte_val)

    return result


def print_c_array(xbm_bytes, size, name="millie_logo"):
    """Print C array for display_logo.c."""
    bytes_per_row = (size + 7) // 8

    print(f"\nconst int {name}_width = {size};")
    print(f"const int {name}_height = {size};")
    print(f"\nconst uint8_t {name}_xbm[] = {{")

    for y in range(size):
        offset = y * bytes_per_row
        row_bytes = xbm_bytes[offset : offset + bytes_per_row]
        hex_str = ", ".join(f"0x{b:02X}" for b in row_bytes)
        print(f"    {hex_str},  /* row {y} */")

    print("};")


def main():
    parser = argparse.ArgumentParser(description="Convert SVG to XBM for OLED")
    parser.add_argument("svg", help="Input SVG file")
    parser.add_argument("--size", type=int, default=48, help="Output size (default: 48)")
    parser.add_argument(
        "--threshold",
        type=int,
        default=80,
        help="Grayscale threshold 0-255 (default: 80, lower = more white pixels)",
    )
    parser.add_argument("--invert", action="store_true", help="Invert black/white")
    args = parser.parse_args()

    pixels = svg_to_bitmap(args.svg, args.size, args.threshold, args.invert)

    print(f"=== {args.size}x{args.size} pixel map (threshold={args.threshold}) ===\n")
    print_ascii(pixels)

    xbm_bytes = pixels_to_xbm_bytes(pixels)
    print_c_array(xbm_bytes, args.size)

    # Stats
    on_count = sum(sum(row) for row in pixels)
    total = args.size * args.size
    print(f"\n/* {on_count}/{total} pixels ON ({100*on_count/total:.0f}%) */")


if __name__ == "__main__":
    main()
