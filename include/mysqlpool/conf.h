#pragma once

#include <string>
#include <cstdint>

#include "mysqlpool/config.h"

namespace jgaa::mysqlpool {

std::string getEnv(const std::string& name, std::string defaultValue = {});

struct DbConfig {
    std::string host = getEnv(MYSQLPOOL_HOST, DEFAULT_MYSQLPOOL_HOST);
    uint16_t port = static_cast<uint16_t>(std::stoi(getEnv(MYSQLPOOL_PORT, DEFAULT_MYSQLPOOL_PORT)));
    size_t max_connections = 6;
    size_t min_connections = 2;

    std::string username = getEnv(MYSQLPOOL_DBUSER, DEFAULT_MYSQLPOOL_DBUSER);
    std::string password = getEnv(MYSQLPOOL_DBPASSW, DEFAULT_MYSQLPOOL_DBPASSW);
    std::string database = getEnv(MYSQLPOOL_DATABASE, DEFAULT_MYSQLPOOL_DATABASE);

    unsigned retry_connect = 20;
    unsigned retry_connect_delay_ms = 2000;
    unsigned timer_interval_ms = 5000;
    unsigned connection_idle_limit_seconds = 120;
};

}
