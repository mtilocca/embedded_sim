"""Evaluate float / ONNX / int8-TFLite models on the held-out load and make the
figures + tables used in the README. Usage: python evaluate.py"""
from __future__ import annotations

import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import onnxruntime as ort
import torch
from ai_edge_litert.interpreter import Interpreter
from sklearn.metrics import confusion_matrix, roc_auc_score, roc_curve, f1_score

from bearing import data, model

ART = Path(__file__).parent / "artifacts"
FIG = Path(__file__).parent / "figures"
FIG.mkdir(exist_ok=True)
plt.rcParams.update({"figure.dpi": 130, "axes.grid": True, "grid.alpha": .3})


def run_tflite(x: np.ndarray, meta: dict) -> np.ndarray:
    it = Interpreter(model_path=str(ART / "bearing_cnn_int8.tflite"))
    it.allocate_tensors()
    i, o = it.get_input_details()[0], it.get_output_details()[0]
    s, z = meta["input_scale"], meta["input_zero_point"]
    out = []
    for w in x:
        q = np.clip(np.round(w / s) + z, -128, 127).astype(np.int8)
        it.set_tensor(i["index"], q.reshape(1, -1, 1, 1))
        it.invoke()
        r = it.get_tensor(o["index"])[0].astype(np.float32)
        out.append((r - meta["logit_zero_point"]) * meta["logit_scale"])
    return np.array(out)


def softmax(l):
    e = np.exp(l - l.max(1, keepdims=True))
    return e / e.sum(1, keepdims=True)


def metrics(name, logits, y):
    p = softmax(logits)
    pred = p.argmax(1)
    anom = 1 - p[:, 0]
    return {"name": name,
            "accuracy": float((pred == y).mean()),
            "macro_f1": float(f1_score(y, pred, average="macro")),
            "auroc_anomaly": float(roc_auc_score(y != 0, anom)),
            "cm": confusion_matrix(y, pred).tolist(),
            "anom": anom, "pred": pred}


def main():
    meta = json.loads((ART / "model_meta.json").read_text())
    xr, y, files = data.test_set()
    x = data.normalise(xr).astype(np.float32)
    print(f"test windows {x.shape}, per class {np.bincount(y)}")

    net = model.Net()
    net.load_state_dict(torch.load(ART / "net.pt"))
    net.eval()
    with torch.no_grad():
        lf = net(torch.from_numpy(x)[:, None, :]).numpy()
    sess = ort.InferenceSession(str(ART / "bearing_cnn.onnx"))
    lo = np.concatenate([sess.run(None, {"window": x[i:i+1, None, :]})[0] for i in range(len(x))])
    lq = run_tflite(x, meta)

    res = [metrics("PyTorch float32", lf, y), metrics("ONNX Runtime float32", lo, y),
           metrics("TFLite int8 (device model)", lq, y)]
    agree = float((res[2]["pred"] == res[0]["pred"]).mean())

    # ---- tables ---------------------------------------------------------------
    rows = ["| Model | Accuracy | Macro F1 | Anomaly AUROC |", "|---|---|---|---|"]
    for r in res:
        rows.append(f"| {r['name']} | {r['accuracy']:.4f} | {r['macro_f1']:.4f} | {r['auroc_anomaly']:.4f} |")
    q = res[2]
    cm = np.array(q["cm"])
    per = ["| Class | Windows | Recall | Precision |", "|---|---|---|---|"]
    for i, c in enumerate(data.CLASSES):
        per.append(f"| {c} | {cm[i].sum()} | {cm[i,i]/cm[i].sum():.4f} | {cm[i,i]/max(cm[:,i].sum(),1):.4f} |")
    foot = ["| Property | Value |", "|---|---|",
            f"| Parameters | {meta['params']} |", f"| MACs / inference | {meta['macs']:,} |",
            f"| `.tflite` size (int8) | {meta['tflite_bytes']/1024:.1f} KiB |",
            f"| Window | {meta['window']} samples @ {meta['fs_hz']} Hz ({meta['window']/meta['fs_hz']*1000:.1f} ms) |",
            f"| int8 vs float prediction agreement | {agree:.4f} |"]
    (ART / "results.md").write_text("\n\n".join(["\n".join(rows), "\n".join(per), "\n".join(foot)]) + "\n")
    (ART / "results.json").write_text(json.dumps(
        [{k: v for k, v in r.items() if k not in ("anom", "pred")} for r in res] + [{"agreement": agree}], indent=1))
    print((ART / "results.md").read_text())

    # ---- figures ---------------------------------------------------------------
    fig, ax = plt.subplots(1, 2, figsize=(9, 3.6))
    cmn = cm / cm.sum(1, keepdims=True)
    im = ax[0].imshow(cmn, cmap="Blues", vmin=0, vmax=1)
    ax[0].set(xticks=range(4), yticks=range(4), xticklabels=data.CLASSES, yticklabels=data.CLASSES,
              xlabel="predicted", ylabel="true", title="int8 model, held-out 3 hp load")
    ax[0].grid(False)
    for i in range(4):
        for j in range(4):
            ax[0].text(j, i, f"{cmn[i,j]:.2f}\n({cm[i,j]})", ha="center", va="center", fontsize=7,
                       color="white" if cmn[i, j] > .5 else "black")
    for r, c in zip(res, ["C0", "C1", "C3"]):
        fpr, tpr, _ = roc_curve(y != 0, r["anom"])
        ax[1].plot(fpr, tpr, c, label=f"{r['name']} (AUC {r['auroc_anomaly']:.3f})", lw=1.5)
    ax[1].set(xlabel="false positive rate", ylabel="true positive rate", title="Anomaly ROC (score = 1 - P(normal))")
    ax[1].legend(fontsize=6, loc="lower right")
    fig.tight_layout(); fig.savefig(FIG / "confusion_roc.png"); plt.close(fig)

    h = json.loads((ART / "history.json").read_text())
    fig, ax = plt.subplots(figsize=(5, 3))
    ax.plot([e["epoch"] for e in h], [e["loss"] for e in h], label="train loss")
    ax2 = ax.twinx(); ax2.grid(False)
    ax2.plot([e["epoch"] for e in h], [e["train_acc"] for e in h], "C1", label="train acc")
    ax.set(xlabel="epoch", ylabel="loss", title="Training"); ax.set_yscale("log")
    fig.tight_layout(); fig.savefig(FIG / "training.png"); plt.close(fig)

    fig, ax = plt.subplots(2, 4, figsize=(11, 4))
    fx = np.fft.rfftfreq(data.WIN, 1 / data.FS)
    for c in range(4):
        w = xr[y == c][5]
        ax[0, c].plot(np.arange(data.WIN) / data.FS * 1000, w, lw=.6); ax[0, c].set_title(data.CLASSES[c])
        ax[1, c].plot(fx, np.abs(np.fft.rfft(w * np.hanning(data.WIN))), lw=.7)
        ax[1, c].set_xlabel("ms" if False else "Hz")
    ax[0, 0].set_ylabel("accel (g)"); ax[1, 0].set_ylabel("|FFT|")
    fig.suptitle("One 512-sample window per class (drive end, 3 hp)")
    fig.tight_layout(); fig.savefig(FIG / "windows.png"); plt.close(fig)

    fig, ax = plt.subplots(figsize=(5, 3))
    for c in range(4):
        ax.hist(q["anom"][y == c], bins=40, range=(0, 1), alpha=.6, label=data.CLASSES[c])
    ax.set(xlabel="anomaly score = 1 - P(normal)", ylabel="windows", title="Score separation (int8)")
    ax.set_yscale("log"); ax.legend(fontsize=7)
    fig.tight_layout(); fig.savefig(FIG / "score_hist.png"); plt.close(fig)


if __name__ == "__main__":
    main()
