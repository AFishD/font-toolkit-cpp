"""Shared default paths for the ftk tooling.

Nothing here is machine-specific: the repository root is derived from this
file's location, and every default can be overridden with an environment
variable or a command-line flag.

    FTK            path to the ftk CLI binary        (default <repo>/build/ftk[.exe])
    FTK_FONTS      path to a font corpus directory   (default <repo>/Fonts)
    FTK_TTX_ROOT   fontTools source checkout with Tests/*.ttx fixtures
                                                     (default <repo>/fonttools/Tests)
"""
import os

_HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(_HERE))  # cpp/tools -> cpp -> <repo>
_EXE = "ftk.exe" if os.name == "nt" else "ftk"

FTK = os.environ.get("FTK") or os.path.join(REPO_ROOT, "build", _EXE)
FONTS = os.environ.get("FTK_FONTS") or os.path.join(REPO_ROOT, "Fonts")
TTX_ROOT = os.environ.get("FTK_TTX_ROOT") or os.path.join(REPO_ROOT, "fonttools", "Tests")
