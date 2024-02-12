#pragma once

#include <iostream>
#include <utility>
#include <chrono>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "mysqlpool/conf.h"
#include "mysqlpool/config.h"
#include "mysqlpool/logging.h"

namespace jgaa::mysqlpool {

// https://stackoverflow.com/questions/68443804/c20-concept-to-check-tuple-like-types
template<class T, std::size_t N>
concept has_tuple_element =
    requires(T t) {
        typename std::tuple_element_t<N, std::remove_const_t<T>>;
        { get<N>(t) } -> std::convertible_to<const std::tuple_element_t<N, T>&>;
    };

template<class T>
concept tuple_like = !std::is_reference_v<T>
                     && requires(T t) {
                            typename std::tuple_size<T>::type;
                            requires std::derived_from<
                                std::tuple_size<T>,
                                std::integral_constant<std::size_t, std::tuple_size_v<T>>
                                >;
                        } && []<std::size_t... N>(std::index_sequence<N...>) {
                            return (has_tuple_element<T, N> && ...);
                        }(std::make_index_sequence<std::tuple_size_v<T>>());


struct aborted : public std::runtime_error {
    aborted() noexcept : std::runtime_error{"aborted"} {};

    template <typename T>
    aborted(T message) : std::runtime_error{message} {};
};

struct resolve_failed : public std::runtime_error {

    template <typename T>
    resolve_failed(T message) : std::runtime_error{message} {};
};


struct db_err : public std::runtime_error {

    db_err(boost::system::error_code ec, std::string message) noexcept
        :  std::runtime_error{std::move(message)}, ec_{ec} {}

    db_err(boost::system::error_code ec) noexcept
        :  std::runtime_error{ec.message()}, ec_{ec} {}

    const boost::system::error_code& error() const noexcept {
        return ec_;
    }

private:
    boost::system::error_code ec_;
};

struct db_err_exists : public db_err {
    db_err_exists(boost::system::error_code ec)
        : db_err(ec, ec.message()) {}
};


template <typename T>
concept OptionalPrintable = requires {
    std::is_same_v<std::remove_cv<T>, std::optional<typename T::value_type>>;
    std::cout << *std::declval<T>();
};

} // namespace

// TODO: Move this out of the global namespace
template <::jgaa::mysqlpool::OptionalPrintable T>
std::ostream& operator << (std::ostream& out, const T& val) {
    if (val) {
        return out << *val;
    }
    return out << "(null)";
}

namespace jgaa::mysqlpool {

using results = boost::mysql::results;

constexpr auto tuple_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);

/*! Database interface
 *
 * Normally a singleton
 *
 */

class Customize {

};

class Mysqlpool {
public:
    Mysqlpool(boost::asio::io_context& ctx, const DbConfig& config)
        : ctx_{ctx}, semaphore_{ctx}, config_{config}
    {
        if (config_.max_connections == 0) {
            assert(config_.max_connections);
            throw std::runtime_error{"Max db-connections must be non-zero!"};
        }
    }

    Mysqlpool(const Mysqlpool&) = delete;
    Mysqlpool(Mysqlpool&&) = delete;

    Mysqlpool& operator = (const Mysqlpool&) = delete;
    Mysqlpool& operator = (Mysqlpool&&) = delete;

    struct Connection {
        enum class State {
            CONNECTED,
            CLOSING,
            CLOSED,
            CONNECTING
        };

        Connection(Mysqlpool& parent, boost::mysql::tcp_connection &&conn);

        ~Connection();


        State state() const noexcept {
            return state_;
        }

        bool isAvailable() const noexcept {
            return !taken() && state_ == State::CONNECTED;
        }

        void setState(State state);

        void close() {
            setState(State::CLOSING);
            connection_.async_close([this](boost::system::error_code ec) {
                parent_.closed(*this);
            });
        }

        void touch();

        auto expires() const noexcept {
            return expires_;
        }

        bool taken() const noexcept {
            return taken_;
        }

        void take() {
            assert(!taken_);
            taken_ = true;
        }

        void release() {
            assert(taken_);
            taken_ = false;
        }

        const auto& uuid() const noexcept {
            return uuid_;
        }

        boost::mysql::tcp_connection connection_;
    private:
        std::atomic<State> state_{State::CLOSED};
        bool taken_{false};
        Mysqlpool& parent_;
        const boost::uuids::uuid uuid_{parent_.uuid_gen_()};
        std::chrono::steady_clock::time_point expires_{};
    };

    class Handle {
    public:
        Handle() = default;
        Handle(const Handle &) = delete;

        Handle(Handle &&v) noexcept
            : parent_{std::exchange(v.parent_, {})}
            , connection_{std::exchange(v.connection_, {})}
            , uuid_{std::exchange(v.uuid_, {})}
        {
        }

        ~Handle() {
            if (parent_) {
                parent_->release(*this);
            } else {
                assert(!connection_);
            }
        }

        Handle& operator = (const Handle) = delete;
        Handle& operator = (Handle && v) noexcept {
            parent_ = v.parent_;
            v.parent_ = {};
            connection_ = v.connection_;
            v.connection_ = {};
            v.uuid_ = uuid_;
            uuid_ = {};
            return *this;
        }

        explicit Handle(Mysqlpool* db, Connection* conn)
            : parent_{db}, connection_{conn}, uuid_{conn->uuid()}
        {
            connection_->take();
        }

        // Return the mysql connection
        auto& connection() {
            assert(connection_);
            return connection_->connection_;
        }

        bool empty() const noexcept {
            return connection_ != nullptr;
        }

        void reset() {
            parent_ = {};
            connection_ = {};
        }

        bool isClosed() const noexcept {
            return connection_->state() == Connection::State::CLOSED;
        }

        auto uuid() const noexcept {
            return uuid_;
        }

        Mysqlpool *parent_{};
        Connection *connection_{};
        boost::uuids::uuid uuid_;

        boost::asio::awaitable<void> reconnect();
    };

    [[nodiscard]] boost::asio::awaitable<Handle> getConnection(bool throwOnEmpty = true);

    // Execute a query or prepared, bound statement
    template <BOOST_MYSQL_EXECUTION_REQUEST T>
    boost::asio::awaitable<results> exec(T query) {
        auto conn = co_await getConnection();
        logQuery("static", query);
        results res;
        boost::mysql::diagnostics diag;
        auto [ec] = co_await conn.connection().async_execute(query,
                                                             res, diag,
                                                             tuple_awaitable);
        if (ec) {
            handleError(ec, diag);
        }
        co_return std::move(res);
    }

    template<tuple_like T>
    boost::asio::awaitable<results> exec(std::string_view query, const T& tuple) {

        results res;
        co_await std::apply([&](auto... args) -> boost::asio::awaitable<void>  {
            res = co_await exec(query, args...);
        }, tuple);

        co_return res;
    }

    template<typename ...argsT>
    boost::asio::awaitable<results> exec(std::string_view query, argsT ...args) {
        auto conn = co_await getConnection();
        logQuery("statement", query, args...);
        results res;
        boost::mysql::diagnostics diag;

    again:

        if constexpr (sizeof...(argsT) == 0) {
            auto [ec] = co_await conn.connection().async_execute(query, res, diag, tuple_awaitable);
            if (!handleError(ec, diag)) {
                co_await conn.reconnect();
                goto again;
            }
        } else {
            auto [ec, stmt] = co_await conn.connection().async_prepare_statement(query, diag, tuple_awaitable);
            if (!handleError(ec, diag)) {
                co_await conn.reconnect();
                goto again;
            }
            std::tie(ec) = co_await conn.connection().async_execute(stmt.bind(args...), res, diag, tuple_awaitable);\
                if (!handleError(ec, diag)) {
                co_await conn.reconnect();
                goto again;
            }
        }

        co_return std::move(res);
    }

    boost::asio::awaitable<void> init();
    boost::asio::awaitable<void> close();
    void closed(Connection& conn);

private:

    template <typename epT, typename connT = boost::mysql::tcp_connection>
    boost::asio::awaitable<void> connect(connT& conn, epT& endpoints, unsigned iteration, bool retry) {

        const auto user = dbUser();
        const auto pwd = dbPasswd();
        boost::mysql::handshake_params params(
            user,
            pwd,
            config_.database
            );

        std::string why;
        unsigned retries = 0;

        for(auto ep : endpoints) {
            MYSQLPOOL_LOG_DEBUG_("New db connection to " << ep.endpoint());
        again:
            if (ctx_.stopped()) {
                MYSQLPOOL_LOG_INFO_("Server is shutting down. Aborting connect to the database.");
                throw aborted{"Server is shutting down"};
            }

            boost::mysql::diagnostics diag;
            boost::system::error_code ec;
            std::tie(ec) = co_await conn.async_connect(ep.endpoint(), params, diag, tuple_awaitable);
            if (ec) {
                if (ec == boost::system::errc::connection_refused
                    || ec == boost::asio::error::broken_pipe
                    || ec == boost::system::errc::connection_reset
                    || ec == boost::system::errc::connection_aborted
                    || ec == boost::asio::error::operation_aborted
                    || ec == boost::asio::error::eof) {
                    if (retry && iteration == 0 && ++retries <= config_.retry_connect) {
                        MYSQLPOOL_LOG_INFO_("Failed to connect to the database server. Will retry "
                                            << retries << "/" << config_.retry_connect);
                        boost::asio::steady_timer timer(ctx_);
                        boost::system::error_code ec;
                        timer.expires_after(std::chrono::milliseconds{config_.retry_connect_delay_ms});
                        timer.wait(ec);
                        goto again;
                    }
                }

                retries = 0;
                MYSQLPOOL_LOG_DEBUG_("Failed to connect to to mysql compatible database at "
                                     << ep.endpoint()<< " as user " << dbUser() << " with database "
                                     << config_.database << ": " << ec.message());
                if (why.empty()) {
                    why = ec.message();
                }

            } else {
                //co_return std::move(conn);
                ++num_open_connections_;
                co_return;
            }
        }

        MYSQLPOOL_LOG_ERROR_("Failed to connect to to mysql compatible database at "
                             << config_.host << ':' << config_.port
                             << " as user " << dbUser() << " with database "
                             << config_.database
                             << ": " << why);

        throw std::runtime_error{"Failed to connect to database"};
    }

    // If it returns false, connection to server is closed
    bool handleError(const boost::system::error_code& ec, boost::mysql::diagnostics& diag);

    template <typename... T>
    std::string logArgs(const T... args) {
        if constexpr (sizeof...(T)) {
            std::stringstream out;
            out << " | args: ";
            auto col = 0;
            ((out << (++col == 1 ? "" : ", ")  << args), ...);
            return out.str();
        }
        return {};
    }

    template <typename... T>
    void logQuery(std::string_view type, std::string_view query, T... bound) {
        MYSQLPOOL_LOG_TRACE_("Exceuting " << type << " SQL query: " << query << logArgs(bound...));
    }

    void startTimer();
    void onTimer(boost::system::error_code ec);

    void release(Handle& h) noexcept;
    std::string dbUser() const;
    std::string dbPasswd() const;

    boost::asio::io_context& ctx_;
    std::atomic<unsigned> num_open_connections_{};
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Connection>> connections_;
    boost::asio::steady_timer timer_{ctx_};
    boost::asio::deadline_timer semaphore_;
    const DbConfig& config_;
    bool closed_{false};
    static boost::uuids::random_generator uuid_gen_;
};

} // namespace

std::ostream& operator << (std::ostream& out, const jgaa::mysqlpool::Mysqlpool::Connection::State& state);
