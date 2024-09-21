#ifndef IO_CONTEXT_HPP
#define IO_CONTEXT_HPP

#include <sys/epoll.h>

#include "callback.hpp"
#include "exception.hpp"

struct io_context {
    int m_epfd;

    inline static thread_local io_context *g_instence = nullptr;

    io_context() : m_epfd(CHECK_CALL(epoll_create1, 0)) {
        g_instence = this;
    }

    void join() {
        std::array<struct epoll_event, 128> events;
        while (true) {
            int ret = epoll_wait(m_epfd, events.data(), events.size(), -1);
            if (ret < 0) {
                throw;   
            }
            for (size_t i = 0; i < ret; i++) {
                auto cb = callback<>::from_address(events[i].data.ptr);
                cb();
            }
        }
    }

    ~io_context() {
        close(m_epfd);
        g_instence = nullptr;
    }

    static io_context &get() {
        assert(g_instence);
        return *g_instence;
    }
};

#endif
