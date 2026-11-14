# -*- coding: utf-8 -*-
"""Spectrum + LFE/LCR interaction display, driven by dsp.LfeEngine."""

import numpy as np
import pyqtgraph as pg

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QSlider, QGroupBox
)
from PyQt6.QtCore import Qt, QRectF

from .widgets import SpeakerIndicator

F_MIN, F_MAX = 20.0, 150.0
LFE_CUT_HZ = 120.0
ALERT_GAIN = 3.5
ALERT_SIGMAS = 2.5          # an alert must stand this far above chance
AXIS_WIDTH = 62

# Heat-map colour ramps (position, RGBA). Amber: LFE in phase with the
# screen channel. Red: out of phase, i.e. cancelling right now.
RAMP_IN_PHASE = np.array([
    (0.00,   0,   0,  0,   0),
    (0.10,  60,  45,  0,  30),
    (0.25, 140, 100,  0,  90),
    (0.45, 215, 160,  0, 165),
    (0.65, 250, 195,  0, 215),
    (1.00, 255, 225, 30, 245),
], dtype=float)
RAMP_OUT_OF_PHASE = np.array([
    (0.00,   0,  0,  0,   0),
    (0.10,  60, 15,  0,  30),
    (0.25, 140, 35,  0,  90),
    (0.45, 220, 60,  0, 170),
    (0.65, 255, 50,  0, 230),
    (1.00, 255, 20, 40, 255),
], dtype=float)

CURVE_COLORS = {"L": '#4FC3F7', "C": '#E6E6E6', "R": '#F48FB1'}


def _ramp(values, ramp):
    out = np.empty((len(values), 4), dtype=np.float64)
    for j in range(4):
        out[:, j] = np.interp(values, ramp[:, 0], ramp[:, j + 1])
    return out


class AnalyzerView(QWidget):
    LABELS = ("L", "C", "R")

    def __init__(self, engine, parent=None):
        super().__init__(parent)
        self.mute_states = {"L": False, "C": False, "R": False}
        self.spk_levels = np.zeros(3)
        self.speaker_decay_seconds = 0.10

        self.ctrl_widget = QWidget()
        self._create_controls(self.ctrl_widget)

        self.ind_L = SpeakerIndicator("L")
        self.ind_C = SpeakerIndicator("C")
        self.ind_R = SpeakerIndicator("R")
        self.indicators = (self.ind_L, self.ind_C, self.ind_R)
        for ind in self.indicators:
            ind.toggled.connect(self.on_speaker_toggled)

        self.plot_container = QWidget()
        lay = QVBoxLayout(self.plot_container)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        self._setup_spectrum_plot(lay)
        self._setup_correlation_plot(lay)

        self.set_engine(engine)
        self.update_params()

    # ------------------------------------------------------------------ setup
    def set_engine(self, engine):
        """Call after the engine was created or reconfigured."""
        self.engine = engine
        f = engine.freqs
        self._spec_sel = np.where((f >= 10.0) & (f <= 165.0))[0]
        fb = f[self._spec_sel]
        self._spec_x = np.log10(fb)

        # Same display weighting as the original app (20-150 Hz tilt + bump)
        pos = (np.log10(fb) - np.log10(F_MIN)) / (np.log10(F_MAX) - np.log10(F_MIN))
        pos = np.clip(pos, 0.0, 1.0)
        tilt = -12.0 * (1.0 - pos)
        bump = 3.0 * np.exp(-0.5 * ((fb - 80.0) / 25.0) ** 2)
        self._ear_weight_db = tilt + bump

        g = engine.grid
        self._grid_x = np.log10(g)
        self._grid_vis = (g >= F_MIN) & (g <= F_MAX)
        ng = len(g)
        self.conflict = np.zeros((3, ng))
        self.conflict_sign = np.ones((3, ng))
        self._heat_rgba = np.zeros((1, ng, 4), dtype=np.uint8)
        step = (self._grid_x[-1] - self._grid_x[0]) / (ng - 1)
        self._heat_rect = QRectF(self._grid_x[0] - step / 2, -90.0,
                                 step * ng, 96.0)

    def _create_controls(self, parent_widget):
        layout = QHBoxLayout(parent_widget)
        layout.setContentsMargins(10, 10, 10, 10)

        def slider_row(vbox, title, initial, lo, hi, value, width=60):
            row = QHBoxLayout()
            row.addWidget(QLabel(title))
            lbl = QLabel(initial)
            lbl.setFixedWidth(width)
            row.addWidget(lbl)
            vbox.addLayout(row)
            sl = QSlider(Qt.Orientation.Horizontal)
            sl.setRange(lo, hi)
            sl.setValue(value)
            sl.valueChanged.connect(self.update_params)
            vbox.addWidget(sl)
            return sl, lbl

        grp_smooth = QGroupBox("Responsiveness")
        v_smooth = QVBoxLayout(grp_smooth)
        self.slider_inertia, self.lbl_inertia_val = slider_row(
            v_smooth, "Inertia:", "70%", 0, 100, 70)
        self.slider_integration, self.lbl_integration_val = slider_row(
            v_smooth, "Integration:", "2.0 s", 500, 5000, 2000)
        self.slider_integration.setToolTip(
            "Averaging time of the LFE/LCR correlation measurement.\n"
            "Longer = more reliable, slower to react.")
        layout.addWidget(grp_smooth)

        grp_alert = QGroupBox("Phase Alert")
        v_alert = QVBoxLayout(grp_alert)
        self.slider_thresh, self.lbl_thresh_val = slider_row(
            v_alert, "Threshold:", "±0.33", 0, 100, 33)
        self.slider_alert_decay, self.lbl_alert_decay_val = slider_row(
            v_alert, "Decay:", "1.00 s", 100, 4000, 1000)
        layout.addWidget(grp_alert)
        layout.addStretch(1)

    def _base_plot(self, widget):
        widget.setBackground('#121212')
        item = widget.getPlotItem()
        item.hideButtons()
        item.setMenuEnabled(False)
        item.setMouseEnabled(x=False, y=False)
        item.enableAutoRange(x=False, y=False)
        item.setClipToView(True)
        item.setXRange(np.log10(F_MIN), np.log10(F_MAX), padding=0)
        item.getAxis('left').setWidth(AXIS_WIDTH)
        ticks = [(np.log10(f), str(f))
                 for f in (20, 30, 40, 50, 60, 80, 100, 120, 150)]
        item.getAxis('bottom').setTicks([ticks])
        item.showGrid(x=True, y=True, alpha=0.3)
        return item

    def _setup_spectrum_plot(self, layout):
        self.fft_plot = pg.PlotWidget()
        item = self._base_plot(self.fft_plot)
        item.setYRange(-90, 6, padding=0)
        item.setTitle("SubGUARD Analyzer : 20Hz - 150Hz")
        item.getAxis('left').setLabel('Level (dBFS)')
        item.getAxis('left').setTicks(
            [[(v, str(v)) for v in (0, -18, -36, -54, -72, -90)]])
        item.getAxis('bottom').setStyle(showValues=False)

        cut = pg.InfiniteLine(
            pos=np.log10(LFE_CUT_HZ), angle=90,
            pen=pg.mkPen('#666666', width=1, style=Qt.PenStyle.DashLine))
        cut.setZValue(0)
        self.fft_plot.addItem(cut)

        # One image for the whole heat map (was one item per FFT bin)
        self.heat_img = pg.ImageItem(axisOrder='row-major')
        self.heat_img.setZValue(0.5)
        self.fft_plot.addItem(self.heat_img)

        # Fade-out above the LFE cut-off
        x_cut, x_max = np.log10(LFE_CUT_HZ), np.log10(F_MAX)
        region = x_max - x_cut
        n_blur = 12
        for i in range(n_blur):
            t = (i + 1) / n_blur
            alpha = min(int(int(255 * (t ** 1.6) * 0.55) * 1.8), 255)
            bar = pg.BarGraphItem(
                x=[x_cut + (i + 0.5) * region / n_blur], height=[100.0],
                width=region / n_blur * 3.0, y0=-90.0,
                brush=pg.mkBrush(0, 0, 0, alpha), pen=None)
            bar.setZValue(5)
            self.fft_plot.addItem(bar)

        self.facade_curve = pg.PlotCurveItem(
            pen=pg.mkPen('#0077FF', width=2),
            brush=pg.mkBrush(0, 119, 255, 110), fillLevel=-120, antialias=True)
        self.facade_curve.setZValue(1)
        self.fft_plot.addItem(self.facade_curve)
        self.lfe_curve = pg.PlotCurveItem(
            pen=pg.mkPen('#00FF00', width=1.5),
            brush=pg.mkBrush(0, 255, 0, 100), fillLevel=-120, antialias=True)
        self.lfe_curve.setZValue(2)
        self.fft_plot.addItem(self.lfe_curve)
        self.fft_plot.setMinimumHeight(220)
        layout.addWidget(self.fft_plot, stretch=5)

    def _setup_correlation_plot(self, layout):
        self.corr_plot = pg.PlotWidget()
        self.corr_plot.setMinimumHeight(150)
        item = self._base_plot(self.corr_plot)
        item.setYRange(-1.2, 1.2, padding=0)
        item.getAxis('left').setLabel('Out  ·  In phase')
        item.getAxis('left').setTicks(
            [[(1, '+1'), (0, '0'), (-1, '-1')]])
        item.getAxis('bottom').setLabel('Frequency (Hz)')

        self.corr_plot.addItem(pg.InfiniteLine(
            pos=0.0, angle=0, pen=pg.mkPen('#555555', width=1)))
        thr_pen = pg.mkPen('#FF5500', width=1, style=Qt.PenStyle.DashLine)
        self.thr_line_pos = pg.InfiniteLine(pos=0.33, angle=0, pen=thr_pen)
        self.thr_line_neg = pg.InfiniteLine(pos=-0.33, angle=0, pen=thr_pen)
        self.corr_plot.addItem(self.thr_line_pos)
        self.corr_plot.addItem(self.thr_line_neg)

        legend = item.addLegend(offset=(-10, 4), labelTextSize='8pt')
        legend.setBrush(pg.mkBrush(18, 18, 18, 180))
        self.corr_curves = []
        for label in self.LABELS:
            curve = pg.PlotCurveItem(
                pen=pg.mkPen(CURVE_COLORS[label], width=1.6),
                antialias=True, name=label)
            self.corr_plot.addItem(curve)
            self.corr_curves.append(curve)
        layout.addWidget(self.corr_plot, stretch=2)

    # --------------------------------------------------------------- controls
    def on_speaker_toggled(self, label, is_active):
        if label in self.mute_states:
            self.mute_states[label] = not is_active

    def update_params(self):
        inertia = self.slider_inertia.value()
        self.lbl_inertia_val.setText(f"{inertia}%")
        # 0 % -> 10 ms, 100 % -> 600 ms spectrum averaging
        self.tau_spec = 0.010 * (60.0 ** (inertia / 100.0))

        self.tau_corr = self.slider_integration.value() / 1000.0
        self.lbl_integration_val.setText(f"{self.tau_corr:.1f} s")

        self.alert_thresh = self.slider_thresh.value() / 100.0
        self.lbl_thresh_val.setText(f"±{self.alert_thresh:.2f}")
        self.thr_line_pos.setValue(self.alert_thresh)
        self.thr_line_neg.setValue(-self.alert_thresh)

        self.alert_decay_seconds = self.slider_alert_decay.value() / 1000.0
        self.lbl_alert_decay_val.setText(f"{self.alert_decay_seconds:.2f} s")

    # ---------------------------------------------------------------- runtime
    def refresh(self, frame_dt, lfe_gain_db=0.0):
        """Run one analysis step and redraw. ``frame_dt`` in seconds."""
        mutes = tuple(self.mute_states[k] for k in self.LABELS)
        res = self.engine.analyze(mutes=mutes, lfe_gain_db=lfe_gain_db,
                                  tau_spec=self.tau_spec,
                                  tau_corr=self.tau_corr)
        if res is None:
            return

        sel = self._spec_sel
        self.facade_curve.setData(
            self._spec_x,
            np.clip(res.facade_db[sel] + self._ear_weight_db, -120.0, 10.0))
        self.lfe_curve.setData(
            self._spec_x,
            np.clip(res.lfe_db[sel] + self._ear_weight_db, -120.0, 10.0))

        # Signed LFE/screen interaction per channel, zero where nothing plays
        signed = res.interaction[:3] * res.gate[:3]
        for curve, row in zip(self.corr_curves, signed):
            curve.setData(self._grid_x, row)

        # Alert above the user threshold, but never on a value that pure
        # chance could produce (few data right after an onset).
        thr = np.clip(np.maximum(self.alert_thresh,
                                 ALERT_SIGMAS * res.sigma[:3]), 0.0, 0.99)
        target = np.clip((np.abs(signed) - thr) / (1.0 - thr), 0.0, 1.0)
        target = np.clip(target * ALERT_GAIN, 0.0, 1.0)
        target[:, ~self._grid_vis] = 0.0

        decay_c = min(frame_dt / max(self.alert_decay_seconds, 1e-3), 1.0)
        rising = target > self.conflict
        coeff = np.where(rising, 0.8, decay_c)
        self.conflict += coeff * (target - self.conflict)
        self.conflict_sign = np.where(target > 0.0, np.sign(signed),
                                      self.conflict_sign)

        # Heat map: worst channel at each frequency
        worst = np.argmax(self.conflict, axis=0)
        cols = np.arange(self.conflict.shape[1])
        vis = np.sqrt(self.conflict[worst, cols])
        sign = self.conflict_sign[worst, cols]
        rgba = np.where((sign < 0)[:, None],
                        _ramp(vis, RAMP_OUT_OF_PHASE),
                        _ramp(vis, RAMP_IN_PHASE))
        self._heat_rgba[0] = rgba
        self.heat_img.setImage(self._heat_rgba, autoLevels=False,
                               levels=None)
        self.heat_img.setRect(self._heat_rect)

        # Speaker indicators
        raw = target.max(axis=1)
        spk_decay = min(frame_dt / self.speaker_decay_seconds, 1.0)
        coeff = np.where(raw > self.spk_levels, 0.7, spk_decay)
        self.spk_levels += coeff * (raw - self.spk_levels)
        for ind, level in zip(self.indicators, self.spk_levels):
            ind.setLevel(float(level))
