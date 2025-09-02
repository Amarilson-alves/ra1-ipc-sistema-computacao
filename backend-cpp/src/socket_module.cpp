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
    if (listen(server_socket_, 5) == SOCKET_ERROR) {
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

    while (running_.load()) {
        SOCKET s = accept(server_socket_, (sockaddr*)&caddr, &clen);
        if (s == INVALID_SOCKET) {
            if (running_.load()) {
                std::cout << make_error_event("socket_accept", "accept failed: " + std::to_string(WSAGetLastError())) << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::cout << "DEBUG [SERVER]: Client connected " << inet_ntoa(caddr.sin_addr) << ":" << ntohs(caddr.sin_port) << std::endl;

        // ---- Leitura da primeira linha para detectar o listener
        std::string firstline;
        {
            char ch;
            // bloqueia até receber 1a linha (para o listener vir com o hello imediatamente)
            while (true) {
                int r = recv(s, &ch, 1, 0);
                if (r <= 0) break;
                if (ch == '\n') break;
                firstline.push_back(ch);
            }
        }

        bool is_listener = false;
        if (!firstline.empty()) {
            try {
                auto hello = nlohmann::json::parse(firstline);
                if (hello.value("role", "") == "listener") {
                    is_listener = true;
                }
            }
            catch (...) { /* não é JSON; então é já a 1a mensagem do remetente */ }
        }

        if (is_listener) {
            // Registra o socket aceito como canal de broadcast para o frontend
            {
                std::lock_guard<std::mutex> lk(listener_mtx_);
                if (listener_socket_ != INVALID_SOCKET) {
                    closesocket(listener_socket_);
                }
                listener_socket_ = s;
            }
            std::cout << make_simple_event("socket_listener_registered", "frontend listener ready") << std::endl;
            // NÃO bloqueie lendo desse socket; ele é só para envio (server -> frontend)
            continue; // volta a aceitar próximos clientes remetentes
        }

        // ---- A partir daqui, 's' é um cliente remetente (envia mensagens)
        // Se houve uma 1a linha não-hello, processe-a como parte da mensagem
        std::string acc;
        if (!firstline.empty()) {
            acc = firstline + "\n";
        }

        char buf[1024];
        while (running_.load()) {
            int n = 0;
            if (!acc.empty()) {
                // já temos dados na 'acc'; pule o recv primeiro
            }
            else {
                n = recv(s, buf, sizeof(buf), 0);
                if (n <= 0) {
                    std::cout << "DEBUG [SERVER]: Sender disconnected" << std::endl;
                    break;
                }
                acc.append(buf, n);
            }

            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);

                ++messages_received_;
                std::cout << "DEBUG [SERVER RECEIVED FROM SENDER]: " << line << std::endl;

                // Monte SEMPRE JSON de resposta para o frontend
                nlohmann::json resp = create_base_event("received");
                resp["from"] = "socket_server";
                try {
                    auto j = nlohmann::json::parse(line);
                    resp["text"] = std::string("ECHO: ") + (j.contains("text") ? j["text"].get<std::string>() : line);
                }
                catch (...) {
                    resp["text"] = std::string("ECHO: ") + line;
                }
                resp["message_number"] = messages_received_;

                // ENVIE o JSON para o LISTENER pelo socket ACEITO correspondente
                {
                    std::lock_guard<std::mutex> lk(listener_mtx_);
                    if (listener_socket_ != INVALID_SOCKET) {
                        std::string out = resp.dump() + "\n";
                        int send_result = ::send(listener_socket_, out.c_str(), static_cast<int>(out.size()), 0);
                        if (send_result == SOCKET_ERROR) {
                            std::cout << "DEBUG [SERVER -> LISTENER SEND ERROR]: " << WSAGetLastError() << std::endl;
                        }
                    }
                    else {
                        std::cout << "DEBUG [SERVER]: No listener socket registered yet" << std::endl;
                    }
                }

                // (Opcional) ACK simples de volta ao remetente
                const char* ack = "ACK\n";
                int ack_result = ::send(s, ack, 4, 0);
                if(ack_result == SOCKET_ERROR) {
                    std::cout << "DEBUG [SERVER -> SENDER ACK ERROR]: " << WSAGetLastError() << std::endl;
				}
             
            }
        }

        closesocket(s);
        std::cout << make_simple_event("socket_disconnected", "Sender disconnected") << std::endl;
    }
}

void SocketModule::client_thread() {
    // Este é o cliente INTERNO que se conecta para receber ecos (APENAS ESCUTA)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    SOCKET c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c == INVALID_SOCKET) {
        std::cout << make_error_event("socket_create", "internal client invalid: " + std::to_string(WSAGetLastError())) << std::endl;
        return;
    }

    sockaddr_in s{};
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    s.sin_port = htons(7070);

    if (connect(c, (sockaddr*)&s, sizeof(s)) == SOCKET_ERROR) {
        std::cout << make_error_event("socket_connect", "internal client connect failed: " + std::to_string(WSAGetLastError())) << std::endl;
        closesocket(c);
        return;
    }

    // Guarda o socket do cliente interno e marca como conectado
    client_socket_ = c;
    connected_.store(true);
    std::cout << "DEBUG [CLIENT]: Internal client connected successfully (LISTENER)" << std::endl;

    // >>> ADICIONE: handshake para o servidor reconhecer este socket como listener
    const char* hello = "{\"role\":\"listener\"}\n";
    ::send(c, hello, static_cast<int>(strlen(hello)), 0);

    std::string acc;
    char buf[1024];
    while (running_.load()) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) {
            std::cout << "DEBUG [CLIENT]: Internal listener connection lost" << std::endl;
            break;
        }
        acc.append(buf, n);
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            // DEBUG: Mostre o que está chegando
            std::cout << "DEBUG [CLIENT RECEIVED FROM SERVER]: " << line << std::endl;

            try {
                auto j = nlohmann::json::parse(line);
                std::cout << j.dump() << std::endl;  // reemita o JSON "puro"
            }
            catch (const std::exception& e) {
                // DEBUG: Mostre o erro de parse
                std::cout << "DEBUG [CLIENT PARSE ERROR]: " << e.what() << " for: " << line << std::endl;

                nlohmann::json ev = create_base_event("received");
                ev["from"] = "socket_client";
                ev["text"] = line;
                std::cout << ev.dump() << std::endl;
            }
        }
    }
    closesocket(c);
    client_socket_ = INVALID_SOCKET;
    connected_.store(false);
    std::cout << "DEBUG [CLIENT]: Internal client disconnected" << std::endl;
}

bool SocketModule::send(const std::string& message) {
    if (!running_.load()) {
        std::cout << make_error_event("socket_send", "Not running") << std::endl;
        return false;
    }

    // Cria um socket temporário para enviar a mensagem
    SOCKET temp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (temp_socket == INVALID_SOCKET) {
        std::cout << make_error_event("socket_send", "Failed to create temp socket: " + std::to_string(WSAGetLastError())) << std::endl;
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(7070);

    std::cout << "DEBUG [SEND]: Connecting to server..." << std::endl;

    if (connect(temp_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        std::cout << make_error_event("socket_send", "Connect failed: " + std::to_string(error)) << std::endl;
        closesocket(temp_socket);
        return false;
    }

    std::cout << "DEBUG [SEND]: Connected successfully, sending message..." << std::endl;

    std::string payload = message;
    if (payload.empty() || payload.back() != '\n') payload += '\n';

    std::cout << "DEBUG [SEND]: Sending to server: " << payload;

    int n = ::send(temp_socket, payload.c_str(), static_cast<int>(payload.size()), 0);

    if (n == SOCKET_ERROR) {
        std::cout << make_error_event("socket_send", "send failed: " + std::to_string(WSAGetLastError())) << std::endl;
        closesocket(temp_socket);
        return false;
    }

    std::cout << "DEBUG [SEND]: Message sent successfully, waiting for server response..." << std::endl;

    // AGUARDA A RESPOSTA DO SERVIDOR antes de fechar
    char response_buf[1024];
    int response_n = recv(temp_socket, response_buf, sizeof(response_buf), 0);
    if (response_n > 0) {
        std::string response(response_buf, response_n);
        std::cout << "DEBUG [SEND]: Server response: " << response;
    }
    else if (response_n == 0) {
        std::cout << "DEBUG [SEND]: Server closed connection" << std::endl;
    }
    else {
        std::cout << "DEBUG [SEND]: Error receiving response: " << WSAGetLastError() << std::endl;
    }

    std::cout << "DEBUG [SEND]: Closing connection..." << std::endl;
    closesocket(temp_socket); // Fecha o socket temporário

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

    // Feche o lado servidor do listener
    {
        std::lock_guard<std::mutex> lk(listener_mtx_);
        if (listener_socket_ != INVALID_SOCKET) {
            closesocket(listener_socket_);
            listener_socket_ = INVALID_SOCKET;
        }
    }

    // Feche o lado cliente interno
    if (client_socket_ != INVALID_SOCKET) {
        closesocket(client_socket_);
        client_socket_ = INVALID_SOCKET;
    }

    if (server_socket_ != INVALID_SOCKET) {
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
    }

    if (server_thread_.joinable()) server_thread_.join();
    if (client_thread_.joinable()) client_thread_.join();

    WSACleanup();

    auto ev = create_base_event("stopped");
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