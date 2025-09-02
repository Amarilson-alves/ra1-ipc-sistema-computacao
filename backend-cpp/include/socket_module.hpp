#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <nlohmann/json.hpp>
#include <mutex>                  // ADICIONADO: para proteger o socket do listener

class IPCManager;

class SocketModule {
public:
    SocketModule(IPCManager* manager);
    ~SocketModule();

    bool start();
    bool send(const std::string& message);
    void stop();
    bool is_connected() const;
    bool is_running() const;
    std::string get_status() const;
    nlohmann::json status() const;

private:
    void cleanup();
    void server_thread();
    void client_thread();
    bool setup_winsock();

    nlohmann::json create_base_event(const std::string& event_type) const;
    nlohmann::json make_simple_event(const std::string& event_type, const std::string& message) const;
    nlohmann::json make_error_event(const std::string& error_type, const std::string& message) const;

    IPCManager* manager_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> connected_{ false };
    SOCKET server_socket_{ INVALID_SOCKET };

    // Lado CLIENTE (usado pelo thread cliente interno)
    SOCKET client_socket_{ INVALID_SOCKET };

    // Lado SERVIDOR (socket aceito que corresponde ao listener interno)
    SOCKET listener_socket_{ INVALID_SOCKET };   // ADICIONADO: socket do listener
    std::mutex listener_mtx_;                    // ADICIONADO: mutex para proteger acesso ao listener

    std::thread server_thread_;
    std::thread client_thread_;
    int messages_sent_{ 0 };
    int messages_received_{ 0 };
};