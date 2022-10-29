from setuptools import find_packages
import os
import subprocess
import sys

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(
            self.get_ext_fullpath(ext.name)))

        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        preset = "linux-python"
        debug = int(os.environ.get("DEBUG", 0)
                    ) if self.debug is None else self.debug
        cfg = "Debug" if debug else "Release"

        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE={cfg}",  # not used on MSVC, but no harm
            f"-DVERSION_INFO={self.distribution.get_version()}",
        ]

        if self.compiler.compiler_type == "msvc":
            cmake_args += [
                f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{cfg.upper()}={extdir}"]
            preset = "windows-python"
        elif self.plat_name.startswith("macosx"):
            preset = "macos-python"

        if os.environ.get("CIBUILDWHEEL", "0") == "1":
            preset = "ci-" + preset

        subprocess.check_call(["cmake", f"--preset {preset}"] + cmake_args)
        subprocess.check_call(["cmake", "--build", "--preset", "default"])


setup(
    name="rollnw",
    version="0.6.dev0",
    author="jmd",
    author_email="joshua.m.dean@gmail.com",
    include_package_data=True,
    description="An homage to Neverwinter Nights",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    ext_modules=[CMakeExtension("src")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
    extras_require={"test": ["pytest>=6.0"]},
    python_requires=">=3.8",
)
