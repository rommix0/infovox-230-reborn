"""Signal helpers for the Speak panel: waveform envelope and spectrogram (numpy only)."""
import numpy as np

NFFT = 512


def minmax(x, n):
    """split x into n nearly equal columns -> (mins, maxs), each n long; empty columns
    (n > len(x)) repeat the sample before them"""
    if len(x) == 0:
        z = np.zeros(n, dtype=x.dtype)
        return z, z
    starts = np.minimum((np.arange(n) * len(x)) // n, len(x) - 1)
    return np.minimum.reduceat(x, starts), np.maximum.reduceat(x, starts)


def spectrogram(x, fs, win_s=0.006, hop_s=0.002):
    """wide-band spectrogram -> (dB array [frames, NFFT//2 + 1], hop in seconds); frame i
    is centred on sample i*hop"""
    win, hop = max(16, round(win_s * fs)), max(1, round(hop_s * fs))
    x = np.asarray(x, dtype=np.float64)
    pad = np.concatenate([np.zeros(win // 2), x, np.zeros(win)])
    nfr = max(1, -(-len(x) // hop))
    idx = np.arange(win)[None, :] + hop * np.arange(nfr)[:, None]
    frames = pad[idx] * np.hanning(win)[None, :]
    mag = np.abs(np.fft.rfft(frames, NFFT, axis=1))
    return 20 * np.log10(mag + 1e-3), hop / fs


def levels(db, range_db=70.0, fs=None, fmax=8000):
    """dB array [frames, bins] -> uint8 image rows [bins, frames], low frequencies at the
    bottom (last row), the loudest point 255 and anything range_db below it 0; with fs,
    only the bins up to fmax Hz"""
    if fs and fmax:
        db = db[:, :min(db.shape[1], int(fmax / (fs / 2) * (db.shape[1] - 1)) + 1)]
    top = db.max()
    v = np.clip((db - (top - range_db)) / range_db, 0, 1) * 255
    return np.ascontiguousarray(v.T[::-1].astype(np.uint8))
