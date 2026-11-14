# -*- coding: utf-8 -*-
"""Custom painted widgets (header, logo, speaker indicators)."""

import os
import sys

from PyQt6.QtWidgets import QWidget, QSizePolicy
from PyQt6.QtCore import Qt, QRectF, pyqtSignal, QPointF
from PyQt6.QtGui import (
    QPainter, QPen, QBrush, QColor, QFont, QCursor,
    QLinearGradient, QRadialGradient, QPixmap
)


def resource_path(relative_path):
    """Absolute path to a resource, in dev and inside a PyInstaller bundle."""
    base_path = getattr(sys, '_MEIPASS', None)
    if base_path is None:
        base_path = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base_path, relative_path)


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
        image_path = resource_path("logo.png")
        self.logo_pixmap = QPixmap(image_path)
        

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)

        if self.logo_pixmap.isNull():
            font = QFont("Arial")
            font.setPointSizeF(max(12.0, self.height() * 0.16))
            font.setBold(True)
            painter.setFont(font)
            painter.setPen(QColor(235, 235, 235))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                             "SubGUARD")
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
