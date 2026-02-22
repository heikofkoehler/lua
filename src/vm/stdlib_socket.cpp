#include "vm/vm.hpp"
#include "value/socket.hpp"
#include <iostream>

namespace {

bool native_socket_create(VM* vm, int argCount) {
    if (argCount != 0) {
        vm->runtimeError("socket.create expects 0 arguments");
        return false;
    }

#ifdef _WIN32
    static bool wsaInitialized = false;
    if (!wsaInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        wsaInitialized = true;
    }
#endif

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        vm->push(Value::nil());
        return true;
    }

    // Set SO_REUSEADDR
    int opt = 1;
#ifdef _WIN32
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    size_t socketIdx = vm->createSocket(fd);
    vm->push(Value::socket(socketIdx));
    return true;
}

bool native_socket_bind(VM* vm, int argCount) {
    if (argCount != 3) {
        vm->runtimeError("socket.bind expects 3 arguments (socket, address, port)");
        return false;
    }

    Value portVal = vm->pop();
    Value addrVal = vm->pop();
    Value sockVal = vm->pop();

    if (!sockVal.isSocket() || !addrVal.isString() || !portVal.isNumber()) {
        vm->runtimeError("Invalid arguments to socket.bind");
        return false;
    }

    SocketObject* sock = vm->getSocket(sockVal.asSocketIndex());
    if (!sock) {
        vm->push(Value::boolean(false));
        return true;
    }

    // Handle string index (needs to look up in VM's string pool)
    std::string address;
    if (addrVal.isRuntimeString()) {
        address = vm->getString(addrVal.asStringIndex())->chars();
    } else {
        address = vm->rootChunk()->getString(addrVal.asStringIndex())->chars();
    }

    bool success = sock->bind(address, (int)portVal.asNumber());
    vm->push(Value::boolean(success));
    return true;
}

bool native_socket_listen(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("socket.listen expects 2 arguments (socket, backlog)");
        return false;
    }

    Value backlogVal = vm->pop();
    Value sockVal = vm->pop();

    if (!sockVal.isSocket() || !backlogVal.isNumber()) {
        vm->runtimeError("Invalid arguments to socket.listen");
        return false;
    }

    SocketObject* sock = vm->getSocket(sockVal.asSocketIndex());
    if (!sock) {
        vm->push(Value::boolean(false));
        return true;
    }

    bool success = sock->listen((int)backlogVal.asNumber());
    vm->push(Value::boolean(success));
    return true;
}

bool native_socket_accept(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("socket.accept expects 1 argument (socket)");
        return false;
    }

    Value sockVal = vm->pop();
    if (!sockVal.isSocket()) {
        vm->runtimeError("Invalid argument to socket.accept");
        return false;
    }

    SocketObject* sock = vm->getSocket(sockVal.asSocketIndex());
    if (!sock) {
        vm->push(Value::nil());
        return true;
    }

    SocketObject* client = sock->accept();
    if (!client) {
        vm->push(Value::nil());
        return true;
    }

    size_t clientIdx = vm->registerSocket(client);
    vm->push(Value::socket(clientIdx));
    return true;
}

bool native_socket_send(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("socket.send expects 2 arguments (socket, data)");
        return false;
    }

    Value dataVal = vm->pop();
    Value sockVal = vm->pop();

    if (!sockVal.isSocket() || !dataVal.isString()) {
        vm->runtimeError("Invalid arguments to socket.send");
        return false;
    }

    SocketObject* sock = vm->getSocket(sockVal.asSocketIndex());
    if (!sock) {
        vm->push(Value::number(-1));
        return true;
    }

    std::string data;
    if (dataVal.isRuntimeString()) {
        data = vm->getString(dataVal.asStringIndex())->chars();
    } else {
        data = vm->rootChunk()->getString(dataVal.asStringIndex())->chars();
    }

    int sent = sock->send(data);
    vm->push(Value::number(sent));
    return true;
}

bool native_socket_receive(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("socket.receive expects 2 arguments (socket, size)");
        return false;
    }

    Value sizeVal = vm->pop();
    Value sockVal = vm->pop();

    if (!sockVal.isSocket() || !sizeVal.isNumber()) {
        vm->runtimeError("Invalid arguments to socket.receive");
        return false;
    }

    SocketObject* sock = vm->getSocket(sockVal.asSocketIndex());
    if (!sock) {
        vm->push(Value::nil());
        return true;
    }

    std::string data = sock->receive((int)sizeVal.asNumber());
    if (data.empty()) {
        vm->push(Value::nil());
    } else {
        size_t strIdx = vm->internString(data);
        vm->push(Value::runtimeString(strIdx));
    }
    return true;
}

bool native_socket_close(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("socket.close expects 1 argument (socket)");
        return false;
    }

    Value sockVal = vm->pop();
    if (!sockVal.isSocket()) {
        vm->runtimeError("Invalid argument to socket.close");
        return false;
    }

    vm->closeSocket(sockVal.asSocketIndex());
    vm->push(Value::nil());
    return true;
}

} // anonymous namespace

void registerSocketLibrary(VM* vm, TableObject* socketTable) {
    vm->addNativeToTable(socketTable, "create", native_socket_create);
    vm->addNativeToTable(socketTable, "bind", native_socket_bind);
    vm->addNativeToTable(socketTable, "listen", native_socket_listen);
    vm->addNativeToTable(socketTable, "accept", native_socket_accept);
    vm->addNativeToTable(socketTable, "send", native_socket_send);
    vm->addNativeToTable(socketTable, "receive", native_socket_receive);
    vm->addNativeToTable(socketTable, "close", native_socket_close);
}
