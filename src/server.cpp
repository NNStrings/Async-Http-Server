#include "exception.hpp"
#include "address_resolver.hpp"
#include "http_server.hpp"
#include "bytes_buffer.hpp"
#include "callback.hpp"
#include "io_context.hpp"
#include "async_file.hpp"

void server() {
    io_context ctx;
    auto acceptor = http_acceptor::make();
    acceptor->do_start("127.0.0.1", "8080");

    ctx.join();
}

int main()
{
    // setlocale(LC_ALL, "zh_CN.UTF-8");
    try {
        server();
    }
    catch (std::system_error const &e) {
        std::println("错误: {} ({} / {})", e.what(), e.code().category().name(), e.code().value());
    }
    return 0;
}