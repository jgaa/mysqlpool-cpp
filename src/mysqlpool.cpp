
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

asio::awaitable<Mysqlpool::Handle> Mysqlpool::getConnection(const Options& opts) {
    while(true) {
        optional<Handle> handle;
        {
            std::scoped_lock lock{mutex_};
            if (closed_) {
                if (opts.throw_on_empty_connection) {
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
                    connections_.emplace_back(std::make_unique<Connection>(*this));
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
        try {
            co_await semaphore_.async_wait(asio::use_awaitable);
        } catch(const boost::system::system_error& ec) {
            if (ec.code() != boost::asio::error::operation_aborted) {
                MYSQLPOOL_LOG_DEBUG_("async_wait on semaphore failed: " << ec.what());
            }
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

        try {
            co_await semaphore_.async_wait(asio::use_awaitable);
            MYSQLPOOL_LOG_WARN_("Graceful database disconnect timed out.");
            std::scoped_lock lock{mutex_};
            for(auto &conn : connections_) {
                if (conn->state() == Connection::State::CONNECTED) {
                    MYSQLPOOL_LOG_INFO_("Closing socket for db-connection " << conn->uuid());
                    conn->connection().stream().lowest_layer().close();
                }
            }
            co_return;
        } catch(const boost::system::system_error& ec) {
            if (ec.code() != boost::asio::error::operation_aborted) {
                MYSQLPOOL_LOG_WARN_("async_wait on semaphore during db-close failed: " << ec.what());
                // Give up.
                co_return;
            }
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
                         << config_.database
                         << " and TLS MODE " << config_.tls_mode);

    connections_.reserve(config_.max_connections);
    const auto initial_connections = min(config_.min_connections, config_.min_connections);
    MYSQLPOOL_LOG_DEBUG_("Mysqlpool::init Opening " << initial_connections << " connections.");
    for(size_t i = 0; i < initial_connections; ++i) {
        try {
            auto conn = make_unique<Connection>(*this);
            co_await connect(conn->connection(), endpoints, 0, true);
            conn->setState(Connection::State::CONNECTED);
            connections_.emplace_back(std::move(conn));
        } catch (const std::exception& ex) {
            MYSQLPOOL_LOG_WARN_("Mysqlpool::init Failed to open initial connection: " << ex.what());
        }
    }

    static constexpr auto one_hundred_years = 8766 * 100;
    semaphore_.expires_from_now(boost::posix_time::hours(one_hundred_years));

    startTimer();

    co_return;
}

bool Mysqlpool::handleError(const boost::system::error_code &ec, boost::mysql::diagnostics &diag, ErrorMode em)
{
    if (ec) {
        MYSQLPOOL_LOG_DEBUG_("Statement failed with error:  "
                             << ec.message() << " (" << ec.value()
                             << "). Client: " << diag.client_message()
                             << ". Server: " << diag.server_message());

        if (em == ErrorMode::EM_IGNORE) {
            MYSQLPOOL_LOG_DEBUG_("Ignoring the error...");
            return false;
        }

        switch(ec.value()) {
        case static_cast<int>(mysql::common_server_errc::er_dup_entry):
            ::boost::throw_exception(db_err_exists{ec}, BOOST_CURRENT_LOCATION);

        case boost::asio::error::eof:
        case boost::asio::error::broken_pipe:
        case boost::system::errc::connection_reset:
        case boost::system::errc::connection_aborted:
        case boost::asio::error::operation_aborted:
            if (em == ErrorMode::EM_RETRY) {
                MYSQLPOOL_LOG_DEBUG_("The error is recoverable if we re-try the query it may succeed...");
                return false; // retry
            }
            MYSQLPOOL_LOG_DEBUG_("The error is recoverable but we will not re-try the query.");
            ::boost::throw_exception(server_err{ec}, BOOST_CURRENT_LOCATION);
        default:
            MYSQLPOOL_LOG_DEBUG_("The error is non-recoverable");
            ::boost::throw_exception(server_err{ec}, BOOST_CURRENT_LOCATION);
        }
    }
    return true;
}

void Mysqlpool::startTimer()
{
    if (closed_ || !config_.timer_interval_ms) {
        return;
    }

    timer_.expires_after(chrono::milliseconds{config_.timer_interval_ms});
    timer_.async_wait([this](auto ec) {
        try {
            onTimer(ec);
        } catch (const exception& ex) {
            MYSQLPOOL_LOG_WARN_("Mysqlpool: - onTimer() threw up: " << ex.what());
        }

        startTimer();
    });
}

void Mysqlpool::onTimer(boost::system::error_code /*ec*/)
{
    if (closed_) {
        MYSQLPOOL_LOG_DEBUG_("Mysqlpool::onTimer() - We are closing down the connection pool.");
        return;
    }

    MYSQLPOOL_LOG_TRACE_("onTimer()");
    const std::scoped_lock lock{mutex_};
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

    if (h.hasConnection()) {
        MYSQLPOOL_LOG_TRACE_("DB Connection " << h.uuid() << " is being released from a handle.");
        const std::scoped_lock lock{mutex_};
        h.release();
    }

    boost::system::error_code ec;
    semaphore_.cancel_one(ec);
}

boost::mysql::ssl_mode Mysqlpool::sslMode() const
{
    if (config_.tls_mode == "disable") {
        return boost::mysql::ssl_mode::disable;
    } else if (config_.tls_mode == "enable") {
        return boost::mysql::ssl_mode::enable;
    } else if (config_.tls_mode == "require") {
        return boost::mysql::ssl_mode::require;
    }

    MYSQLPOOL_LOG_WARN_("Unknown SSL mode: " << config_.tls_mode);
    return boost::mysql::ssl_mode::require;
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
                         << parent_->config_.port
                         << " with TLS mode " << parent_->config_.tls_mode);

    if (endpoints.empty()) {
        MYSQLPOOL_LOG_ERROR_("Failed to resolve hostname "
                             << parent_->config_.host << " for the database server");
        ::boost::throw_exception(resolve_failed{"Failed to resolve database hostname"}, BOOST_CURRENT_LOCATION);
    }

    connection_->resetConnection();
    connection_->clearCache();
    connection_->setState(Connection::State::CONNECTING);
    connection_->setTimeZone({});
    try {
        co_await parent_->connect(connection_->connection(), endpoints, 0, true);
        connection_->setState(Connection::State::CONNECTED);
    } catch(const std::runtime_error& ex) {
        connection_->setState(Connection::State::CLOSED);
        MYSQLPOOL_LOG_WARN_("Failed to reconnect: " << ex.what());
    }

    co_return;
}

Mysqlpool::Connection::Connection(Mysqlpool &parent)
    : parent_{parent} {

    MYSQLPOOL_LOG_TRACE_("db Connection " << uuid() << " is being costructed");
    resetConnection();
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

void Mysqlpool::Connection::resetConnection()
{
    MYSQLPOOL_LOG_TRACE_("db Connection " << uuid() << " gets a new socket.");
    connection_ = make_unique<connection_t>(parent_.ctx_.get_executor(), ssl_ctx_);
}

asio::ssl::context Mysqlpool::Connection::getSslContext(const TlsConfig &config) {
    auto tls_mode = boost::asio::ssl::context::tls_client;

    if (config.version == "") {
        ;
    } else if (config.version == "tls_1.2") {
        tls_mode = boost::asio::ssl::context::tlsv12_client;
    } else if (config.version == "tls_1.3") {
        tls_mode = boost::asio::ssl::context::tlsv13_client;
    } else {
        throw std::runtime_error{"Invalid TLS version: " + config.version};
    }

    boost::asio::ssl::context ctx{tls_mode};

    if (!config.allow_ssl) {
        ctx.set_options(boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::no_sslv3);
    }
    if (!config.allow_tls_1_1) {
        ctx.set_options(boost::asio::ssl::context::no_tlsv1_1);
    }
    if (!config.allow_tls_1_2) {
        ctx.set_options(boost::asio::ssl::context::no_tlsv1_2);
    }
    if (!config.allow_tls_1_3) {
        ctx.set_options(boost::asio::ssl::context::no_tlsv1_3);
    }
    if (config.verify_peer) {
        ctx.set_verify_mode(boost::asio::ssl::verify_peer);

        std::ranges::for_each(config.ca_files, [&ctx](const auto& file) {
            ctx.load_verify_file(file);
        });

        std::ranges::for_each(config.ca_paths, [&ctx](const auto& path) {
            ctx.add_verify_path(path);
        });

    } else {
        ctx.set_verify_mode(boost::asio::ssl::verify_none);
    }

    if (!config.cert_file.empty()) {
        ctx.use_certificate_file(config.cert_file, boost::asio::ssl::context::pem);
    }
    if (!config.key_file.empty()) {
        ctx.use_private_key_file(config.key_file, boost::asio::ssl::context::pem);
    }

    if (config.password_callback) {
        ctx.set_password_callback(config.password_callback);
    }

    return std::move(ctx);
}

// Impl. based from https://stackoverflow.com/questions/2262386/generate-sha256-with-openssl-and-c
sha256_hash_t sha256(const std::span<const uint8_t> in) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const ScopedExit bye{[context] {
        EVP_MD_CTX_free(context);
    }};

    if (context != nullptr) {
        if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 0) {
            if (EVP_DigestUpdate(context, in.data(), in.size()) != 0) {
                sha256_hash_t hash;
                unsigned int lengthOfHash = 0;
                if (EVP_DigestFinal_ex(context, hash.data(), &lengthOfHash) != 0) {
                    assert(lengthOfHash == hash.size());
                    return hash;
                }
            }
        }
    }
    throw runtime_error{"sha256 failed!"};
}

} // namespace
