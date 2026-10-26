# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path
from PyInstaller.utils.hooks import collect_dynamic_libs

project_dir = Path(SPECPATH)
root_dir = project_dir.parent
qt_binaries = []
for package in ("PySide6", "shiboken6"):
    for source, destination in collect_dynamic_libs(package):
        # Keep extension modules in their package folders, but put Qt/MSVC DLLs
        # at the bundle root so the Windows loader finds them deterministically.
        qt_binaries.append((source, "." if source.lower().endswith(".dll") else destination))
excludes = [
    "torch", "torchvision", "torchaudio", "triton", "torchgen", "functorch",
    "transformers", "onnxruntime", "pyarrow", "polars", "scipy", "h5py",
    "tables", "tensorflow", "jax", "jaxlib", "sklearn", "cv2", "PIL",
    "matplotlib", "IPython", "notebook",
]

a = Analysis(
    ["main.py"],
    pathex=[str(project_dir)],
    binaries=qt_binaries,
    datas=[(str(root_dir / "ana96.m"), ".")],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=excludes,
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz, a.scripts, [], exclude_binaries=True,
    name="SonarFlowMeterHost", debug=False, bootloader_ignore_signals=False,
    strip=False, upx=True, console=False, disable_windowed_traceback=False,
    argv_emulation=False, target_arch=None, codesign_identity=None,
    entitlements_file=None, uac_admin=True,
)
coll = COLLECT(
    exe, a.binaries, a.datas, a.zipfiles,
    strip=False, upx=True, upx_exclude=[], name="SonarFlowMeterHost",
)
