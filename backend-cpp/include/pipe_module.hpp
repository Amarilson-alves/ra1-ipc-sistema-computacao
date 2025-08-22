#pragma once
#include <string>
#include <memory>
#include <thread>

// Forward declaration para evitar include circular
class IPCManager;

class PipeModule {
public:
    PipeModule(IPCManager* manager);
    ~PipeModule();

    bool start();
    void stop();
    bool send(const std::string& message);
    bool is_running() const;

private:
    void cleanup();
    void reader_thread();
    void writer_thread();

    IPCManager* manager_;
    bool running_ = false;

    // Handles do Windows para pipes e processo
    void* read_pipe_ = nullptr;
    void* write_pipe_ = nullptr;
    void* child_process_ = nullptr;
    void* child_thread_ = nullptr;

    std::thread reader_thread_;
    std::thread writer_thread_;
};