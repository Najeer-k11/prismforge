#!/usr/bin/env python3
"""Generates PrismForge's built-in channel textures (resources/textures/*.png).

Every texture is synthesised here from a fixed seed, so the set is
reproducible and free of third-party licensing. All textures tile seamlessly.

    python3 tools/generate_textures.py [output-dir]

Requires numpy and Pillow. Orientation: PrismForge flips images vertically on
upload by default ("Flip Y"), so texel row 0 in GL is the *bottom* row of the
PNG. Textures whose content depends on the vertical direction (the RGBA noise
channel offset, the water normal map, the UV grid labels) are generated in GL
orientation and flipped just before writing, so they are correct with Flip Y on.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "resources" / "textures"


def rng(seed):
    return np.random.default_rng(seed)


def to_u8(a):
    return np.clip(np.rint(a * 255.0), 0, 255).astype(np.uint8)


def normalize(a, lo_pct=0.5, hi_pct=99.5):
    lo, hi = np.percentile(a, [lo_pct, hi_pct])
    return np.clip((a - lo) / (hi - lo), 0.0, 1.0)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def gl_to_image(a):
    """Rows of `a` are bottom-up (GL); PNG rows are top-down."""
    return a[::-1]


def save(name, array, mode):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{name}.png"
    Image.fromarray(np.ascontiguousarray(array), mode).save(path, optimize=True)
    print(f"{path.name:24s} {array.shape[1]}x{array.shape[0]} {mode:4s} {path.stat().st_size // 1024} KiB")


def spectral(n, beta, r, anisotropy=None):
    """Tileable fractal noise by spectral synthesis: power spectrum ~ 1/f^beta."""
    white = r.standard_normal((n, n))
    fx = np.fft.fftfreq(n)[None, :]
    fy = np.fft.fftfreq(n)[:, None]
    if anisotropy is not None:
        fx = fx * anisotropy[0]
        fy = fy * anisotropy[1]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0
    spectrum = np.fft.fft2(white) * f ** (-beta / 2.0)
    spectrum[0, 0] = 0.0
    return normalize(np.real(np.fft.ifft2(spectrum)))


def voronoi(n, points, r):
    """Toroidal Worley noise: (F1, F2, id value) per pixel, distances in cells."""
    pts = r.random((points, 2)) * n
    ids = r.random(points)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float64)
    f1 = np.full((n, n), np.inf)
    f2 = np.full((n, n), np.inf)
    cell = np.zeros((n, n))
    for (px, py), value in zip(pts, ids):
        dx = np.abs(xx - px)
        dy = np.abs(yy - py)
        dx = np.minimum(dx, n - dx)
        dy = np.minimum(dy, n - dy)
        d = np.sqrt(dx * dx + dy * dy)
        closer = d < f1
        f2 = np.where(closer, f1, np.minimum(f2, d))
        cell = np.where(closer, value, cell)
        f1 = np.where(closer, d, f1)
    spacing = n / np.sqrt(points)
    return f1 / spacing, f2 / spacing, cell


def blur(a, radius):
    """Periodic Gaussian blur (keeps tiling)."""
    n = a.shape[0]
    f = np.fft.fftfreq(n)
    g = np.exp(-2.0 * (np.pi * radius) ** 2 * (f[None, :] ** 2 + f[:, None] ** 2))
    if a.ndim == 3:
        return np.stack([np.real(np.fft.ifft2(np.fft.fft2(a[..., c]) * g)) for c in range(a.shape[2])], -1)
    return np.real(np.fft.ifft2(np.fft.fft2(a) * g))


def palette(t, stops):
    """Piecewise-linear colour ramp; stops = [(position, (r, g, b)), ...]."""
    pos = np.array([s[0] for s in stops])
    out = np.zeros(t.shape + (3,))
    for c in range(3):
        out[..., c] = np.interp(t, pos, [s[1][c] for s in stops])
    return out


# ---------------------------------------------------------------- noise

def rgba_noise(n, seed):
    """White noise where G(x, y) = R(x - 37, y - 17) and A likewise from B (GL
    texel coordinates). Lets shaders build fast 3D value noise from one 2D
    lookup: uv = p.xy + vec2(37, 17) * floor(p.z), then mix(.y, .x, fract(p.z))."""
    r = rng(seed)
    red = r.integers(0, 256, (n, n), dtype=np.uint8)
    blue = r.integers(0, 256, (n, n), dtype=np.uint8)
    green = np.roll(red, shift=(17, 37), axis=(0, 1))   # out[y, x] = in[y - 17, x - 37]
    alpha = np.roll(blue, shift=(17, 37), axis=(0, 1))
    save(f"rgba-noise-{n}", gl_to_image(np.stack([red, green, blue, alpha], -1)), "RGBA")


def gray_noise(n, seed):
    save(f"gray-noise-{n}", rng(seed).integers(0, 256, (n, n), dtype=np.uint8), "L")


def blue_noise(n, seed, sigma=1.9):
    """Void-and-cluster blue noise (Ulichney); value = rank / n^2."""
    r = rng(seed)
    d = np.minimum(np.arange(n), n - np.arange(n)).astype(np.float64)
    kernel = np.exp(-(d[None, :] ** 2 + d[:, None] ** 2) / (2.0 * sigma * sigma))
    kernel_f = np.fft.fft2(kernel)

    def energy(mask):
        return np.real(np.fft.ifft2(np.fft.fft2(mask) * kernel_f))

    def shifted(i):
        y, x = divmod(i, n)
        return np.roll(kernel, (y, x), (0, 1))

    count = n * n
    initial = count // 10
    mask = np.zeros((n, n))
    mask.flat[r.choice(count, initial, replace=False)] = 1.0
    e = energy(mask)
    # Relax the initial pattern: move the tightest cluster into the largest void.
    while True:
        cluster = int(np.argmax(np.where(mask > 0, e, -np.inf)))
        mask.flat[cluster] = 0.0
        e -= shifted(cluster)
        void = int(np.argmin(np.where(mask == 0, e, np.inf)))
        if void == cluster:
            mask.flat[cluster] = 1.0
            e += shifted(cluster)
            break
        mask.flat[void] = 1.0
        e += shifted(void)

    rank = np.zeros(count, dtype=np.int64)
    prototype, proto_e = mask.copy(), e.copy()
    for k in range(initial - 1, -1, -1):          # phase 1: remove clusters
        cluster = int(np.argmax(np.where(prototype > 0, proto_e, -np.inf)))
        prototype.flat[cluster] = 0.0
        proto_e -= shifted(cluster)
        rank[cluster] = k
    for k in range(initial, count):              # phases 2-3: fill voids
        void = int(np.argmin(np.where(mask == 0, e, np.inf)))
        mask.flat[void] = 1.0
        e += shifted(void)
        rank[void] = k
    save(f"blue-noise-{n}", to_u8(rank.reshape(n, n) / (count - 1)), "L")


def fbm_noise(n, seed):
    save(f"fbm-noise-{n}", to_u8(spectral(n, 2.0, rng(seed))), "L")


def clouds(n, seed):
    r = rng(seed)
    base = spectral(n, 2.8, r)
    detail = spectral(n, 1.8, r)
    c = smoothstep(0.35, 0.9, 0.75 * base + 0.25 * detail)
    save(f"clouds-{n}", to_u8(c), "L")


def cells(n, seed):
    f1, f2, cell = voronoi(n, 64, rng(seed))
    edge = np.clip((f2 - f1) * 1.5, 0.0, 1.0)
    rgb = np.stack([np.clip(f1, 0.0, 1.0), edge, cell], -1)
    save(f"cells-{n}", to_u8(rgb), "RGB")


# ---------------------------------------------------------------- materials

def rock(n, seed):
    r = rng(seed)
    large = spectral(n, 2.2, r)
    medium = spectral(n, 1.7, r)
    strata = spectral(n, 2.0, r, anisotropy=(0.25, 1.0))
    f1, f2, _ = voronoi(n, 90, r)
    cracks = smoothstep(0.0, 0.07, f2 - f1)
    grain = normalize(blur(r.standard_normal((n, n)), 0.7))

    t = 0.45 * large + 0.3 * medium + 0.25 * strata
    color = palette(t, [(0.0, (0.20, 0.18, 0.17)), (0.35, (0.38, 0.35, 0.32)),
                        (0.65, (0.52, 0.49, 0.45)), (0.85, (0.62, 0.58, 0.52)),
                        (1.0, (0.70, 0.67, 0.62))])
    lichen = smoothstep(0.62, 0.8, spectral(n, 2.1, r)) * 0.35
    color = color * (1 - lichen[..., None]) + np.array([0.42, 0.44, 0.30]) * lichen[..., None]
    color *= (0.55 + 0.45 * cracks)[..., None]
    color *= (0.85 + 0.3 * grain)[..., None]
    save(f"rock-{n}", to_u8(np.clip(color, 0, 1)), "RGB")


def ground(n, seed):
    r = rng(seed)
    patches = spectral(n, 2.4, r)
    grass = smoothstep(0.38, 0.62, patches)
    blades = normalize(blur(r.standard_normal((n, n)), 0.6))
    dirt_t = spectral(n, 1.8, r)
    dirt = palette(dirt_t, [(0.0, (0.22, 0.16, 0.11)), (0.5, (0.36, 0.27, 0.18)), (1.0, (0.48, 0.38, 0.27))])
    green = palette(0.6 * blades + 0.4 * spectral(n, 2.0, r),
                    [(0.0, (0.10, 0.17, 0.05)), (0.5, (0.22, 0.34, 0.10)), (1.0, (0.40, 0.48, 0.18))])
    f1, f2, cell = voronoi(n, 220, r)
    pebble = smoothstep(0.30, 0.18, f1) * (cell > 0.85) * 0.8
    color = dirt * (1 - grass[..., None]) + green * grass[..., None]
    stone = palette(cell, [(0.0, (0.38, 0.36, 0.34)), (1.0, (0.58, 0.55, 0.50))])
    shade = (1.0 - 0.5 * f1)[..., None]
    color = color * (1 - pebble[..., None]) + stone * shade * pebble[..., None]
    save(f"ground-{n}", to_u8(np.clip(color, 0, 1)), "RGB")


def wood(n, seed):
    r = rng(seed)
    warp = normalize(blur(spectral(n, 3.0, r), 8.0))
    fine = spectral(n, 1.3, r, anisotropy=(1.0, 0.08))
    y = np.arange(n)[:, None] / n
    rings = 0.5 + 0.5 * np.sin(2.0 * np.pi * (12.0 * y + 1.5 * warp))
    rings = rings ** 2
    t = 0.7 * rings + 0.3 * fine
    color = palette(t, [(0.0, (0.62, 0.42, 0.24)), (0.5, (0.45, 0.28, 0.14)), (1.0, (0.26, 0.14, 0.06))])
    save(f"wood-{n}", to_u8(np.broadcast_to(color, (n, n, 3))), "RGB")


def water_normal(n, seed):
    """Normal map (OpenGL convention: +Y up in texture space, Flip Y on) of a
    wind-driven ripple height field; RGB = normal * 0.5 + 0.5."""
    r = rng(seed)
    fx = np.fft.fftfreq(n)[None, :] * n
    fy = np.fft.fftfreq(n)[:, None] * n
    k = np.sqrt(fx * fx + fy * fy)
    k[0, 0] = 1.0
    wind = np.array([0.8, 0.6])
    align = np.abs((fx * wind[0] + fy * wind[1]) / k) ** 2
    spectrum = np.exp(-1.0 / (k / 6.0) ** 2) / k ** 3.2 * (0.25 + align) * np.exp(-(k / 90.0) ** 2)
    noise = r.standard_normal((n, n)) + 1j * r.standard_normal((n, n))
    h = np.real(np.fft.ifft2(noise * np.sqrt(spectrum)))
    h = (h - h.mean()) / h.std()
    strength = 2.5
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength   # rows are bottom-up (GL)
    normal = np.stack([-dx, -dy, np.ones_like(h)], -1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    save(f"water-normal-{n}", gl_to_image(to_u8(normal * 0.5 + 0.5)), "RGB")


def uv_grid(n):
    """8x8 checker with labels; A1 is the bottom-left cell when Flip Y is on."""
    cells_per_side = 8
    size = n // cells_per_side
    img = Image.new("RGB", (n, n))
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.load_default(size=size // 3)
    except TypeError:
        font = ImageFont.load_default()
    for row in range(cells_per_side):          # row 0 = bottom
        for col in range(cells_per_side):
            hue = (col + row * 0.5) / cells_per_side
            base = np.array([0.5 + 0.5 * np.cos(2 * np.pi * (hue + o)) for o in (0.0, 1 / 3, 2 / 3)])
            light = 0.75 if (row + col) % 2 == 0 else 0.45
            color = tuple(int(c * light * 255) for c in base)
            x0, y0 = col * size, (cells_per_side - 1 - row) * size
            draw.rectangle([x0, y0, x0 + size - 1, y0 + size - 1], fill=color)
            label = f"{chr(ord('A') + col)}{row + 1}"
            draw.text((x0 + size / 2, y0 + size / 2), label, fill=(255, 255, 255), font=font, anchor="mm")
    arr = np.array(img)
    arr[::size // 4, :] = arr[::size // 4, :] // 2 + 64
    arr[:, ::size // 4] = arr[:, ::size // 4] // 2 + 64
    arr[::size, :] = 255
    arr[:, ::size] = 255
    save(f"uv-grid-{n}", arr, "RGB")


def main():
    rgba_noise(256, 1)
    rgba_noise(64, 2)
    gray_noise(256, 3)
    blue_noise(128, 4)
    fbm_noise(512, 5)
    clouds(512, 6)
    cells(256, 7)
    rock(512, 8)
    ground(512, 9)
    wood(512, 10)
    water_normal(512, 11)
    uv_grid(512)


if __name__ == "__main__":
    main()
