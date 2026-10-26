# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path

project_dir = Path(SPECPATH)
root_dir = project_dir.parent
a = Analysis(
    ["main.py"],
    pathex=[str(project_dir)],
    # Dependencies are collected from the active virtual environment.  Do not
    # use absolute DLL paths from a developer's Anaconda installation.
    binaries=[],
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
# PySide6 6.11's Qt6Core is linked against the Windows ICU ABI (unversioned
# UCNV_* exports).  PyInstaller can otherwise pick Anaconda's ICU 73 DLLs,
# whose exports have a _73 suffix and cause an entry-point error at startup.
_windows_icu = Path(r"C:\Windows\System32")
_bad_icu = {"icudt78.dll"}
a.binaries = [item for item in a.binaries if Path(item[0]).name.lower() not in _bad_icu]
for _index, _item in enumerate(a.binaries):
    _name = Path(_item[0]).name.lower()
    if _name in {"icu.dll", "icuin.dll", "icuuc.dll"}:
        _source = _windows_icu / _name
        if _source.is_file():
            a.binaries[_index] = (_item[0], str(_source), _item[2])
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
