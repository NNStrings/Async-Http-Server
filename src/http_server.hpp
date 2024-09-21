#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <string>
#include <map>
#include <algorithm>
#include <cassert>
#include <memory>

#include "exception.hpp"
#include "address_resolver.hpp"
#include "bytes_buffer.hpp"
#include "async_file.hpp"

using StringMap = std::map<std::string, std::string>;

struct http11_header_parser {
    std::string m_header;
    std::string m_header_line;
    StringMap m_header_keys;
    std::string m_body;
    bool m_header_finished = false;

    void reset_state() {
        m_header.clear();
        m_header_line.clear();
        m_header_keys.clear();
        m_body.clear();
        m_header_finished = 0;
    }

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

template <typename HeaderParser = http11_header_parser>
struct _http_base_parser {
    HeaderParser m_header_parser;
    bool m_body_finished = false;
    size_t body_accumulated_size = 0;
    size_t content_length = 0;

    void reset_state() {
        m_header_parser.reset_state();
        m_body_finished = false;
        body_accumulated_size = 0;
        content_length = 0;
    }

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

template <typename HeaderParser = http11_header_parser>
struct http_request_parser : _http_base_parser<HeaderParser> {
    std::string method() {
        return this->_handline_first();  
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

    void _begin_header(std::string_view first, std::string_view second,
                       std::string_view third) {
        m_header_writer.begin_header(first, second, third);
    }

    void reset_state() {
        m_header_writer.reset_state();        
    }

    bytes_buffer &buffer() {
        return m_header_writer.buffer();
    }

    void writer_header(std::string_view key, std::string_view value) {
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
template <typename HeaderWriter = http11_header_writer>
struct http_request_writer : _http_base_writer<HeaderWriter> {
    void begin_header(int status) {
        this->_begin_header("HTTP/1.1", std::to_string(status), "OK");
    }
};

// "HTTP1.1 200 OK"     response
template <typename HeaderWriter = http11_header_writer>
struct http_response_writer : _http_base_writer<HeaderWriter> {
    void begin_header(int status) {
        this->_begin_header("HTTP/1.1", std::to_string(status), "OK");
    }
};

struct http_connection_handler : std::enable_shared_from_this<http_connection_handler> {
    async_file m_conn;
    bytes_buffer m_readbuf{1024};
    http_request_parser<> m_req_parser;
    http_response_writer<> m_res_writer;

    using pointer = std::shared_ptr<http_connection_handler>;

    static pointer make() {
        return std::make_shared<pointer::element_type>();
    }

    void do_start(int connfd) {
        m_conn = async_file::async_wrap(connfd);
        do_read();
    }

    void do_read() {
        return m_conn.async_read(m_readbuf, [self = this->shared_from_this()] (exception<size_t> ret) {
            if (ret.error()) {
                return;
            }

            size_t n = ret.value();
            if (n == 0) {
                return;
            }

            self->m_req_parser.push_chunk(self->m_readbuf.subspan(0, n));
            if (!self->m_req_parser.request_finished()) {
                return self->do_read();
            }
            else {
                return self->do_handle();
            }
        });
    }

    void do_handle() {
        std::string body = std::move(m_req_parser.body());
        m_req_parser.reset_state();

        if (body.empty()) {
            body = "你好，你的请求正文为空哦";
        }
        else {
            body = std::format("你好，你的请求是: [{}]，共 {} 字节", body, body.size());
        }

        m_res_writer.begin_header(200);
        m_res_writer.writer_header("Server", "co_http");
        m_res_writer.writer_header("Content-type", "text/html;charset=utf-8");
        m_res_writer.writer_header("Connection", "keep-alive");
        m_res_writer.writer_header("Content-length", std::to_string(body.size()));
        m_res_writer.end_header();

        // std::println("我的响应头: {}", buffer);
        // std::println("我的响应正文: {}", body);
        // std::println("正在响应");

        m_res_writer.write_body(body);
        return do_write(m_res_writer.buffer());
    }

    void do_write(bytes_const_view buffer) {
        return m_conn.async_write(buffer, [self = shared_from_this(), buffer] (exception<size_t> ret) {
            if (ret.error()) {
                return;
            }
            auto n = ret.value();

            if (buffer.size() == n) {
                self->m_res_writer.reset_state();
                return self->do_read();
            }
            return self->do_write(buffer.subspan(n));
        });
    }
};

struct http_acceptor : std::enable_shared_from_this<http_acceptor> {
    async_file m_listen;
    address_resolver::address m_addr;

    using pointer = std::shared_ptr<http_acceptor>;

    static pointer make() {
        return std::make_shared<pointer::element_type>();
    }

    void do_start(std::string name, std::string port) {
        address_resolver resolver;
        std::println("正在监听：{}:{}", name, port);
        auto entry = resolver.resolve(name, port);
        int listenfd = entry.create_socket_and_bind();

        m_listen = async_file::async_wrap(listenfd);
        return do_accept();
    }

    void do_accept() {
        return m_listen.async_accept(m_addr, [self = shared_from_this()] (exception<int> ret) {
            auto connfd = ret.except("accept");

            http_connection_handler::make()->do_start(connfd);
            return self->do_accept();
        });
    }
};

#endif
