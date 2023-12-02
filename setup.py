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

        preset = "linux"
        debug = int(os.environ.get("DEBUG", 0)
                    ) if self.debug is None else self.debug
        cfg = "Debug" if debug else "RelWithDebInfo"

        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPython3_ROOT_DIR={os.path.dirname(sys.executable)}",
            f"-DVERSION_INFO={self.distribution.get_version()}",
            "-DROLLNW_BUILD_PYTHON=ON",
            "-DROLLNW_BUILD_TESTS=OFF"
        ]

        if self.compiler.compiler_type == "msvc":
            cmake_args += [
                f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{cfg.upper()}={extdir}"]
            preset = "windows"
        elif self.plat_name.startswith("macosx"):
            preset = "macos"

        if os.environ.get("CIBUILDWHEEL", "0") == "1":
            preset = "cibuildwheel-" + preset

        subprocess.check_call(["cmake", f"--preset {preset}"] + cmake_args)
        subprocess.check_call(["cmake", "--build", "--preset", "default"])


setup(
    name="rollnw",
    version="0.17.dev0",
    author="jmd",
    author_email="joshua.m.dean@gmail.com",
    packages=["rollnw-stubs"],
    include_package_data=True,
    package_data={
        "rollnw-stubs": ['__init__.pyi', 'kernel.pyi', 'model.pyi', 'nwn1.pyi', 'script.pyi']
    },
    description="An homage to Neverwinter Nights",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    ext_modules=[CMakeExtension("src")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
    extras_require={"test": ["pytest>=6.0"]},
    python_requires=">=3.10",
)
