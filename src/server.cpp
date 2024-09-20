#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <system_error>
#include <netdb.h>
#include <unistd.h>
#include <print>
#include <cstring>
#include <thread>
#include <map>
#include <vector>
#include <cassert>
#include <charconv>
#include <string>

#include "eref.hpp"
#include "bytes_buffer.hpp"
#include "exception.hpp"

struct address_resovle_fatptr {
    struct sockaddr *m_addr;
    socklen_t m_addrlen;
};

struct socket_address_storage {
    union {
        struct sockaddr m_addr;
        struct sockaddr_storage m_addr_storage;
    };
    socklen_t m_addrlen = sizeof(sockaddr_storage);
    operator address_resovle_fatptr() {
        return {&m_addr, m_addrlen};
    } 
};

struct address_resolve_entry {
    struct addrinfo *m_curr = nullptr;

    address_resovle_fatptr get_address() const {
        return {m_curr->ai_addr, m_curr->ai_addrlen};
    }

    int create_socket() const {
        int sockfd = CHECK_CALL(socket, m_curr->ai_family, m_curr->ai_socktype,
                                        m_curr->ai_protocol);
        return sockfd;
    }

    int create_socket_and_bind() const {
        int sockfd = create_socket();
        auto serve_addr = get_address();
        CHECK_CALL(bind, sockfd, serve_addr.m_addr, serve_addr.m_addrlen);
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

struct address_resolve {
    struct addrinfo *m_head = nullptr;

    address_resolve_entry resolve(std::string const &name, std::string const &service) {
        int err = getaddrinfo(name.c_str(), service.c_str(), NULL, &m_head);
        if (err) {
            auto ec = std::error_code(err, gai_category());
            throw std::system_error(ec, name + ": " + service);
        }
        return {m_head};
    }

    address_resolve_entry get_first_entry() {
        return {m_head};
    }

    address_resolve() = default;

    address_resolve(address_resolve &&that) : m_head(that.m_head) {
        that.m_head = nullptr;
    }

    ~address_resolve() {
        if (m_head) {
            freeaddrinfo(m_head);
        }
    }
};

using StringMap = std::map<std::string, std::string>;

struct http11_request_parser {
    std::string m_header;
    std::string m_header_line;
    StringMap m_header_keys;
    std::string m_body;
    bool m_header_finished = false;

    [[nodiscard]] bool header_finished() const {
        return m_header_finished;
    }

    void _extract_header() {
        size_t pos = m_header.find("\r\n");
        while (pos != std::string::npos) {
            pos += 2;
            size_t next_pos = m_header.find("\r\n", pos);
            size_t line_len = std::string::npos;
            if (next_pos != std::string::npos) {
                line_len = next_pos - pos;
            }
            std::string_view line = std::string_view(m_header).substr(pos, line_len);
            size_t colon = line.find(": ");
            if (colon != std::string::npos) {
                std::string key = std::string(line.substr(0, colon));
                std::string_view value = line.substr(colon + 2);
                std::transform(key.begin(), key.end(), key.begin(), [] (char c) {
                    if (c >= 'A' && c <= 'Z') {
                        c += 'a' - 'A';
                    }
                    return c;
                });
                m_header_keys.insert_or_assign(std::move(key), value);
            }
            pos = next_pos;
        }
    }

    void push_chunk(std::string_view chunk) {
        assert(!m_header_finished);
        m_header.append(chunk);
        size_t header_len = m_header.find("\r\n\r\n");
        if (header_len != std::string::npos) {
            m_header_finished = true;
            m_body = m_header.substr(header_len + 4);
            m_header.resize(header_len);
            _extract_header();
        }
    }

    std::string& headline() {
        return m_header_line;
    }

    StringMap &headers() {
        return m_header_keys;
    }

    std::string &headers_raw() {
        return m_header;
    }

    std::string &extra_body() {
        return m_body;
    }
};

template <typename HeaderParser = http11_request_parser>
struct _http_base_parser {
    HeaderParser m_header_parser;
    bool m_body_finished = false;
    size_t body_accumulated_size = 0;
    size_t content_length = 0;

    [[nodiscard]] bool header_finished() const {
        return m_header_parser.header_finished();
    }    

    [[nodiscard]] bool request_finished() const {
        return m_body_finished;
    }

    std::string &headers_raw() {
        return m_header_parser.headers_raw();
    }

    std::string headline() {
        return m_header_parser.headline();
    }

    StringMap &headers() {
        return m_header_parser.headers();
    }

    // "GET / HTTP1.1"      request
    // "HTTP1.1 200 OK"     response
    std::string _handline_first() {
        auto &line = m_header_parser.headline();
        size_t space = line.find(" ");
        if (space == std::string::npos) {
            return "";
        }
        return line.substr(0, space);   
    }

    std::string _handline_second() {
        auto &line = m_header_parser.headline();
        size_t space1 = line.find(" ");
        if (space1 == std::string::npos) {
            return "";
        }
        size_t space2 = line.find(" ", space1);
        if (space2 == std::string::npos) {
            return "";
        }
        return line.substr(space1 + 1, space2 - space1 - 1);
    }

    std::string _handline_third() {
        auto &line = m_header_parser.headline();
        size_t space1 = line.find(" ");
        if (space1 == std::string::npos) {
            return "";
        }
        size_t space2 = line.find(" ", space1);
        if (space2 == std::string::npos) {
            return "";
        }
        return line.substr(space2);
    }

    std::string body() {
        return m_header_parser.extra_body();
    }

    size_t _extract_content_length() {
        auto &headers = m_header_parser.headers();
        auto it = headers.find("content-length");
        if (it == headers.end()) {
            return 0;
        }
        try {
            return std::stoi(it->second);
        }
        catch (std::logic_error const &) {
            return 0;
        }
    }

    void push_chunk(std::string_view chunk) {
        assert(!m_body_finished);
        if (!m_header_parser.header_finished()) {
            m_header_parser.push_chunk(chunk);
            if (m_header_parser.header_finished()) {
                body_accumulated_size = body().size();
                content_length = _extract_content_length();
                if (body_accumulated_size >= content_length) {
                    m_body_finished = true;
                }
            }
        }
        else {
            body().append(chunk);
            body_accumulated_size += chunk.size();
            if (body_accumulated_size >= content_length) {
                m_body_finished = true;
            }
        }
    }

    std::string read_some_body() {
        return std::move(body());
    }
};

enum class http_method {
    UNKWONN = -1,
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH,
    TRACE,
    CONNECT
};

template <typename HeaderParser = http11_request_parser>
struct http_request_parser : _http_base_parser<HeaderParser> {
    http_method method() {
        return eref::enum_from_name<http_method>(this->_handline_first());  
    }

    std::string url() {
        return this->_handline_second();
    }

    std::string version() {
        return this->_handline_second();
    }
};

struct http11_header_writer {
    bytes_buffer m_buffer;

    void reset_state() {
        m_buffer.clear();
    }

    bytes_buffer &buffer() {
        return m_buffer;
    }

    void begin_header(std::string_view first, std::string_view second,
                      std::string_view third) {
        m_buffer.append(first);
        m_buffer.append_literial(" ");
        m_buffer.append(second);
        m_buffer.append_literial(" ");
        m_buffer.append(third);
    }

    void writer_header(std::string_view key, std::string_view value) {
        m_buffer.append_literial("\r\n");
        m_buffer.append(key);
        m_buffer.append_literial(": ");
        m_buffer.append(value);
    }
    
    void end_header() {
        m_buffer.append_literial("\r\n\r\n");
    }
};

template <typename HeaderWriter = http11_header_writer>
struct _http_base_writer {
    HeaderWriter m_header_writer;

    void _begin_writer(std::string_view first, std::string_view second,
                       std::string_view third) {
        m_header_writer.begin_header(first, second, third);
    }

    void reset_state() {
        m_header_writer.reset_state();        
    }

    bytes_buffer &buffer() {
        return m_header_writer.buffer();
    }

    void write_header(std::string_view key, std::string_view value) {
        m_header_writer.writer_header(key, value);
    }

    void end_header() {
        m_header_writer.end_header();
    }

    void write_body(std::string_view body) {
        m_header_writer.buffer().append(body);
    }
};

// "GET / HTTP1.1"      request
template <typename HeaderWriter = http11_request_parser>
struct http_request_writer : _http_base_writer<HeaderWriter> {
    void begin_header(std::string_view method, std::string_view url) {
        this->_begin_writer(method, url, "HTTP1.1");
    }
};

// "HTTP1.1 200 OK"     response
template <typename HeaderWriter = http11_header_writer>
struct http_response_writer : _http_base_writer<HeaderWriter> {
    void begin_header(int status) {
        this->_begin_writer("HTTP1.1", std::to_string(status), "OK");
    }
};

std::vector<std::thread> pool;

void server() {
    address_resolve resolver;
    resolver.resolve("127.0.0.1", "8080");
    // resolver.resolve("-1", "8080");
    std::println("listening: 127.0.0.1");
    auto entry = resolver.get_first_entry();   
    int listenfd = entry.create_socket_and_bind();
    CHECK_CALL(listen, listenfd, SOMAXCONN);
    socket_address_storage addr;
    while (true) {
        socket_address_storage addr;
        int connid = CHECK_CALL(accept, listenfd, &addr.m_addr, &addr.m_addrlen);
        pool.emplace_back([connid] {
            while (true) {
                char buf[1024];
                http_request_parser req_parse;
                do {
                    // TCP 基于流，可能会粘包
                    size_t n = CHECK_CALL(read, connid, buf, sizeof(buf));
                    if (n == 0) {
                        std::println("收到对面关闭了连接: {}", connid);
                        goto quit;
                    }
                    req_parse.push_chunk(std::string_view(buf, n));
                } while (!req_parse.request_finished());

                std::println("收到请求: {}", connid);
                // std::println("请求头: {}", req_parse.headers_raw());
                // std::println("请求正文: {}", req_parse.body());
                std::string body = req_parse.body();

                if (body.empty()) {
                    body = "你好，你的请求正文为空哦";
                }
                else {
                    body = "你好，你的请求正文是: [" + body + "]";
                }

                // std::string res = "HTTP/1.1 200 OK\r\nServer: co_http\r\nConnection: close\r\nContent-length: " + 
                //                    std::to_string(body.size()) + "\r\n\r\n" + body;
                http_response_writer res_writer;
                res_writer.begin_header(200);
                res_writer.write_header("Server", "co_http");
                res_writer.write_header("Content-type", "text/html;charset=utf-8");
                res_writer.write_header("Connection", "keep-alive");
                res_writer.write_header("Content-length", std::to_string(body.size()));
                res_writer.end_header();
                auto &buffer = res_writer.buffer();
                if (CHECK_CALL_EXCEPT(EPIPE, write, connid, buffer.data(), buffer.size()) == -1) {
                    break;
                }
                if (CHECK_CALL_EXCEPT(EPIPE, write, connid, body.data(), body.size()) == -1) {
                    break;
                }

                // std::println("我的响应头: {}", buffer.data());
                // std::println("我的响应正文: {}", body);

                std::println("正在响应: {}", connid);
            }
        quit:
            std::println("连接结束: {}", connid);
            close(connid);
        });
    }
}

int main()
{
    // setlocale(LC_ALL, "zh_CN.UTF-8");
    try {
        server();
    }
    catch (std::system_error const &e) {
        std::println("错误: {}", e.what());
    }
    for (auto &it : pool) {
        it.join();
    }
    return 0;
}