# -*- mode: python ; coding: utf-8 -*-
# Build with:  ./build_mac.sh      (or: pyinstaller --noconfirm SubGuard.spec)

import os

# Optional assets: dropped next to this file, they are picked up automatically.
datas = [('logo.png', '.')] if os.path.exists('logo.png') else []
icon = 'SubGuard.icns' if os.path.exists('SubGuard.icns') else None

a = Analysis(
    ['main.py'],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['tkinter', 'matplotlib', 'pytest'],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='SubGuard',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,              # UPX breaks code signatures on Apple Silicon
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,       # native: arm64 on an M-series Mac
    codesign_identity=None,
    entitlements_file=None,
    icon=icon,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name='SubGuard',
)

app = BUNDLE(
    coll,
    name='SubGuard.app',
    icon=icon,
    bundle_identifier='com.subguard.analyzer',
    info_plist={
        'NSMicrophoneUsageDescription':
            'SubGuard needs access to the audio input to analyze low frequencies.',
        'NSHighResolutionCapable': True,
        'CFBundleShortVersionString': '2.0.0',
    },
)
