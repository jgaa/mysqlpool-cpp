
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include "mysqlpool/logging.h"
#include "mysqlpool/mysqlpool.h"
#include "mysqlpool/conf.h"
#include "mysqlpool/config.h"

#ifdef _MSC_VER
#   include <stdlib.h>
// Use _putenv_s when compiled with Microsoft's compiler
// Thank you Microsoft!
#   define setenv(key, value, ignore) _putenv_s(key, value)
#endif

using namespace std;
namespace mp = jgaa::mysqlpool;

extern void run_examples(const mp::DbConfig& config);

int main(int argc, char* argv[]) {
    // Don't make the app crash with an uincaught exception if Linux can't deal with your locale setting!
    // In stead, switch to the "C" locale which hopefully works.
    try {
        locale loc("");
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    std::string log_level = "info";
    mp::DbConfig config;

    const auto appname = filesystem::path(argv[0]).stem().string();

    namespace po = boost::program_options;
    po::options_description general("Options");
    general.add_options()
        ("help,h", "Print help and exit")
        ("version,v", "Print version and exit")
        ("log-to-console,C",
         po::value(&log_level)->default_value(log_level),
         "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
        ;
    po::options_description db_opts("Database options");
    db_opts.add_options()
        ("db-user",
         po::value(&config.username),
         "Mysql user to use when logging into the mysql server")
        ("db-passwd",
         po::value(&config.password),
         "Mysql password to use when logging into the mysql server")
        ("db-name",
         po::value(&config.database)->default_value(config.database),
         "Database to use")
        ("db-host",
         po::value(&config.host)->default_value(config.host),
         "Hostname or IP address for the database server")
        ("db-port",
         po::value(&config.port)->default_value(config.port),
         "Port number for the database server")
        ("db-min-connections",
         po::value(&config.min_connections)->default_value(config.min_connections),
         "Max concurrent connections to the database server")
        ("db-max-connections",
         po::value(&config.max_connections)->default_value(config.max_connections),
         "Max concurrent connections to the database server")
        ("db-retry-connect",
         po::value(&config.retry_connect)->default_value(config.retry_connect),
         "Retry connect to the database-server # times on startup. Useful when using containers, where nextappd may be running before the database is ready.")
        ("db-retry-delay",
         po::value(&config.retry_connect_delay_ms)->default_value(config.retry_connect_delay_ms),
         "Milliseconds to wait between connection retries")
        ;

    po::options_description cmdline_options;
    cmdline_options.add(general).add(db_opts);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line arguments: " << ex.what() << endl;
        return -1;
    }

    if (vm.count("help")) {
        std::cout <<appname << " [options]";
        std::cout << cmdline_options << std::endl;
        return -2;
    }

    if (vm.count("version")) {
        std::cout << appname << endl
                  << "Using C++ standard " << __cplusplus << endl
                  << "Boost " << BOOST_LIB_VERSION << endl
                  << "Platform " << BOOST_PLATFORM << endl
                  << "Compiler " << BOOST_COMPILER << endl
                  << "Build date " <<__DATE__ << endl
                  << "Mysqlpool " << MYSQLPOOL_VERSION << endl;
        return -3;
    }

    // Set up logging
    // In this example we use this macro to use whatever log framework
    // mysqlpool is configured to use.
    // In your app, you will typically choose what log framework to use
    // and configure it in your app.
    MYSQLPOOL_TEST_LOGGING_SETUP(log_level);

    MYSQLPOOL_LOG_INFO_(appname << " starting up.");

    try {
        run_examples(config);
    } catch (const exception& ex) {
        MYSQLPOOL_LOG_ERROR_("Caught exception: " << ex.what());
        return -5;
    }

    MYSQLPOOL_LOG_INFO_(appname << " is done!");
}
