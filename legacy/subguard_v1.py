# -*- coding: utf-8 -*-

import numpy as np
import sounddevice as sd
import pyqtgraph as pg
import sys
import os

from scipy.signal import butter, sosfilt, windows, savgol_filter

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QSlider, QGroupBox, QComboBox, QPushButton, QSizePolicy, QCheckBox
)
from PyQt6.QtCore import Qt, QTimer, QRectF, pyqtSignal, QPointF
from PyQt6.QtGui import (
    QPainter, QPen, QBrush, QColor, QFont, QCursor, 
    QPainterPath, QLinearGradient, QRadialGradient, QPixmap, QIcon
)

# --- FUNCTION TO HANDLE RESOURCES IN PYINSTALLER ---
def resource_path(relative_path):
    """ Get absolute path to resource, works for dev and for PyInstaller """
    try:
        # PyInstaller creates a temp folder and stores path in _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")

    return os.path.join(base_path, relative_path)

# --- DEFAULT CONFIGURATION ---
DEFAULT_SAMPLE_RATE = 48000
DEFAULT_FFT_SIZE = 16384 # Normal Precision
DEFAULT_CHUNK = 512
MAX_CHANNELS = 16 # Increased to support up to 9.1.4

CHANNEL_MAPPINGS = {
    "5.1 ITU - (L, R, C, LFE, Ls, Rs)": 6,
    "5.1 FILM - (L, C, R, Ls, Rs, LFE)": 6,
    "5.1 DTS  - (L, R, Ls, Rs, C, LFE)": 6,
    "7.1 DTS 1 - (L, C, R, Ls, Rs, Lr, Rr, LFE)": 8,
    "7.1 DTS 2 - (L, R, C, LFE, Ls, Rs, Lr, Rr)": 8,
    # --- NEW IMMERSIVE FORMATS ---
    "5.1.2 - (L, R, C, LFE, Ls, Rs, Ltf, Rtf)": 8,
    "5.1.4 - (L, R, C, LFE, Ls, Rs, Ltf, Rtf, Ltr, Rtr)": 10,
    "7.1.2 - (L, R, C, LFE, Ls, Rs, Lb, Rb, Ltf, Rtf)": 10,
    "7.1.4 - (L, R, C, LFE, Ls, Rs, Lb, Rb, Ltf, Rtf, Ltr, Rtr)": 12,
    "9.1.2 - (L, R, C, LFE, Lw, Rw, Ls, Rs, Lb, Rb, Ltf, Rtf)": 12,
    "9.1.4 - (L, R, C, LFE, Lw, Rw, Ls, Rs, Lb, Rb, Ltf, Rtf, Ltr, Rtr)": 14
}

CHANNEL_SCHEMES = {
    "5.1 ITU - (L, R, C, LFE, Ls, Rs)": ["L", "R", "C", "LFE", "Ls", "Rs"],
    "5.1 FILM - (L, C, R, Ls, Rs, LFE)": ["L", "C", "R", "Ls", "Rs", "LFE"],
    "5.1 DTS  - (L, R, Ls, Rs, C, LFE)": ["L", "R", "Ls", "Rs", "C", "LFE"],
    "7.1 DTS 1 - (L, C, R, Ls, Rs, Lr, Rr, LFE)":
        ["L", "C", "R", "Ls", "Rs", "Lr", "Rr", "LFE"],
    "7.1 DTS 2 - (L, R, C, LFE, Ls, Rs, Lr, Rr)":
        ["L", "R", "C", "LFE", "Ls", "Rs", "Lr", "Rr"],
    
    # --- NEW IMMERSIVE SCHEMES ---
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
        ["L", "R", "C", "LFE", "Lw", "Rw", "Ls", "Rs", "Lb", "Rb", "Ltf", "Rtf", "Ltr", "Rtr"],
}

# GPU Rendering
pg.setConfigOptions(useOpenGL=True, antialias=True)


class GradientHeader(QWidget):
    """
    "Black Hole" Top Header.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setAutoFillBackground(False)
        
        self.current_level = 0.0
        self.animation_enabled = True 

    def set_animation_enabled(self, enabled: bool):
        self.animation_enabled = enabled
        if not enabled:
            self.current_level = 0.0 
            self.update()

    def set_level(self, target_level):
        if not self.animation_enabled:
            return

        if target_level > self.current_level:
            self.current_level = self.current_level * 0.8 + target_level * 0.2
        else:
            self.current_level = self.current_level * 0.95 + target_level * 0.05
        self.update()

    def lerp_color(self, c1, c2, t):
        r = int(c1.red() + (c2.red() - c1.red()) * t)
        g = int(c1.green() + (c2.green() - c1.green()) * t)
        b = int(c1.blue() + (c2.blue() - c1.blue()) * t)
        a = int(c1.alpha() + (c2.alpha() - c1.alpha()) * t)
        return QColor(r, g, b, a)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)

        rect = self.rect()
        w = rect.width()
        h = rect.height()

        # 1. Solid Background (Charcoal)
        base_color_top = QColor(10, 10, 10)
        base_color_bottom = QColor(4, 4, 4)
        vgrad = QLinearGradient(0, 0, 0, h)
        vgrad.setColorAt(0.0, base_color_top)
        vgrad.setColorAt(1.0, base_color_bottom)
        p.fillRect(rect, vgrad)

        # 2. The Black Hole (Radial Stretched)
        p.save()
        p.translate(w * 0.5, h * 0.75)
        
        scale_factor_base = 3.0  
        
        dynamic_scale = scale_factor_base
        if self.animation_enabled:
            dynamic_scale += (self.current_level * 0.15)
            
        p.scale(w * dynamic_scale, h * dynamic_scale)

        rgrad = QRadialGradient(0, 0, 0.5)
        
        c0_start = QColor(5, 12, 18)
        c0_mid1  = QColor(9, 34, 48)
        c0_mid2  = QColor(15, 40, 45) 
        c0_edge  = QColor(20, 25, 20)
        
        c1_start = QColor(10, 20, 25)
        c1_mid1  = QColor(15, 50, 55)    
        c1_mid2  = QColor(30, 70, 40)    
        c1_edge  = QColor(60, 65, 20)    

        t = self.current_level if self.animation_enabled else 0.0
        
        col_start = self.lerp_color(c0_start, c1_start, t)
        col_mid1  = self.lerp_color(c0_mid1,  c1_mid1,  t)
        col_mid2  = self.lerp_color(c0_mid2,  c1_mid2,  t)
        col_edge  = self.lerp_color(c0_edge,  c1_edge,  t)
        col_end   = QColor(8, 8, 8, 0)

        rgrad.setColorAt(0.0, col_start)
        rgrad.setColorAt(0.35, col_mid1)
        rgrad.setColorAt(0.6, col_mid2)
        rgrad.setColorAt(0.85, col_edge)
        rgrad.setColorAt(1.0, col_end)

        p.setBrush(QBrush(rgrad))
        p.setPen(Qt.PenStyle.NoPen)
        p.setOpacity(0.9) 
        p.drawEllipse(QPointF(0, 0), 0.5, 0.5)
        
        p.restore()
        p.end()


class SubGuardLogoWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumSize(100, 100) 
        
        # --- USE RESOURCE_PATH FOR IMAGE ---
        image_path = resource_path("logo.png")
        self.logo_pixmap = QPixmap(image_path)
        
        if self.logo_pixmap.isNull():
            print(f"WARNING: 'logo.png' not found at {image_path}")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)

        if self.logo_pixmap.isNull():
            painter.setBrush(QColor(255, 85, 0))
            painter.drawRect(0, 0, 100, 100)
            return

        target_rect = QRectF(self.rect())
        scaled_pix = self.logo_pixmap.scaled(
            target_rect.size().toSize(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation
        )
        x_pos = (target_rect.width() - scaled_pix.width()) / 2
        y_pos = (target_rect.height() - scaled_pix.height()) / 2
        painter.drawPixmap(int(x_pos), int(y_pos), scaled_pix)


class SpeakerIndicator(QWidget):
    toggled = pyqtSignal(str, bool)

    def __init__(self, label: str, parent=None):
        super().__init__(parent)
        self.label = label
        self.level = 0.0
        self.is_active = True
        
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumSize(60, 90)
        self.setCursor(QCursor(Qt.CursorShape.PointingHandCursor))

    def setLevel(self, level: float):
        self.level = float(max(0.0, min(1.0, level)))
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.is_active = not self.is_active
            self.toggled.emit(self.label, self.is_active)
            self.update()

    def _alert_color(self, l: float) -> QColor:
        l = max(0.0, min(1.0, l))
        t = l 
        r = 255
        g = int(255 * (1.0 - t))
        b = 0
        a = int(60 + 195 * t)
        return QColor(r, g, b, a)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        
        if not self.is_active:
            p.setOpacity(0.4)
        else:
            p.setOpacity(1.0)

        w_avail = self.width()
        h_avail = self.height()
        target_aspect = 0.65 
        
        if w_avail / h_avail > target_aspect:
            draw_h = h_avail * 0.95
            draw_w = draw_h * target_aspect
        else:
            draw_w = w_avail * 0.95
            draw_h = draw_w / target_aspect
            
        off_x = (w_avail - draw_w) / 2
        off_y = (h_avail - draw_h) / 2
        
        r = QRectF(off_x, off_y, draw_w, draw_h)
        
        cab_h = r.height() * 0.60
        cab_w = cab_h * 0.55
        cx = r.center().x()
        cab_x = cx - cab_w / 2.0
        cab_y = r.top() + r.height() * 0.05 

        level = self.level if self.is_active else 0.0

        cab_edge = QColor(200, 200, 200)
        cab_fill = QColor(20, 20, 20)
        tw_fill = QColor(10, 10, 10)
        wf_fill = QColor(18, 18, 18)
        text_col = QColor(235, 235, 235)
        alert_col = self._alert_color(level)
        
        cab_rect = QRectF(cab_x, cab_y, cab_w, cab_h)
        p.setPen(QPen(cab_edge, max(1.5, draw_w * 0.04)))
        p.setBrush(QBrush(cab_fill))
        p.drawRoundedRect(cab_rect, draw_w*0.08, draw_w*0.08)

        tw_r = cab_w * 0.18
        tw_cx = cx
        tw_cy = cab_y + cab_h * 0.25
        tw_rect = QRectF(tw_cx - tw_r, tw_cy - tw_r, 2 * tw_r, 2 * tw_r)
        p.setBrush(QBrush(tw_fill))
        p.setPen(QPen(cab_edge, max(1.0, draw_w * 0.02)))
        p.drawEllipse(tw_rect)

        wf_r = cab_w * 0.30
        wf_cx = cx
        wf_cy = cab_y + cab_h * 0.68
        wf_rect = QRectF(wf_cx - wf_r, wf_cy - wf_r, 2 * wf_r, 2 * wf_r)
        p.setBrush(QBrush(wf_fill))
        p.setPen(QPen(cab_edge, max(1.2, draw_w * 0.03)))
        p.drawEllipse(wf_rect)
        
        arc_width = cab_w * 2.5
        arc_height = r.height() * 0.25
        arc_rect = QRectF(
            cx - arc_width / 2.0,
            cab_y + cab_h + r.height() * 0.02,
            arc_width,
            arc_height
        )

        font = QFont("Arial")
        font.setPointSizeF(max(8, cab_w * 0.45)) 
        font.setBold(True)
        p.setFont(font)
        p.setPen(QPen(text_col, 1.2))
        
        label_rect = QRectF(
            cab_x,
            cab_y + cab_h - r.height() * 0.02,
            cab_w,
            arc_rect.top() - (cab_y + cab_h) + r.height() * 0.3
        )

        base_thick = max(2.0, r.height() * 0.025)
        extra_thick = r.height() * 0.09 * level
        pen_width = base_thick + extra_thick

        start_angle = 210 * 16
        span_angle = 120 * 16

        base_grey = QColor(120, 120, 120, 80)
        p.setPen(QPen(base_grey, base_thick, cap=Qt.PenCapStyle.RoundCap))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawArc(arc_rect, start_angle, span_angle)

        if level > 0.01:
            glow_color = QColor(alert_col)
            glow_color.setAlpha(int(alert_col.alpha() * 0.5)) 
            glow_pen = QPen(glow_color, pen_width * 1.9,
                            cap=Qt.PenCapStyle.RoundCap)
            p.setPen(glow_pen)
            p.drawArc(arc_rect, start_angle, span_angle)

            main_pen = QPen(alert_col, pen_width,
                            cap=Qt.PenCapStyle.RoundCap)
            p.setPen(main_pen)
            p.drawArc(arc_rect, start_angle, span_angle)

        p.setPen(QPen(text_col, 1.2))
        p.drawText(label_rect,
                   Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignVCenter,
                   self.label)
        p.end()


class SubGuardAnalyzer(QWidget):
    def __init__(self, parent=None, fft_size=DEFAULT_FFT_SIZE):
        super().__init__(parent)
        self.current_fft_size = fft_size
        self.mute_states = {"L": False, "C": False, "R": False}

        # Filter at 10Hz (internal calc) for clean 20Hz display
        self.sos_filter = butter(4, 10, 'hp', fs=DEFAULT_SAMPLE_RATE, output='sos')
        
        self.init_dsp_context(self.current_fft_size)

        self.SMOOTH_ALPHA = 0.25
        self.alert_decay_seconds = 1.0
        self.frame_dt = 0.012 
        self.alert_decay_coeff = 0.1

        self.speaker_decay_seconds = 0.10
        self.speaker_decay_coeff = 0.1
        self.spk_prev_L = 0.0
        self.spk_prev_C = 0.0
        self.spk_prev_R = 0.0

        self.ALERT_THRESH = -0.33 
        self.alert_gain = 3.5

        self.conflict_gradient = [
            (0.00, (0,   0,   0,   0)),
            (0.10, (51,  32,  0,  30)),
            (0.25, (136, 68,  0, 90)),
            (0.45, (221, 136, 0, 170)),
            (0.65, (255, 85,  0, 230)),
            (1.00, (255, 34,  0, 255)),
        ]

        self.blur_bars = []
        self.heatmap_bars = []

        self.ctrl_widget = QWidget()
        self.ind_L = None
        self.ind_C = None
        self.ind_R = None
        self.speaker_panel = QWidget()

        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(0, 0, 0, 0)

        self.fft_plot_container = QWidget()
        fft_layout = QVBoxLayout(self.fft_plot_container)
        fft_layout.setContentsMargins(0, 0, 0, 0)
        self.setup_fft_plot(fft_layout)
        main_layout.addWidget(self.fft_plot_container, stretch=1)

        self.create_controls(self.ctrl_widget)
        self.build_speaker_objects()
        self.update_fft_params()

    def init_dsp_context(self, fft_size):
        self.current_fft_size = fft_size
        self.window_func = windows.blackmanharris(fft_size).astype(np.float32)
        self.norm_factor = np.sum(self.window_func) / 2.0
        freqs = np.fft.rfftfreq(fft_size, d=1.0 / DEFAULT_SAMPLE_RATE)
        
        # Internal calc starts at 10Hz
        self.mask_indices = np.where((freqs >= 10) & (freqs <= 150))[0]

        self.freqs_log_view = np.log10(freqs[self.mask_indices])
        self.freqs_log_view = np.nan_to_num(self.freqs_log_view, nan=1.0, neginf=1.0)

        freq_band = freqs[self.mask_indices]
        f_low = 20.0
        f_high = 150.0
        
        if f_high > f_low:
             pos = (np.log10(freq_band) - np.log10(f_low)) / (np.log10(f_high) - np.log10(f_low))
             pos = np.clip(pos, 0.0, 1.0)
             tilt = -12.0 * (1.0 - pos)
             bump = 3.0 * np.exp(-0.5 * ((freq_band - 80.0) / 25.0) ** 2)
             self.ear_weight_db = (tilt + bump).astype(np.float32)
        else:
             self.ear_weight_db = np.zeros(len(freq_band), dtype=np.float32)

        num_points = len(self.mask_indices)
        self.prev_facade_vis = np.full(num_points, -60.0, dtype=np.float32)
        self.prev_lfe_vis = np.full(num_points, -60.0, dtype=np.float32)
        self.prev_conflict_level = np.zeros(num_points, dtype=np.float32)
        self.prev_conflict_L = np.zeros(num_points, dtype=np.float32)
        self.prev_conflict_C = np.zeros(num_points, dtype=np.float32)
        self.prev_conflict_R = np.zeros(num_points, dtype=np.float32)

    def rebuild_plot_items(self):
        for bar in self.heatmap_bars:
            self.fft_plot.removeItem(bar)
        self.heatmap_bars.clear()

        x_log = self.freqs_log_view.copy()
        if len(x_log) > 1:
            bar_width = float(np.mean(np.diff(x_log)))
        else:
            bar_width = (self.x_max_log - self.x_min_log)

        for x in x_log:
            bar = pg.BarGraphItem(
                x=[x], height=[96.0], width=bar_width, y0=-90.0,
                brush=pg.mkBrush(0, 0, 0, 0), pen=None
            )
            bar.setZValue(0.5)
            self.fft_plot.addItem(bar)
            self.heatmap_bars.append(bar)

    def create_controls(self, parent_widget):
        layout = QHBoxLayout(parent_widget)
        layout.setContentsMargins(10, 10, 10, 10)

        grp_smooth = QGroupBox("Responsiveness")
        v_smooth = QVBoxLayout()
        grp_smooth.setLayout(v_smooth)

        h_inertia = QHBoxLayout()
        h_inertia.addWidget(QLabel("Inertia:"))
        self.lbl_inertia_val = QLabel("70%")
        self.lbl_inertia_val.setFixedWidth(50)
        h_inertia.addWidget(self.lbl_inertia_val)
        v_smooth.addLayout(h_inertia)

        self.slider_inertia = QSlider(Qt.Orientation.Horizontal)
        self.slider_inertia.setRange(0, 100)
        self.slider_inertia.setValue(70)
        self.slider_inertia.valueChanged.connect(self.update_fft_params)
        v_smooth.addWidget(self.slider_inertia)
        layout.addWidget(grp_smooth)

        grp_alert = QGroupBox("Phase Alert")
        v_alert = QVBoxLayout()
        grp_alert.setLayout(v_alert)

        h_thresh = QHBoxLayout()
        h_thresh.addWidget(QLabel("Phase Alert:"))
        self.lbl_thresh_val = QLabel("-0.33")
        self.lbl_thresh_val.setFixedWidth(80)
        h_thresh.addWidget(self.lbl_thresh_val)
        v_alert.addLayout(h_thresh)

        self.slider_thresh = QSlider(Qt.Orientation.Horizontal)
        self.slider_thresh.setRange(-100, 0)
        self.slider_thresh.setValue(-33)
        self.slider_thresh.valueChanged.connect(self.update_fft_params)
        v_alert.addWidget(self.slider_thresh)

        h_decay = QHBoxLayout()
        h_decay.addWidget(QLabel("Decay:"))
        self.lbl_alert_decay_val = QLabel("1.00 s")
        self.lbl_alert_decay_val.setFixedWidth(60)
        h_decay.addWidget(self.lbl_alert_decay_val)
        v_alert.addLayout(h_decay)

        self.slider_alert_decay = QSlider(Qt.Orientation.Horizontal)
        self.slider_alert_decay.setRange(100, 4000)
        self.slider_alert_decay.setValue(1000)
        self.slider_alert_decay.valueChanged.connect(self.update_fft_params)
        v_alert.addWidget(self.slider_alert_decay)
        layout.addWidget(grp_alert)
        layout.addStretch(1)

    def build_speaker_objects(self):
        self.ind_L = SpeakerIndicator("L")
        self.ind_C = SpeakerIndicator("C")
        self.ind_R = SpeakerIndicator("R")
        self.ind_L.toggled.connect(self.on_speaker_toggled)
        self.ind_C.toggled.connect(self.on_speaker_toggled)
        self.ind_R.toggled.connect(self.on_speaker_toggled)

    def on_speaker_toggled(self, label: str, is_active: bool):
        if label in self.mute_states:
            self.mute_states[label] = not is_active

    def update_fft_params(self):
        inertia_ui = self.slider_inertia.value()
        self.lbl_inertia_val.setText(f"{inertia_ui}%")
        ratio = inertia_ui / 100.0
        inertia_internal = 0.5 + 0.48 * ratio
        alpha = 1.0 - inertia_internal
        alpha = float(np.clip(alpha, 0.02, 0.5))
        self.SMOOTH_ALPHA = alpha

        thr = self.slider_thresh.value() / 100.0
        self.lbl_thresh_val.setText(f"{thr:.2f}")
        self.ALERT_THRESH = thr

        decay_ms = self.slider_alert_decay.value()
        decay_sec = decay_ms / 1000.0
        self.alert_decay_seconds = decay_sec
        self.lbl_alert_decay_val.setText(f"{decay_sec:.2f} s")
        
        coeff_decay = self.frame_dt / max(decay_sec, 1e-3)
        coeff_decay = float(np.clip(coeff_decay, 0.0, 1.0))
        self.alert_decay_coeff = coeff_decay
        self.speaker_decay_coeff = min(self.frame_dt / self.speaker_decay_seconds, 1.0)

    def setup_fft_plot(self, layout):
        self.fft_plot = pg.PlotWidget()
        self.fft_plot.setBackground('#121212')
        self.fft_plot.setLogMode(x=False, y=False)
        plot_item = self.fft_plot.getPlotItem()
        plot_item.hideButtons()
        plot_item.setMenuEnabled(False) 
        if hasattr(plot_item, 'layout') and plot_item.layout is not None:
            plot_item.layout.setContentsMargins(0, 0, 0, 0)
            plot_item.layout.setSpacing(0)
        plot_item.setContentsMargins(0, 0, 0, 0)
        plot_item.setClipToView(True)
        plot_item.setDownsampling(auto=False)

        # --- DISPLAY ZOOMED AT 20Hz ---
        self.x_min_log = np.log10(20)
        self.x_max_log = np.log10(150)
        plot_item.setXRange(self.x_min_log, self.x_max_log, padding=0)
        plot_item.setYRange(-90, 6, padding=0)
        plot_item.enableAutoRange(x=False, y=False)
        plot_item.setMouseEnabled(x=False, y=False)
        plot_item.setTitle("SubGUARD Analyzer : 20Hz - 150Hz")
        plot_item.getAxis('left').setLabel('Level (dBFS)')
        y_ticks = [(0, '0'), (-18, '-18'), (-36, '-36'), (-54, '-54'), (-72, '-72'), (-90, '-90')]
        plot_item.getAxis('left').setTicks([y_ticks])
        plot_item.getAxis('bottom').setLabel('Frequency (Hz)')
        freq_values = [20, 30, 40, 50, 60, 80, 100, 120, 150]
        all_ticks = [(np.log10(f), str(f)) for f in freq_values]
        plot_item.getAxis('bottom').setTicks([all_ticks])
        plot_item.showGrid(x=True, y=True, alpha=0.3)

        self.lfe_cut_line = pg.InfiniteLine(
            pos=np.log10(120), angle=90,
            pen=pg.mkPen('#666666', width=1, style=Qt.PenStyle.DashLine)
        )
        self.lfe_cut_line.setZValue(0)
        self.fft_plot.addItem(self.lfe_cut_line)
        self.rebuild_plot_items()

        x_cut = np.log10(120.0)
        if x_cut < self.x_max_log:
            region_width = self.x_max_log - x_cut
            N_blur = 12
            self.blur_bars = []
            for i in range(N_blur):
                x_center = x_cut + (i + 0.5) * (region_width / N_blur)
                width = region_width / N_blur * 3.0
                t = (i + 1) / N_blur
                base_alpha = int(255 * (t ** 1.6) * 0.55)
                alpha = min(int(base_alpha * 1.8), 255)
                blur_bar = pg.BarGraphItem(
                    x=[x_center], height=[100.0], width=width, y0=-90.0,
                    brush=pg.mkBrush(0, 0, 0, alpha), pen=None
                )
                blur_bar.setZValue(5)
                self.fft_plot.addItem(blur_bar)
                self.blur_bars.append(blur_bar)
        
        dummy_x = np.linspace(self.x_min_log, self.x_max_log, 100)
        dummy_y = np.full(100, -120.0)
        self.facade_curve = pg.PlotCurveItem(
            x=dummy_x, y=dummy_y, pen=pg.mkPen('#0077FF', width=2),
            brush=pg.mkBrush(0, 119, 255, 110), fillLevel=-120, antialias=True
        )
        self.facade_curve.setZValue(1)
        self.fft_plot.addItem(self.facade_curve)
        self.lfe_curve = pg.PlotCurveItem(
            x=dummy_x, y=dummy_y, pen=pg.mkPen('#00FF00', width=1.5),
            brush=pg.mkBrush(0, 255, 0, 100), fillLevel=-120, antialias=True
        )
        self.lfe_curve.setZValue(2)
        self.fft_plot.addItem(self.lfe_curve)
        self.base_line = self.facade_curve
        layout.addWidget(self.fft_plot)

    def clean_array(self, arr):
        arr = np.nan_to_num(arr, nan=-120.0, posinf=10.0, neginf=-120.0)
        return np.clip(arr, -120.0, 10.0).astype(np.float32)

    def gradient_color(self, val):
        val = float(np.clip(val, 0.0, 1.0))
        for i in range(len(self.conflict_gradient) - 1):
            p0, c0 = self.conflict_gradient[i]
            p1, c1 = self.conflict_gradient[i + 1]
            if val >= p0 and val <= p1:
                t = (val - p0) / max((p1 - p0), 1e-6)
                r = int(c0[0] + t * (c1[0] - c0[0]))
                g = int(c0[1] + t * (c1[1] - c0[1]))
                b = int(c0[2] + t * (c1[2] - c0[2]))
                a = int(c0[3] + t * (c1[3] - c0[3]))
                return (r, g, b, a)
        return (0, 0, 0, 0)

    def process_fft_analysis(self, data_calibrated, channel_labels):
        if data_calibrated.shape[0] < self.current_fft_size:
            return None
        indices = {label: i for i, label in enumerate(channel_labels)}
        idx_L = indices.get("L")
        idx_C = indices.get("C")
        idx_R = indices.get("R")
        idx_LFE = indices.get("LFE")
        if idx_LFE is None:
            return None
        data_frame = data_calibrated[-self.current_fft_size:]
        ATTENUATION_FACTOR = 10 ** (-3.0 / 20.0)
        L_sig = C_sig = R_sig = None
        if idx_L is not None and not self.mute_states["L"]:
            L_sig = data_frame[:, idx_L] * ATTENUATION_FACTOR
        if idx_C is not None and not self.mute_states["C"]:
            C_sig = data_frame[:, idx_C] * ATTENUATION_FACTOR
        if idx_R is not None and not self.mute_states["R"]:
            R_sig = data_frame[:, idx_R] * ATTENUATION_FACTOR
        signals = [s for s in [L_sig, C_sig, R_sig] if s is not None]
        if not signals:
            Façade_Signal = np.zeros(self.current_fft_size, dtype=np.float32)
        else:
            Façade_Signal = np.sum(np.stack(signals, axis=0), axis=0)
        LFE_Signal = data_frame[:, idx_LFE]
        if L_sig is not None: L_sig = np.nan_to_num(L_sig).astype(np.float32)
        if C_sig is not None: C_sig = np.nan_to_num(C_sig).astype(np.float32)
        if R_sig is not None: R_sig = np.nan_to_num(R_sig).astype(np.float32)
        Façade_Signal = np.nan_to_num(Façade_Signal).astype(np.float32)
        LFE_Signal = np.nan_to_num(LFE_Signal).astype(np.float32)
        if L_sig is not None: L_sig = sosfilt(self.sos_filter, L_sig)
        if C_sig is not None: C_sig = sosfilt(self.sos_filter, C_sig)
        if R_sig is not None: R_sig = sosfilt(self.sos_filter, R_sig)
        Façade_Signal = sosfilt(self.sos_filter, Façade_Signal)
        LFE_Signal = sosfilt(self.sos_filter, LFE_Signal)
        window = self.window_func
        LFE_FFT_full = np.fft.rfft(LFE_Signal * window)
        LFE_FFT = LFE_FFT_full[self.mask_indices]
        LFE_Mag = np.abs(LFE_FFT)
        MIN_AMP = 1e-9
        val_lfe = np.maximum((LFE_Mag / self.norm_factor), MIN_AMP)
        LFE_Amp_Raw = 20 * np.log10(val_lfe)
        LFE_Amp_Raw = self.clean_array(LFE_Amp_Raw).astype(np.float32)
        Façade_FFT_full = np.fft.rfft(Façade_Signal * window)
        Façade_FFT = Façade_FFT_full[self.mask_indices]
        Façade_Mag = np.abs(Façade_FFT)
        val_f = np.maximum((Façade_Mag / self.norm_factor), MIN_AMP)
        Façade_Amp_Raw = 20 * np.log10(val_f)
        Façade_Amp_Raw = self.clean_array(Façade_Amp_Raw).astype(np.float32)
        n_bins = len(self.mask_indices)
        Severity_L = np.zeros(n_bins, dtype=np.float32)
        Severity_C = np.zeros(n_bins, dtype=np.float32)
        Severity_R = np.zeros(n_bins, dtype=np.float32)
        def compute_severity(sig):
            if sig is None:
                return np.zeros(n_bins, dtype=np.float32), np.zeros(n_bins, dtype=np.float32)
            X_FFT_full = np.fft.rfft(sig * window)
            X_FFT = X_FFT_full[self.mask_indices]
            X_Mag = np.abs(X_FFT)
            val_x = np.maximum((X_Mag / self.norm_factor), MIN_AMP)
            X_Amp = 20 * np.log10(val_x)
            X_Amp = self.clean_array(X_Amp).astype(np.float32)
            X_Norm = (X_Amp - (-120.0)) / 160.0
            LFE_Norm = (LFE_Amp_Raw - (-120.0)) / 160.0
            gate = (X_Amp > -80.0) & (LFE_Amp_Raw > -80.0)
            Cross = X_FFT * np.conj(LFE_FFT)
            Den = (X_Mag * LFE_Mag) + MIN_AMP
            Phase_Corr = np.real(Cross) / Den
            CorrMag = np.abs(Phase_Corr)
            CorrMag = np.clip(CorrMag, 0.0, 1.0).astype(np.float32)
            Sev = X_Norm * LFE_Norm * CorrMag
            Sev = np.clip(Sev, 0.0, 1.0)
            Sev *= gate.astype(np.float32)
            return X_Amp, Sev
        if L_sig is not None: _, Severity_L = compute_severity(L_sig)
        if C_sig is not None: _, Severity_C = compute_severity(C_sig)
        if R_sig is not None: _, Severity_R = compute_severity(R_sig)
        Severity_raw = np.maximum.reduce([Severity_L, Severity_C, Severity_R])
        Façade_Amp_Raw64 = Façade_Amp_Raw.astype(np.float64)
        LFE_Amp_Raw64 = LFE_Amp_Raw.astype(np.float64)
        if Façade_Amp_Raw64.shape[0] >= 5:
            Façade_Amp_Raw64 = savgol_filter(Façade_Amp_Raw64, 5, 1, mode='interp')
            LFE_Amp_Raw64 = savgol_filter(LFE_Amp_Raw64, 5, 1, mode='interp')
        Façade_Amp_Raw = Façade_Amp_Raw64.astype(np.float32)
        LFE_Amp_Raw = LFE_Amp_Raw64.astype(np.float32)
        Façade_Amp_Vis = Façade_Amp_Raw.copy()
        LFE_Amp_Vis = LFE_Amp_Raw.copy()
        if (self.prev_facade_vis is not None) and (len(self.prev_facade_vis) == len(Façade_Amp_Vis)):
            alpha = float(self.SMOOTH_ALPHA)
            inv_alpha = 1.0 - alpha
            Façade_Amp_Vis = inv_alpha * self.prev_facade_vis + alpha * Façade_Amp_Raw
            LFE_Amp_Vis = inv_alpha * self.prev_lfe_vis + alpha * LFE_Amp_Raw
        Façade_Amp_Vis = self.clean_array(Façade_Amp_Vis)
        LFE_Amp_Vis = self.clean_array(LFE_Amp_Vis)
        self.prev_facade_vis = Façade_Amp_Vis
        self.prev_lfe_vis = LFE_Amp_Vis
        return (Façade_Amp_Vis, LFE_Amp_Vis, Severity_raw, Façade_Amp_Raw, LFE_Amp_Raw, Severity_L, Severity_C, Severity_R)

    def update_spectrum(self, input_buffer, channel_labels, frame_interval_sec):
        self.frame_dt = frame_interval_sec
        self.update_fft_params() 
        plot_item = self.fft_plot.getPlotItem()
        plot_item.setXRange(self.x_min_log, self.x_max_log, padding=0)
        plot_item.setYRange(-90, 6, padding=0)
        fft_data = self.process_fft_analysis(input_buffer, channel_labels)
        if fft_data is None:
            return
        (Façade_Vis, LFE_Vis, Severity_raw, Façade_Amp_Raw, LFE_Amp_Raw, Severity_L, Severity_C, Severity_R) = fft_data
        x_data = self.freqs_log_view
        if self.base_line is None or len(self.facade_curve.xData) != len(x_data):
            self.prev_facade_vis = None
            self.prev_lfe_vis = None
            self.prev_conflict_level = np.zeros_like(LFE_Vis)
            self.prev_conflict_L = np.zeros_like(LFE_Vis)
            self.prev_conflict_C = np.zeros_like(LFE_Vis)
            self.prev_conflict_R = np.zeros_like(LFE_Vis)
            self.spk_prev_L = 0.0
            self.spk_prev_C = 0.0
            self.spk_prev_R = 0.0
        facade_display = self.clean_array(Façade_Vis + self.ear_weight_db)
        lfe_display = self.clean_array(LFE_Vis + self.ear_weight_db)
        self.facade_curve.setData(x_data, facade_display)
        self.lfe_curve.setData(x_data, lfe_display)
        thr = float(self.ALERT_THRESH)
        thr_abs = float(np.clip(abs(thr), 0.0, 0.99))
        def map_severity(sev_array):
            sev = sev_array.astype(np.float32)
            if thr_abs > 0.0:
                sev = np.clip((sev - thr_abs) / (1.0 - thr_abs), 0.0, 1.0)
            sev = np.clip(sev * self.alert_gain, 0.0, 1.0)
            return sev
        Sev_global = map_severity(Severity_raw)
        Sev_L = map_severity(Severity_L)
        Sev_C = map_severity(Severity_C)
        Sev_R = map_severity(Severity_R)
        attack_c = 0.8
        decay_c = float(self.alert_decay_coeff)
        def smooth_levels(prev, target):
            target = np.clip(target, 0.0, 1.0).astype(np.float32)
            rising = target > prev
            new = np.empty_like(target)
            new[rising] = (1.0 - attack_c) * prev[rising] + attack_c * target[rising]
            new[~rising] = (1.0 - decay_c) * prev[~rising] + decay_c * target[~rising]
            new = np.clip(new, 0.0, 1.0)
            vis = np.sqrt(new)
            return new, vis
        self.prev_conflict_level, vis_global = smooth_levels(self.prev_conflict_level, Sev_global)
        self.prev_conflict_L, vis_L = smooth_levels(self.prev_conflict_L, Sev_L)
        self.prev_conflict_C, vis_C = smooth_levels(self.prev_conflict_C, Sev_C)
        self.prev_conflict_R, vis_R = smooth_levels(self.prev_conflict_R, Sev_R)
        if len(self.heatmap_bars) == len(vis_global):
            for i, bar in enumerate(self.heatmap_bars):
                color = self.gradient_color(float(vis_global[i]))
                bar.setOpts(brush=pg.mkBrush(*color))
        level_L_raw = float(np.max(Sev_L)) if Sev_L.size else 0.0
        level_C_raw = float(np.max(Sev_C)) if Sev_C.size else 0.0
        level_R_raw = float(np.max(Sev_R)) if Sev_R.size else 0.0
        spk_attack = 0.7
        spk_decay = float(self.speaker_decay_coeff)
        def smooth_scalar(prev, target):
            target = max(0.0, min(1.0, target))
            if target > prev: return (1.0 - spk_attack) * prev + spk_attack * target
            else: return (1.0 - spk_decay) * prev + spk_decay * target
        self.spk_prev_L = smooth_scalar(self.spk_prev_L, level_L_raw)
        self.spk_prev_C = smooth_scalar(self.spk_prev_C, level_C_raw)
        self.spk_prev_R = smooth_scalar(self.spk_prev_R, level_R_raw)
        if self.ind_L is not None: self.ind_L.setLevel(self.spk_prev_L)
        if self.ind_C is not None: self.ind_C.setLevel(self.spk_prev_C)
        if self.ind_R is not None: self.ind_R.setLevel(self.spk_prev_R)


# --- IMPORTANT: Main Class ---
class SubGuardApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SubGUARD")
        self.setGeometry(100, 100, 1000, 800)
        self.setMinimumSize(950, 600)
        
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #1E1E1E;
                color: #E6E6E6;
                font-family: Arial;
            }
            QGroupBox {
                border: 1px solid #444;
                border-radius: 5px;
                margin-top: 10px;
                font-weight: bold;
                color: #E6E6E6;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                padding: 0 5px;
                color: #AAA;
            }
            QPushButton {
                background-color: #333;
                border: 1px solid #555;
                border-radius: 4px;
                padding: 4px;
                color: #E6E6E6;
            }
            QPushButton:hover {
                background-color: #444;
            }
            QPushButton:checked {
                background-color: #555;
                border-color: #888;
            }
            QComboBox {
                background-color: #252525;
                border: 1px solid #555;
                border-radius: 4px;
                padding: 4px;
                color: #E6E6E6;
            }
            QComboBox::drop-down {
                border: 0px;
            }
            QCheckBox {
                color: #E6E6E6;
                spacing: 5px;
            }
            QCheckBox::indicator {
                width: 13px;
                height: 13px;
            }
        """)

        self.stream = None
        self.fft_size = DEFAULT_FFT_SIZE
        self.timer_interval = 12 
        self.fft_buffer = np.zeros((self.fft_size, MAX_CHANNELS), dtype=np.float32)
        
        self.current_vu_level = 0.0
        self.current_format = "5.1 ITU - (L, R, C, LFE, Ls, Rs)"
        self.current_channels = CHANNEL_MAPPINGS[self.current_format]
        self.current_labels = CHANNEL_SCHEMES[self.current_format]

        self.device_map = {}
        self.lfe_analyzer = SubGuardAnalyzer(self, fft_size=self.fft_size)

        self.settings_panel = None
        self.btn_toggle_settings = None
        self.header_logo = None
        self.top_bar = None

        self.setup_ui()
        self.setup_audio_stream()

        self.timer = QTimer()
        self.timer.timeout.connect(self.update_ui)
        self.timer.start(self.timer_interval)

    def setup_ui(self):
        central_widget = QWidget()
        self.main_layout = QVBoxLayout(central_widget)
        self.main_layout.setContentsMargins(0, 0, 0, 0)
        self.main_layout.setSpacing(0)

        self.top_bar = GradientHeader()
        h_top = QHBoxLayout(self.top_bar)
        h_top.setContentsMargins(20, 20, 20, 20)
        h_top.setSpacing(0)

        self.header_logo = SubGuardLogoWidget()
        self.lfe_analyzer.ind_L.toggled.connect(self.lfe_analyzer.on_speaker_toggled)
        self.lfe_analyzer.ind_C.toggled.connect(self.lfe_analyzer.on_speaker_toggled)
        self.lfe_analyzer.ind_R.toggled.connect(self.lfe_analyzer.on_speaker_toggled)

        h_top.addWidget(self.header_logo, stretch=1)
        h_top.addWidget(self.lfe_analyzer.ind_L, stretch=1)
        h_top.addWidget(self.lfe_analyzer.ind_C, stretch=1)
        h_top.addWidget(self.lfe_analyzer.ind_R, stretch=1)

        self.main_layout.addWidget(self.top_bar, stretch=1)

        self.settings_panel = QWidget()
        h_settings = QHBoxLayout(self.settings_panel)
        h_settings.setContentsMargins(10, 0, 10, 10)

        audio_grp = self.create_audio_controls()
        perf_grp = self.create_perf_controls()
        
        h_settings.addWidget(audio_grp)
        h_settings.addWidget(perf_grp)
        h_settings.addWidget(self.lfe_analyzer.ctrl_widget)
        h_settings.addStretch(1)

        self.main_layout.addWidget(self.settings_panel)
        self.main_layout.addWidget(self.lfe_analyzer.fft_plot_container, stretch=3)

        self.create_overlay_settings()
        self.setCentralWidget(central_widget)
        self.populate_device_selector()

    def create_overlay_settings(self):
        self.btn_toggle_settings = QPushButton("⚙ ▾", self.lfe_analyzer.fft_plot_container)
        self.btn_toggle_settings.setCheckable(True)
        self.btn_toggle_settings.setChecked(True)
        self.btn_toggle_settings.setFixedSize(50, 24)
        self.btn_toggle_settings.setStyleSheet("""
            QPushButton {
                background-color: rgba(40, 40, 40, 150);
                color: #AAA;
                border: 1px solid #555;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: rgba(60, 60, 60, 200);
                color: #FFF;
            }
            QPushButton:checked {
                background-color: rgba(80, 80, 80, 200);
            }
        """)
        self.btn_toggle_settings.move(10, 10)
        self.btn_toggle_settings.clicked.connect(self.toggle_settings_panel)

    def toggle_settings_panel(self):
        visible = self.btn_toggle_settings.isChecked()
        self.settings_panel.setVisible(visible)
        self.btn_toggle_settings.setText("⚙ ▾" if visible else "⚙ ▴")

    def create_audio_controls(self):
        audio_grp = QGroupBox("Source & Format")
        v_audio = QVBoxLayout(audio_grp)
        v_audio.addWidget(QLabel("Channel Format:"))
        self.format_selector = QComboBox()
        self.format_selector.addItems(CHANNEL_MAPPINGS.keys())
        self.format_selector.setCurrentText(self.current_format)
        self.format_selector.currentIndexChanged.connect(self.on_format_changed)
        v_audio.addWidget(self.format_selector)
        v_audio.addWidget(QLabel("Input Device:"))
        self.device_selector = QComboBox()
        self.device_selector.currentIndexChanged.connect(self.on_device_changed)
        v_audio.addWidget(self.device_selector)
        self.status_label = QLabel("Status: Waiting...")
        v_audio.addWidget(self.status_label)
        return audio_grp

    def create_perf_controls(self):
        grp = QGroupBox("Performance")
        v_layout = QVBoxLayout(grp)
        v_layout.addWidget(QLabel("Refresh Rate:"))
        self.fps_selector = QComboBox()
        self.fps_selector.addItem("90 FPS", 12)
        self.fps_selector.addItem("60 FPS", 17) 
        self.fps_selector.addItem("30 FPS", 33) 
        self.fps_selector.currentIndexChanged.connect(self.on_fps_changed)
        v_layout.addWidget(self.fps_selector)
        v_layout.addWidget(QLabel("FFT Precision:"))
        self.fft_selector = QComboBox()
        self.fft_selector.addItem("UltraPrecision (32k)", 32768)
        self.fft_selector.addItem("NormalPrecision (16k)", 16384)
        self.fft_selector.addItem("Mode Eco (8k)", 8192)
        idx_normal = self.fft_selector.findData(16384)
        if idx_normal != -1: self.fft_selector.setCurrentIndex(idx_normal)
        self.fft_selector.currentIndexChanged.connect(self.on_precision_changed)
        v_layout.addWidget(self.fft_selector)
        self.chk_anim = QCheckBox("Back. Anim")
        self.chk_anim.setChecked(True)
        self.chk_anim.toggled.connect(self.on_anim_toggled)
        v_layout.addWidget(self.chk_anim)
        v_layout.addStretch(1)
        return grp

    def on_anim_toggled(self, checked: bool):
        if self.top_bar: self.top_bar.set_animation_enabled(checked)

    def on_fps_changed(self):
        new_interval = self.fps_selector.currentData()
        if new_interval:
            self.timer_interval = int(new_interval)
            if self.timer.isActive(): self.timer.start(self.timer_interval)

    def on_precision_changed(self):
        new_fft = self.fft_selector.currentData()
        if not new_fft: return
        new_fft = int(new_fft)
        if new_fft == self.fft_size: return
        was_running = False
        if self.stream and self.stream.active:
            self.stream.stop()
            was_running = True
        self.timer.stop()
        self.fft_size = new_fft
        self.fft_buffer = np.zeros((self.fft_size, MAX_CHANNELS), dtype=np.float32)
        self.lfe_analyzer.init_dsp_context(self.fft_size)
        self.lfe_analyzer.rebuild_plot_items()
        if was_running and self.stream: self.stream.start()
        self.timer.start(self.timer_interval)

    def get_input_devices(self, required_channels):
        devices = sd.query_devices()
        input_devices = []
        for d in devices:
            if d['max_input_channels'] >= required_channels:
                name = f"{d['name']} ({d['max_input_channels']} ch max)"
                input_devices.append({'name': name, 'index': d['index'], 'max_ch': d['max_input_channels']})
        return input_devices

    def populate_device_selector(self):
        required_ch = CHANNEL_MAPPINGS[self.current_format]
        self.device_selector.clear()
        self.device_map = {}
        devices = self.get_input_devices(required_ch)
        if not devices:
            self.status_label.setText(f"Status: 🔴 No device found (≥{required_ch} ch).")
            self.device_selector.setCurrentIndex(-1)
            return
        for d in devices:
            self.device_selector.addItem(d['name'], d['index'])
            self.device_map[d['index']] = {'index': d['index'], 'max_ch': d['max_ch']}
        self.device_selector.setCurrentIndex(0)

    def on_format_changed(self):
        self.current_format = self.format_selector.currentText()
        self.current_channels = CHANNEL_MAPPINGS[self.current_format]
        self.current_labels = CHANNEL_SCHEMES[self.current_format]
        self.populate_device_selector()
        self.on_device_changed()

    def on_device_changed(self):
        if self.device_selector.count() == 0: return
        device_id = self.device_selector.currentData()
        self.setup_audio_stream(device_id=device_id)

    def setup_audio_stream(self, device_id=None):
        if self.stream and self.stream.active:
            self.stream.stop()
            self.stream.close()
            self.stream = None
        channels_to_use = self.current_channels
        if device_id is None:
             self.status_label.setText("Status: Waiting for device...")
             return
        try:
            self.stream = sd.InputStream(
                samplerate=DEFAULT_SAMPLE_RATE, blocksize=DEFAULT_CHUNK,
                channels=channels_to_use, device=device_id, callback=self.audio_callback
            )
            self.stream.start()
            self.status_label.setText(f"Status: 🟢 Connected to ID {device_id} ({self.device_selector.currentText()}).")
        except Exception as e:
            self.status_label.setText(f"Status: 🔴 Connection failed (ID {device_id}). Error: {e}")
            self.stream = None

    def audio_callback(self, indata, frames, time, status):
        if indata.shape[1] == self.current_channels:
            self.fft_buffer = np.roll(self.fft_buffer, -frames, axis=0)
            self.fft_buffer[-frames:, :self.current_channels] = indata

    def update_ui(self):
        if self.stream and self.stream.active:
            data_to_send = self.fft_buffer
            recent = data_to_send[-2048:] 
            rms = np.sqrt(np.mean(recent**2))
            db_val = 20 * np.log10(rms + 1e-9)
            normalized_level = np.clip((db_val + 60) / 60, 0.0, 1.0)
            if self.top_bar: self.top_bar.set_level(normalized_level)
        else:
            data_to_send = np.zeros((self.fft_size, MAX_CHANNELS), dtype=np.float32)
            if self.top_bar: self.top_bar.set_level(0.0)
        interval_sec = self.timer_interval / 1000.0
        self.lfe_analyzer.update_spectrum(data_to_send, self.current_labels, interval_sec)

    def closeEvent(self, event):
        if self.stream and self.stream.active:
            self.stream.stop()
            self.stream.close()
        super().closeEvent(event)

if __name__ == '__main__':
    if sys.platform == 'darwin':
        os.environ['QT_MAC_WANTS_LAYER'] = '1'
    try:
        app = QApplication(sys.argv)
        window = SubGuardApp()
        window.show()
        sys.exit(app.exec())
    except Exception as e:
        print(f"CRITICAL APPLICATION FAILURE : {e}")
        sys.exit(1)