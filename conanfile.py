from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.build import check_max_cppstd, check_min_cppstd

class mysqlpoolRecipe(ConanFile):
    name = "mysqlpool"
    version = "0.2.0"

    # Optional metadata
    license = " BSL-1.0"
    author = "Jarle Aase jgaa@jgaa.com"
    url = "https://github.com/jgaa/mysqlpool-cpp"
    description = "Lightweight async connection-pool library, built on top of boost.mysql."
    topics = ("boost.mysql", "mysql", "mariadb")

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"logger": ["logfault", "clog", "internal", "boost", "none"],
        "log_level": ["trace", "debug", "info", "warn"],
        "integration_tests": [True, False],
        "env_prefix": ["ANY"]}
    default_options = {"logger": "clog",
        "log_level": "info",
        "integration_tests": False,
        "env_prefix": "MYSQLPOOL"}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "config.h.template", "CMakeLists.txt", "src/*", "include/*", "tests/*", "cmake/*", "examples/*"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self, generator="Ninja")

        tc.variables["MYSQLPOOL_LOGGER"] = self.options.logger
        tc.variables["MYSQLPOOL_LOG_LEVEL_STR"] = self.options.log_level
        tc.variables["MYSQLPOOL_WITH_CONAN"] = True
        tc.variables["MYSQLPOOL_WITH_TESTS"] = not self.conf.get("tools.build:skip_test", default=False)
        tc.variables["MYSQLPOOL_WITH_INTEGRATION_TESTS"] = self.options.integration_tests
        tc.variables["MYSQLPOOL_DBUSER"] = str(self.options.env_prefix) + "_DBUSER"
        tc.variables["MYSQLPOOL_DBPASSW"] = str(self.options.env_prefix) + "_DBPASS"
        tc.variables["MYSQLPOOL_DBHOST"] = str(self.options.env_prefix) + "_DBHOST"
        tc.variables["MYSQLPOOL_DBPORT"] = str(self.options.env_prefix) + "_DBPORT"
        tc.variables["MYSQLPOOL_DATABASE"] = str(self.options.env_prefix) + "_DATABASE"
        tc.variables["MYSQLPOOL_TLS_MODE"] = str(self.options.env_prefix) + "_TLS_MODE"

        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
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
        self.requires("boost/[>=1.84.0]")
        self.requires("zlib/[~1.3]")
        self.requires("openssl/[~3]")
        #self.requires("logfault/[>=0.5.0]") // Waiting for submission to Conan Center to be approved
        if not self.conf.get("tools.build:skip_test", default=False):
            self.test_requires("gtest/[>=1.14]")

    # def test(self):
    #     if can_run(self):
    #         cmd = os.path.join(self.cpp.build.bindir, "example")
    #         self.run(cmd, env="conanrun")
