#pragma once

#include <string>
#include <cstdint>

#include "mysqlpool/config.h"

namespace jgaa::mysqlpool {

std::string getEnv(const std::string& name, std::string defaultValue = {});

/** Configuration for an instance of mysqlpool. */

struct DbConfig {
    /// Host where the DB server is running.
    std::string host = getEnv(MYSQLPOOL_DBHOST, DEFAULT_MYSQLPOOL_HOST);

    /// Port used by the DB server
    uint16_t port = static_cast<uint16_t>(std::stoi(getEnv(MYSQLPOOL_DBPORT, DEFAULT_MYSQLPOOL_PORT)));

    /// Max number of connections to the server
    size_t max_connections = 6;

    /*! Minimum number of connections.
     *
     *  These connections are created when the instance is created.
     *  If the connections are idle more than `connection_idle_limit_seconds`,
     *  the connections are closed.
     */
    size_t min_connections = 2;

    /// Username used to log on to the server
    std::string username = getEnv(MYSQLPOOL_DBUSER, DEFAULT_MYSQLPOOL_DBUSER);

    /// Password used to log on to the server
    std::string password = getEnv(MYSQLPOOL_DBPASSW, DEFAULT_MYSQLPOOL_DBPASSW);

    /// Database (name) to use
    std::string database = getEnv(MYSQLPOOL_DATABASE, DEFAULT_MYSQLPOOL_DATABASE);

    /*! TLS mode
     *
     *  - disable: Never use TLS
     *  - enable: Use TLS if the server supports it, fall back to non-encrypted connection if it does not.
     *  - require: Always use TLS; abort the connection if the server does not support it.
     */
    std::string ssl_mode = getEnv(MYSQLPOOL_DB_TLS_MODE, DEFAULT_MYSQLPOOL_TLS_MODE);

    /*! Number of times to retry connecting to the database
     *
     *  This is useful if the app is started before or at the same time as the
     *  database, and the database needs some time to initialize itself before
     *  it can accept connections.
     */
    unsigned retry_connect = 20;

    /// Time to wait between retry attempts
    unsigned retry_connect_delay_ms = 2000;

    /*! Interval between timer events
     *
     *  The timer events are used to disconnect connetions that
     *  have been idle for too long.
     */
    unsigned timer_interval_ms = 5000;

    /// How long a connection can be idle/unused before it can be disconnected
    unsigned connection_idle_limit_seconds = 120;

    /// How long the pool will wait while shutting down to gracefully disconnect connections.
    unsigned timeout_close_all_databases_seconds = 10;
};

}
