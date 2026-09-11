# -*- coding: utf-8 -*-
"""
Echo DIY — 自动转写服务
监听 recordings 目录, 发现新 .wav 自动用 faster-whisper 转写为中文文本
输出: 同名 .txt 文件 + 控制台打印

用法:
  python transcribe.py [录音目录]
  例: python transcribe.py recordings
"""
import os, sys, time, glob
import wave
import numpy as np
import noisereduce as nr
from scipy.signal import butter, sosfilt
from faster_whisper import WhisperModel

# Windows 控制台可能是 GBK, 强制 UTF-8 输出避免 print 崩溃
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

DIR = sys.argv[1] if len(sys.argv) > 1 else "recordings"
os.makedirs(DIR, exist_ok=True)

print("加载 faster-whisper small 模型 ...", flush=True)
model = WhisperModel("small", device="cpu", compute_type="int8")
print(f"模型就绪, 监听 {DIR} (Ctrl+C 退出)", flush=True)


def load_and_denoise(path):
    """读 WAV → float32 [-1,1], 最强去噪:
    1) 带通滤波 80Hz-7kHz (去低频隆隆 + 高频摩擦刺耳)
    2) noisereduce 强降噪 prop_decrease=0.95 (前 0.5s 作噪声样本)
    """
    with wave.open(path, "rb") as wf:
        rate = wf.getframerate()
        n = wf.getnframes()
        data = np.frombuffer(wf.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0

    # 1. 带通滤波 (语音主要能量 300Hz-3.4kHz, 留余量)
    sos = butter(4, [80, 7000], btype="bandpass", fs=rate, output="sos")
    data = sosfilt(sos, data).astype(np.float32)

    # 2. 最强降噪
    noise_len = min(int(rate * 0.5), len(data))
    if noise_len > 0 and len(data) > noise_len:
        data = nr.reduce_noise(y=data, sr=rate, y_noise=data[:noise_len],
                               prop_decrease=0.95, n_fft=2048)

    # 3. 峰值归一化 (滤波会衰减, 拉回满幅避免 whisper 误判音量)
    peak = np.max(np.abs(data))
    if peak > 1e-6:
        data = data / peak * 0.9
    return data, rate


seen = set()
while True:
    for wav in sorted(glob.glob(os.path.join(DIR, "*.wav"))):
        if wav in seen:
            continue
        seen.add(wav)
        txt_path = wav[:-4] + ".txt"
        if os.path.exists(txt_path):
            continue
        print(f"[转写] {os.path.basename(wav)} ...", flush=True)
        try:
            audio, rate = load_and_denoise(wav)
            segments, info = model.transcribe(audio, language="zh", beam_size=5, vad_filter=True)
            text = "".join(s.text for s in segments).strip()
            with open(txt_path, "w", encoding="utf-8") as f:
                f.write(text)
            print(f"[完成] {os.path.basename(wav)} -> {text}", flush=True)
        except Exception as e:
            print(f"[失败] {os.path.basename(wav)}: {e}", flush=True)
    time.sleep(2)