#ifndef PIPE_MODULE_HPP
#define PIPE_MODULE_HPP

#include <string>
#include <thread>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// Forward declaration
class IPCManager;

class PipeModule {
public:
    PipeModule(IPCManager* manager);
    ~PipeModule();

    bool start();
    void stop();
    bool send(const std::string& message);
    std::string get_status() const;
    bool is_running() const;

private:
    void cleanup();
    void reader_thread();

    IPCManager* manager_;
    bool running_;
    bool reader_running_;
    int messages_sent_;
    int messages_received_;
    void* read_pipe_;      // HANDLE para leitura
    void* write_pipe_;     // HANDLE para escrita
    void* child_process_;  // HANDLE para processo filho
    std::thread reader_thread_;
};

#endif // PIPE_MODULE_HPP