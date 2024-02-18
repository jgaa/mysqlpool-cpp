
#include <string_view>
#include <array>

#include "mysqlpool/conf.h"
#include "mysqlpool/mysqlpool.h"
#include "mysqlpool/logging.h"

using namespace std;
namespace asio = boost::asio;
namespace mysql = boost::mysql;

namespace jgaa::mysqlpool {

const std::string& toString(::jgaa::mysqlpool::Mysqlpool::Connection::State state) {
    static const auto names = std::to_array<std::string>({"CONNECTED", "CLOSING", "CLOSED", "CONNECTING"});
    return names.at(static_cast<size_t>(state));
}

boost::uuids::random_generator Mysqlpool::uuid_gen_;

std::string getEnv(const std::string& name, std::string def) {
    if (auto var = std::getenv(name.c_str())) {
        return var;
    }
    return def;
}

asio::awaitable<Mysqlpool::Handle> Mysqlpool::getConnection(bool throwOnEmpty) {
    while(true) {
        optional<Handle> handle;
        {
            std::scoped_lock lock{mutex_};
            if (closed_) {
                if (throwOnEmpty) {
                    throw runtime_error{"The db-connection pool is closed."};
                }
                co_return Handle{};
            }

            // Happy path. We have an open connection available
            if (auto it = std::ranges::find_if(connections_, [](const auto& c) {
                    return c->isAvailable();
                } ); it != connections_.end()) {
                handle.emplace(this, &**it);
            }

            if (!handle) {
                // Se if we have any closed connections in the pool
                if (auto it = std::ranges::find_if(connections_, [](const auto& c) {
                        return !c->taken() && c->state() == Connection::State::CLOSED;
                    } ); it != connections_.end()) {
                    MYSQLPOOL_LOG_DEBUG_("Mysqlpool::getConnection: re-using closed connection " << (*it)->uuid());
                    handle.emplace(this, &**it);
                }

                // See if we can open more connections
                if (!handle && config_.max_connections > connections_.size()) {
                    MYSQLPOOL_LOG_DEBUG_("Mysqlpool::getConnection: The pool is full. Opening a new connection.");
                    mysql::tcp_connection conn{ctx_.get_executor()};
                    connections_.emplace_back(std::make_unique<Connection>(*this, std::move(conn)));
                    handle.emplace(this, &*connections_.back());
                }
            }
        } // mutex

        if (handle) {
            if (handle->isClosed()) {
                MYSQLPOOL_LOG_TRACE_("Got an unused DB connection, "
                                     << handle->uuid()
                                     << ", but it is closed. Will try to connect it");
                try {
                    co_await handle->reconnect();
                } catch (const runtime_error& ex) {
                    MYSQLPOOL_LOG_WARN_("Failed to connect CLOSED connection " << handle->uuid());
                }
            }
            MYSQLPOOL_LOG_TRACE_("Returning DB connection " << handle->uuid());
            co_return std::move(*handle);
        }


        MYSQLPOOL_LOG_TRACE_("Waiting for a DB connection to become available...");
        const auto [ec] = co_await semaphore_.async_wait(as_tuple(asio::use_awaitable));
        if (ec != boost::asio::error::operation_aborted) {
            MYSQLPOOL_LOG_DEBUG_("async_wait on semaphore failed: " << ec.message());
        }
        MYSQLPOOL_LOG_TRACE_("Done waiting");
    }

    assert(false); // Should never end up here
    co_return Handle{};
}

boost::asio::awaitable<void> Mysqlpool::close()
{
    MYSQLPOOL_LOG_DEBUG_("Closing all database connections...");
    closed_ = true;
    timer_.cancel();
    semaphore_.expires_from_now(
        boost::posix_time::seconds(config_.timeout_close_all_databases_seconds));

    while(true) {

        size_t num_connected = 0;
        {
            std::scoped_lock lock{mutex_};

            // Remove all closed entries
            erase_if(connections_, [](const auto &v) {
                return v->state() == Connection::State::CLOSED;
            });

            if (connections_.empty()) {
                MYSQLPOOL_LOG_DEBUG_("Done closing database connections.");
                co_return;
            }

            MYSQLPOOL_LOG_DEBUG_("Mysqlpool::close: The pool has "
                                 << connections_.size()
                                 << " remainig connections.");

            for(auto &conn : connections_) {
                if (conn->state() == Connection::State::CONNECTED) {
                    ++num_connected;
                    if (!conn->taken()) {
                        conn->take();
                        conn->close();
                    }
                }
            }
        }

        const auto [ec] = co_await semaphore_.async_wait(as_tuple(asio::use_awaitable));
        MYSQLPOOL_LOG_TRACE_("Wait ended. ec=" << ec);
        if (!ec) {
            MYSQLPOOL_LOG_WARN_("Graceful database disconnect timed out.");
            std::scoped_lock lock{mutex_};
            for(auto &conn : connections_) {
                if (conn->state() == Connection::State::CONNECTED) {
                    MYSQLPOOL_LOG_INFO_("Closing socket for db-connection " << conn->uuid());
                    conn->connection_.stream().lowest_layer().close();
                }
            }
            co_return;
        }

        if (ec && ec != boost::asio::error::operation_aborted) {
            MYSQLPOOL_LOG_WARN_("async_wait on semaphore during db-close failed: " << ec.message());
            // Give up.
            co_return;
        }
    }
}

void Mysqlpool::closed(Connection &conn)
{
    conn.setState(Connection::State::CLOSED);

    boost::system::error_code ec;
    if (closed_) {
        semaphore_.cancel(ec);
        return;
    }

    // If we have requests pending, they may want to reuse this connection.
    semaphore_.cancel_one(ec);
}


boost::asio::awaitable<void> Mysqlpool::init() {

    asio::ip::tcp::resolver resolver(ctx_.get_executor());
    auto endpoints = co_await resolver.async_resolve(config_.host,
                                                     std::to_string(config_.port),
                                                     boost::asio::use_awaitable);

    if (endpoints.empty()) {
        MYSQLPOOL_LOG_ERROR_("Mysqlpool::init Failed to resolve hostname "
                             << config_.host << " tor the database server: ");
        throw runtime_error{"Failed to resolve database hostname"};
    }

    MYSQLPOOL_LOG_DEBUG_("Mysqlpool::init Connecting to mysql compatible database at "
                         << config_.host << ':' << config_.port
                         << " as user " << dbUser() << " with database "
                         << config_.database);


    connections_.reserve(config_.max_connections);
    const auto initial_connections = min(config_.min_connections, config_.min_connections);
    MYSQLPOOL_LOG_DEBUG_("Mysqlpool::init Opening " << initial_connections << " connections.");
    for(size_t i = 0; i < initial_connections; ++i) {
        try {
            mysql::tcp_connection conn{ctx_.get_executor()};
            co_await connect(conn, endpoints, 0, true);
            connections_.emplace_back(make_unique<Connection>(*this, std::move(conn)));
            connections_.back()->setState(Connection::State::CONNECTED);
        } catch (const std::exception& ex) {
            MYSQLPOOL_LOG_WARN_("Mysqlpool::init Failed to open initial connection: " << ex.what());
        }
    }

    static constexpr auto one_hundred_years = 8766 * 100;
    semaphore_.expires_from_now(boost::posix_time::hours(one_hundred_years));

    startTimer();

    co_return;
}

bool Mysqlpool::handleError(const boost::system::error_code &ec, boost::mysql::diagnostics &diag)
{
    if (ec) {
        MYSQLPOOL_LOG_DEBUG_("Statement failed with error:  "
                             << ec.message() << " (" << ec.value()
                             << "). Client: " << diag.client_message()
                             << ". Server: " << diag.server_message());

        switch(ec.value()) {
        case static_cast<int>(mysql::common_server_errc::er_dup_entry):
            throw db_err_exists{ec};

        case boost::asio::error::eof:
        case boost::asio::error::broken_pipe:
        case boost::system::errc::connection_reset:
        case boost::system::errc::connection_aborted:
        case boost::asio::error::operation_aborted:
            MYSQLPOOL_LOG_DEBUG_("The error is recoverable if we re-try the query it may succeed...");
            return false; // retry

        default:
            MYSQLPOOL_LOG_DEBUG_("The error is non-recoverable");
            throw db_err{ec};
        }
    }
    return true;
}

void Mysqlpool::startTimer()
{
    if (closed_) {
        return;
    }

    timer_.expires_from_now(chrono::milliseconds{config_.timer_interval_ms});
    timer_.async_wait([this](auto ec) {
        try {
            onTimer(ec);
        } catch (const exception& ex) {
            MYSQLPOOL_LOG_WARN_("Mysqlpool: - onTimer() threw up: " << ex.what());
        }

        startTimer();
    });
}

void Mysqlpool::onTimer(boost::system::error_code ec)
{
    if (closed_) {
        MYSQLPOOL_LOG_DEBUG_("Mysqlpool::onTimer() - We are closing down the connection pool.");
        return;
    }

    MYSQLPOOL_LOG_TRACE_("onTimer()");
    std::scoped_lock lock{mutex_};
    const auto watermark = chrono::steady_clock::now();
    for(auto& conn : connections_) {
        if (conn->isAvailable() && conn->expires() <= watermark) {
            MYSQLPOOL_LOG_DEBUG_("Mysqlpool::onTimer - Closing idle connection " << conn->uuid());
            --num_open_connections_;
            conn->close();
        }
    }

    MYSQLPOOL_LOG_DEBUG_("Mysqlpool::onTimer() we now have " << num_open_connections_ << " open db connections.");
}

void Mysqlpool::release(Handle &h) noexcept {
    if (h.connection_) {
        MYSQLPOOL_LOG_TRACE_("DB Connection " << h.uuid() << " is being released from a handle.");
        std::scoped_lock lock{mutex_};
        assert(!h.connection_->isAvailable());
        h.connection_->touch();
        h.connection_->release();
    }
    boost::system::error_code ec;
    semaphore_.cancel_one(ec);
}


string Mysqlpool::dbUser() const
{
    return config_.username;
}

string Mysqlpool::dbPasswd() const
{
    if (config_.password.empty()) {

        MYSQLPOOL_LOG_WARN_("Database password for " << dbUser() << " is not set.");
    }

    return config_.password;
}

boost::asio::awaitable<void> Mysqlpool::Handle::reconnect()
{
    asio::ip::tcp::resolver resolver(parent_->ctx_.get_executor());
    auto endpoints = resolver.resolve(parent_->config_.host,
                                      std::to_string(parent_->config_.port));

    MYSQLPOOL_LOG_DEBUG_("Will try to connect "
                         << uuid()
                         << " to the database server at "
                         << parent_->config_.host << ":"
                         << parent_->config_.port);

    if (endpoints.empty()) {
        MYSQLPOOL_LOG_ERROR_("Failed to resolve hostname "
                             << parent_->config_.host << " for the database server");
        throw resolve_failed{"Failed to resolve database hostname"};
    }

    connection_->setState(Connection::State::CONNECTING);
    try {
        co_await parent_->connect(connection_->connection_, endpoints, 0, true);
        connection_->setState(Connection::State::CONNECTED);
    } catch(const std::runtime_error& ex) {
        connection_->setState(Connection::State::CLOSED);
        MYSQLPOOL_LOG_WARN_("Failed to reconnect: " << ex.what());
    }

    co_return;
}

Mysqlpool::Connection::Connection(Mysqlpool &parent, boost::mysql::tcp_connection &&conn)
    : connection_{std::move(conn)}, parent_{parent} {
    MYSQLPOOL_LOG_TRACE_("db Connection " << uuid() << " is being costructed");
    touch();
}

Mysqlpool::Connection::~Connection()
{
    MYSQLPOOL_LOG_TRACE_("db Connection " << uuid() << " is being deleted");
}

void Mysqlpool::Connection::setState(State state) {
    MYSQLPOOL_LOG_TRACE_("db Connection " << uuid() << " changing state from "
                                          << toString(state_) << " to " << toString(state));
    state_ = state;
}

void Mysqlpool::Connection::touch() {
    MYSQLPOOL_LOG_TRACE_("db Connection " << uuid() << " is touched.");
    expires_ = std::chrono::steady_clock::now() + chrono::seconds{parent_.config_.connection_idle_limit_seconds};
}


} // namespace
