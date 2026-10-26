"""Application entry point.

PySide6 ships a set of Qt and MSVC DLLs next to its extension modules.  In a
PyInstaller one-file build Windows can start resolving ``QtCore.pyd`` before
the normal PyInstaller Qt runtime hook has established all DLL search paths.
That produces the rather unhelpful ``DLL load failed while importing QtCore``
message on some machines.  Establish the paths before importing any Qt code.
"""

import os
import sys
import ctypes
from pathlib import Path


def _prepare_qt_dll_search_path() -> None:
    if not getattr(sys, "frozen", False):
        return

    bundle_dir = Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    candidates = (bundle_dir, bundle_dir / "PySide6", bundle_dir / "shiboken6")
    existing = [str(path) for path in candidates if path.is_dir()]

    # AddDllDirectory is the reliable Windows loader API; PATH is retained for
    # older Windows builds and for third-party DLLs loaded by Qt plugins.
    add_dll_directory = getattr(os, "add_dll_directory", None)
    if add_dll_directory:
        for path in existing:
            try:
                add_dll_directory(path)
            except OSError:
                pass
    os.environ["PATH"] = os.pathsep.join(existing + [os.environ.get("PATH", "")])
    os.environ.setdefault("QT_PLUGIN_PATH", str(bundle_dir / "PySide6" / "plugins"))

    # Some Windows installations do not apply the updated DLL search path to
    # an extension module that is imported immediately afterwards.  Load the
    # native libraries by absolute path first; QtCore.pyd then reuses these
    # already-loaded modules instead of resolving them from the system PATH.
    dll_dirs = (bundle_dir, bundle_dir / "PySide6", bundle_dir / "shiboken6")
    preload_names = (
        "icuuc.dll",
        "MSVCP140.dll",
        "MSVCP140_1.dll",
        "MSVCP140_2.dll",
        "VCRUNTIME140.dll",
        "VCRUNTIME140_1.dll",
        "Qt6Core.dll",
        "shiboken6.abi3.dll",
        "pyside6.abi3.dll",
    )
    for name in preload_names:
        for directory in dll_dirs:
            candidate = directory / name
            if candidate.is_file():
                try:
                    ctypes.WinDLL(str(candidate))
                except OSError:
                    pass
                break


_prepare_qt_dll_search_path()

from app.main_window import main


if __name__ == "__main__":
    main()
