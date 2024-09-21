#ifndef ASYNC_FILE_HPP
#define ASYNC_FILE_HPP

#include <fcntl.h>
#include <sys/epoll.h>

#include "exception.hpp"
#include "bytes_buffer.hpp"
#include "callback.hpp"
#include "io_context.hpp"
#include "address_resolver.hpp"

struct async_file {
    int m_fd = -1;

    async_file() = default;
    explicit async_file(int fd) : m_fd(fd) {}

    static async_file async_wrap(int fd) {
        int flags = CHECK_CALL(fcntl, fd, F_GETFL);
        flags |= O_NONBLOCK;
        CHECK_CALL(fcntl, fd, F_SETFL, flags);

        struct epoll_event event;
        event.events = EPOLLET;
        event.data.ptr = nullptr;
        CHECK_CALL(epoll_ctl, io_context::get().m_epfd, EPOLL_CTL_ADD, fd, &event);

        return async_file{fd};
    }

    void async_read(bytes_view buf, callback<exception<size_t>> cb) {
        auto ret = convert_error<size_t>(read(m_fd, buf.data(), buf.size()));

        if (!ret.is_error(EAGAIN)) {
            cb(ret);
            return;
        }

        callback<> resume = [this, buf, cb = std::move(cb)] () mutable {
            return async_read(buf, std::move(cb));
        };

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        event.data.ptr = resume.leak_address();
        CHECK_CALL(epoll_ctl, io_context::get().m_epfd, EPOLL_CTL_MOD, m_fd, &event);
    }

    void async_write(bytes_const_view buf, callback<exception<size_t>> cb) {
        auto ret = convert_error<size_t>(write(m_fd, buf.data(), buf.size()));

        if (!ret.is_error(EAGAIN)) {
            cb(ret);
            return;
        }

        callback<> resume = [this, buf, cb = std::move(cb)] () mutable {
            return async_write(buf, std::move(cb));
        };

        struct epoll_event event;
        event.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
        event.data.ptr = resume.leak_address();
        CHECK_CALL(epoll_ctl, io_context::get().m_epfd, EPOLL_CTL_MOD, m_fd, &event);
    }

    void async_accept(address_resolver::address &addr, callback<exception<int>> cb) {
        auto ret = convert_error<int>(accept(m_fd, &addr.m_addr, &addr.m_addrlen));

        if (!ret.is_error(EAGAIN)) {
            cb(ret);
            return;
        }

        callback<> resume = [this, &addr, cb = std::move(cb)] () mutable {
            return async_accept(addr, std::move(cb));
        };

        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        event.data.ptr = resume.leak_address();
        CHECK_CALL(epoll_ctl, io_context::get().m_epfd, EPOLL_CTL_MOD, m_fd, &event);
    }

    async_file(async_file &&that) noexcept : m_fd(that.m_fd) {
        that.m_fd = -1;
    }

    async_file &operator=(async_file &&that) noexcept {
        std::swap(m_fd, that.m_fd);
        return *this;
    }

    ~async_file() {
        if (m_fd == -1) {
            return;
        }
        close(m_fd);
        epoll_ctl(io_context::get().m_epfd, EPOLL_CTL_DEL, m_fd, nullptr);
    }
};


#endif
