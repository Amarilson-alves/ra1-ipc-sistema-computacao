#ifndef IPC_MANAGER_HPP
#define IPC_MANAGER_HPP

#include <memory>
#include <string>
#include <atomic>
#include "pipe_module.hpp"
#include "socket_module.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

class IPCManager {
public:
    IPCManager();
    ~IPCManager();

    bool start(const std::string& mechanism);
    void stop();
    bool send(const std::string& message);
    std::string get_status() const;
    void run_child_mode();

    // Helper functions for event creation
    static json create_base_event(const std::string& event_type);
    static std::string make_error_event(const std::string& where, const std::string& message);
    static std::string make_simple_event(const std::string& event_type, const std::string& message);

private:
    std::unique_ptr<PipeModule> pipe_module_;
    std::unique_ptr<SocketModule> socket_module_;
    std::string current_mechanism_;
    std::atomic<bool> running_{ false };
};

#endif // IPC_MANAGER_HPP