# -*- coding: utf-8 -*-
"""Main window: audio input, settings panel, analyzer view."""

import time
from collections import deque

import numpy as np

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QGroupBox, QComboBox, QPushButton, QCheckBox
)
from PyQt6.QtCore import QTimer

from .analyzer_view import AnalyzerView
from .dsp import LfeEngine
from .widgets import GradientHeader, SubGuardLogoWidget

try:
    import sounddevice as sd
    AUDIO_ERROR = None
except Exception as exc:  # PortAudio missing, etc.
    sd = None
    AUDIO_ERROR = str(exc)

DEFAULT_SAMPLE_RATE = 48000
DEFAULT_CHUNK = 512
LFE_INBAND_GAIN_DB = 10.0   # cinema B-chain: LFE plays 10 dB above a screen channel

CHANNEL_SCHEMES = {
    "5.1 ITU - (L, R, C, LFE, Ls, Rs)": ["L", "R", "C", "LFE", "Ls", "Rs"],
    "5.1 FILM - (L, C, R, Ls, Rs, LFE)": ["L", "C", "R", "Ls", "Rs", "LFE"],
    "5.1 DTS  - (L, R, Ls, Rs, C, LFE)": ["L", "R", "Ls", "Rs", "C", "LFE"],
    "7.1 DTS 1 - (L, C, R, Ls, Rs, Lr, Rr, LFE)":
        ["L", "C", "R", "Ls", "Rs", "Lr", "Rr", "LFE"],
    "7.1 DTS 2 - (L, R, C, LFE, Ls, Rs, Lr, Rr)":
        ["L", "R", "C", "LFE", "Ls", "Rs", "Lr", "Rr"],
    "5.1.2 - (L, R, C, LFE, Ls, Rs, Ltf, Rtf)":
        ["L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf"],
    "5.1.4 - (L, R, C, LFE, Ls, Rs, Ltf, Rtf, Ltr, Rtr)":
        ["L", "R", "C", "LFE", "Ls", "Rs", "Ltf", "Rtf", "Ltr", "Rtr"],
    "7.1.2 - (L, R, C, LFE, Ls, Rs, Lb, Rb, Ltf, Rtf)":
        ["L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf"],
    "7.1.4 - (L, R, C, LFE, Ls, Rs, Lb, Rb, Ltf, Rtf, Ltr, Rtr)":
        ["L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf", "Ltr", "Rtr"],
    "9.1.2 - (L, R, C, LFE, Lw, Rw, Ls, Rs, Lb, Rb, Ltf, Rtf)":
        ["L", "R", "C", "LFE", "Lw", "Rw", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf"],
    "9.1.4 - (L, R, C, LFE, Lw, Rw, Ls, Rs, Lb, Rb, Ltf, Rtf, Ltr, Rtr)":
        ["L", "R", "C", "LFE", "Lw", "Rw", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf",
         "Ltr", "Rtr"],
}

STYLE = """
    QMainWindow, QWidget {
        background-color: #1E1E1E; color: #E6E6E6; font-family: Arial;
    }
    QGroupBox {
        border: 1px solid #444; border-radius: 5px; margin-top: 10px;
        font-weight: bold; color: #E6E6E6;
    }
    QGroupBox::title {
        subcontrol-origin: margin; subcontrol-position: top left;
        padding: 0 5px; color: #AAA;
    }
    QPushButton {
        background-color: #333; border: 1px solid #555; border-radius: 4px;
        padding: 4px; color: #E6E6E6;
    }
    QPushButton:hover { background-color: #444; }
    QPushButton:checked { background-color: #555; border-color: #888; }
    QComboBox {
        background-color: #252525; border: 1px solid #555; border-radius: 4px;
        padding: 4px; color: #E6E6E6;
    }
    QComboBox::drop-down { border: 0px; }
    QCheckBox { color: #E6E6E6; spacing: 5px; }
    QCheckBox::indicator { width: 13px; height: 13px; }
"""

OVERLAY_BUTTON_STYLE = """
    QPushButton {
        background-color: rgba(40, 40, 40, 150); color: #AAA;
        border: 1px solid #555; border-radius: 4px;
    }
    QPushButton:hover { background-color: rgba(60, 60, 60, 200); color: #FFF; }
    QPushButton:checked { background-color: rgba(80, 80, 80, 200); }
"""


class SubGuardApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SubGUARD")
        self.setGeometry(100, 100, 1000, 900)
        self.setMinimumSize(950, 720)
        self.setStyleSheet(STYLE)

        self.stream = None
        self.sample_rate = DEFAULT_SAMPLE_RATE
        self.window_sec = 1.0
        self.timer_interval = 17
        # Audio thread -> UI thread hand-off. deque.append/popleft are atomic.
        self.audio_queue = deque(maxlen=512)

        self.current_format = "5.1 ITU - (L, R, C, LFE, Ls, Rs)"
        self._apply_format()

        self.engine = LfeEngine(self.sample_rate, window_sec=self.window_sec)
        self.view = AnalyzerView(self.engine, self)

        self.setup_ui()
        self.populate_device_selector()
        self.on_device_changed()

        self._last_tick = time.perf_counter()
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_ui)
        self.timer.start(self.timer_interval)

    # --------------------------------------------------------------------- UI
    def setup_ui(self):
        central = QWidget()
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        self.top_bar = GradientHeader()
        h_top = QHBoxLayout(self.top_bar)
        h_top.setContentsMargins(20, 20, 20, 20)
        h_top.setSpacing(0)
        h_top.addWidget(SubGuardLogoWidget(), stretch=1)
        for ind in self.view.indicators:
            h_top.addWidget(ind, stretch=1)
        main_layout.addWidget(self.top_bar, stretch=1)

        self.settings_panel = QWidget()
        h_settings = QHBoxLayout(self.settings_panel)
        h_settings.setContentsMargins(10, 0, 10, 10)
        h_settings.addWidget(self.create_audio_controls())
        h_settings.addWidget(self.create_perf_controls())
        h_settings.addWidget(self.view.ctrl_widget)
        h_settings.addStretch(1)
        main_layout.addWidget(self.settings_panel)
        main_layout.addWidget(self.view.plot_container, stretch=4)

        self.btn_toggle_settings = QPushButton("⚙ ▾", self.view.plot_container)
        self.btn_toggle_settings.setCheckable(True)
        self.btn_toggle_settings.setChecked(True)
        self.btn_toggle_settings.setFixedSize(50, 24)
        self.btn_toggle_settings.setStyleSheet(OVERLAY_BUTTON_STYLE)
        self.btn_toggle_settings.move(10, 10)
        self.btn_toggle_settings.clicked.connect(self.toggle_settings_panel)
        self.setCentralWidget(central)

    def toggle_settings_panel(self):
        visible = self.btn_toggle_settings.isChecked()
        self.settings_panel.setVisible(visible)
        self.btn_toggle_settings.setText("⚙ ▾" if visible else "⚙ ▴")

    def create_audio_controls(self):
        grp = QGroupBox("Source & Format")
        v = QVBoxLayout(grp)
        v.addWidget(QLabel("Channel Format:"))
        self.format_selector = QComboBox()
        self.format_selector.addItems(CHANNEL_SCHEMES.keys())
        self.format_selector.setCurrentText(self.current_format)
        self.format_selector.currentIndexChanged.connect(self.on_format_changed)
        v.addWidget(self.format_selector)
        v.addWidget(QLabel("Input Device:"))
        self.device_selector = QComboBox()
        self.device_selector.currentIndexChanged.connect(self.on_device_changed)
        v.addWidget(self.device_selector)
        self.status_label = QLabel("Status: Waiting...")
        self.status_label.setWordWrap(True)
        v.addWidget(self.status_label)
        return grp

    def create_perf_controls(self):
        grp = QGroupBox("Analysis")
        v = QVBoxLayout(grp)
        v.addWidget(QLabel("Refresh Rate:"))
        self.fps_selector = QComboBox()
        self.fps_selector.addItem("90 FPS", 11)
        self.fps_selector.addItem("60 FPS", 17)
        self.fps_selector.addItem("30 FPS", 33)
        self.fps_selector.setCurrentIndex(1)
        self.fps_selector.currentIndexChanged.connect(self.on_fps_changed)
        v.addWidget(self.fps_selector)

        v.addWidget(QLabel("Time / Frequency:"))
        self.window_selector = QComboBox()
        self.window_selector.addItem("Fast (0.5 s · 4 Hz)", 0.5)
        self.window_selector.addItem("Normal (1 s · 2 Hz)", 1.0)
        self.window_selector.addItem("Ultra (2 s · 1 Hz)", 2.0)
        self.window_selector.setCurrentIndex(1)
        self.window_selector.currentIndexChanged.connect(self.on_window_changed)
        v.addWidget(self.window_selector)

        self.chk_lfe_gain = QCheckBox("LFE +10 dB (theatre level)")
        self.chk_lfe_gain.setToolTip(
            "Weigh the LFE as it plays in a cinema: 10 dB of in-band gain\n"
            "over a screen channel. Affects the curve and the alert.")
        v.addWidget(self.chk_lfe_gain)

        self.chk_anim = QCheckBox("Back. Anim")
        self.chk_anim.setChecked(True)
        self.chk_anim.toggled.connect(self.top_bar.set_animation_enabled)
        v.addWidget(self.chk_anim)
        v.addStretch(1)
        return grp

    # ------------------------------------------------------------- settings
    def on_fps_changed(self):
        self.timer_interval = int(self.fps_selector.currentData())
        self.timer.start(self.timer_interval)

    def on_window_changed(self):
        self.window_sec = float(self.window_selector.currentData())
        self.engine.configure(self.window_sec)
        self.view.set_engine(self.engine)

    def _apply_format(self):
        labels = CHANNEL_SCHEMES[self.current_format]
        self.current_channels = len(labels)
        self.channel_indices = [labels.index(k) for k in ("L", "C", "R", "LFE")]

    def on_format_changed(self):
        self.current_format = self.format_selector.currentText()
        self._apply_format()
        self.populate_device_selector()
        self.on_device_changed()

    # ---------------------------------------------------------------- audio
    def populate_device_selector(self):
        self.device_selector.blockSignals(True)
        self.device_selector.clear()
        if sd is None:
            self.status_label.setText(f"Status: 🔴 Audio unavailable: {AUDIO_ERROR}")
        else:
            for d in sd.query_devices():
                if d['max_input_channels'] >= self.current_channels:
                    self.device_selector.addItem(
                        f"{d['name']} ({d['max_input_channels']} ch max)",
                        d['index'])
            if self.device_selector.count() == 0:
                self.status_label.setText(
                    f"Status: 🔴 No device found (≥{self.current_channels} ch).")
        self.device_selector.blockSignals(False)

    def close_stream(self):
        if self.stream is not None:
            try:
                self.stream.stop()
                self.stream.close()
            except Exception:
                pass
            self.stream = None
        self.audio_queue.clear()

    def on_device_changed(self):
        self.close_stream()
        if sd is None or self.device_selector.count() == 0:
            return
        device_id = self.device_selector.currentData()
        rates = [DEFAULT_SAMPLE_RATE]
        try:
            native = int(sd.query_devices(device_id)['default_samplerate'])
            if native not in rates:
                rates.append(native)
        except Exception:
            pass

        error = None
        for rate in rates:
            try:
                self.stream = sd.InputStream(
                    samplerate=rate, blocksize=DEFAULT_CHUNK,
                    channels=self.current_channels, device=device_id,
                    dtype='float32', callback=self.audio_callback)
                self.stream.start()
            except Exception as exc:
                error = exc
                self.stream = None
                continue
            self.set_sample_rate(rate)
            self.status_label.setText(
                f"Status: 🟢 {self.device_selector.currentText()} @ {rate} Hz")
            return
        self.status_label.setText(f"Status: 🔴 Connection failed: {error}")

    def set_sample_rate(self, rate):
        if rate != self.sample_rate:
            self.sample_rate = rate
            self.engine = LfeEngine(rate, window_sec=self.window_sec)
            self.view.set_engine(self.engine)
        else:
            self.engine.reset()

    def audio_callback(self, indata, frames, time_info, status):
        # Real-time thread: copy and leave.
        self.audio_queue.append(indata.copy())

    # --------------------------------------------------------------- runtime
    def update_ui(self):
        now = time.perf_counter()
        dt = min(max(now - self._last_tick, 1e-3), 0.25)
        self._last_tick = now

        blocks = []
        while self.audio_queue:
            blocks.append(self.audio_queue.popleft())

        if blocks and blocks[0].shape[1] == self.current_channels:
            data = blocks[0] if len(blocks) == 1 else np.concatenate(blocks)
            used = data[:, self.channel_indices]
            rms = float(np.sqrt(np.mean(np.square(data[-2048:]))))
            level = np.clip((20 * np.log10(rms + 1e-9) + 60) / 60, 0.0, 1.0)
        elif self.stream is not None:
            used = None                      # stream alive, nothing new yet
            level = None
        else:
            used = np.zeros((int(self.sample_rate * dt), 4), dtype=np.float32)
            level = 0.0

        if level is not None:
            self.top_bar.set_level(level)
        if used is not None:
            self.engine.push(used)
        gain = LFE_INBAND_GAIN_DB if self.chk_lfe_gain.isChecked() else 0.0
        self.view.refresh(dt, lfe_gain_db=gain)

    def closeEvent(self, event):
        self.timer.stop()
        self.close_stream()
        super().closeEvent(event)
