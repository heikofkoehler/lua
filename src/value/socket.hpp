#ifndef LUA_SOCKET_HPP
#define LUA_SOCKET_HPP

#include "vm/gc.hpp"
#include "common/common.hpp"
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    using socket_t = int;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

class SocketObject : public GCObject {
public:
    SocketObject(socket_t fd) : GCObject(Type::SOCKET), fd_(fd) {}
    ~SocketObject();

    bool bind(const std::string& address, int port);
    bool listen(int backlog);
    SocketObject* accept();
    int send(const std::string& data);
    std::string receive(int bufferSize);
    void close();

    socket_t fd() const { return fd_; }
    bool isValid() const { return fd_ != INVALID_SOCKET; }

    void markReferences() override {}

private:
    socket_t fd_;
};

#endif // LUA_SOCKET_HPP
