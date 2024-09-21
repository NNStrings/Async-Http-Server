#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP

#include <type_traits>
#include <stdexcept>
#include <system_error>
#include <print>
#include <fmt/format.h>
#include <netdb.h>

template <typename T>
struct exception {
    std::make_signed_t<T> m_res;

    exception() = default;
    exception(std::make_signed_t<T> res) noexcept : m_res(res) {}

    int error() {
        if (m_res < 0) {
            return -m_res;
        }
        return 0;
    }

    bool is_error(int res) const noexcept {
        return m_res == -res;
    }

    std::error_code error_code() const noexcept {
        if (m_res < 0) {
            return std::error_code(-m_res, std::system_category());
        }
        return std::error_code();
    }

    T except(const char *what) {
        if (m_res < 0) {
            auto ec = error_code();
            std::println(stderr, "{}: {}", what, ec.message());
            throw std::system_error(ec, what);
        }
        return m_res;
    }

    T value() const {
        if (m_res < 0) {
            auto ec = error_code();
            std::println(stderr, "{}", ec.message());
            throw std::system_error(ec);
        }
        return m_res;
    }

    T value_unsafe() const {
        assert(m_res >= 0);
        return m_res;
    }
};

template <typename U, typename T>
exception<U> convert_error(T res) {
    if (res == -1) {
        return -errno;
    }
    return res;
}

[[noreturn]] void _throw_system_error(const char *what) {
    auto ec = std::error_code(errno, std::system_category());
    std::println(stderr, "{}: {} ({}.{})", what, ec.message(), ec.category().name(), ec.value());
    throw std::system_error(ec, what);
}

template <int Except = 0, class T>
T check_error(const char *what, T res) {
    if (res == -1) {
        if constexpr (Except != 0) {
            if (errno == Except) {
                return -1;
            }
        }
        _throw_system_error(what);
    }
    return res;
}

#define SOURCE_INFO_IMPL_2(file, line) "In " file ":" #line ": "
#define SOURCE_INFO_IMPL(file, line) SOURCE_INFO_IMPL_2(file, line)
#define SOURCE_INFO(...) SOURCE_INFO_IMPL(__FILE__, __LINE__) __VA_ARGS__
#define CHECK_CALL_EXCEPT(except, func, ...) check_error<except>(SOURCE_INFO() #func, func(__VA_ARGS__))
#define CHECK_CALL(func, ...) check_error(SOURCE_INFO(#func), func(__VA_ARGS__))

std::error_category const &gai_category() {
    static struct final : std::error_category {
        char const *name() const noexcept override {
            return "getaddrinfo";
        }

        std::string message(int err) const override {
            return gai_strerror(err);
        }
    } instance;
    return instance;
}

#endif
