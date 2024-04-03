from conans import ConanFile, CMake

class libiconvConan(ConanFile):
    name = "libiconv"
    version = "1.15.0"
    branch = "master"
    generators = "cmake"
    settings =  "os", "compiler", "arch", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = "shared=False", "fPIC=True"
    url = "http://github.com/kwallner/libiconv"
    scm = { "type": "git", "url": "auto", "revision": "auto" }
    no_copy_source = True
    
    def config_options(self):
        del self.settings.compiler.libcxx

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.env_info.libiconv_DIR = self.package_folder
   
