# -*- coding: utf-8 -*-
"""
LFE vs screen-channel (L/C/R) analysis engine. Pure numpy/scipy, no GUI.

Principle
---------
Only 10-160 Hz matters, so the audio is low-pass filtered and decimated
(48 kHz -> ~500 Hz) by a *streaming* filter: every input sample is processed
exactly once. All spectral work then happens on a few hundred samples per
channel, which gives sub-Hz frequency resolution for a negligible CPU cost.

The phase relation between two channels cannot be read from a single FFT
frame (one frame always looks 100 % coherent). The engine therefore keeps
time-averaged auto and cross spectra, additionally smoothed over 1/3 octave,
and derives from them:

    interaction  s  = 2 Re(Sxy) / (Sxx + Syy)          in [-1, +1]
    coherence    g2 = |Sxy|^2 / (Sxx Syy)               in [0, 1]
    phase        ph = arg(Sxy)

Each value also comes with ``sigma``, its statistical uncertainty: how far
``s`` can stray from 0 by pure chance for unrelated signals, given the
bandwidth and the amount of audio actually averaged so far.

``s`` is what the electrical/acoustic sum does compared to a plain power
sum: the level change of X + LFE is 10*log10(1 + s) dB. +1 means fully in
phase at equal level (+3 dB), -1 means full cancellation, 0 means the two
signals do not interact (decorrelated, in quadrature, or one of them is
much quieter than the other).
"""

from dataclasses import dataclass

import numpy as np
from scipy.signal import cheby2, sosfilt, sosfreqz

# Channel order expected by the engine
CH_L, CH_C, CH_R, CH_LFE = 0, 1, 2, 3
N_CH = 4

TARGET_FS = 500.0           # decimated sample rate (Hz), approximately
ALIAS_FREE_HZ = 150.0       # band that must stay free of aliasing
NFFT = 2048                 # zero-padded FFT length (fine frequency grid)
BAND_LO, BAND_HI = 8.0, 190.0   # bins kept for the analysis
GRID_LO, GRID_HI = 15.0, 160.0  # log grid of the correlation outputs
GRID_POINTS = 128
SMOOTH_OCT = 1.0 / 3.0      # frequency smoothing of the cross spectra
GATE_LO_DB, GATE_HI_DB = -90.0, -75.0  # level gate (per-band power)
EPS = 1e-30


class Decimator:
    """Streaming anti-alias low-pass + decimation, state kept across blocks."""

    def __init__(self, fs, n_ch=N_CH):
        self.fs_in = float(fs)
        self.factor = max(1, int(round(self.fs_in / TARGET_FS)))
        self.fs_out = self.fs_in / self.factor
        # Everything above (fs_out - ALIAS_FREE_HZ) folds into the useful
        # band, so the stop band starts there (90 dB down).
        stop_hz = self.fs_out - ALIAS_FREE_HZ
        self.sos = cheby2(10, 90.0, stop_hz, fs=self.fs_in, output='sos')
        self.n_ch = n_ch
        self.reset()

    def reset(self):
        self._zi = np.zeros((self.sos.shape[0], 2, self.n_ch))
        self._phase = 0

    def response(self, freqs):
        """Magnitude response of the anti-alias filter at ``freqs`` (Hz)."""
        _, h = sosfreqz(self.sos, worN=np.asarray(freqs, dtype=float),
                        fs=self.fs_in)
        return np.abs(h)

    def process(self, x):
        """x: (n, n_ch) at fs_in  ->  (m, n_ch) at fs_out."""
        x = np.nan_to_num(np.asarray(x, dtype=np.float64))
        if x.shape[0] == 0:
            return np.zeros((0, self.n_ch))
        y, self._zi = sosfilt(self.sos, x, axis=0, zi=self._zi)
        start = (-self._phase) % self.factor
        self._phase = (self._phase + x.shape[0]) % self.factor
        return y[start::self.factor]


@dataclass
class Analysis:
    freqs: np.ndarray          # (nb,) fine frequency axis of the spectra
    facade_db: np.ndarray      # (nb,) L+C+R sum, dBFS (sine peak reads true)
    lfe_db: np.ndarray         # (nb,) LFE, dBFS
    grid: np.ndarray           # (ng,) log frequency axis of what follows
    interaction: np.ndarray    # (4, ng) L, C, R, facade vs LFE, in [-1, 1]
    coherence: np.ndarray      # (4, ng) magnitude-squared coherence [0, 1]
    phase: np.ndarray          # (4, ng) radians, X relative to LFE
    gate: np.ndarray           # (4, ng) 0..1, both signals actually present
    sigma: np.ndarray          # (4, ng) 1-sigma uncertainty of interaction


class LfeEngine:
    def __init__(self, fs=48000.0, window_sec=1.0, facade_trim_db=-3.0):
        self.dec = Decimator(fs)
        self.fs = self.dec.fs_out
        self.facade_gain = 10.0 ** (facade_trim_db / 20.0)
        self.last = None
        self.configure(window_sec)

    # ------------------------------------------------------------------ setup
    def configure(self, window_sec):
        """(Re)build everything that depends on the analysis window length."""
        n = int(round(window_sec * self.fs))
        self.n_win = int(np.clip(n, 32, NFFT))
        self.window_sec = self.n_win / self.fs
        k = np.arange(self.n_win)
        self.window = 0.5 - 0.5 * np.cos(2.0 * np.pi * k / self.n_win)  # Hann

        freqs = np.fft.rfftfreq(NFFT, d=1.0 / self.fs)
        self._band = np.where((freqs >= BAND_LO) & (freqs <= BAND_HI))[0]
        self.freqs = freqs[self._band]

        # A full-scale sine reads 0 dB; anti-alias filter droop is undone.
        norm = self.window.sum() / 2.0
        self._scale = 1.0 / (norm * self.dec.response(self.freqs))

        # Fractional-octave smoothing matrix: fine bins -> log grid.
        self.grid = np.geomspace(GRID_LO, GRID_HI, GRID_POINTS)
        resolution = 2.0 * self.fs / self.n_win          # Hann main lobe
        fwhm = np.maximum(
            self.grid * (2.0 ** (SMOOTH_OCT / 2) - 2.0 ** (-SMOOTH_OCT / 2)),
            resolution)
        sigma = fwhm / 2.355
        self._bandwidth = 1.064 * fwhm       # equivalent width of a Gaussian
        w = np.exp(-0.5 * ((self.freqs[None, :] - self.grid[:, None])
                           / sigma[:, None]) ** 2)
        self._W = w / w.sum(axis=1, keepdims=True)
        self.reset(keep_filter=True)

    def reset(self, keep_filter=False):
        if not keep_filter:
            self.dec.reset()
        nb = len(self.freqs)
        ng = len(self.grid)
        self._buf = np.zeros((self.n_win, N_CH))
        self._pending = 0
        self._p_spec = np.zeros((nb, 2))      # facade, LFE (display)
        # Averaged statistics, all real so one matrix product smooths them:
        # 0-4 power of L, C, R, F, LFE; 5-8 Re, 9-12 Im of X.conj(LFE)
        self._stats = np.zeros((nb, 13))
        self._m1 = np.zeros((ng, 4))          # see _effective_duration
        self._m2 = np.zeros((ng, 4))
        self.last = None

    # ---------------------------------------------------------------- runtime
    def push(self, block):
        """Feed raw audio, shape (n, 4) ordered L, C, R, LFE."""
        y = self.dec.process(block)
        m = y.shape[0]
        if m == 0:
            return
        if m >= self.n_win:
            self._buf = y[-self.n_win:].copy()
        else:
            self._buf = np.concatenate((self._buf[m:], y))
        self._pending += m

    def analyze(self, mutes=(False, False, False), lfe_gain_db=0.0,
                tau_spec=0.15, tau_corr=2.0):
        """Update the averages with the audio pushed since the last call."""
        if self._pending == 0:
            return self.last
        dt = self._pending / self.fs
        self._pending = 0

        x = self._buf - self._buf.mean(axis=0)
        X = np.fft.rfft(x * self.window[:, None], NFFT, axis=0)[self._band]
        X *= self._scale[:, None]
        X[:, :CH_LFE] *= self.facade_gain
        X[:, CH_LFE] *= 10.0 ** (lfe_gain_db / 20.0)

        active = np.array([not m for m in mutes], dtype=float)
        lcr = X[:, :CH_LFE] * active[None, :]
        lfe = X[:, CH_LFE]
        Y = np.concatenate((lcr, lcr.sum(axis=1, keepdims=True)), axis=1)

        now = np.empty_like(self._stats)
        now[:, :4] = Y.real ** 2 + Y.imag ** 2
        now[:, 4] = lfe.real ** 2 + lfe.imag ** 2
        cross = Y * np.conj(lfe)[:, None]
        now[:, 5:9] = cross.real
        now[:, 9:13] = cross.imag

        tau_corr = max(tau_corr, 1e-3)
        a_spec = 1.0 - np.exp(-dt / max(tau_spec, 1e-3))
        a_corr = 1.0 - np.exp(-dt / tau_corr)
        self._p_spec += a_spec * (now[:, 3:5] - self._p_spec)
        self._stats += a_corr * (now - self._stats)

        sm = self._W @ self._stats          # (ng, 13) 1/3-octave smoothed
        px = sm[:, :4]
        pl = sm[:, 4:5]
        c_re = sm[:, 5:9]
        c_im = sm[:, 9:13]
        interaction = 2.0 * c_re / (px + pl + EPS)
        coherence = (c_re ** 2 + c_im ** 2) / (px * pl + EPS)
        low_db = 10.0 * np.log10(np.minimum(px, pl) + EPS)
        g = np.clip((low_db - GATE_LO_DB) / (GATE_HI_DB - GATE_LO_DB), 0, 1)
        gate = g * g * (3.0 - 2.0 * g)

        # How much independent data backs the estimate. The energy-weighted
        # averaging time (Kish effective duration) is short right after an
        # onset: a loud new sound outweighs everything averaged before it.
        sm_now = self._W @ now[:, :5]
        q = np.sqrt(sm_now[:, :4] * sm_now[:, 4:5])
        self._m1 += a_corr * (q - self._m1)
        self._m2 = (self._m2 * np.exp(-2.0 * dt / tau_corr)
                    + q * q * dt / tau_corr ** 2)
        t_eff = self._m1 ** 2 / (self._m2 + EPS)
        dof = self._bandwidth[:, None] * (t_eff + self.window_sec / 1.5)
        sigma = 1.0 / np.sqrt(2.0 * np.maximum(dof, 0.5))

        spec_db = 10.0 * np.log10(self._p_spec + 1e-15)
        self.last = Analysis(
            freqs=self.freqs,
            facade_db=spec_db[:, 0],
            lfe_db=spec_db[:, 1],
            grid=self.grid,
            interaction=np.clip(interaction, -1.0, 1.0).T,
            coherence=np.clip(coherence, 0.0, 1.0).T,
            phase=np.arctan2(c_im, c_re).T,
            gate=gate.T,
            sigma=np.minimum(sigma, 1.0).T,
        )
        return self.last
