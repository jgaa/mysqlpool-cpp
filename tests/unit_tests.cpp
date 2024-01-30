
#include "mysqlpool/logging.h"

#include <boost/fusion/adapted.hpp>

#include "mysqlpool/mysqlpool.h"

#include "gtest/gtest.h"
//#include "mysqlpool/test_helper.h"

using namespace std;
using namespace std::string_literals;
using namespace jgaa::mysqlpool;

TEST(UnitConfig, EnvVars) {
    const auto used_host = "myhost.example.com"s;
    const auto used_port = "123"s;
    const auto used_user = "alice"s;
    const auto used_passwd = "secret"s;
    const auto used_db = "mysql"s;

    setenv(MYSQLPOOL_HOST, used_host.c_str(), 1);
    setenv(MYSQLPOOL_PORT, used_port.c_str(), 1);
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

int main( int argc, char * argv[] )
{
    MYSQLPOOL_TEST_LOGGING_SETUP("trace");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();;
}
