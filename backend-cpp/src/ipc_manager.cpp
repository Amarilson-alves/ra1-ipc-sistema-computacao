#include "ipc_manager.hpp"
#include "shared_memory_module.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <windows.h>

// Helper functions for event creation
json IPCManager::create_base_event(const std::string& event_type) {
    json event;
    event["event"] = event_type;
    event["pid"] = GetCurrentProcessId();

    // ADICIONADO: mechanism para eventos globais
    if (event_type == "backend_started" || event_type == "backend_stopped" ||
        event_type == "error" || event_type == "status") {
        event["mechanism"] = "system";
    }

    // Get current timestamp in ISO 8601 format
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    char buffer[80];
    struct tm timeinfo;
    localtime_s(&timeinfo, &in_time_t);
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    event["ts"] = buffer;

    return event;
}

std::string IPCManager::make_error_event(const std::string& where, const std::string& message) {
    json event = create_base_event("error");
    event["where"] = where;
    event["message"] = message;
    return event.dump();
}

std::string IPCManager::make_simple_event(const std::string& event_type, const std::string& message) {
    json event = create_base_event(event_type);
    event["message"] = message;
    return event.dump();
}

IPCManager::IPCManager() : current_mechanism_("none") {
    pipe_module_ = std::make_unique<PipeModule>(this);
    socket_module_ = std::make_unique<SocketModule>(this);
    shm_ = std::make_unique<SharedMemoryModule>(this);

    // Log startup
    json event = create_base_event("backend_started");
    std::cout << event.dump() << std::endl;
}

IPCManager::~IPCManager() {
    stop();
}

bool IPCManager::start(const std::string& mechanism) {
    stop(); // Stop any current mechanism

    std::cerr << "DEBUG [COMANDO]: start" << std::endl;
    std::cerr << "DEBUG [MECANISMO]: " << mechanism << std::endl;

    if (mechanism == "pipe") {
        if (pipe_module_->start()) {
            current_mechanism_ = "pipe";
            running_.store(true);

            json event = create_base_event("started");
            event["mechanism"] = "pipe";
            std::cout << event.dump() << std::endl;

            return true;
        }
    }
    else if (mechanism == "socket") {
        if (socket_module_->start()) {
            current_mechanism_ = "socket";
            running_.store(true);

            json event = create_base_event("started");
            event["mechanism"] = "socket";
            std::cout << event.dump() << std::endl;

            return true;
        }
    }
    else if (mechanism == "shm") {
        if (shm_->start()) {
            current_mechanism_ = "shm";
            running_.store(true);

            json event = create_base_event("started");
            event["mechanism"] = "shm";
            std::cout << event.dump() << std::endl;

            return true;
        }
    }
    else {
        std::cerr << make_error_event("unknown_mechanism", "Mechanism not implemented: " + mechanism) << std::endl;
        return false;
    }

    return false;
}

void IPCManager::stop() {
    if (current_mechanism_ == "pipe") {
        pipe_module_->stop();
    }
    else if (current_mechanism_ == "socket") {
        socket_module_->stop();
    }
    else if (current_mechanism_ == "shm") {
        shm_->stop();
    }

    current_mechanism_ = "none";
    running_.store(false);

    std::cerr << "DEBUG [STOP]: Parando mecanismo" << std::endl;
}

bool IPCManager::send(const std::string& message) {
    std::cerr << "DEBUG [COMANDO]: send" << std::endl;
    std::cerr << "DEBUG [SEND]: Entrou no comando send" << std::endl;

    if (current_mechanism_ == "pipe") {
        if (!pipe_module_->is_running()) {
            std::cerr << "DEBUG [SEND ERROR]: PipeModule não está ativo" << std::endl;
            std::cerr << "DEBUG [PIPE_MODULE]: Nulo" << std::endl;
            std::cerr << make_error_event("send_failed", "No active pipe mechanism") << std::endl;
            return false;
        }
        return pipe_module_->send(message);
    }
    else if (current_mechanism_ == "socket") {
        if (!socket_module_->is_running()) {
            std::cerr << make_error_event("send_failed", "No active socket mechanism") << std::endl;
            return false;
        }
        return socket_module_->send(message);
    }
    else if (current_mechanism_ == "shm") {
        if (!shm_->is_running()) {
            std::cerr << make_error_event("send_failed", "No active shared memory mechanism") << std::endl;
            return false;
        }
        return shm_->send(message);
    }
    else {
        std::cerr << make_error_event("send_failed", "No active mechanism") << std::endl;
        return false;
    }
}

std::string IPCManager::get_status() const {
    std::cerr << "DEBUG [COMANDO]: status" << std::endl;
    std::cerr << "DEBUG [STATUS]: Solicitando status" << std::endl;

    json event = create_base_event("status");
    event["mechanism"] = current_mechanism_;

    if (current_mechanism_ == "pipe") {
        event["pipe_running"] = pipe_module_->is_running();
    }
    else if (current_mechanism_ == "socket") {
        event["socket_running"] = socket_module_->is_running();
        event["socket_connected"] = socket_module_->is_connected();
    }
    else if (current_mechanism_ == "shm") {
        event["shm_running"] = shm_->is_running();
        // Adicione quaisquer outros status específicos da memória compartilhada aqui
    }
    else {
        event["mechanism"] = "none";
    }

    return event.dump();
}

json IPCManager::status() {
    json j = create_base_event("status");
    j["mechanism"] = current_mechanism_;
    j["running"] = running_.load();  // CORRIGIDO: usando .load() para atomic

    if (current_mechanism_ == "pipe" && pipe_module_) {
        j["pipe_running"] = pipe_module_->is_running();
    }
    else if (current_mechanism_ == "socket" && socket_module_) {
        j["socket_running"] = socket_module_->is_running();
        j["socket_connected"] = socket_module_->is_connected();
    }
    else if (current_mechanism_ == "shm" && shm_) {
        // Adicione o status específico da memória compartilhada
        json shm_status = shm_->status_json();
        j.update(shm_status); // Mescla os dados de status do shm
    }

    return j;
}

void IPCManager::run_child_mode() {
    // Pipe child process mode - simples echo server
    std::cout << make_simple_event("child_started", "Pipe child process started") << std::endl;

    char buffer[1024];
    DWORD bytesRead;

    while (true) {
        if (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string message(buffer);

                // Echo back to parent with acknowledgment
                std::string response = "ECHO: " + message;
                DWORD bytesWritten;
                WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), response.c_str(), response.size(), &bytesWritten, nullptr);
            }
        }
        else {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) {
                std::cout << make_simple_event("child_exiting", "Parent process disconnected") << std::endl;
            }
            break;
        }
    }
}