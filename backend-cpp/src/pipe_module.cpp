#include "pipe_module.hpp"
#include "ipc_common.hpp"
#include <windows.h>
#include <thread>
#include <iostream>

// Forward declaration da classe principal
class IPCManager;

PipeModule::PipeModule(IPCManager* manager) : manager_(manager) {}

PipeModule::~PipeModule() {
    stop();
}

bool PipeModule::start() {
    if (running_) return true;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    HANDLE hChildStd_IN_Rd = nullptr;
    HANDLE hChildStd_IN_Wr = nullptr;
    HANDLE hChildStd_OUT_Rd = nullptr;
    HANDLE hChildStd_OUT_Wr = nullptr;

    // Create pipes for child process
    if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) {
        std::cout << make_error_event("pipe_create", "Failed to create output pipe") << std::endl;
        return false;
    }

    if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &saAttr, 0)) {
        std::cout << make_error_event("pipe_create", "Failed to create input pipe") << std::endl;
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_OUT_Wr);
        return false;
    }

    // Create child process
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));

    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = hChildStd_OUT_Wr;
    siStartInfo.hStdInput = hChildStd_IN_Rd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    TCHAR cmdline[] = TEXT("ra1_ipc_backend.exe pipe_child");
    BOOL success = CreateProcess(
        nullptr,           // Application name
        cmdline,           // Command line
        nullptr,           // Process security attributes
        nullptr,           // Primary thread security attributes
        TRUE,              // Handles are inherited
        CREATE_NO_WINDOW,  // Creation flags
        nullptr,           // Use parent's environment
        nullptr,           // Use parent's current directory
        &siStartInfo,      // STARTUPINFO pointer
        &piProcInfo        // PROCESS_INFORMATION pointer
    );

    if (!success) {
        std::cout << make_error_event("pipe_process", "Failed to create child process") << std::endl;
        CloseHandle(hChildStd_IN_Rd);
        CloseHandle(hChildStd_IN_Wr);
        CloseHandle(hChildStd_OUT_Rd);
        CloseHandle(hChildStd_OUT_Wr);
        return false;
    }

    // Close handles we don't need in parent
    CloseHandle(hChildStd_OUT_Wr);
    CloseHandle(hChildStd_IN_Rd);
    CloseHandle(piProcInfo.hThread);

    read_pipe_ = hChildStd_OUT_Rd;
    write_pipe_ = hChildStd_IN_Wr;
    child_process_ = piProcInfo.hProcess;
    running_ = true;

    // Start reader thread
    reader_thread_ = std::thread(&PipeModule::reader_thread, this);

    // REMOVIDO: std::cout << make_simple_event("ready", "Pipe mechanism started") << std::endl;
    return true;
}

void PipeModule::stop() {
    if (!running_) return;

    running_ = false;

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    cleanup();
    std::cout << make_simple_event("stopped", "Pipe mechanism stopped") << std::endl;
}

void PipeModule::cleanup() {
    if (read_pipe_) CloseHandle(static_cast<HANDLE>(read_pipe_));
    if (write_pipe_) CloseHandle(static_cast<HANDLE>(write_pipe_));
    if (child_process_) CloseHandle(static_cast<HANDLE>(child_process_));

    read_pipe_ = write_pipe_ = child_process_ = nullptr;
}

void PipeModule::reader_thread() {
    HANDLE hPipe = static_cast<HANDLE>(read_pipe_);
    char buffer[1024];
    DWORD bytesRead;

    while (running_) {
        if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string message(buffer);
                json event = create_base_event("received");
                event["text"] = message;
                event["from"] = "child";
                std::cout << event.dump() << std::endl;
            }
        }
        else {
            if (running_) {
                std::cout << make_error_event("pipe_read", "Failed to read from pipe") << std::endl;
            }
            break;
        }
    }
}

bool PipeModule::send(const std::string& message) {
    if (!running_) return false;

    HANDLE hPipe = static_cast<HANDLE>(write_pipe_);
    DWORD bytesWritten;

    BOOL success = WriteFile(hPipe, message.c_str(), message.size(), &bytesWritten, nullptr);
    if (success) {
        json event = create_base_event("sent");
        event["bytes"] = bytesWritten;
        event["text"] = message;
        std::cout << event.dump() << std::endl;
        return true;
    }
    else {
        std::cout << make_error_event("pipe_send", "Failed to write to pipe") << std::endl;
        return false;
    }
}

bool PipeModule::is_running() const {
    return running_;
}