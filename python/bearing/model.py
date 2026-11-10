"""Tiny 1-D CNN for bearing condition. Written so every layer maps 1:1 onto a
TFLite Micro op (CONV_2D, MEAN, FULLY_CONNECTED), which lets export.py build the
int8 .tflite directly without TensorFlow."""
from __future__ import annotations

import torch
from torch import nn

# (out_channels, kernel, stride) per conv layer; input is (B, 1, 512)
CONVS = [(8, 16, 4), (16, 8, 2), (32, 8, 2)]
N_CLASSES = 4


class Net(nn.Module):
    def __init__(self):
        super().__init__()
        layers, cin = [], 1
        for cout, k, s in CONVS:
            layers += [nn.Conv1d(cin, cout, k, s), nn.BatchNorm1d(cout), nn.ReLU()]
            cin = cout
        self.features = nn.Sequential(*layers)
        self.fc = nn.Linear(cin, N_CLASSES)

    def forward(self, x):  # x: (B, 1, 512), z-scored
        return self.fc(self.features(x).mean(dim=-1))


def macs(win: int = 512) -> int:
    t, cin, total = win, 1, 0
    for cout, k, s in CONVS:
        t = (t - k) // s + 1
        total += t * cout * cin * k
        cin = cout
    return total + cin * N_CLASSES
