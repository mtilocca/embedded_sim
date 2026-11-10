| Model | Accuracy | Macro F1 | Anomaly AUROC |
|---|---|---|---|
| PyTorch float32 | 0.9537 | 0.9496 | 1.0000 |
| ONNX Runtime float32 | 0.9537 | 0.9496 | 1.0000 |
| TFLite int8 (device model) | 0.9489 | 0.9442 | 1.0000 |

| Class | Windows | Recall | Precision |
|---|---|---|---|
| normal | 948 | 1.0000 | 1.0000 |
| inner | 715 | 0.7930 | 0.9844 |
| ball | 713 | 1.0000 | 0.8271 |
| outer | 715 | 0.9860 | 1.0000 |

| Property | Value |
|---|---|
| Parameters | 5548 |
| MACs / inference | 183,040 |
| `.tflite` size (int8) | 8.8 KiB |
| Window | 512 samples @ 12000 Hz (42.7 ms) |
| int8 vs float prediction agreement | 0.9945 |
