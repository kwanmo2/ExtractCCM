"""Generates a synthetic point-mapping sample: samples/point_mapping/*.

reference.png  8-bit sRGB "phone photo": ColorChecker colors with a warm cast and +-30 %
               per-patch brightness error (like reflections / exposure differences)
camera.png     12-bit raw in a 16-bit PNG, black level 64: the same colors in a different
               layout, seen through a desaturating camera response
session.ccmproj  point-mapping session with all 24 pairs (6 gray)
Usage: python tools/make_point_mapping_sample.py   (needs numpy)
"""
import json
import os
import struct
import zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "samples", "point_mapping")

LAB = np.array([
    [37.54, 14.37, 14.92], [64.66, 19.27, 17.50], [49.32, -3.82, -22.54], [43.46, -12.74, 22.72],
    [54.94, 9.61, -24.79], [70.48, -32.26, -0.37], [62.73, 35.83, 56.50], [39.43, 10.75, -45.17],
    [50.57, 48.64, 16.67], [30.10, 22.54, -20.87], [71.77, -24.13, 58.19], [71.51, 18.24, 67.37],
    [28.37, 15.42, -49.80], [54.38, -39.72, 32.27], [42.43, 51.05, 28.62], [81.80, 2.67, 80.41],
    [50.63, 51.28, -14.12], [49.57, -29.71, -28.32], [95.19, -1.03, 2.93], [81.29, -0.57, 0.44],
    [66.89, -0.75, -0.06], [50.76, -0.13, 0.14], [35.63, -0.46, -0.48], [20.64, 0.07, -0.46]])
NAMES = ["Dark Skin", "Light Skin", "Blue Sky", "Foliage", "Blue Flower", "Bluish Green", "Orange",
         "Purplish Blue", "Moderate Red", "Purple", "Yellow Green", "Orange Yellow", "Blue", "Green", "Red",
         "Yellow", "Magenta", "Cyan", "White", "Neutral 8", "Neutral 6.5", "Neutral 5", "Neutral 3.5", "Black"]


def lab_to_linear_srgb(lab):
    w50, w65 = np.array([0.96422, 1, 0.82521]), np.array([0.95047, 1, 1.08883])
    fy = (lab[:, 0] + 16) / 116
    fx, fz = fy + lab[:, 1] / 500, fy - lab[:, 2] / 200
    finv = lambda f: np.where(f ** 3 > 216 / 24389, f ** 3, (116 * f - 16) / (24389 / 27))
    xyz = np.stack([finv(fx), finv(fy), finv(fz)], 1) * w50
    ma = np.array([[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]])
    cat = np.linalg.inv(ma) @ np.diag((ma @ w65) / (ma @ w50)) @ ma
    m = np.array([[3.2404542, -1.5371385, -0.4985314], [-0.9692660, 1.8760108, 0.0415560],
                  [0.0556434, -0.2040259, 1.0572252]])
    return (m @ cat @ xyz.T).T


def write_png(path, img):
    h, w, _ = img.shape
    depth = 16 if img.dtype == np.uint16 else 8
    data = img.astype(">u2") if depth == 16 else img.astype(np.uint8)
    raw = b"".join(b"\x00" + data[y].tobytes() for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(5)
    rgb = lab_to_linear_srgb(LAB)

    # reference: 6x4 grid, warm cast, per-patch brightness error, sRGB encoded
    rh, rw, size = 800, 1200, 120
    ref = np.full((rh, rw, 3), 0.18)
    ref_centers = []
    bright = rng.uniform(0.7, 1.3, 24)
    cast = np.array([1.12, 1.0, 0.80])
    for i in range(24):
        cx, cy = 150 + (i % 6) * 180, 130 + (i // 6) * 180
        ref[cy - size // 2:cy + size // 2, cx - size // 2:cx + size // 2] = np.clip(rgb[i] * cast * bright[i], 0, 1)
        ref_centers.append([cx, cy])
    ref += rng.normal(0, 0.004, ref.shape)
    enc = np.where(ref <= 0.0031308, 12.92 * ref, 1.055 * np.clip(ref, 0, 1) ** (1 / 2.4) - 0.055)
    write_png(os.path.join(OUT, "reference.png"), np.clip(np.rint(enc * 255), 0, 255).astype(np.uint8))

    # camera: shuffled 4x6 layout, desaturating response, 12-bit raw with black level
    ch, cw, size, black, white = 1000, 1600, 140, 64, 4095
    cam_m = np.array([[0.62, 0.28, 0.10], [0.22, 0.58, 0.20], [0.08, 0.30, 0.40]]) * np.array([1.0, 0.8, 0.45])[:, None]
    order = rng.permutation(24)
    cam = np.full((ch, cw, 3), black + 60.0)
    cam_centers = [None] * 24
    for slot, i in enumerate(order):
        cx, cy = 140 + (slot % 4) * 380, 110 + (slot // 4) * 160
        val = black + np.clip(cam_m @ rgb[i] * 0.85, 0, 1) * (white - black)
        cam[cy - size // 2:cy + size // 2, cx - size // 2:cx + size // 2] = val
        cam_centers[i] = [cx, cy]
    cam += rng.normal(0, 6, cam.shape)
    write_png(os.path.join(OUT, "camera.png"), np.clip(np.rint(cam), 0, 65535).astype(np.uint16))

    pairs = [{"name": NAMES[i], "ref": ref_centers[i], "cam": cam_centers[i], "neutral": i >= 18, "enabled": True}
             for i in range(24)]
    session = {
        "mode": 1, "image": "camera.png", "image_is_raw": False, "data_bits": 12,
        "black_mode": 0, "black": [64, 64, 64], "clip_percent": 98, "output_space": "srgb",
        "model": 0, "objective": 0, "separate_gain": True, "bound_coefficients": True,
        "coef_min": -16, "coef_max": 16, "offset_mode": 0, "neutral_weight": 1, "quantize": False,
        "point_mapping": {"reference_image": "reference.png", "reference_encoding": 0, "ref_half_size": 30,
                          "cam_half_size": 35, "neutralize_reference": True, "match_brightness": False,
                          "pairs": pairs},
    }
    json.dump(session, open(os.path.join(OUT, "session.ccmproj"), "w"), indent=2)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
