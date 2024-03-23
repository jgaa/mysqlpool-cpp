from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.build import check_max_cppstd, check_min_cppstd

class mysqlpoolRecipe(ConanFile):
    name = "mysqlpool"
    version = "0.1.0"

    # Optional metadata
    license = " BSL-1.0"
    author = "Jarle Aase jgaa@jgaa.com"
    url = "https://github.com/jgaa/mysqlpool-cpp"
    description = "Lightweight async connection-pool library, built on top of boost.mysql."
    topics = ("boost.mysql", "mysql", "mariadb")

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"logger": ["logfault", "clog", "internal", "boost", "none"], "log_level": ["trace", "debug", "info", "warn"]}
    default_options = {"logger": "clog", "log_level": "info"}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "config.h.template", "CMakeLists.txt", "src/*", "include/*", "tests/*", "cmake/*", "examples/*"

    #def config_options(self):

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self, generator="Ninja")

        tc.variables["MYSQLPOOL_LOGGER"] = self.options.logger
        tc.variables["MYSQLPOOL_LOG_LEVEL_STR"] = self.options.log_level
        tc.variables["MYSQLPOOL_WITH_CONAN"] = True

        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        # if not self.conf.get("tools.build:skip_test", default=False):
        #     self.run(os.path.join(test_folder, "unit_tests"))
        if not self.conf.get("tools.build:skip_test", default=False):
            cmake.test()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["mysqlpool"]
        self.cpp_info.set_property("cmake_target_name", "mysqlpool::mysqlpool")

    def validate(self):
        check_min_cppstd(self, "20")

    def requirements(self):
        #self.requires("boost/[>=1.84.0]")
        self.requires("zlib/[~1.3]")
        self.requires("openssl/[~3]")
        self.requires("logfault/[>=0.5.0]")
        self.test_requires("gtest/[>=1.14]")

    # def test(self):
    #     if can_run(self):
    #         cmd = os.path.join(self.cpp.build.bindir, "example")
    #         self.run(cmd, env="conanrun")
