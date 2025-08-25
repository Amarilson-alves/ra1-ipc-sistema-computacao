#pragma once
#include <string>
#include <memory>
#include <thread>

class IPCManager;

class PipeModule {
public:
    PipeModule(IPCManager* manager);
    ~PipeModule();

    bool start();
    void stop();
    bool send(const std::string& message);
    bool is_running() const;
    std::string get_status() const;

private:
    void cleanup();
    void reader_thread();

    IPCManager* manager_;
    bool running_ = false;
    bool reader_running_ = false;

    // Handles do Windows
    void* read_pipe_ = nullptr;
    void* write_pipe_ = nullptr;
    void* child_process_ = nullptr;

    std::thread reader_thread_;
    int messages_sent_ = 0;
    int messages_received_ = 0;
};