
#include <regex>

#include "mysqlpool/logging.h"
#include <boost/fusion/adapted.hpp>

#include "mysqlpool/mysqlpool.h"

#include "gtest/gtest.h"
//#include "mysqlpool/test_helper.h"

#ifdef _MSC_VER
#   include <stdlib.h>
    // Use _putenv_s when compiled with Microsoft's compiler
#   define setenv(key, value, ignore) _putenv_s(key, value)
#endif

using namespace std;
using namespace std::string_literals;
using namespace jgaa::mysqlpool;

namespace {
struct ClogRedirector {
    ClogRedirector(string& out, ostream& ios = std::clog)
        : out_{out}, ios_{ios} {

        orig_ = ios.rdbuf();
        ios_.rdbuf(buffer_.rdbuf());
    }

    ~ClogRedirector() {
        out_ = buffer_.str();
        ios_.rdbuf(orig_);
    }

private:
    ostream& ios_;
    decltype(std::clog.rdbuf()) orig_{};
    stringstream buffer_;
    string& out_;
};
}

TEST(UnitConfig, EnvVars) {
    const auto used_host = "myhost.example.com"s;
    const auto used_port = "123"s;
    const auto used_user = "alice"s;
    const auto used_passwd = "secret"s;
    const auto used_db = "mysql"s;

    setenv(MYSQLPOOL_DBHOST, used_host.c_str(), 1);
    setenv(MYSQLPOOL_DBPORT, used_port.c_str(), 1);
    setenv(MYSQLPOOL_DBUSER, used_user.c_str(), 1);
    setenv(MYSQLPOOL_DBPASSW, used_passwd.c_str(), 1);
    setenv(MYSQLPOOL_DATABASE, used_db.c_str(), 1);

    DbConfig config;
    EXPECT_EQ(config.host, used_host);
    EXPECT_EQ(config.port, std::stoi(used_port));
    EXPECT_EQ(config.username, used_user);
    EXPECT_EQ(config.password, used_passwd);
    EXPECT_EQ(config.database, used_db);

}

#ifdef MYSQLPOOL_LOG_WITH_LOGFAULT
TEST(Logfault, Hello) {

    string output;
    {
        ClogRedirector redir{output};
        MYSQLPOOL_LOG_INFO_("Test log");
    }

    regex pattern{R"(.* INFO .* Test log.*)"};
    EXPECT_TRUE(regex_search(output, pattern));
}
#endif

#ifdef MYSQLPOOL_LOG_WITH_CLOG
TEST(Clog, Hello) {

    string output;
    {
        ClogRedirector redir{output};
        MYSQLPOOL_LOG_INFO_("Test log");
    }

    regex pattern{R"(.*Test log.*)"};
    EXPECT_TRUE(regex_search(output, pattern));
}
#endif

#ifdef MYSQLPOOL_LOG_WITH_INTERNAL_LOG
TEST(InternalLog, Hello) {

    string output;
    {
        ClogRedirector redir{output};
        MYSQLPOOL_LOG_INFO_("Test log");
    }

    regex pattern{R"(INFO \d* Test log.*)"};
    EXPECT_TRUE(regex_search(output, pattern));
}
#endif

#ifdef MYSQLPOOL_LOG_WITH_BOOST_LOG
TEST(BoostLog, Hello) {

    string output;
    {
        ClogRedirector redir{output};
        MYSQLPOOL_LOG_INFO_("Test log");
        boost::log::core::get()->flush();
    }

    regex pattern{R"(.*\[info\]\: Test log.*)"};
    EXPECT_TRUE(regex_search(output, pattern));
}
#endif

int main( int argc, char * argv[] )
{
    MYSQLPOOL_TEST_LOGGING_SETUP("debug");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();;
}
