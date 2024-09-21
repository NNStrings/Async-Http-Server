#ifndef ADDRESS_RESOLVER_HPP
#define ADDRESS_RESOLVER_HPP

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "exception.hpp"

struct address_resolver {
    struct address_ref {
        struct sockaddr *m_addr;
        socklen_t m_addrlen;
    };

    struct address {
        union {
            struct sockaddr m_addr;
            struct sockaddr_storage m_addr_storage;
        };
        socklen_t m_addrlen = sizeof(struct sockaddr_storage);

        operator address_ref() {
            return {&m_addr, m_addrlen};
        }
    };

    struct address_info {
        struct addrinfo *m_curr = nullptr;

        address_ref get_address() const {
            return {m_curr->ai_addr, m_curr->ai_addrlen};
        }

        int create_socket() const {
            int sockfd = CHECK_CALL(socket, m_curr->ai_family, m_curr->ai_socktype, m_curr->ai_protocol);
            return sockfd;
        }

        int create_socket_and_bind() const {
            int sockfd = create_socket();
            address_ref serve_addr = get_address();
            int on = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
            CHECK_CALL(bind, sockfd, serve_addr.m_addr, serve_addr.m_addrlen);
            CHECK_CALL(listen, sockfd, SOMAXCONN);
            return sockfd;
        }

        [[nodiscard]] bool next_entry() {
            m_curr = m_curr->ai_next;
            if (m_curr == nullptr) {
                return false;
            }
            return true;
        }
    };

    struct addrinfo *m_head = nullptr;

    address_info resolve(std::string const &name, std::string const &service) {
        int err = getaddrinfo(name.c_str(), service.c_str(), NULL, &m_head);
        if (err != 0) {
            auto ec = std::error_code(err, gai_category());
            throw std::system_error(ec, name + ":" + service);
        }
        return {m_head};
    }

    address_resolver() = default;

    address_resolver(address_resolver &&that) : m_head(that.m_head) {
        that.m_head = nullptr;
    }

    ~address_resolver() {
        if (m_head) {
            freeaddrinfo(m_head);
        }
    }
};


#endif
