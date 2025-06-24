#pragma once

#include <iostream>
#include <utility>
#include <chrono>
#include <span>
#include <tuple>
#include <algorithm>
#include <ranges>
#include <deque>

#include <boost/asio.hpp>
#include <boost/mysql.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/functional/hash.hpp>

#include "mysqlpool/conf.h"
#include "mysqlpool/config.h"
#include "mysqlpool/logging.h"

namespace jgaa::mysqlpool {

template <typename T>
struct ScopedExit {
    explicit ScopedExit(T&& fn)
        : fn_{std::move(fn)} {}

    ScopedExit(const ScopedExit&) = delete;
    ScopedExit(ScopedExit&&) = delete;

    ~ScopedExit() {
        fn_();
    }

    ScopedExit& operator =(const ScopedExit&) = delete;
    ScopedExit& operator =(ScopedExit&&) = delete;

private:
    T fn_;
};

#include <coroutine>
#include <optional>

template<typename T>
struct Generator {
    struct promise_type {
        std::optional<T> current;

        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept   { return {}; }
        std::suspend_always yield_value(T value) noexcept {
            current = std::move(value);
            return {};
        }
        void unhandled_exception() { std::terminate(); }
        void return_void() noexcept {}
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit Generator(handle_type h) : coro(h) {}
    Generator(Generator&& rhs) noexcept : coro(rhs.coro) { rhs.coro = {}; }
    ~Generator() { if (coro) coro.destroy(); }

    struct iterator {
        handle_type coro;
        bool done;

        iterator(handle_type h, bool d) : coro(h), done(d) {}

        iterator& operator++() {
            coro.resume();
            done = coro.done();
            return *this;
        }
        T const& operator*() const {
            return *coro.promise().current;
        }
        bool operator==(std::default_sentinel_t) const { return done; }
    };

    iterator begin() {
        coro.resume();
        return {coro, coro.done()};
    }
    std::default_sentinel_t end() { return {}; }

private:
    handle_type coro;
};

template<std::ranges::input_range Range, typename F>
auto BindTupleGenerator(const Range& objects, F fn)
    -> Generator< decltype(fn(*std::begin(objects))) >
{
    using TupleType = decltype(fn(*std::begin(objects)));
    for (auto const& obj : objects) {
        // create the per-object tuple on the fly:
        TupleType tpl = fn(obj);
        co_yield std::move(tpl);
    }
}

template<class T>
concept HasPromiseType = requires {
    typename std::remove_reference_t<T>::promise_type;
};

template<class G>
concept IterableGenerator = requires(G g) {
    { g.begin() } -> std::input_iterator;               // we can start iterating
    { g.end()   } -> std::sentinel_for<decltype(g.begin())>;  // we can end
};

template<class G>
concept IsGenerator = HasPromiseType<G>; // && IterableGenerator<G>;

class FieldViewMatrix {
public:
    // using value_type = boost::mysql::field_view;
    // using Row = std::span<const value_type>;
    using Field = boost::mysql::field_view;
    using Row = std::span<const Field>;
    using value_type = Row;

    class RowIterator {
    public:
        // 1) Iterator‐traits for C++20:
        using iterator_concept = std::input_iterator_tag;
        using value_type        = Row;
        using reference         = Row;
        using difference_type   = std::ptrdiff_t;
        using pointer           = void;  // not really used for input_iterator

        // 2) Default ctor so sentinel_for<RowIterator,RowIterator>’s semiregular<> works:
        RowIterator() noexcept = default;

        // 3) Your existing ctor, storing a pointer to the parent & an index:
        RowIterator(const FieldViewMatrix* parent, std::size_t pos) noexcept
            : parent_(parent), pos_(pos) {}

        // 4) Dereference:
        reference operator*() const noexcept {
            return (*parent_)[pos_];
        }

        // 5) Pre-increment
        RowIterator& operator++() noexcept {
            ++pos_;
            return *this;
        }
        // 6) Post-increment
        RowIterator operator++(int) noexcept {
            RowIterator tmp = *this;
            ++pos_;
            return tmp;
        }

        // 7) Equality comparison
        friend bool operator==(const RowIterator& a, const RowIterator& b) noexcept {
            return a.parent_ == b.parent_ && a.pos_ == b.pos_;
        }
        friend bool operator!=(const RowIterator& a, const RowIterator& b) noexcept {
            return !(a == b);
        }

    private:
        const FieldViewMatrix* parent_ = nullptr;
        std::size_t pos_ = 0;
    };


    FieldViewMatrix(std::size_t rows, std::size_t cols)
        : rows_(rows), cols_(cols), data_(rows * cols) {}

    Field& operator()(std::size_t row, std::size_t col) {
        return data_[row * cols_ + col];
    }

    const Field& operator()(std::size_t row, std::size_t col) const {
        return data_[row * cols_ + col];
    }

    Row operator[](std::size_t row) const {
        return Row{&data_[row * cols_], cols_};
    }

    std::size_t row_count() const { return rows_; }

    auto begin() const {
        return RowIterator{this, 0};
    }

    auto end() const {
        return RowIterator{this, rows_};
    }

    template <typename T>
    void set(std::size_t row, std::size_t col, const T& value) {
        const auto pos = row * cols_ + col;
        auto it = data_.begin() + pos;
        data_.emplace(it, value);
    }

    void set(std::size_t row, std::size_t col, const std::optional<std::string_view>& value) {
        if (value) {
            set(row, col, *value);
        } else {
            set(row, col, nullptr);
        }
    }

    Field& at(std::size_t row, std::size_t col) {
        if (row >= rows_ || col >= cols_) {
            throw std::out_of_range("FieldViewMatrix index out of range");
        }
        return data_[row * cols_ + col];
    }

    std::size_t cols() const noexcept {
        return cols_;
    }

    std::size_t rows() const noexcept {
        return rows_;
    }

    template <typename V>
    const std::string& addBuffer(const V& str) {
        return buffers_.emplace_back(str);
    }

    template <typename V>
    void copy(std::size_t row, std::size_t col, const V& str) {
        const auto& v = buffers_.emplace_back(str);
        set(row, col, v);
    }

    template <typename V>
    void copy(std::size_t row, std::size_t col, const std::optional<V>& str) {
        if (str) {
            const auto& v = buffers_.emplace_back(str.value());
            set(row, col, std::string_view{v});
            return;
        }
        set(row, col, nullptr);
    }

private:
    std::size_t rows_, cols_;
    std::vector<Field> data_;
    std::deque<std::string> buffers_;

    // class RowIterator {
    // public:
    //     RowIterator(const FieldViewMatrix& parent, std::size_t pos)
    //         : parent_(parent), pos_(pos) {}

    //     Row operator*() const { return parent_[pos_]; }
    //     RowIterator& operator++() { ++pos_; return *this; }
    //     bool operator!=(const RowIterator& other) const { return pos_ != other.pos_; }

    // private:
    //     const FieldViewMatrix& parent_;
    //     std::size_t pos_;
    // };
};


using sha256_hash_t = std::array<uint8_t, 32>;
sha256_hash_t sha256(const std::span<const uint8_t> in);

inline ::std::ostream& operator << (::std::ostream& out, const boost::mysql::blob_view& blob) {
    return out << "{ blob size: " << blob.size() << " }";
}

template<typename T>
concept FieldViewContainer = std::ranges::range<T> && std::same_as<std::ranges::range_value_t<T>, boost::mysql::field_view>;

template <FieldViewContainer T>
std::ostream& operator << (std::ostream& out, const T& range) {
    auto col = 0;
    for (const auto& f : range) {
        out << (++col == 1 ? "" : ", ") << f;
    }
    return out;
}

template<typename T>
concept RangeOfFieldViewContainer =
    std::ranges::range<T> &&
    FieldViewContainer<std::ranges::range_value_t<T>>;

// https://stackoverflow.com/questions/68443804/c20-concept-to-check-tuple-like-types
template<class T, std::size_t N>
concept has_tuple_element =
    requires(T t) {
        typename std::tuple_element_t<N, std::remove_const_t<T>>;
        { get<N>(t) } -> std::convertible_to<const std::tuple_element_t<N, T>&>;
    };

// template<class T>
// concept tuple_like = !std::is_reference_v<T>
//                      && requires(T t) {
//                             typename std::tuple_size<T>::type;
//                             requires std::derived_from<
//                                 std::tuple_size<T>,
//                                 std::integral_constant<std::size_t, std::tuple_size_v<T>>
//                                 >;
//                         } && []<std::size_t... N>(std::index_sequence<N...>) {
//                             return (has_tuple_element<T, N> && ...);
//                         }(std::make_index_sequence<std::tuple_size_v<T>>());

template<typename T>
concept tuple_like = requires(T&& t) {
    std::tuple_size<std::remove_cvref_t<T>>::value;    // etc…
};

// a concept that’s true if *any* of the Args is tuple_like
template<typename... Args>
concept AnyTupleArg = ( tuple_like<std::remove_cvref_t<Args>> || ... );

/*! Exception thrown if a statement was aborted.
 *
 *  For example if a connection is about to made after
 *  `close()` is called.
 */
struct aborted : public std::runtime_error {
    aborted() noexcept : std::runtime_error{"aborted"} {};

    template <typename T>
    aborted(T message) : std::runtime_error{message} {};
};

/*! The hostname of the database server could not be resolved */
struct resolve_failed : public std::runtime_error {

    template <typename T>
    resolve_failed(T message) : std::runtime_error{message} {};
};

/*! Some error occured for a query */
struct server_err : public std::runtime_error {

    server_err(boost::system::error_code ec, std::string message) noexcept
        :  std::runtime_error{std::move(message)}, ec_{ec} {}

    server_err(boost::system::error_code ec) noexcept
        :  std::runtime_error{ec.message()}, ec_{ec} {}

    const boost::system::error_code& error() const noexcept {
        return ec_;
    }

private:
    boost::system::error_code ec_;
};

/*! A query failed because a unique constraint was violated */
struct db_err_exists : public server_err {
    db_err_exists(boost::system::error_code ec)
        : server_err(ec, ec.message()) {}
};

namespace detail {

template <typename T>
concept OptionalPrintable = requires (T v) {
    std::is_same_v<std::remove_cv<T>, std::optional<typename T::value_type>>;
    std::cout << *v;
};

template <OptionalPrintable T>
std::ostream& operator << (std::ostream& out, const T& val) {
    if (val) {
        return out << *val;
    }
    return out << "(null)";
}

}

using results = boost::mysql::results;

constexpr auto tuple_awaitable = boost::asio::as_tuple(boost::asio::use_awaitable);

struct Options {

    Options(bool reconnect = true) : reconnect_and_retry_query{reconnect} {}

    bool reconnect_and_retry_query{true};
    std::string time_zone;
    bool throw_on_empty_connection{false};

    /// Any prepared statement that is executed will be closed after the query is executed
    bool close_prepared_statement{false};
};


template<typename T>
concept IsOptions =
    std::is_same_v<std::remove_cvref_t<T>, Options>;

template<typename T, typename ...ArgsT>
const T& getFirstArgumentAsConstRef(const T& first, ArgsT...) {
    return first;
}

template<typename T, typename ...ArgsT>
T& getFirstArgumentAsRef(T& first, ArgsT...) {
    return first;
}


class Mysqlpool {
    enum class ErrorMode {
        EM_IGNORE, // Cant use "IGNORE" as it clashes with one of Microsofts *many* cursed macros!
        EM_RETRY,
        EM_ALWAYS_FAIL
    };


public:
    using connection_t = boost::mysql::tcp_ssl_connection;

    template <typename... T>
    static std::string logArgs(const T... args) {
        using namespace detail;
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
    static void logQuery(std::string_view type, std::string_view query, T... bound) {
        MYSQLPOOL_LOG_TRACE_("Executing " << type << " SQL query: " << query << logArgs(bound...));
    }

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

    class StatementCache {
    public :
        StatementCache() = default;

        boost::mysql::statement& operator[](const std::span<const char> key) {
            return cache_[makeHash(key)];
        }

        void clear() {
            cache_.clear();
        }

        size_t size() const noexcept {
            return cache_.size();
        }

        void erase(const std::span<const char> key) {
            cache_.erase(makeHash(key));
        }

        static sha256_hash_t makeHash(const std::span<const char> key) {
            return sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(key.data()), key.size()));
        }

    private:
        struct ArrayHasher {
            std::size_t operator()(const sha256_hash_t& key) const {
                /* I might my wrong, but it makes no sense to me to re-hash a cryptographic hash */
                return *reinterpret_cast<const size_t *>(key.data());
            }
        };
        std::unordered_map<sha256_hash_t, boost::mysql::statement, ArrayHasher> cache_;
    };

    struct Connection {
        enum class State {
            CONNECTED,
            CLOSING,
            CLOSED,
            CONNECTING
        };

        Connection(Mysqlpool& parent);
        Connection(const Connection&) = delete;
        Connection(Connection&&) = delete;
        ~Connection();

        Connection& operator = (const Connection&) = delete;
        Connection& operator = (Connection&&) = delete;

        State state() const noexcept {
            return state_;
        }

        bool isAvailable() const noexcept {
            return !taken() && state_ == State::CONNECTED;
        }

        void setState(State state);

        void close() {
            setState(State::CLOSING);
            connection().async_close([this](boost::system::error_code /*ec*/) {
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

        void clearCache() {
            stmt_cache_.clear();
        }

        auto& stmtCache() noexcept {
            return stmt_cache_;
        }

        // NB: not synchronized. Assumes safe access when it's not being changed.
        void setTimeZone(const std::string& name) {
            time_zone_name_ = name;
        }

        // NB: not synchronized. Assumes safe access when it's not being changed.
        std::string timeZone() const {
            return time_zone_name_;
        }

        // NB: not synchronized. Assumes safe access when it's not being changed.
        bool isSameTimeZone(std::string_view name) const noexcept {
            return name == time_zone_name_;
        }

        // Cache for prepared statements (per connection)
        using statement_t = std::tuple<boost::system::error_code, boost::mysql::statement *>;

        boost::asio::awaitable<statement_t>
        getStmt(boost::mysql::diagnostics& diag, std::string_view query) {
            auto& cached_stmt = stmt_cache_[query];
            if (!cached_stmt.valid()) {
                logQuery("prepare-stmt", query);
                auto [ec, actual_stmt] = co_await connection().async_prepare_statement(query, diag, tuple_awaitable);
                if (ec) {
                    stmt_cache_.erase(query);
                    boost::system::error_code sec = ec;
                    boost::mysql::statement *null_stmt = nullptr;
                    co_return std::make_tuple(ec, null_stmt);
                }
                cached_stmt = std::move(actual_stmt);
            }

            co_return std::make_tuple(boost::system::error_code{}, &cached_stmt);
        }

        connection_t& connection() {
            assert(connection_);
            return *connection_;
        }

        void resetConnection();

private:
        static boost::asio::ssl::context getSslContext(const TlsConfig& config);

        Mysqlpool& parent_;
        boost::asio::ssl::context ssl_ctx_{getSslContext(parent_.config_.tls)};
        std::unique_ptr<connection_t> connection_;
        std::atomic<State> state_{State::CLOSED};
        bool taken_{false};
        std::string time_zone_name_;
        StatementCache stmt_cache_;
        const boost::uuids::uuid uuid_{parent_.uuid_gen_()};
        std::chrono::steady_clock::time_point expires_{};
    };

    class Handle {
    public:
        /*! Transaction handle
         *
         *  A transaction handle is a RAII object you will
         *  use to commit or rollback the transaction.
         *
         *  If the transaction is not committed or rolled back, it will be
         *  rolled back when the transaction object goes out of scope.
         *  If this happen, the Handle will also be released and
         *  any instance of the Handle will be reset and not usable.
         *
         *  Once a transaction is committed or rolled back, the transaction
         *  object is empty and can not be used again, unless you re-assign
         *  a new transaction to it.
         */
        class Transaction {
        public:
            Transaction(Handle& h, bool readOnly)
                : handle_{&h}, read_only_{readOnly}
            {
            }

            Transaction(const Transaction&) = delete;
            Transaction(Transaction&& t) noexcept
                : handle_{t.handle_}, read_only_{t.read_only_} {
                assert(handle_);
                t.handle_ = {};
            };

            /*! Commit the current transaction */
            boost::asio::awaitable<void> commit() {
                assert(handle_);
                if (handle_) {
                    if (handle_->failed()) {
                        if (!read_only_) {
                            throw std::runtime_error{"The transaction cannot be committed as a query has already failed!"};
                        }
                    } else {
                        co_await handle_->commit();
                    }
                }
                handle_ = {};
                co_return;
            }

            /*! Roll back the current transaction */
            boost::asio::awaitable<void> rollback() {
                assert(handle_);
                if (handle_ && !handle_->failed()) {
                    co_await handle_->rollback();
                }
                handle_ = {};
                co_return;
            }

            /*! Commit the transaction later
             *
             *  This will commit the transaction and release the connection in
             *  another coroutine. The current handle will be reset and unusable.
             *
             *  The purpose of this method is to allow a transaction to be committed and
             *  the database connection to be released back to the connection pool even
             *  if the Handle object itself remains in scope.
             */
            void commitLater() {
                assert(handle_);
                if (handle_) {
                    handle_->commitAndReleaseLater();
                    handle_ = {};
                }
            }

            /*! Roll back the transaction later
             *
             *  This will roll back the transaction and release the connection in
             *  another coroutine. The current handle will be reset and unusable.
             *
             *  The purpose of this method is to allow a transaction to be rolled back and
             *  the database connection to be released back to the connection pool even
             *  if the Handle object itself remains in scope.
             */

            void rollbackLater() {
                assert(handle_);
                if (handle_) {
                    handle_->rollbackAndReleaseLater();
                    handle_ = {};
                }
            }

            ~Transaction() noexcept {
                if (!empty()) {
                    if (handle_ && handle_->failed()) {
                        MYSQLPOOL_LOG_DEBUG_("Handle failed. Transaction not committed or rolled back!)");
                        return;
                    }
                    assert(handle_);
                    handle_->rollbackAndReleaseLater();
                }
            }

            bool empty() const noexcept {
                return handle_ == nullptr;
            }

            Transaction& operator = (const Transaction&) = delete;
            Transaction& operator = (Transaction&& t) noexcept {
                assert(t.handle_);
                handle_ = t.handle_;
                read_only_ = t.read_only_;
                t.handle_ = {};
                return *this;
            }

        private:
            Handle *handle_ = {};
            bool read_only_ = false;
        };


        Handle() = default;
        Handle(const Handle &) = delete;

        Handle(Handle &&v) noexcept
            : parent_{std::exchange(v.parent_, {})}
            , connection_{std::exchange(v.connection_, {})}
            , uuid_{std::exchange(v.uuid_, {})}
            , has_transaction_{std::exchange(v.has_transaction_, false)}
            , failed_{std::exchange(v.failed_, false)}
            , ex_state_{std::exchange(v.ex_state_, {})}
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
            parent_ = std::exchange(v.parent_, {});
            connection_ = std::exchange(v.connection_, {});
            uuid_ = std::exchange(v.uuid_, {});
            has_transaction_ = std::exchange(v.has_transaction_, false);
            failed_ = std::exchange(v.failed_, false);
            ex_state_ = std::exchange(v.ex_state_, {});
            return *this;
        }

        explicit Handle(Mysqlpool* db, Connection* conn)
            : parent_{db}, connection_{conn}, uuid_{conn->uuid()}
        {
            connection_->take();
        }

        // Return the mysql connection
        connection_t& connection() {
            assert(connection_);
            return connection_->connection();
        }

        bool empty() const noexcept {
            return connection_ == nullptr;
        }

        void reset() {
            parent_ = {};
            connection_ = {};
            has_transaction_ = false;
            failed_ = false;
        }

        bool isClosed() const noexcept {
            return connection_->state() == Connection::State::CLOSED;
        }

        auto uuid() const noexcept {
            return uuid_;
        }

        auto& connectionWrapper() noexcept {
            return connection_;
        }

        bool hasConnection() const noexcept {
            return connection_ != nullptr;
        }

        const auto& connectionWrapper() const noexcept {
            return connection_;
        }

        /*! Starts a transaction
         *
         *  @param readOnly If true, the transaction is read-only
         *  @param reconnect If true, the connection will be reconnected
         *      if a non-recoverable error occurs during the TRANSACTION query.
         *
         *  @return A transaction object that you will use to commit or rollback the transaction.
         */
        [[nodiscard]] boost::asio::awaitable<Transaction> transaction(bool readOnly = false, bool reconnect = true) {
            assert(connection_);
            assert(!has_transaction_);
            Options opts;
            opts.reconnect_and_retry_query = reconnect;
            co_await exec_(readOnly ? "START TRANSACTION READ ONLY" : "START TRANSACTION", {});
            has_transaction_ = true;
            co_return Transaction{*this, readOnly};
        }

    private:
        friend class Transaction;
        boost::asio::awaitable<void> commit() {
            assert(connection_);
            assert(has_transaction_);
            if (connection_ && has_transaction_) {
                co_await exec_("COMMIT", {false});
            }
            has_transaction_ = false;
            co_return;
        }

        boost::asio::awaitable<void> rollback() {
            assert(connection_);
            assert(has_transaction_);
            if (connection_ && has_transaction_) {
                co_await exec_("ROLLBACK", {false});
            }
            has_transaction_ = false;
            co_return;
        }


        boost::asio::awaitable<bool> handleTimezone(const Options& opts) {
            if (!opts.time_zone.empty()
                && !connectionWrapper()->isSameTimeZone(opts.time_zone)) {
                boost::mysql::diagnostics diag;
                results res;

                static const std::string_view ts_query = "SET time_zone=?";
                auto [sec, stmt] = co_await connectionWrapper()->getStmt(diag, ts_query);
                if (!handleError(sec, diag, errorMode(opts))) {
                    co_await reconnect();
                    co_return false;
                }
                assert(stmt != nullptr);
                logQuery("locale", ts_query, opts.time_zone);
                auto [ec] = co_await connection().async_execute(stmt->bind(opts.time_zone), res, diag, tuple_awaitable);
                if (!handleError(ec, diag, errorMode(opts))) {
                    co_await reconnect();
                    co_return false;
                }
                connectionWrapper()->setTimeZone(opts.time_zone);
            }
            co_return true;
        }

    public:
        /*! Batch based execution */
        template<typename ...argsT>
        [[nodiscard]] boost::asio::awaitable<void>
        start_exec(std::string_view query, const Options& opts, argsT ...args) {
            boost::mysql::diagnostics diag;
            boost::mysql::execution_state ex_state;

            try {
        again:
                if (!co_await handleTimezone(opts)) {
                    goto again;
                }

                if constexpr (sizeof...(argsT) == 0) {
                    logQuery("static-start", query);
                    auto [ec] = co_await connection().async_start_execution(query, ex_state, diag, tuple_awaitable);
                    if (!handleError(ec, diag, errorMode(opts))) {
                        co_await reconnect();
                        goto again;
                    }
                } else {
                    auto [sec, stmt] = co_await connectionWrapper()->getStmt(diag, query);
                    if (!handleError(sec, diag, errorMode(opts))) {
                        co_await reconnect();
                        goto again;
                    }
                    assert(stmt != nullptr);
                    assert(stmt->valid());

                    boost::system::error_code ec; // error status for query execution
                     if constexpr (sizeof...(argsT) == 1 && RangeOfFieldViewContainer<std::remove_cvref_t<decltype(getFirstArgumentAsConstRef(args...))>>) {
                        // Handle dynamic arguments as a range of field_view
                        logQuery("stmt-dynarg-start", query, args...);

                        auto arg = getFirstArgumentAsConstRef(args...);
                        auto [err] = co_await connection().async_start_execution(stmt->bind(arg.begin(), arg.end()), ex_state, diag, tuple_awaitable);
                        ec = err;
                    } else {
                        logQuery("stmt-start", query, args...);
                        auto [err] = co_await connection().async_start_execution(stmt->bind(args...), ex_state, diag, tuple_awaitable);
                        ec = err;
                    }

                    // Close the statement before we evaluate the query. The error handling for the
                    // query may throw an exception, and we need to close the statement before that.
                    if (opts.close_prepared_statement) {
                        // Close the statement (if any error occurs, we will just log it and continue
                        logQuery("close-stmt", query);
                        const auto [csec] = co_await connection().async_close_statement(*stmt, diag, tuple_awaitable);
                        if (sec) {
                            handleError(sec, diag, ErrorMode::EM_IGNORE);
                        }
                        connectionWrapper()->stmtCache().erase(query);
                    }

                    // Handle the query error if any
                    if (!handleError(ec, diag, errorMode(opts))) {
                        co_await reconnect();
                        goto again;
                    }
                }

                ex_state_.emplace(std::move(ex_state));
                co_return;

            } catch (const std::runtime_error& ex) {
                failed_ = true;
                throw;
            }
        }

        [[nodiscard]] boost::asio::awaitable<boost::mysql::rows_view>
        readSome() {
            assert(ex_state_);
            boost::mysql::diagnostics diag;
            results res;
            auto [ec, rows] = co_await connection().async_read_some_rows(*ex_state_, diag, tuple_awaitable);
            if (!handleError(ec, diag, ErrorMode::EM_ALWAYS_FAIL)) {
                throw server_err{ec};
            };
            co_return rows;
        }

        bool shouldReadMore() const noexcept {
            assert(ex_state_);
            if (!ex_state_) {
                return false;
            }
            return ex_state_->should_read_rows();
        }

        // If unset, we are not in batch mode
        auto getExecutionState() const noexcept {
            return ex_state_;
        }

        template<typename ...argsT>
        [[nodiscard]] boost::asio::awaitable<results>
        exec_(std::string_view query, const Options& opts, argsT&& ...args) {
            results res;
            boost::mysql::diagnostics diag;
            auto error_mode = errorMode(opts);
            try {
again:
                if (!co_await handleTimezone(opts)) {
                    goto again;
                }

                if constexpr (sizeof...(argsT) == 0) {
                    logQuery("static", query);
                    auto [ec] = co_await connection().async_execute(query, res, diag, tuple_awaitable);
                    if (!handleError(ec, diag, errorMode(opts))) {
                        co_await reconnect();
                        goto again;
                    }
                } else {
                    auto [sec, stmt] = co_await connectionWrapper()->getStmt(diag, query);
                    if (!handleError(sec, diag, errorMode(opts))) {
                        co_await reconnect();
                        goto again;
                    }
                    assert(stmt != nullptr);
                    assert(stmt->valid());

                    boost::system::error_code ec; // error status for query execution
                    if constexpr (sizeof...(argsT) == 1
                                  && IsGenerator<std::remove_cvref_t<decltype(getFirstArgumentAsConstRef(args...))>>) {
                        error_mode = ErrorMode::EM_ALWAYS_FAIL; // Assume batch insert. We can't safely retry once we have started to add data.
                        logQuery("stmt-batch-coro-start", query);
                        auto count = 0u;
                        auto& gen = getFirstArgumentAsRef(args...);
                        for (auto&& tpl : gen) {
                            auto [err] = co_await connection().async_execute(
                                stmt->bind(std::apply(
                                    [&](auto&&... elems){
                                        return std::tuple<decltype(elems)...>(
                                            std::forward<decltype(elems)>(elems)...
                                            );
                                    },
                                    tpl
                                    )),
                                res, diag, tuple_awaitable
                                );
                            if (err) {
                                ec = err;
                                MYSQLPOOL_LOG_DEBUG_("Batch insert failed for #" << count
                                                                                 << " with ec=" << ec.message());
                                break;
                            }
                            ++count;
                        }
                    } else if constexpr (sizeof...(argsT) == 1
                                         && RangeOfFieldViewContainer<std::remove_cvref_t<decltype(getFirstArgumentAsConstRef(args...))>>) {
                        error_mode = ErrorMode::EM_ALWAYS_FAIL; // Assume batch insert. We can't safely retry once we have started to add data.
                        auto&& batch = getFirstArgumentAsConstRef(args...); // Perfect-forward to avoid copies

                        logQuery("stmt-batch-insert-start", query);
                        for (auto&& row : batch) {
                            // 'row' is a FieldViewContainer (e.g., vector<field_view>)
                            //logQuery("stmt-batch-dynarg-start", query, row);
                            auto [err] = co_await connection().async_execute(stmt->bind(row.begin(), row.end()), res, diag, tuple_awaitable);
                            if (err) {
                                ec = err;
                                MYSQLPOOL_LOG_DEBUG_("Batch insert failed with ec=" << ec.message());
                                break;
                            }
                        }
                    } else if constexpr (sizeof...(args) == 1 && FieldViewContainer<std::remove_cvref_t<decltype(getFirstArgumentAsConstRef(args...))>>) {
                        // Handle dynamic arguments as a range of field_view
                        logQuery("stmt-dynarg", query, args...);

                        auto arg = getFirstArgumentAsConstRef(args...);
                        auto [err] = co_await connection().async_execute(stmt->bind(arg.begin(), arg.end()), res, diag, tuple_awaitable);
                        ec = err;
                    } else {
                        logQuery("stmt", query, args...);
                        auto [err] = co_await connection().async_execute(stmt->bind(args...), res, diag, tuple_awaitable);
                        ec = err;
                    }

                    // Close the statement before we evaluate the query. The error handling for the
                    // query may throw an exception, and we need to close the statement before that.
                    if (opts.close_prepared_statement) {
                        // Close the statement (if any error occurs, we will just log it and continue
                        logQuery("close-stmt", query);
                        const auto [csec] = co_await connection().async_close_statement(*stmt, diag, tuple_awaitable);
                        if (csec) {
                            handleError(csec, diag, ErrorMode::EM_IGNORE);
                        }
                        connectionWrapper()->stmtCache().erase(query);
                    }

                    // Handle the query error if any
                    if (!handleError(ec, diag, error_mode)) {
                        co_await reconnect();
                        goto again;
                    }
                }

                co_return std::move(res);
            } catch (const std::runtime_error& ex) {
                failed_ = true;
                throw;
            }
        }

        template<typename... Args>
        requires (!AnyTupleArg<Args...>)
        [[nodiscard]] boost::asio::awaitable<results>
        exec(std::string_view query, Args&&... args)
        {
            if constexpr (sizeof...(Args) > 0) {
                if constexpr (IsOptions<decltype(getFirstArgumentAsConstRef(args...))>) {
                    // user explicitly passed an Options as first argument
                    co_return co_await exec_(
                        query,
                        std::forward<Args>(args)...             // perfectly forwards opts + rest
                        );
                } else {
                    // user did not pass an Options as first argument
                    co_return co_await exec_(
                        query,
                        Options{},                            // default opts
                        std::forward<Args>(args)...             // rest of the args
                        );
                }
            } else {
                co_return co_await exec_(query, Options{}); // no args, just query
            }
        }

        template<tuple_like T>
        [[nodiscard]]
        auto exec(std::string_view query, T&& tuple)
            -> boost::asio::awaitable<results>
        {
            return std::apply(
                [&](auto&&... args) {
                    return exec_(
                        query,
                        Options{},
                        std::forward<decltype(args)>(args)...
                        );
                },
                std::forward<T>(tuple)
                );
        }


        template<tuple_like T>
        [[nodiscard]]
        boost::asio::awaitable<results>
        exec(std::string_view    query,
             Options      const& opts,
             T&&                tuple)
        {
            results res;
            co_await std::apply(
                [&](auto&&... args) -> boost::asio::awaitable<void> {
                    res = co_await exec_(
                        query,
                        opts,
                        std::forward<decltype(args)>(args)...
                        );
                },
                std::forward<T>(tuple)
                );

            co_return res;
        }

        boost::asio::awaitable<void> reconnect();

        void release() {
            assert(connection_);
            assert(!connection_->isAvailable());
            if (failed_ || (ex_state_ && !ex_state_->complete())) {
                connection_->close();
            } else {
                connection_->touch();
            }
            connection_->release();
        }

        bool failed() const noexcept {
            return failed_;
        }

    private:
        ErrorMode errorMode(const Options& opts) const noexcept {
            if (opts.reconnect_and_retry_query) {
                if (has_transaction_) {
                    MYSQLPOOL_LOG_TRACE_("Handle::errorMode: Ignoring 'reconnect_and_retry_query' option as a transaction is active");
                } else {
                    return ErrorMode::EM_RETRY;
                }
            }
            return ErrorMode::EM_ALWAYS_FAIL;
        }

        void commitAndReleaseLater() noexcept {
            if (!empty()) {
                assert(has_transaction_);
                auto *parent = parent_; // parent_ will be reset by std::move()
                parent->doAndRelease(std::move(*this), [](Handle& handle) -> boost::asio::awaitable<void> {
                    co_await handle.commit();
                });
            }
        }

        void rollbackAndReleaseLater() noexcept {
            if (!empty()) {
                assert(has_transaction_);
                auto *parent = parent_; // parent_ will be reset by std::move()
                parent->doAndRelease(std::move(*this), [](Handle& handle) -> boost::asio::awaitable<void> {
                    co_await handle.rollback();
                });
            }
        }

        Mysqlpool *parent_{};
        Connection *connection_{};
        boost::uuids::uuid uuid_;
        std::optional<boost::mysql::execution_state> ex_state_;
        bool has_transaction_ = false;
        bool failed_ = false;
    };

    void doAndRelease(Handle && handle, auto fn) noexcept {
        try {
            boost::asio::co_spawn(ctx_, [h=std::move(handle), fn=std::move(fn)]() mutable -> boost::asio::awaitable<void> {
                try {
                    co_await fn(h);
                } catch (const std::exception& ex) {
                    MYSQLPOOL_LOG_ERROR_("Request failed with exception: " << ex.what());
                }
                // When h goes out of scope, it will be returned to the connection pool
            }, boost::asio::detached);
        } catch (const std::exception& ex) {
            MYSQLPOOL_LOG_ERROR_("doAndRelease: Failed to start coroutine: " << ex.what());
        } catch (...) {
            MYSQLPOOL_LOG_ERROR_("doAndRelease: Failed to start coroutine");
        }
    }

    [[nodiscard]] boost::asio::awaitable<Handle> getConnection(const Options& opts = {});


    // template<typename ...argsT>
    // [[nodiscard]] boost::asio::awaitable<results>
    // exec(std::string_view query, const Options& opts, argsT&& ...args) {
    //     auto conn = co_await getConnection(opts);
    //     co_return co_await conn.exec_(query, opts, std::forward<argsT>(args)...);
    // }

    // template<typename ...argsT>
    // [[nodiscard]] boost::asio::awaitable<results>
    // exec(std::string_view query, argsT&& ...args) {
    //     auto conn = co_await getConnection({});
    //     co_return co_await conn.exec_(query, Options{}, std::forward<argsT>(args)...);
    // }

    template<typename... Args>
    [[nodiscard]] boost::asio::awaitable<results>
    exec(std::string_view query, Args&&... args)
    {
        if constexpr (sizeof...(Args) > 0) {
            if constexpr (IsOptions<decltype(getFirstArgumentAsConstRef(args...))>) {
                // user explicitly passed an Options as first argument
                auto&& conn = co_await getConnection(getFirstArgumentAsConstRef(args...));
                co_return co_await conn.exec_(
                    query,
                    std::forward<Args>(args)...             // perfectly forwards opts + rest
                    );
            } else {
                // user did not pass an Options as first argument
                auto&& conn = co_await getConnection(); // get connection with default opts
                co_return co_await conn.exec_(
                    query,
                    Options{},                            // default opts
                    std::forward<Args>(args)...             // rest of the args
                    );
            }
        } else {
            auto&& conn = co_await getConnection(); // get connection with default opts
            co_return co_await conn.exec_(query, Options{}); // no args, just query
        }
    }

    /*! Initialize the pool
     *
     * Init will initialzie the internal structures and try to
     * open `DbConfig.min_connections` connections to the database.
     */
    boost::asio::awaitable<void> init();

    /*! Gracefully closes all the database connections
     *
     *  A connection will not be closed if the client is currently
     *  exeuting a query or having an instance of a `Handle` that
     *  holds a connection object.
     *
     *  Close will wait up to `DbConfig.timeout_close_all_databases_seconds`
     *  to gracefully close all connections. After that it returns wether
     *  the connections are closed or not.
     *
     *  New statements can not be executed, and `getConnection()` will fail
     *  after `close()` is called.
     */
    boost::asio::awaitable<void> close();

    boost::mysql::ssl_mode sslMode() const;
private:

    /*! Internal method called by a `Connection` after it is closed.
     *
     *  Used to allow the pool to maintain state of the connections
     *  and pending queries.
     */
    void closed(Connection& conn);

    template <typename epT, typename connT = connection_t>
    boost::asio::awaitable<void> connect(connT& conn, epT& endpoints, unsigned iteration, bool retry) {

        const auto user = dbUser();
        const auto pwd = dbPasswd();
        boost::mysql::handshake_params params{
            user,
            pwd,
            config_.database
        };

        params.set_ssl(sslMode());

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
                MYSQLPOOL_LOG_DEBUG_("Connected to " << ep.endpoint()
                                                     << " as user " << dbUser()
                                                     << " with database " << config_.database
                                                     << (conn.uses_ssl() ? " using TLS" : " without TLS"));
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
    static bool handleError(const boost::system::error_code& ec,
                            boost::mysql::diagnostics& diag,
                            ErrorMode mode = ErrorMode::EM_ALWAYS_FAIL);


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

const std::string& toString(::jgaa::mysqlpool::Mysqlpool::Connection::State state);

} // namespace

// Does not work with the nightnmare named Boost.Log
//::std::ostream& operator << (::std::ostream& out, const ::jgaa::mysqlpool::Mysqlpool::Connection::State state);



