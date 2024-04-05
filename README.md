# mysqlpool-cpp

[![CI](https://github.com/jgaa/mysqlpool-cpp/actions/workflows/ci.yaml/badge.svg)](https://github.com/jgaa/mysqlpool-cpp/actions/workflows/ci.yaml)

Lightweight async connection-pool library, built on top of boost.mysql.

The library require C++20 and use C++20 coroutines.

## Status

First BETA release.

Please create an issue if you find any problems or if you have suggestions
on how to make the library more useful.

Friction is a *bug*! If you experience any friction, please create an issue!

PR's with fixes and improvements are most welcome!

## Supported platforms

- Linux with g++ 13
- Linux with clang 17
- Windows with msvc 14
- MacOS with g++ 13 (Apples outdated clang compiler will not do)

## Introduction

The Boost.mysql library *mysql* is a low-level database driver for mysql/mariadb
that use boost.asio for communications, and support asio completion handlers.

It is a complete rewrite of the mysql "driver". It use Boost.asio for network
traffic to the database. It support the same kind of *continuations* as Boost.asio,
something that makes it a very powerful tool for C++ developers, especially
those of us who use coroutines for async processing.

There are however a few challenges with using a library like this directly.
Even if the database calls are async and support async coroutines, one
connection can only process one request at the time. The reason for this
is (to the best of my understanding) that mysql and mariadb use a request/response
model in the database client/server communications protocol. So even if
your code had asked for some data and is ready to call another database
method, it needs to wait for the response from the previous one before it
can continue on that connection. Or it needs to juggle multiple connections. It could create
a new connection for each request - but this would give some overhead as it
takes time to set up a connections - especially if you use TLS. Another
challenge is error handling. Each request in a real application require
error handling. For many SQL queries you will first create a prepared
statement so you can *bind* the arguments to the query, and then
execute the prepared statement. Both steps require careful error handling
and reporting of errors.

The simple and traditional solution for this kind of problem is to use a
connection-pool of some sort.

When I started using Boost.mysql in a project, I immediately realized that
I needed a connection pool. It was one of the first things I implemented.
I also realized that this is a general problem with a general solution, and
that the connection pool would be better off as a separate project. That
way I can re-use it in other projects, and other people can use it as well.
One less wheel to re-invent for everybody ;)

This library, **mysylpool-cpp** is a higher level library that provide a connection-pool and
async execution of queries to the database server. Unlike Boost.mysql, it
supports C++20 coroutines only. That's what *I* need, and I don't want to
add the overhead in development time and time writing tests, to use the full
flexibility of the asio completion handlers. May be, if there is enough
complaints, I will add this in the future. Or maybe someone will offer a PR
with this. Until then, the pool is C++20 async coroutines only. That said,
there is nothing to prevent you from using the pool to get a connection,
and then call the Boost.mysql methods directly with any kind of continuation you
need. However, you will have to deal with the error handling yourself.

## Features
 - Dynamic number of connections in the pool. You specify the minimum and maximum number of connections, and the idle time before a connection is closed.
 - Semi automatic error handling, where recoverable errors can be retried automatically. If you use this, `co_await pool.exec(...)` will return when the query finally succeeds, or throw an exception if the retry count reach a configurable threshold.
 - Requests are queued if all the available connections are in use. The pool will open more connections in the background up to the limit.
 - The high level `exec` method handles connection allocation, per request options, error handling and reporting and data binding.
 - Prepared Statements are handled automatically. Each connection has a cache of prepared statements, based on a cryptographic hash from the SQL query. If a new query is seen, a prepared statement is created and added to the cache before the query is executed.
 - All SQL queries can be logged, include the arguments to prepared statements.
 - Time Zone can be specified for a query. The pool will then ensure that the connection
 used for that request use the specified time zone. Useful for servers that handle
 requests for users from different time zones.
 - Flexible logging options

## Error handling

Mysqlpool-cpp will throw exceptions on unrecoverable errors. For recoverable
errors you can choose if you want it to try to re-connect to the server. It's
a common problem with a connection pool like this that connections are broken;
may be because the network fails, may be because the database server restarted.
In a world full of containers that start in random order and some tines fail,
it is useful to have the possibility to re-connect.

In my project, my server will send a query to the database when it starts up,
and use the retry option to wait for the database server to come online. Later
it retries idempotent queries, and immediately fails on queries that change data and
should not be retried if there is an error.

## Logging

When there is an error, it logs the error and the available diagnostic data.
It can also log all queries, including the data sent to prepared statements. This
is quite useful during development. The log for a prepared statement may look like:

```
1024-03-19 15:52:21.561 UTC TRACE 9 Executing statement SQL query: SELECT date, user, color, ISNULL(notes), ISNULL(report) FROM day WHERE user=? AND YEAR(date)=? AND MONTH(date)=? ORDER BY date | args: dd2068f6-9cbb-11ee-bfc9-f78040cadf6b, 2024, 12
```

Logging in C++ is something that many people have strong opinions about. My opinion is
that it must be possible. Mysqlpool-cpp offer several compile time alternatives.

**Loggers**
- **clog** Just log to std::clog with simple formatting. Note that this logger does not support any runtime log-level. It will use the log-level set in `MYSQLPOOL_LOG_LEVEL_STR`.
- **internal** Allows you to forward the log events from the library to whatever log system you use.
- **logfault** Use the [logfault](https://github.com/jgaa/logfault) logger. This require that this logger is used project wide, which is unusual. Except for my own projects.
- **boost** Uses Boost.log, via `BOOST_LOG_TRIVIAL`. This require that this logger is used project wide.
- **none** Does not log anything. Pretends to be just another C++ library ;)

You can specify your chosen logger via the `MYSQLPOOL_LOGGER` CMake variable. For example `cmake .. -CMYSQLPOOL_LOGGER=boost`.

If you use the **internal** logger, here is an example on how to use it. In the lambda
passed to `SetHandler`, you can bridge it to the logger you use in your application:

```C++
void BridgeLogger(jgaa::mysqlpool::LogLevel llevel = jgaa::mysqlpool::LogLevel::INFO) {

    jgaa::mysqlpool::Logger::Instance().SetLogLevel(llevel);

    jgaa::mysqlpool::Logger::Instance().SetHandler([](jgaa::mysqlpool::LogLevel level,
                                             const std::string& msg) {
        static constexpr auto levels = std::to_array<std::string_view>({"NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"});

        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        std::clog << std::put_time(std::localtime(&now), "%c") << ' '
                  << levels.at(static_cast<size_t>(level))
                  << ' ' << std::this_thread::get_id() << ' '
                  << msg << std::endl;
    });

    MYSQLPOOL_LOG_INFO_("Logging on level " << level << ". Boost version is " << BOOST_LIB_VERSION);
}
```

**Log levels**

When you develop your code, trace level logging can be useful. For example, the logging of SQL statements
happens on **trace** log level. In production code you will probably never user trace level. Probably not
even **debug** level. In order to totally remove all these log statements from the compiled code, you
can set a minimum log level at compile time. This is done with the CMake variable `MYSQLPOOL_LOG_LEVEL_STR`.
For example: `cmake .. -CMYSQLPOOL_LOG_LEVEL_STR=info` will remove all trace and debug messages from
the code. Even if you run your application with trace log level, mysqlpool-cpp will only show messages
with level **info**, **warning** and **error**.

## CMake options and variables 
| name | type | explanation |
|------|------|-------------|
| DEFAULT_MYSQLPOOL_DATABASE | string | Default database name |
| DEFAULT_MYSQLPOOL_DBPASSW | string | Default db password |
| DEFAULT_MYSQLPOOL_DBUSER | string | Default db user |
| DEFAULT_MYSQLPOOL_HOST | string | Default host for the server | 
| DEFAULT_MYSQLPOOL_PORT | string | Default port to the server |
| DEFAULT_MYSQLPOOL_TLS_MODE | string | TLS requirements |
| LOGFAULT_DIR | string | Path to the Logfault header only library. If used, CMake will not install it from *cmake/3rdparty.cmake*" |
| MYSQLPOOL_DBHOST | string | Environment variable to get the dbservers hostname or IP address from |
| MYSQLPOOL_DBPASSW | string | Environment variable to get user login password from. |
| MYSQLPOOL_DBPORT | string | Environment variable to get the dbservers port number from |
| MYSQLPOOL_DBUSER | string | Environment variable to get login user name name from. |
| MYSQLPOOL_DB_TLS_MODE | string | Environment variable to get the TLS mode from . One of: 'disable', 'enable', 'require' |
| MYSQLPOOL_EMBEDDED | bool | Tells CMake not to install dependencies from *cmake/3rdparty.cmake* |
| MYSQLPOOL_LOGGER | string | Log system to use. One of 'clog', 'internal', 'logfault', 'boost' or 'none'. |
| MYSQLPOOL_LOG_LEVEL_STR | string | Minimum log level to enable. One of 'none', error', 'warn', 'info', 'debug', 'trace'. |
| MYSQLPOOL_WITH_CONAN | bool | Tells CMake that conan is in charge. |
| MYSQLPOOL_WITH_EXAMPLES | bool | Builds the example |
| MYSQLPOOL_WITH_INTGRATION_TESTS | bool | Enables integration tests |
| MYSQLPOOL_WITH_TESTS | bool | Enables unit tests |

## Use

When you use the library, you can ask for a handle to a database connection. The connection
is a Boost.mysql connection. When the handle goes out of scope, the connection is
returned to the pool.

Example:

```C++
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

```

You can use this code in your project like this:

```C++
#include <boost/asio.hpp>

#include "mysqlpool/mysqlpool.h"
#include "mysqlpool/conf.h"
#include "mysqlpool/config.h"

using namespace std;
namespace mp = jgaa::mysqlpool;

void exampe() {
    mp::DbConfig config;
    boost::asio::io_context ctx;

    mp::Mysqlpool pool(ctx, config);

    auto res = boost::asio::co_spawn(ctx, [&]() -> boost::asio::awaitable<void> {
        // Initialize the connection pool.
        // It will connect to the database and keep a pool of connections.
        co_await pool.init();

        // Ping the server
        co_await ping_the_db_server(pool);

        // Gracefully shut down the connection-pool.
        co_await pool.close();

    }, boost::asio::use_future);

    // Let the main thread run the boost.asio event loop.
    ctx.run();

    // Wait for the coroutine to run
    res.get();
}
```

A more high level way is to just tell the pool what you want done, and
let it deal with everything, and throw an exception if the operation
failed.

Let's just query the database for it's version. That is a normal
SQL query without any arguments.

```C++

boost::asio::awaitable<void> get_db_version(mp::Mysqlpool& pool) {

    // Mysqlpool-cpp handles the connection, and release it before exec() returns.
    // If there is a problem, mysqlpool-cpp will retry if appropriate.
    // If not, it will throw an exception.
    const auto res = co_await pool.exec("SELECT @@version");

    // We have to check that the db server sent us something.
    if (!res.rows().empty()) {

        // Here we use Boost.mysql's `result` object to get the result.
        const auto db_version = res.rows()[0][0].as_string();
        cout << "Database version: " << db_version << endl;
    }
}

```

Now, lets do some real work.

```C++

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
    // This time we format the timestamp in the rows to a string on the server.
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

    co_await pool.exec("DROP TABLE test_table");
    co_return;
}

```

## Building

You can build the library using CMake.

You can also build it using conan to get the dependencies. However, the library
is not yet in the Conan Center - so your project can't depend on it.

The simplest way to use it in a project today is probably to use CMake's `ExternalProject_Add` or
to add it as a git submodule.

**git submodule**

If you use it as a git submodule, make sure to
set `MYSQLPOOL_EMBEDDED` to `ON`

You can use something like below in your projects `CMakeLists.txt`, adjusted with your paths and preferences.

```cmake

set(MYSQLPOOL_EMBEDDED ON CACHE INTERNAL "")
set(MYSQLPOOL_LOGGER boost CACHE INTERNAL "")
set(MYSQLPOOL_LOG_LEVEL_STR trace CACHE INTERNAL "")
set(DEFAULT_MYSQLPOOL_DBUSER myuser CACHE INTERNAL "")
set(DEFAULT_MYSQLPOOL_DATABASE mydb CACHE INTERNAL "")
set(MYSQLPOOL_WITH_TESTS OFF CACHE INTERNAL "")
set(MYSQLPOOL_WITH_EXAMPLES OFF CACHE INTERNAL "")
add_subdirectory(dependencies/mysqlpool-cpp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dependencies/mysqlpool-cpp/include)

```

