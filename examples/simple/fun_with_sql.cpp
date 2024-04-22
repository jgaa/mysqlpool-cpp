
#include <boost/asio.hpp>

#include "mysqlpool/logging.h"
#include "mysqlpool/mysqlpool.h"
#include "mysqlpool/conf.h"
#include "mysqlpool/config.h"

using namespace std;
namespace mp = jgaa::mysqlpool;

boost::asio::awaitable<void> ping_the_db_server(mp::Mysqlpool& pool) {

    // Lets get an actual connection to the database
    // handle is a Handle to a Connection.
    // It will automatically release the connection when it goes out of scope.
    auto handle = co_await pool.getConnection();

    // When we obtain a handle, we can use the native boost.mysql methods.
    // Let's try it and ping the server.
    // If the server is not available, the async_ping will throw an exception.

    cout << "Pinging the server..." << endl;
    co_await handle.connection().async_ping(boost::asio::use_awaitable);
}

boost::asio::awaitable<void> get_db_version_using_boost_mysql(mp::Mysqlpool& pool) {

    // Lets get an actual connection to the database
    auto handle = co_await pool.getConnection();

    // hande is a Handle to a Connection.
    // It will automatically release the connection when it goes out of scope.

    // Now, let's see what version the database server uses, by sending a query: 'SELECT @@version'
    // This time we will use proper error handling.
    // Please see the boost.mysql documentation for more information.

    boost::mysql::results res;
    boost::mysql::diagnostics diag;
    const auto [ec] = co_await handle.connection().async_execute("SELECT @@version", res, diag, mp::tuple_awaitable);
    if (ec) {
        MYSQLPOOL_LOG_ERROR_("Error: " << ec.message()
                                       << ", diag client: " << diag.client_message()
                                       << ", diag server: " << diag.server_message());
        co_return;
    }
    if (res.has_value() && !res.rows().empty()) {
        const auto db_version = res.rows()[0][0].as_string();
        cout << "Database version: " << db_version << endl;
    }

    co_return;
}


boost::asio::awaitable<void> get_db_version(mp::Mysqlpool& pool) {

    // Now, lets do the same as above, but with less code.
    // Note that we leave most of the error-handling to Mysqlpool.
    // We also let Mysqlpool handle the connection, and release it before exec() returns.
    // If there is a problem, Mysqlpool will retry if appropriate.
    // If not, it will throw an exception.

    const auto res = co_await pool.exec("SELECT @@version");

    // if pool.exec() returned, we know that the result is not empty.
    assert(!res.empty());

    // We still have to check that the db server sent us something.
    if (!res.rows().empty()) {
        const auto db_version = res.rows()[0][0].as_string();
        cout << "Database version: " << db_version << endl;
    }

    co_return;
}

boost::asio::awaitable<void> add_and_query_data(mp::Mysqlpool& pool) {
    // Create a table we can play with in the current database
    co_await pool.exec("CREATE OR REPLACE TABLE test_table (id INT, name TEXT, created_date TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP)");

    // Insert data using prepared statements. The `exec` method can take any number
    // of arguments, as long as they are compatible with Boost.mysql data values.
    co_await pool.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 1, "Alice");
    co_await pool.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 2, "Bob");
    co_await pool.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 3, "Charlie");

    // Now, let's query the table and loop over the results.
    cout << "Data inserted." << endl;
    auto res = co_await pool.exec("SELECT * FROM test_table");
    for(auto row : res.rows()) {
        cout << "id: " << row[0].as_int64() << ", name: "
             << row[1].as_string()
             << ", created: "
             << row[2].as_datetime()
             << endl;
    }

    // Change Bob's name to David
    co_await pool.exec("UPDATE test_table SET name = ? WHERE id = ?", "David", 2);

    cout << "Data updated." << endl;
    res = co_await pool.exec("SELECT * FROM test_table");
    for(auto row : res.rows()) {
        cout << "id: " << row[0].as_int64() << ", name: " << row[1].as_string() << endl;
    }

    // Now, lets insert another row, but this time we will use a tuple to carry the data
    auto data = make_tuple(4, "Eve"s);
    co_await pool.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", data);

    cout << "More data inserted ." << endl;
    res = co_await pool.exec("SELECT * FROM test_table WHERE id = ?", 4);
    for(auto row : res.rows()) {
        cout << "id: " << row[0].as_int64() << ", name: " << row[1].as_string() << endl;
    }

    // All the queries so far has used automatic error handling.
    // Let's disable that, so that the query will throw and exception if there
    // is a problem with the connection to the server.
    mp::Options opts;
    opts.reconnect_and_retry_query = false;
    // We put `opts` after the query, and the data after that.
    co_await pool.exec("UPDATE test_table SET name = ? WHERE id = ?", opts, "Fred", 2);

    cout << "Data updated. David changed name again!" << endl;
    res = co_await pool.exec("SELECT * FROM test_table");
    for(auto row : res.rows()) {
        cout << "id: " << row[0].as_int64() << ", name: " << row[1].as_string() << endl;
    }

    // Let's use a lambda to print the rows. No need to repeat the code.
    const auto print = [](const auto& rows) {
        for(auto row : rows) {
            cout << "id: " << row[0].as_int64() << ", name: "
                 << row[1].as_string()
                 << ", created: "
                 << row[2].as_string()
                 << endl;
        }
    };

    // Now, lets see how we can change the time zone of the query.
    // This time we format the timestamp in the table to a string on the server.
    // This does not make much sense, but for queries that specify a date or time,
    // it's quite useful if the users are in different time zones.

    // first, lets use whatever is default.
    cout << "Default time zone:" << endl;
    res = co_await pool.exec("SELECT id, name, DATE_FORMAT(created_date, '%Y-%m-%d %H:%m') FROM test_table");
    print(res.rows());

    opts.reconnect_and_retry_query = true; // We want to retry if there is a problem.
    opts.time_zone = "CET"; // Central European Time
    cout << "Time zone :" << opts.time_zone << endl;
    res = co_await pool.exec("SELECT id, name, DATE_FORMAT(created_date, '%Y-%m-%d %H:%m') FROM test_table", opts);
    print(res.rows());

    opts.time_zone = "America/New_York";
    cout << "Time zone :" << opts.time_zone << endl;
    res = co_await pool.exec("SELECT id, name, DATE_FORMAT(created_date, '%Y-%m-%d %H:%m') FROM test_table", opts);
    print(res.rows());

    co_return;
}

boost::asio::awaitable<void> use_transaction(mp::Mysqlpool& pool) {

    // Very often, you will need transactions in order to commit
    // several related queries to the database.
    // This is one area where the traditional SQL database servers
    // shine compared to most NoSQL databases.

    // In order to use transactions, we need to use a Handle to a Connection
    // and keep that handle instance alive until the transaction is done.
    // In most cases that simply means to first create the handle, and
    // then the transaction.

    // Create the handle
    auto handle = co_await pool.getConnection();

    // Create the transaction
    auto trx = co_await handle.transaction();

    // Make some queries - typically INSERT, UPDATE or DELETE
    // Note that you must use the `handle` instance to execute the queries,
    // since the transaction is bound to that handle.
    co_await handle.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 5, "George");
    co_await handle.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 6, "Hannah");

    // Now you can commit the transaction or roll it back.
    // If the trx instance goes out of scope with an active transaction,
    // the transaction is rolled back.
    co_await trx.commit();

    // You cannot use the trx instance after commit or rollback, but the handle is
    // still valid. You can use it directly with `exec(...)` or you can create
    // a new transaction

    co_return;
}

boost::asio::awaitable<void> use_the_same_connection(mp::Mysqlpool& pool) {
    // Some times you may want to use the same connection for several queries
    // This can be done by using the same handle for several queries.

    // Create the handle
    auto handle = co_await pool.getConnection();

    // Make some queries
    // Note that we use `handle` to execute the queries.
    co_await handle.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 7, "Ivan");
    co_await handle.exec("INSERT INTO test_table (id, name) VALUES (?, ?)", 8, "Jane");

    // Using the same connection lets you set specific options on that connection,
    // and it guarantees that the queries are executed in sequential order.
}


// Entry point from main()
void run_examples(const mp::DbConfig& config){


    // Create an io_context, which is the heart of the boost.asio library.
    // It will manage all asynchronous operations.
    boost::asio::io_context ctx;

    // Create an instance of Mysqlool
    // It will connect to the database and keep a pool of connections.
    mp::Mysqlpool pool(ctx, config);

    // Start a coroutine context, and work in it until we are done.
    auto res = boost::asio::co_spawn(ctx, [&]() -> boost::asio::awaitable<void> {
        // Initialize the connection pool.
        // It will connect to the database and keep a pool of connections.
        try {
            co_await pool.init();

            // Run trough the examples
            co_await ping_the_db_server(pool);
            co_await get_db_version_using_boost_mysql(pool);
            co_await get_db_version(pool);
            co_await add_and_query_data(pool);
            co_await use_transaction(pool);
            co_await use_the_same_connection(pool);

            co_await pool.exec("DROP TABLE test_table");
            // Gracefully shut down the connection-pool.
            co_await pool.close();
        } catch (const exception& ex) {
            MYSQLPOOL_LOG_DEBUG_("Caught exception in coroutine: " << ex.what());

            // The main thread will not be released to deal with the exception
            // until the asio context stops.
            // Normally that happens in `pool.close()`. But if there is an exception,
            // that is not called or completed.
            ctx.stop();
            throw;
        }

        }, boost::asio::use_future);

    // Let the main thread run the boost.asio event loop.
    ctx.run();

    try {
        // Now, let's deal with exceptions from the coroutines, if any
        res.get();
    } catch (const exception& ex) {
        MYSQLPOOL_LOG_ERROR_("Caught exception from coroutine: " << ex.what());
    }

    MYSQLPOOL_LOG_INFO_("Example run is done: ");
}

