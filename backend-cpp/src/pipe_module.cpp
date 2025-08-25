#include "pipe_module.hpp"
#include "ipc_common.hpp"
#include <windows.h>
#include <thread>
#include <iostream>
#include <sstream>
#include <vector>

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

    // CORREÇÃO 1: Pai NÃO deve herdar os handles que vai usar
    SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0); // pai lê
    SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0);  // pai escreve

    // Create child process
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));

    siStartInfo.cb = sizeof(STARTUPINFO);
    // CORREÇÃO 2: Redirecionar stderr para o mesmo pipe do stdout
    siStartInfo.hStdError = hChildStd_OUT_Wr;
    siStartInfo.hStdOutput = hChildStd_OUT_Wr;
    siStartInfo.hStdInput = hChildStd_IN_Rd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    // CORREÇÃO 3: Usar caminho do executável atual
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring cmdLine = L"\"" + std::wstring(exePath) + L"\" pipe_child";

    // Converter para TCHAR (suporte a UNICODE/ANSI)
    std::vector<TCHAR> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
    cmdLineBuffer.push_back('\0');

    BOOL success = CreateProcess(
        nullptr,           // Application name
        cmdLineBuffer.data(), // Command line
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
        DWORD error = GetLastError();
        std::stringstream ss;
        ss << "Failed to create child process. Error code: " << error;
        std::cout << make_error_event("pipe_process", ss.str()) << std::endl;

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
    messages_sent_ = 0;
    messages_received_ = 0;

    // Start reader thread
    reader_running_ = true;
    reader_thread_ = std::thread(&PipeModule::reader_thread, this);

    // Log do processo filho criado
    json event = create_base_event("process_created");
    event["child_pid"] = piProcInfo.dwProcessId;
    event["mechanism"] = "pipe";
    std::cout << event.dump() << std::endl;

    return true;
}

void PipeModule::stop() {
    if (!running_) return;

    running_ = false;
    reader_running_ = false;

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }

    cleanup();

    json event = create_base_event("stopped");
    event["mechanism"] = "pipe";
    event["messages_sent"] = messages_sent_;
    event["messages_received"] = messages_received_;
    std::cout << event.dump() << std::endl;
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

    while (reader_running_) {
        if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string message(buffer);
                messages_received_++;

                json event = create_base_event("received");
                event["text"] = message;
                event["from"] = "child";
                event["bytes"] = bytesRead;
                event["message_number"] = messages_received_;
                std::cout << event.dump() << std::endl;
            }
        }
        else {
            DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE && reader_running_) {
                std::stringstream ss;
                ss << "Read error. Code: " << error;
                std::cout << make_error_event("pipe_read", ss.str()) << std::endl;
            }
            break;
        }
    }
}

bool PipeModule::send(const std::string& message) {
    if (!running_) return false;

    HANDLE hPipe = static_cast<HANDLE>(write_pipe_);
    DWORD bytesWritten;

    // CORREÇÃO 4: Adicionar nova linha para o processo filho
    std::string payload = message;
    if (payload.back() != '\n') {
        payload += '\n';
    }

    BOOL success = WriteFile(hPipe, payload.c_str(), payload.size(), &bytesWritten, nullptr);
    if (success) {
        messages_sent_++;
        json event = create_base_event("sent");
        event["bytes"] = bytesWritten;
        event["text"] = message;
        event["message_number"] = messages_sent_;
        std::cout << event.dump() << std::endl;
        return true;
    }
    else {
        DWORD error = GetLastError();
        std::stringstream ss;
        ss << "Write error. Code: " << error;
        std::cout << make_error_event("pipe_send", ss.str()) << std::endl;
        return false;
    }
}

std::string PipeModule::get_status() const {
    std::stringstream ss;
    ss << "Pipe Module - ";
    ss << (running_ ? "Running" : "Stopped");
    if (running_) {
        ss << " | Sent: " << messages_sent_;
        ss << " | Received: " << messages_received_;
    }
    return ss.str();
}

bool PipeModule::is_running() const {
    return running_;
}