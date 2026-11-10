"""CWRU bearing dataset: loading, windowing and the load-based train/test split."""
from __future__ import annotations

from pathlib import Path

import numpy as np
from scipy.io import loadmat

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "data"))
from fetch_cwru import manifest  # noqa: E402

RAW = Path(__file__).resolve().parents[1] / "data" / "raw"
CLASSES = ["normal", "inner", "ball", "outer"]
FS = 12_000          # Hz, sampling rate of the files we use
WIN = 512            # samples per inference window (42.7 ms at 12 kHz)
TRAIN_LOADS = (0, 1, 2)
TEST_LOADS = (3,)


def load_signal(file_id: int) -> np.ndarray:
    m = loadmat(RAW / f"{file_id}.mat")
    key = next(k for k in m if k.endswith("_DE_time"))
    return m[key].ravel().astype(np.float32)


def normalise(w: np.ndarray) -> np.ndarray:
    """Per-window z-score. Identical maths runs on the MCU, which removes any
    dependence on sensor gain / motor load amplitude."""
    w = w - w.mean(axis=-1, keepdims=True)
    return w / (w.std(axis=-1, keepdims=True) + 1e-6)


def windows(sig: np.ndarray, stride: int) -> np.ndarray:
    n = (len(sig) - WIN) // stride + 1
    idx = np.arange(WIN)[None, :] + stride * np.arange(n)[:, None]
    return sig[idx]


def build(loads: tuple[int, ...], stride: int):
    """Returns raw windows (N, WIN), labels (N,), and metadata dicts."""
    xs, ys, meta = [], [], []
    for row in manifest():
        if row["load_hp"] not in loads:
            continue
        w = windows(load_signal(row["file"]), stride)
        xs.append(w)
        ys.append(np.full(len(w), CLASSES.index(row["cls"])))
        meta.append(np.full(len(w), row["file"]))
    return np.concatenate(xs), np.concatenate(ys), np.concatenate(meta)


def train_set():
    # 50 % overlap is fine for training; the test load is never seen.
    return build(TRAIN_LOADS, WIN // 2)


def test_set():
    # Non-overlapping windows on the held-out load.
    return build(TEST_LOADS, WIN)
