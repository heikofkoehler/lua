#include "value/socket.hpp"
#include <iostream>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#endif

SocketObject::~SocketObject() {
    close();
}

bool SocketObject::bind(const std::string& address, int port) {
    if (!isValid()) return false;

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr);

    if (::bind(fd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

bool SocketObject::listen(int backlog) {
    if (!isValid()) return false;
    if (::listen(fd_, backlog) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

SocketObject* SocketObject::accept() {
    if (!isValid()) return nullptr;

    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    socket_t clientFd = ::accept(fd_, (struct sockaddr*)&clientAddr, &clientLen);

    if (clientFd == INVALID_SOCKET) {
        return nullptr;
    }

    return new SocketObject(clientFd);
}

int SocketObject::send(const std::string& data) {
    if (!isValid()) return -1;
    int sent = ::send(fd_, data.c_str(), data.length(), 0);
    if (sent < 0) {
        std::cerr << "send failed: " << errno << " (fd: " << fd_ << ")" << std::endl;
    }
    return sent;
}

std::string SocketObject::receive(int bufferSize) {
    if (!isValid()) return "";

    char* buffer = new char[bufferSize];
    int bytesReceived = ::recv(fd_, buffer, bufferSize, 0);

    if (bytesReceived <= 0) {
        if (bytesReceived < 0) {
            std::cerr << "recv failed: " << errno << " (fd: " << fd_ << ")" << std::endl;
        }
        delete[] buffer;
        return "";
    }

    std::string data(buffer, bytesReceived);
    delete[] buffer;
    return data;
}

void SocketObject::close() {
    if (isValid()) {
#ifdef _WIN32
        ::closesocket(fd_);
#else
        ::close(fd_);
#endif
        fd_ = INVALID_SOCKET;
    }
}
