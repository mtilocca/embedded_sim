"""Train the bearing-condition CNN. Usage: python train.py"""
from __future__ import annotations

import json
import time
from pathlib import Path

import numpy as np
import torch
from torch import nn

from bearing import data, model

OUT = Path(__file__).parent / "artifacts"
EPOCHS, BATCH, LR, SEED = 25, 256, 3e-3, 0


def augment(x: torch.Tensor) -> torch.Tensor:
    # small random gain-free noise; windows are z-scored so amplitude is fixed
    return x + 0.05 * torch.randn_like(x)


def main():
    torch.manual_seed(SEED)
    np.random.seed(SEED)
    OUT.mkdir(exist_ok=True)

    xr, y, _ = data.train_set()
    x = data.normalise(xr).astype(np.float32)
    print(f"train windows {x.shape}, per class {np.bincount(y)}")
    xt, yt = torch.from_numpy(x)[:, None, :], torch.from_numpy(y)

    # class-balanced sampling (healthy windows are over/under-represented)
    w = 1.0 / np.bincount(y)[y]
    sampler = torch.utils.data.WeightedRandomSampler(w, len(y), replacement=True)
    dl = torch.utils.data.DataLoader(torch.utils.data.TensorDataset(xt, yt), BATCH, sampler=sampler)

    net = model.Net()
    opt = torch.optim.AdamW(net.parameters(), LR, weight_decay=1e-3)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, LR, total_steps=EPOCHS * len(dl))
    lossf = nn.CrossEntropyLoss()
    hist = []
    t0 = time.time()
    for ep in range(EPOCHS):
        net.train()
        tot = correct = n = 0
        for xb, yb in dl:
            opt.zero_grad()
            out = net(augment(xb))
            loss = lossf(out, yb)
            loss.backward()
            opt.step()
            sched.step()
            tot += loss.item() * len(yb)
            correct += (out.argmax(1) == yb).sum().item()
            n += len(yb)
        hist.append({"epoch": ep + 1, "loss": tot / n, "train_acc": correct / n})
        print(f"epoch {ep+1:2d} loss {tot/n:.4f} acc {correct/n:.4f}", flush=True)

    torch.save(net.state_dict(), OUT / "net.pt")
    (OUT / "history.json").write_text(json.dumps(hist))
    print(f"trained in {time.time()-t0:.0f}s, {sum(p.numel() for p in net.parameters())} params")


if __name__ == "__main__":
    main()
