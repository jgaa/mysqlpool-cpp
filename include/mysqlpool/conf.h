#pragma once

#include <thread>
#include <string>
#include <cstdint>

namespace jgaa::mysqlpool {

std::string getEnv(const std::string& name, std::string defaultValue = {});

struct DbConfig {
    std::string host = "localhost";
    uint16_t port = 3306; // Default for mysql
    size_t max_connections = 2;

    std::string username = getEnv("NA_DBUSER");
    std::string password = getEnv("NA_DBPASSWD");
    std::string database = getEnv("NA_DATABASE");

    unsigned retry_connect = 20;
    unsigned retry_connect_delay_ms = 2000;
};

}
