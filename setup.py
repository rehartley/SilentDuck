#!/usr/bin/env python3
"""setup.py -- packaging metadata for SILENT DUCK (SD).

Build / install with:

    pip install .
    # or, for local development against the live source tree:
    pip install -e .

The project ships as a handful of flat modules rather than a package
directory, so `py_modules` is used in place of `packages`/find_packages().
"""

import pathlib
import re
import subprocess
import sys

import setuptools
from setuptools.command.build_py import build_py as _build_py

HERE = pathlib.Path(__file__).parent


def _read_version():
    """Scrape otp.py's `versionStr` so this file can't drift out of sync
    with the version `otp -v` reports at runtime."""
    src = (HERE / "otp.py").read_text(encoding="utf-8")
    match = re.search(r"""^versionStr\s*=\s*['"]v?([^'"]+)['"]""", src, re.MULTILINE)
    if not match:
        raise RuntimeError("could not find versionStr in otp.py")
    return match.group(1)


class BuildStandaloneExe(_build_py):
    """After the normal build, also run PyInstaller so `pip install .`
    produces a standalone executable (dist/otp[.exe]), not just the
    installed Python package.

    PyInstaller doesn't cross-compile, so this always builds for whatever
    OS/arch is running the install -- an .exe on Windows, a native binary
    on Linux/macOS.
    """

    def run(self):
        super().run()
        self.announce("running PyInstaller to build the standalone executable", level=2)
        subprocess.run(
            [sys.executable, "-m", "PyInstaller", "otp.spec", "--noconfirm"],
            cwd=HERE,
            check=True,
        )


LONG_DESCRIPTION = (HERE / "README.md").read_text(encoding="utf-8")

setuptools.setup(
    name="silentduck",
    version=_read_version(),
    description="SILENT DUCK (SD) - one time pad (OTP) encryption processing sampler",
    long_description=LONG_DESCRIPTION,
    long_description_content_type="text/markdown",
    author="Robert Hartley",
    url="https://github.com/rehartley/SilentDuck",
    license="0BSD",
    py_modules=["otp", "pytextedit"],
    python_requires=">=3.9",
    install_requires=[
        'windows-curses>=2.4.0; sys_platform == "win32"',
    ],
    cmdclass={
        "build_py": BuildStandaloneExe,
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Environment :: Console",
        "Environment :: Console :: Curses",
        "Intended Audience :: End Users/Desktop",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "Topic :: Security :: Cryptography",
    ],
)
