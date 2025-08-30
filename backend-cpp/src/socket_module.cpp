#include "socket_module.hpp"
#include "ipc_common.hpp"
#include "ipc_manager.hpp"
#include <iostream>
#include <sstream>
#include <thread>

SocketModule::SocketModule(IPCManager* manager) : manager_(manager) {}

SocketModule::~SocketModule() {
    stop();
}

bool SocketModule::setup_winsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cout << make_error_event("winsock_init", "WSAStartup failed: " + std::to_string(result)) << std::endl;
        return false;
    }
    return true;
}

bool SocketModule::start() {
    if (running_.load()) return true;

    // Setup Winsock
    if (!setup_winsock()) {
        return false;
    }

    // Create server socket
    server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_ == INVALID_SOCKET) {
        std::cout << make_error_event("socket_create", "Failed to create server socket: " + std::to_string(WSAGetLastError())) << std::endl;
        return false;
    }

    // Set socket options
    int enable = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable)) == SOCKET_ERROR) {
        std::cout << make_error_event("socket_option", "Failed to set socket options: " + std::to_string(WSAGetLastError())) << std::endl;
        closesocket(server_socket_);
        return false;
    }

    // Bind socket
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(7070);

    if (bind(server_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cout << make_error_event("socket_bind", "Failed to bind socket: " + std::to_string(WSAGetLastError())) << std::endl;
        closesocket(server_socket_);
        return false;
    }

    // Listen for connections
    if (listen(server_socket_, 1) == SOCKET_ERROR) {
        std::cout << make_error_event("socket_listen", "Failed to listen on socket: " + std::to_string(WSAGetLastError())) << std::endl;
        closesocket(server_socket_);
        return false;
    }

    running_.store(true);

    // Start both server and client threads
    server_thread_ = std::thread(&SocketModule::server_thread, this);
    client_thread_ = std::thread(&SocketModule::client_thread, this);

    std::cout << make_simple_event("ready", "Socket mechanism started on port 7070") << std::endl;
    return true;
}

void SocketModule::server_thread() {
    sockaddr_in caddr{};
    int clen = sizeof(caddr);
    SOCKET s = accept(server_socket_, (sockaddr*)&caddr, &clen);
    if (s == INVALID_SOCKET) {
        if (running_.load()) std::cout << make_error_event("socket_accept", "Accept failed: " + std::to_string(WSAGetLastError())) << std::endl;
        return;
    }

    client_socket_ = s;
    connected_.store(true);
    std::cout << make_simple_event("socket_connected", "Client connected") << std::endl;

    std::string acc;
    char buf[1024];
    while (running_.load()) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        acc.append(buf, buf + n);
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            ++messages_received_;
            json resp = create_base_event("received");
            resp["from"] = "socket_server";
            try {
                auto j = json::parse(line);
                resp["text"] = std::string("ECHO: ") + (j.contains("text") ? j["text"].get<std::string>() : line);
            }
            catch (...) {
                resp["text"] = std::string("ECHO: ") + line;
            }
            std::string out = resp.dump() + "\n";
            ::send(s, out.c_str(), (int)out.size(), 0);
        }
    }
    closesocket(s);
    client_socket_ = INVALID_SOCKET;
    connected_.store(false);
    std::cout << make_simple_event("socket_disconnected", "Client disconnected") << std::endl;
}

void SocketModule::client_thread() {
    // Pequena pausa para garantir que o servidor esteja pronto
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET) {
        std::cout << make_error_event("socket_create", "client invalid: " + std::to_string(WSAGetLastError())) << std::endl;
        return;
    }

    sockaddr_in s{};
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    s.sin_port = htons(7070);

    if (connect(c, (sockaddr*)&s, sizeof(s)) == SOCKET_ERROR) {
        std::cout << make_error_event("socket_connect", "connect failed: " + std::to_string(WSAGetLastError())) << std::endl;
        closesocket(c);
        return;
    }

    std::string acc;
    char buf[1024];
    while (running_.load()) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        acc.append(buf, buf + n);
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            try {
                auto j = json::parse(line);
                ++messages_received_;
                j["message_number"] = messages_received_;
                std::cout << j.dump() << std::endl;
            }
            catch (...) {
                ++messages_received_;
                json ev = create_base_event("received");
                ev["from"] = "socket_client";
                ev["text"] = line;
                ev["message_number"] = messages_received_;
                std::cout << ev.dump() << std::endl;
            }
        }
    }
    closesocket(c);
}

bool SocketModule::send(const std::string& message) {
    if (!running_.load() || client_socket_ == INVALID_SOCKET) {
        std::cout << make_error_event("socket_send", "Not connected") << std::endl;
        return false;
    }

    std::string payload = message;
    if (payload.empty() || payload.back() != '\n') payload += '\n';

    int n = ::send(client_socket_, payload.c_str(), (int)payload.size(), 0);
    if (n == SOCKET_ERROR) {
        std::cout << make_error_event("socket_send", "send failed: " + std::to_string(WSAGetLastError())) << std::endl;
        return false;
    }

    ++messages_sent_;
    json ev = create_base_event("sent");
    ev["bytes"] = n;
    ev["text"] = message;
    ev["message_number"] = messages_sent_;
    std::cout << ev.dump() << std::endl;
    return true;
}

void SocketModule::stop() {
    if (!running_.load()) return;

    running_.store(false);
    connected_.store(false);

    // Close sockets
    if (client_socket_ != INVALID_SOCKET) {
        closesocket(client_socket_);
        client_socket_ = INVALID_SOCKET;
    }

    if (server_socket_ != INVALID_SOCKET) {
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
    }

    // Join threads
    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    if (client_thread_.joinable()) {
        client_thread_.join();
    }

    WSACleanup();

    json ev = create_base_event("stopped");
    ev["message"] = "Socket mechanism stopped";
    ev["messages_sent"] = messages_sent_;
    ev["messages_received"] = messages_received_;
    ev["running"] = running_.load();
    ev["connected"] = connected_.load();
    std::cout << ev.dump() << std::endl;
}

void SocketModule::cleanup() {
    stop();
}

bool SocketModule::is_connected() const {
    return connected_.load();
}

bool SocketModule::is_running() const {
    return running_.load();
}

std::string SocketModule::get_status() const {
    std::stringstream ss;
    ss << "Socket Module - ";
    ss << (running_.load() ? "Running" : "Stopped");
    if (running_.load()) {
        ss << " | " << (connected_.load() ? "Connected" : "Waiting");
        ss << " | Sent: " << messages_sent_;
        ss << " | Received: " << messages_received_;
    }
    return ss.str();
}

nlohmann::json SocketModule::status() const {
    json status = create_base_event("status");
    status["mechanism"] = "socket";
    status["running"] = running_.load();
    status["connected"] = connected_.load();
    status["messages_sent"] = messages_sent_;
    status["messages_received"] = messages_received_;
    return status;
}

nlohmann::json SocketModule::create_base_event(const std::string& event_type) const {
    json event;
    event["event"] = event_type;
    event["mechanism"] = "socket";
    event["timestamp"] = std::time(nullptr);
    return event;
}

nlohmann::json SocketModule::make_simple_event(const std::string& event_type, const std::string& message) const {
    json event = create_base_event(event_type);
    event["message"] = message;
    return event;
}

nlohmann::json SocketModule::make_error_event(const std::string& error_type, const std::string& message) const {
    json event = create_base_event("error");
    event["error_type"] = error_type;
    event["message"] = message;
    return event;
}