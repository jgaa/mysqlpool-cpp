
#include "mysqlpool/conf.h"
#include "mysqlpool/mysqlpool.h"
#include "mysqlpool/logging.h"

using namespace std;
namespace asio = boost::asio;
namespace mysql = boost::mysql;

namespace jgaa::mysqlpool {

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
            if (connections_.empty()) {
                if (throwOnEmpty) {
                    throw runtime_error{"No database connections are open. Is the server shutting down?"};
                }
                co_return Handle{};
            }
            if (auto it = std::ranges::find_if(connections_, [](const auto& c) {
                    return c.available_;
                } ); it != connections_.end()) {
                it->available_ = false;
                handle.emplace(this, &*it);
            }
        }

        if (handle) {
            MYSQLPOOL_LOG_TRACE_("Returning a DB connection.");
            co_return std::move(*handle);
        }

        MYSQLPOOL_LOG_TRACE_("Waiting for a DB connection to become available...");
        const auto [ec] = co_await semaphore_.async_wait(as_tuple(asio::use_awaitable));
        if (ec != boost::asio::error::operation_aborted) {
            MYSQLPOOL_LOG_DEBUG_("async_wait on semaphore failed: " << ec.message());
        }
        MYSQLPOOL_LOG_TRACE_("Done waiting");
    }

    co_return Handle{};
}

boost::asio::awaitable<void> Mysqlpool::close()
{
    MYSQLPOOL_LOG_DEBUG_("Closing database connections...");
    while(true) {
        // Use this to get the connections while they are available
        auto conn = co_await getConnection(false);
        if (conn.empty()) {
            MYSQLPOOL_LOG_DEBUG_("Done closing database connections.");
            break; // done
        }

        try {
            MYSQLPOOL_LOG_TRACE_("Closing db connection.");
            co_await conn.connection().async_close(asio::use_awaitable);
            conn.reset();

            // Delete the Connection object
            std::scoped_lock lock{mutex_};
            if (auto it = find_if(connections_.begin(), connections_.end(), [&](const auto& v) {
                    return addressof(v) == conn.connection_;
                }); it != connections_.end()) {
                connections_.erase(it);
            } else {
                MYSQLPOOL_LOG_ERROR_("Failed to lookup a connection I just closed!");
            }
        } catch(const exception&) {}
    }
}

boost::asio::awaitable<void> Mysqlpool::init() {

    asio::ip::tcp::resolver resolver(ctx_.get_executor());
    auto endpoints = co_await resolver.async_resolve(config_.host,
                                                     std::to_string(config_.port),
                                                     boost::asio::use_awaitable);

    if (endpoints.empty()) {
        MYSQLPOOL_LOG_ERROR_("Failed to resolve hostname "
                             << config_.host << " tor the database server: ");
        throw runtime_error{"Failed to resolve database hostname"};
    }

    MYSQLPOOL_LOG_DEBUG_("Connecting to mysql compatible database at "
                         << config_.host << ':' << config_.port
                         << " as user " << dbUser() << " with database "
                         << config_.database);


    connections_.reserve(config_.max_connections);


    for(size_t i = 0; i < config_.max_connections; ++i) {
        mysql::tcp_connection conn{ctx_.get_executor()};
        co_await connect(conn, endpoints, 0, true);
        connections_.emplace_back(std::move(conn));
        co_return;
    }

    static constexpr auto one_hundred_years = 8766 * 100;
    semaphore_.expires_from_now(boost::posix_time::hours(one_hundred_years));

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

void Mysqlpool::release(Handle &h) noexcept {
    if (h.connection_) {
        std::scoped_lock lock{mutex_};
        assert(h.connection_->available_ == false);
        h.connection_->available_ = true;
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

    MYSQLPOOL_LOG_DEBUG_("Will try to re-connect to the database server at "
                         << parent_->config_.host << ":" << parent_->config_.port);

    if (endpoints.empty()) {
        MYSQLPOOL_LOG_ERROR_("Failed to resolve hostname "
                             << parent_->config_.host << " tor the database server: ");
        throw resolve_failed{"Failed to resolve database hostname"};
    }

    co_await parent_->connect(connection_->connection_, endpoints, 0, true);
    co_return;
}


} // namespace
