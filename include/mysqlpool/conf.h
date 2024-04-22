#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <string_view>
#include <functional>
#include <boost/asio/ssl.hpp>

#include "mysqlpool/config.h"

namespace jgaa::mysqlpool {

std::string getEnv(const std::string& name, std::string defaultValue = {});

/** Configuration for an instance of mysqlpool. */

struct TlsConfig {
    using password_cb_t = std::function<std::string (
        std::size_t, // The maximum size for a password.
        boost::asio::ssl::context_base::password_purpose // Whether password is for reading or writing.
        )>;

    /*! TLS version to use.
     *
     *  If unset, it use the default client settings for boost.asio (boost::asio::ssl::context_base::tls_client)
     *
     *  One of:
     *      - "" (empty string): Use the default settings for the client
     *      - "": Use TLS 1.2
     *      - "tls_1.3": Use TLS 1.3
     */
    std::string version;

    /*! Allow SSL versions. They are all depricated and should not be allowed */
    bool allow_ssl = false;

    /*! Allow TLS 1.1 */
    bool allow_tls_1_1 = false;

    /*! Allow TLS 1.2 */
    bool allow_tls_1_2 = true;

    /*! Allow TLS 1.3 */
    bool allow_tls_1_3 = true;

    /*! Very often the peer will use a self-signed certificate.
     *
     * Must be enabled if you use a public database server on the internet.
     */
    bool verify_peer = false;

    /*! CA files for verification */
    std::vector<std::string> ca_files;

    /*! CA paths for verification */
    std::vector<std::string> ca_paths;

    /*! Cert to use for the client */
    std::string cert_file;

    /*! Key to use for the client */
    std::string key_file;

    /*! Password callback if the private key use a password */
    password_cb_t password_callback;
};

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
    std::string tls_mode = getEnv(MYSQLPOOL_DB_TLS_MODE, DEFAULT_MYSQLPOOL_TLS_MODE);

    /*! TLS configuration */
    TlsConfig tls;

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
