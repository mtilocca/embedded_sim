"""Export the trained net: ONNX (float) and a fully-int8 TFLite model.

TensorFlow cannot run on every dev machine (its wheels need AVX), so instead of
onnx2tf / ai-edge-torch we write the .tflite flatbuffer ourselves. The model is
only CONV_2D x3 -> MEAN -> FULLY_CONNECTED, so this is short, and the result is
checked against the reference TFLite interpreter (ai-edge-litert) in evaluate.py.

Quantisation scheme = the TFLite int8 spec that TFLite Micro kernels expect:
  activations: per-tensor asymmetric int8
  conv weights: per-output-channel symmetric int8 (zero point 0)
  fc weights: per-tensor symmetric int8
  biases: int32 with scale = input_scale * weight_scale
Usage: python export.py
"""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import torch
from ai_edge_litert import schema_py_generated as S
import flatbuffers

from bearing import data, model

OUT = Path(__file__).parent / "artifacts"
CALIB_PCT = 99.9  # clip activation ranges at this percentile (outliers hurt int8 resolution)


def fold_bn(net: model.Net):
    """Return [(W[Cout,K,Cin], b[Cout], stride)] for the convs and (Wfc, bfc)."""
    mods = list(net.features)
    convs = []
    for i in range(0, len(mods), 3):
        c, bn = mods[i], mods[i + 1]
        scale = bn.weight / torch.sqrt(bn.running_var + bn.eps)
        w = c.weight * scale[:, None, None]
        b = (c.bias - bn.running_mean) * scale + bn.bias
        convs.append((w.detach().numpy(), b.detach().numpy(), c.stride[0]))
    return convs, (net.fc.weight.detach().numpy(), net.fc.bias.detach().numpy())


def float_forward(x, convs, fc, record=None):
    """numpy float forward of the folded net; records activation tensors."""
    a = x  # (B, 1, T)
    acts = [a]
    for w, b, s in convs:
        cout, cin, k = w.shape
        t = (a.shape[-1] - k) // s + 1
        idx = np.arange(k)[None, :] + s * np.arange(t)[:, None]       # (t, k)
        patches = a[:, :, idx]                                         # (B, cin, t, k)
        a = np.einsum("bctk,ock->bot", patches, w) + b[None, :, None]
        a = np.maximum(a, 0)
        acts.append(a)
    m = a.mean(-1)
    acts.append(m)
    logits = m @ fc[0].T + fc[1]
    acts.append(logits)
    return logits, acts


def qparams(lo, hi):
    lo, hi = min(lo, 0.0), max(hi, 0.0)
    scale = max((hi - lo) / 255.0, 1e-8)
    zp = int(round(-128 - lo / scale))
    return float(scale), int(np.clip(zp, -128, 127))


def quant_w_perchannel(w):  # w: (Cout, ...)
    s = np.abs(w.reshape(w.shape[0], -1)).max(1) / 127.0
    s = np.maximum(s, 1e-12)
    q = np.round(w / s.reshape(-1, *[1] * (w.ndim - 1))).clip(-127, 127).astype(np.int8)
    return q, s.astype(np.float32)


def build_tflite(convs, fc, qin, qacts, qlogit) -> bytes:
    tensors, buffers = [], [S.BufferT()]

    def add_buf(arr: np.ndarray) -> int:
        b = S.BufferT()
        b.data = np.frombuffer(arr.tobytes(), dtype=np.uint8)
        buffers.append(b)
        return len(buffers) - 1

    def add_t(name, shape, dtype, buf=0, scale=None, zp=None, qdim=0):
        t = S.TensorT()
        t.name = name.encode()
        t.shape = list(shape)
        t.type = dtype
        t.buffer = buf
        q = S.QuantizationParametersT()
        q.scale = [float(v) for v in np.atleast_1d(scale)]
        q.zeroPoint = [int(v) for v in np.atleast_1d(zp)]
        q.quantizedDimension = qdim
        t.quantization = q
        tensors.append(t)
        return len(tensors) - 1

    I8, I32 = S.TensorType.INT8, S.TensorType.INT32
    T = data.WIN
    cur = add_t("input", [1, T, 1, 1], I8, scale=qin[0], zp=qin[1])
    inp = cur
    in_scale, ops = qin[0], []
    for li, ((w, b, s), (osc, ozp)) in enumerate(zip(convs, qacts[1:4])):
        cout, cin, k = w.shape
        wq, wsc = quant_w_perchannel(w)
        wq = wq.transpose(0, 2, 1)[:, :, None, :]                      # OHWI
        bq = np.round(b / (in_scale * wsc)).astype(np.int32)
        tw = add_t(f"conv{li}_w", wq.shape, I8, add_buf(wq), wsc, np.zeros(cout), 0)
        tb = add_t(f"conv{li}_b", bq.shape, I32, add_buf(bq), in_scale * wsc, np.zeros(cout), 0)
        T = (T - k) // s + 1
        to = add_t(f"conv{li}_out", [1, T, 1, cout], I8, scale=osc, zp=ozp)
        o = S.OperatorT()
        o.opcodeIndex = 0
        o.inputs, o.outputs = [cur, tw, tb], [to]
        o.builtinOptionsType = S.BuiltinOptions.Conv2DOptions
        c = S.Conv2DOptionsT()
        c.padding, c.strideW, c.strideH = S.Padding.VALID, 1, s
        c.fusedActivationFunction = S.ActivationFunctionType.RELU
        c.dilationWFactor = c.dilationHFactor = 1
        o.builtinOptions = c
        ops.append(o)
        cur, in_scale = to, osc

    # MEAN over H and W; output quant == input quant so the kernel stays integer.
    axes = add_t("mean_axes", [2], I32, add_buf(np.array([1, 2], np.int32)), 1.0, 0)
    tm = add_t("mean_out", [1, convs[-1][0].shape[0]], I8, scale=qacts[3][0], zp=qacts[3][1])
    o = S.OperatorT()
    o.opcodeIndex, o.inputs, o.outputs = 1, [cur, axes], [tm]
    o.builtinOptionsType = S.BuiltinOptions.ReducerOptions
    r = S.ReducerOptionsT()
    r.keepDims = False
    o.builtinOptions = r
    ops.append(o)

    wf, bf = fc
    wsc = max(np.abs(wf).max() / 127.0, 1e-12)
    wfq = np.round(wf / wsc).clip(-127, 127).astype(np.int8)
    bfq = np.round(bf / (qacts[3][0] * wsc)).astype(np.int32)
    tfw = add_t("fc_w", wfq.shape, I8, add_buf(wfq), wsc, 0)
    tfb = add_t("fc_b", bfq.shape, I32, add_buf(bfq), qacts[3][0] * wsc, 0)
    tout = add_t("logits", [1, wf.shape[0]], I8, scale=qlogit[0], zp=qlogit[1])
    o = S.OperatorT()
    o.opcodeIndex, o.inputs, o.outputs = 2, [tm, tfw, tfb], [tout]
    o.builtinOptionsType = S.BuiltinOptions.FullyConnectedOptions
    f = S.FullyConnectedOptionsT()
    f.fusedActivationFunction = S.ActivationFunctionType.NONE
    o.builtinOptions = f
    ops.append(o)

    def opcode(code, ver):
        oc = S.OperatorCodeT()
        oc.builtinCode = oc.deprecatedBuiltinCode = code
        oc.version = ver
        return oc

    sg = S.SubGraphT()
    sg.tensors, sg.operators = tensors, ops
    sg.inputs, sg.outputs = [inp], [tout]
    sg.name = b"main"
    m = S.ModelT()
    m.version = 3
    m.operatorCodes = [opcode(S.BuiltinOperator.CONV_2D, 3),
                       opcode(S.BuiltinOperator.MEAN, 2),
                       opcode(S.BuiltinOperator.FULLY_CONNECTED, 4)]
    m.subgraphs, m.buffers = [sg], buffers
    m.description = b"bearing-cnn int8"
    fb = flatbuffers.Builder(1024)
    fb.Finish(m.Pack(fb), b"TFL3")
    return bytes(fb.Output())


def main():
    net = model.Net()
    net.load_state_dict(torch.load(OUT / "net.pt"))
    net.eval()

    # ---- ONNX (float) -------------------------------------------------------
    torch.onnx.export(net, torch.zeros(1, 1, data.WIN), OUT / "bearing_cnn.onnx",
                      input_names=["window"], output_names=["logits"], opset_version=17,
                      dynamo=False)

    # ---- calibration ---------------------------------------------------------
    xr, y, _ = data.train_set()
    rng = np.random.default_rng(0)
    sel = rng.choice(len(xr), 4000, replace=False)
    x = data.normalise(xr[sel]).astype(np.float32)[:, None, :]
    convs, fc = fold_bn(net)
    logits, acts = float_forward(x, convs, fc)

    def rng_of(a):
        return np.percentile(a, 100 - CALIB_PCT), np.percentile(a, CALIB_PCT)

    qin = qparams(*rng_of(acts[0]))
    # ReLU outputs: lower bound is 0; use the max percentile
    qacts = [qin] + [qparams(0.0, float(np.percentile(a, CALIB_PCT))) for a in acts[1:4]]
    # mean output shares conv3's quant params (index 3 = conv3 out); keep alias for build_tflite
    qlogit = qparams(float(logits.min()), float(logits.max()))
    tfl = build_tflite(convs, fc, qin, qacts, qlogit)
    (OUT / "bearing_cnn_int8.tflite").write_bytes(tfl)

    meta = {"classes": data.CLASSES, "window": data.WIN, "fs_hz": data.FS,
            "input_scale": qin[0], "input_zero_point": qin[1],
            "logit_scale": qlogit[0], "logit_zero_point": qlogit[1],
            "macs": model.macs(), "tflite_bytes": len(tfl),
            "params": int(sum(p.numel() for p in net.parameters()))}
    (OUT / "model_meta.json").write_text(json.dumps(meta, indent=2))
    print(json.dumps(meta, indent=2))


if __name__ == "__main__":
    main()
