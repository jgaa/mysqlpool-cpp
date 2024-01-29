# mysqlpool-cpp

Lightweight async connection-pool library, built on top of boost.mysql.

The library require C++20 and use C++20 coroutines.

## Status
Under initial implementation. Not ready for adaption yet.

## Introduction

The mysql library *mysql* is a low-level database driver for mysql/mariadb
that use boost.asio for communications, and support asio completion handlers.

This library is a higher level library that provide a connection-pool and
async execution of queries to the database server. It will automatically
re-connect to the database server if the connection is lost.

In the example below, `db.exec()` will obtain a db-connection from the pool
of connections - and wait if necessary until one becomes available.
Then it will prepare a statement (since we give extra arguments to bind
to a prepared statement) and execute the query on the database server. If all
is well, it returns a result-set. If something fails, we get an exception.

```C++

    auto ctx = boost::asio::io_context;
    DbConfig cfg;
    Mysqlpool db{ctx, cfg};

    // start threads
    db.init();
    ...

    // Do something with a database
    auto date = "2024-01-24"s;
    auto user = "myself"s;

    co_await db.exec(
        R"(INSERT INTO day (date, user, color) VALUES (?, ?, ?)
            ON DUPLICATE KEY UPDATE color=?)",
        date, user,
        // insert
        color,
        // update
        color
        );

    static constexpr string_view selectCols = "id, user, name, kind, descr, active, parent, version";


    // Query for all the nodes in a logical tree in a table, belonging to user
    const auto res = co_await db.exec(format(R"(
        WITH RECURSIVE tree AS (
          SELECT * FROM node WHERE user=?
          UNION
          SELECT n.* FROM node AS n, tree AS p
          WHERE n.parent = p.id or n.parent IS NULL
        )
        SELECT {} from tree ORDER BY parent, name)", selectCols), user);

        if (res.has_value()) {
            for(const auto& row : res.rows()) {
            // Do something with the returned data

```

The examples are for now from the [project](https://github.com/jgaa/next-app) I moved the code from.

