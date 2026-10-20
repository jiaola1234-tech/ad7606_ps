# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path

project_dir = Path(SPECPATH)
root_dir = project_dir.parent
hiddenimports = []

excludes = [
    "torch",
    "torchvision",
    "torchaudio",
    "triton",
    "torchgen",
    "functorch",
    "transformers",
    "onnxruntime",
    "pyarrow",
    "polars",
    "scipy",
    "h5py",
    "tables",
    "tensorflow",
    "jax",
    "jaxlib",
    "sklearn",
    "cv2",
    "PIL",
    "matlab",
    "win32com",
    "matplotlib",
    "IPython",
    "notebook",
]

datas = [
    (str(root_dir / "ana96.m"), "."),
]

block_cipher = None


a = Analysis(
    ['main.py'],
    pathex=[str(project_dir)],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=excludes,
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='FlowmeterHost',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='FlowmeterHost',
)
