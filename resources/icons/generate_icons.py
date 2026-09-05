"""
Generates high-resolution multi-size FluidCore Windows icon (.ico) and PNG assets.
Visual design: Sleek modern aesthetic with deep indigo canvas, layered pages, and
a dynamic cyan fluid core curve.
"""

import math
from PIL import Image, ImageDraw

def create_fluidcore_image(size: int) -> Image.Image:
    # 4x supersampling for ultra-crisp antialiased render
    scale = 4
    canvas_size = size * scale
    img = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    pad = canvas_size * 0.08
    r = canvas_size * 0.22

    # Background squircle / rounded rect: Deep rich indigo (#111625 -> #1E293B)
    bg_box = [pad, pad, canvas_size - pad, canvas_size - pad]
    draw.rounded_rectangle(bg_box, radius=r, fill=(20, 26, 44, 255))

    # Subtle inner border glow
    draw.rounded_rectangle(bg_box, radius=r, outline=(56, 189, 248, 80), width=int(scale * 1.5))

    # Base page representation: back card (tilted / offset)
    back_card = [
        canvas_size * 0.26,
        canvas_size * 0.22,
        canvas_size * 0.74,
        canvas_size * 0.78
    ]
    draw.rounded_rectangle(back_card, radius=canvas_size * 0.06, fill=(30, 41, 59, 230), outline=(71, 85, 105, 180), width=int(scale))

    # Front main card (active document canvas)
    front_card = [
        canvas_size * 0.22,
        canvas_size * 0.28,
        canvas_size * 0.70,
        canvas_size * 0.84
    ]
    draw.rounded_rectangle(front_card, radius=canvas_size * 0.06, fill=(248, 250, 252, 255), outline=(148, 163, 184, 255), width=int(scale * 1.2))

    # Horizontal document text lines on front card
    line_x0 = canvas_size * 0.30
    line_x1 = canvas_size * 0.62
    for y_rel in [0.38, 0.45, 0.52]:
        y = canvas_size * y_rel
        draw.line([line_x0, y, line_x1, y], fill=(203, 213, 225, 255), width=int(scale * 1.8))
    draw.line([line_x0, canvas_size * 0.59, canvas_size * 0.48, canvas_size * 0.59], fill=(203, 213, 225, 255), width=int(scale * 1.8))

    # Dynamic "Fluid Core" cyan/indigo wave curve across the card
    # Quadratic bezier / sine wave
    wave_points = []
    steps = 100
    for i in range(steps + 1):
        t = i / steps
        # S-curve from bottom-left to top-right
        x = canvas_size * (0.28 + 0.48 * t)
        y = canvas_size * (0.76 - 0.42 * t + 0.12 * math.sin(t * math.pi * 1.5))
        wave_points.append((x, y))

    # Draw fluid glowing stroke (glow layers + core)
    draw.line(wave_points, fill=(14, 165, 233, 90), width=int(scale * 5.5), joint="curve")
    draw.line(wave_points, fill=(56, 189, 248, 200), width=int(scale * 3.5), joint="curve")
    draw.line(wave_points, fill=(255, 255, 255, 255), width=int(scale * 1.4), joint="curve")

    # Vibrant anchor nodes (bi-directional link circles)
    p0 = wave_points[0]
    p1 = wave_points[-1]
    node_r = canvas_size * 0.04
    draw.ellipse([p0[0] - node_r, p0[1] - node_r, p0[0] + node_r, p0[1] + node_r], fill=(14, 165, 233, 255), outline=(255, 255, 255, 255), width=int(scale * 1.2))
    draw.ellipse([p1[0] - node_r, p1[1] - node_r, p1[0] + node_r, p1[1] + node_r], fill=(99, 102, 241, 255), outline=(255, 255, 255, 255), width=int(scale * 1.2))

    # Downsample with Lanczos filter for crisp rendering
    return img.resize((size, size), Image.Resampling.LANCZOS)

def main():
    sizes = [256, 128, 64, 48, 32, 16]
    images = [create_fluidcore_image(s) for s in sizes]

    # Save 256x256 PNG
    images[0].save("resources/icons/fluidcore.png", format="PNG")
    print("Saved resources/icons/fluidcore.png")

    # Save multi-resolution Windows ICO
    images[0].save(
        "resources/icons/fluidcore.ico",
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=images[1:]
    )
    print("Saved resources/icons/fluidcore.ico with sizes:", sizes)

if __name__ == "__main__":
    main()
