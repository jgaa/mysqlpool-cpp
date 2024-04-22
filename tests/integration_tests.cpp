
#include <thread>

#include "mysqlpool/logging.h"

#include <boost/fusion/adapted.hpp>

#include "mysqlpool/mysqlpool.h"

#include "gtest/gtest.h"
//#include "mysqlpool/test_helper.h"

using namespace std;
using namespace std::string_literals;
using namespace jgaa::mysqlpool;

// Too run this test, the environment variable MYSQLPOOL_DBPASSW must be set with
// a password to the database server to use.

// If the database is not running on localhost:3306, MYSQLPOOL_DBHOST and MYSQLPOOL_DBPORT
// may also need to be set.
// If you test with another user than 'root', MYSQLPOOL_DBUSER needs to be set.
// If you test aith another database than 'mysql', MYSQLPOOL_DATABASE needs to be set


namespace {
auto get_config() {
    DbConfig c;

    if (c.database.empty()) {
        c.database = "mysql";
    }

    if (c.username.empty()) {
        c.username = "root";
    }

    return c;
}

// Simple wrapper to handle the boilerplate code to run an async test with a Mysqlpool object
template <typename rvalT, typename T>
    requires std::invocable<T&, Mysqlpool&>
auto run_async_test(const T& test, rvalT failValue) {
    auto config = get_config();
    boost::asio::io_context ctx;
    Mysqlpool db{ctx, config};
    promise<rvalT> promise;
    boost::asio::co_spawn(ctx, [&]() -> boost::asio::awaitable<void> {
        try {
            co_await db.init();
            promise.set_value(co_await test(db));
        } catch (const std::exception& ex) {
            MYSQLPOOL_LOG_ERROR_("Request failed with exception: " << ex.what());
            promise.set_value(failValue);
        }
    }, boost::asio::detached);

    std::jthread thread {[&ctx] {
            ctx.run();
        }
    };

    const auto result = promise.get_future().get();

    ctx.stop();
    return result;
}

} // anon ns


TEST(Functional, PingServer) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<bool> {
        auto connection = co_await db.getConnection({});
        co_await connection.connection().async_ping(boost::asio::use_awaitable);
        co_return true;
    };

    EXPECT_TRUE(run_async_test(test, false));
}

TEST(Functional, CreateTable) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<bool> {
        co_await db.exec(R"(CREATE OR REPLACE TABLE mysqlpool
            (
                id UUID not NULL default UUID() PRIMARY KEY,
                name VARCHAR(128) NOT NULL,
                color VARCHAR(16) NOT NULL DEFAULT 'green'
            )
        )");
        co_return true;
    };

    EXPECT_TRUE(run_async_test(test, false));
}

TEST(Functional, InsertData) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<int64_t> {
        auto res = co_await db.exec(R"(INSERT INTO mysqlpool (name) VALUES (?))", "Glad");
        co_return res.affected_rows();
    };

    EXPECT_EQ(run_async_test(test, 0), 1);
}

TEST(Functional, InsertDataRows) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<int64_t> {
        auto res = co_await db.exec(R"(INSERT INTO mysqlpool (name)
            VALUES (?), (?), (?))", "Nusse", "Bugsy", "Kleo");
        assert(res.has_value());
        co_return res.affected_rows();
    };

    EXPECT_EQ(run_async_test(test, 0), 3);
}

TEST(Functional, UnpreparedStatementQuery) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<int64_t> {
        auto res = co_await db.exec(R"(SELECT COUNT(*) FROM mysqlpool)");

        if (res.has_value() && !res.rows().empty()) {
            co_return res.rows().front().at(0).as_int64();
        }

        co_return -1;
    };

    EXPECT_EQ(run_async_test(test, 0LL), 4);
}

TEST(Functional, InsertDataRowsWithTuple) {

    const auto tuple = make_tuple(string_view{"Ares"}, string_view{"Bjeff"}, string_view{"Floete"});

    auto test = [&tuple](Mysqlpool& db) -> boost::asio::awaitable<int64_t> {
        auto res = co_await db.exec(R"(INSERT INTO mysqlpool (name)
            VALUES (?), (?), (?))", tuple);
        co_return res.affected_rows();
    };

    EXPECT_EQ(run_async_test(test, 0), 3);
}

TEST(Functional, ClosePool) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<bool> {
        co_await db.close();
        co_return true;
    };

    EXPECT_NO_THROW(run_async_test(test, false));
}

TEST(Functional, TimeZone) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<bool> {
        jgaa::mysqlpool::Options opts;
        opts.time_zone = "UTC";

        auto res = co_await db.exec("SELECT @@session.time_zone");
        EXPECT_TRUE(res.has_value() && !res.rows().empty());
        if (res.has_value() && !res.rows().empty()) {
            const auto zone = res.rows().front().at(0).as_string();
            EXPECT_NE(zone, opts.time_zone);
        }

        res = co_await db.exec("SELECT @@session.time_zone", opts);
        EXPECT_TRUE(res.has_value() && !res.rows().empty());
        if (res.has_value() && !res.rows().empty()) {
            const auto zone = res.rows().front().at(0).as_string();
            EXPECT_EQ(zone, opts.time_zone);
        }
        co_return true;
    };

    run_async_test(test, false);
}

TEST (Functional, TransactionRollback) {


    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<bool> {
        Options opts{false};
        auto handle = co_await db.getConnection({});

        auto trx = co_await handle.transaction();

        auto res = co_await handle.exec(R"(INSERT INTO mysqlpool (name) VALUES (?))", opts, "Bean");
        EXPECT_EQ(res.affected_rows(), 1);

        EXPECT_FALSE(trx.empty());
        co_await trx.rollback();
        EXPECT_TRUE(trx.empty());

        res = co_await handle.exec(R"(SELECT COUNT(*) FROM mysqlpool WHERE name = ?)", opts, "Bean");
        EXPECT_EQ(res.rows().front().at(0).as_int64(), 0);
        co_return true;
    };

    run_async_test(test, false);
}

TEST (Functional, TransactionCommit) {

    auto test = [](Mysqlpool& db) -> boost::asio::awaitable<bool> {

        Options opts{false};

        auto handle = co_await db.getConnection({});

        auto trx = co_await handle.transaction();

        auto res = co_await handle.exec(R"(INSERT INTO mysqlpool (name) VALUES (?))", opts, "Bean");
        EXPECT_EQ(res.affected_rows(), 1);

        EXPECT_FALSE(trx.empty());
        co_await trx.commit();
        EXPECT_TRUE(trx.empty());

        res = co_await handle.exec(R"(SELECT COUNT(*) FROM mysqlpool WHERE name = ?)", opts, "Bean");
        EXPECT_EQ(res.rows().front().at(0).as_int64(), 1);
        co_return true;
    };

    run_async_test(test, false);
}

int main( int argc, char * argv[] )
{
    MYSQLPOOL_TEST_LOGGING_SETUP("trace");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();;
}
