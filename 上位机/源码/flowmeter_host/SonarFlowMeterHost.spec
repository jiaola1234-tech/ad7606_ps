# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path

project_dir = Path(SPECPATH)
root_dir = project_dir.parent
conda_bin = Path.home() / "anaconda3" / "Library" / "bin"


a = Analysis(
    ["main.py"],
    pathex=[str(project_dir)],
    binaries=[
        (str(conda_bin / "ffi.dll"), "."),
        (str(conda_bin / "libcrypto-3-x64.dll"), "."),
        (str(conda_bin / "libssl-3-x64.dll"), "."),
        (str(conda_bin / "liblzma.dll"), "."),
        (str(conda_bin / "libbz2.dll"), "."),
    ],
    datas=[(str(root_dir / "ana96.m"), ".")],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
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
        "matplotlib",
        "IPython",
        "notebook",
    ],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="SonarFlowMeterHost",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    uac_admin=True,
)
