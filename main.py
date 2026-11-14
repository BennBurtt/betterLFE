# -*- coding: utf-8 -*-
"""SubGUARD - LFE vs L/C/R low-frequency interaction analyzer."""

import os
import sys

import pyqtgraph as pg
from PyQt6.QtWidgets import QApplication


def main():
    if sys.platform == 'darwin':
        os.environ.setdefault('QT_MAC_WANTS_LAYER', '1')
    # GPU drawing as in the original app. SUBGUARD_OPENGL=0 falls back to
    # the software painter (also used automatically without PyOpenGL).
    use_gl = os.environ.get('SUBGUARD_OPENGL', '1') != '0'
    if use_gl:
        try:
            import OpenGL  # noqa: F401
        except Exception:
            use_gl = False
    pg.setConfigOptions(useOpenGL=use_gl, antialias=True)

    from betterlfe.app import SubGuardApp
    app = QApplication(sys.argv)
    window = SubGuardApp()
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
