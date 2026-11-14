# -*- coding: utf-8 -*-
"""Accuracy checks of the analysis engine on synthetic signals."""

import os
import sys

import numpy as np
import pytest
from scipy.signal import hilbert

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from betterlfe.dsp import LfeEngine, CH_L, CH_C, CH_R, CH_LFE  # noqa: E402

FS = 48000
BLOCK = 512


def run(engine, sig, **kw):
    """Stream ``sig`` (n, 4) through the engine like the audio callback."""
    out = None
    for i in range(0, len(sig), BLOCK):
        engine.push(sig[i:i + BLOCK])
        if (i // BLOCK) % 2 == 1:           # ~47 analyses per second
            out = engine.analyze(**kw)
    return out


def sine(freq, level_db, seconds=4.0, phase=0.0):
    t = np.arange(int(seconds * FS)) / FS
    return 10 ** (level_db / 20) * np.sin(2 * np.pi * freq * t + phase)


def noise(seconds, seed):
    rng = np.random.default_rng(seed)
    return 0.1 * rng.standard_normal(int(seconds * FS))


def four(l=None, c=None, r=None, lfe=None):
    n = max(len(s) for s in (l, c, r, lfe) if s is not None)
    sig = np.zeros((n, 4))
    for idx, s in ((CH_L, l), (CH_C, c), (CH_R, r), (CH_LFE, lfe)):
        if s is not None:
            sig[:, idx] = s
    return sig


def band(res, lo=25.0, hi=120.0):
    return (res.grid >= lo) & (res.grid <= hi)


@pytest.mark.parametrize("freq", [20.0, 31.7, 63.3, 100.0, 149.0])
@pytest.mark.parametrize("window", [0.5, 1.0, 2.0])
def test_level_is_accurate(freq, window):
    """A sine at -20 dBFS on the LFE reads -20 dB whatever its frequency."""
    eng = LfeEngine(FS, window_sec=window, facade_trim_db=0.0)
    res = run(eng, four(lfe=sine(freq, -20.0)))
    assert abs(res.lfe_db.max() - (-20.0)) < 0.4
    assert abs(res.freqs[np.argmax(res.lfe_db)] - freq) < 0.3


def test_facade_trim_and_sum():
    eng = LfeEngine(FS, facade_trim_db=-3.0)
    s = sine(50.0, -20.0)
    res = run(eng, four(l=s, c=s, r=s))
    # three identical channels: +9.54 dB, minus the 3 dB trim
    assert abs(res.facade_db.max() - (-20.0 + 9.54 - 3.0)) < 0.4


def test_no_aliasing():
    """Full-scale tones above the band must not fold back into it."""
    for freq in (380.0, 450.0, 620.0, 1000.0, 5000.0):
        eng = LfeEngine(FS, facade_trim_db=0.0)
        res = run(eng, four(lfe=sine(freq, 0.0, seconds=3.0)))
        in_band = res.freqs <= 150.0
        assert res.lfe_db[in_band].max() < -80.0, freq


def test_in_phase_and_out_of_phase():
    n = noise(8.0, 1)
    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, four(l=n, lfe=n))
    b = band(res)
    assert res.interaction[CH_L, b].min() > 0.97
    assert res.coherence[CH_L, b].min() > 0.97
    assert np.abs(res.interaction[CH_C, b]).max() < 1e-6     # silent channel

    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, four(l=-n, lfe=n))
    assert res.interaction[CH_L, band(res)].max() < -0.97
    assert np.abs(np.abs(res.phase[CH_L, band(res)]) - np.pi).max() < 0.05


def test_quadrature_is_coherent_but_does_not_sum():
    n = noise(8.0, 2)
    q = np.imag(hilbert(n))                # same signal, 90 degrees apart
    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, four(l=q, lfe=n))
    b = band(res)
    assert np.abs(res.interaction[CH_L, b]).max() < 0.1
    assert res.coherence[CH_L, b].min() > 0.9
    assert np.abs(np.abs(res.phase[CH_L, b]) - np.pi / 2).max() < 0.1


def test_decorrelated_stays_quiet():
    """Independent signals must not trip the default 0.33 alert threshold."""
    worst = []
    for seed in range(6):
        eng = LfeEngine(FS, facade_trim_db=0.0)
        res = run(eng, four(l=noise(10.0, 10 + seed),
                            lfe=noise(10.0, 100 + seed)))
        worst.append(np.abs(res.interaction[CH_L, band(res, 20.0, 150.0)]))
    worst = np.concatenate(worst)
    assert np.mean(worst > 0.33) < 0.03
    assert np.median(worst) < 0.12


def test_level_balance_limits_interaction():
    """In phase but LFE 20 dB louder: the screen channel barely matters."""
    n = noise(8.0, 3)
    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, four(l=0.1 * n, lfe=n))
    expected = 2 * 0.1 / (1 + 0.01)
    assert np.abs(res.interaction[CH_L, band(res)] - expected).max() < 0.02
    assert res.coherence[CH_L, band(res)].min() > 0.97


def test_partial_correlation():
    a, b_ = noise(12.0, 4), noise(12.0, 5)
    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, four(l=a + b_, lfe=a), tau_corr=4.0)   # true s = 2/3
    sel = band(res, 40.0, 120.0)
    assert abs(np.mean(res.interaction[CH_L, sel]) - 2.0 / 3.0) < 0.08


def test_mute_removes_channel():
    n = noise(6.0, 6)
    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, four(l=n, lfe=n), mutes=(True, False, False))
    assert np.abs(res.interaction[CH_L]).max() < 1e-6
    assert res.facade_db.max() < -120.0


def test_block_size_independent():
    n = noise(3.0, 7)
    sig = four(l=n, r=-n, lfe=n)
    a = LfeEngine(FS)
    b = LfeEngine(FS)
    a.push(sig)
    rng = np.random.default_rng(0)
    i = 0
    while i < len(sig):
        k = int(rng.integers(1, 2000))
        b.push(sig[i:i + k])
        i += k
    np.testing.assert_allclose(a._buf, b._buf, atol=1e-12)


def test_uncertainty_guards_onsets():
    """Unrelated sounds starting after silence: |s| stays within chance level
    (2.5 sigma), while a truly shared signal clears it within half a second."""
    hits = []
    for seed in range(4):
        sig = np.zeros((FS * 6, 4))
        sig[FS * 2:, CH_L] = noise(4.0, 20 + seed)
        sig[FS * 2:, CH_LFE] = noise(4.0, 40 + seed)
        eng = LfeEngine(FS, facade_trim_db=0.0)
        for i in range(0, len(sig), 800):
            eng.push(sig[i:i + 800])
            res = eng.analyze()
            if i > FS * 2:
                b = band(res, 20.0, 150.0)
                s = np.abs(res.interaction[CH_L, b] * res.gate[CH_L, b])
                hits.append(s > np.maximum(0.33, 2.5 * res.sigma[CH_L, b]))
    assert np.mean(np.concatenate(hits)) < 0.03

    n = noise(1.0, 60)
    sig = np.zeros((FS * 3, 4))
    sig[FS * 2:, CH_L] = n
    sig[FS * 2:, CH_LFE] = n
    eng = LfeEngine(FS, facade_trim_db=0.0)
    res = run(eng, sig[:int(FS * 2.5)])
    b = band(res, 20.0, 150.0)
    s = res.interaction[CH_L, b] * res.gate[CH_L, b]
    assert np.mean(s > np.maximum(0.33, 2.5 * res.sigma[CH_L, b])) > 0.9
