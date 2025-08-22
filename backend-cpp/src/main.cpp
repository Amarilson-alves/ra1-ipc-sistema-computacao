#include <iostream>
#include <string>
#include <memory>
#include <windows.h>
#include <thread>
#include "ipc_common.hpp"
#include "pipe_module.hpp"

class IPCManager {
public:
    IPCManager() = default;

    void handle_command(const json& j) {
        try {
            std::string command = j.at("cmd").get<std::string>();

            if (command == "start") {
                std::string mechanism = j.at("mechanism").get<std::string>();
                if (mechanism == "pipe") {
                    if (!pipe_module_) {
                        pipe_module_ = std::make_unique<PipeModule>(this);
                    }
                    if (pipe_module_->start()) {
                        std::cout << make_simple_event("ready", "Pipe mechanism started") << std::endl;
                    }
                }
                else {
                    std::cout << make_error_event("unknown_mechanism", "Mechanism not implemented: " + mechanism) << std::endl;
                }
            }
            else if (command == "send") {
                if (pipe_module_ && pipe_module_->is_running()) {
                    std::string text = j.at("text").get<std::string>();
                    pipe_module_->send(text);
                }
                else {
                    std::cout << make_error_event("send_failed", "No active pipe mechanism") << std::endl;
                }
            }
            else if (command == "stop") {
                if (pipe_module_) {
                    pipe_module_->stop();
                    pipe_module_.reset();
                    std::cout << make_simple_event("stopped", "Pipe mechanism stopped") << std::endl;
                }
            }
            else if (command == "status") {
                json event = create_base_event("status");
                event["pipe_running"] = (pipe_module_ && pipe_module_->is_running());
                event["mechanism"] = pipe_module_ ? "pipe" : "none";
                std::cout << event.dump() << std::endl;
            }
            else {
                std::cout << make_error_event("unknown_command", "Command not implemented: " + command) << std::endl;
            }

        }
        catch (const std::exception& e) {
            std::cout << make_error_event("process_command", e.what()) << std::endl;
        }
    }

private:
    std::unique_ptr<PipeModule> pipe_module_;
};

int main(int argc, char* argv[]) {
    // Configuração inicial para evitar buffering no stdin/stdout
    // Isso é CRUCIAL para a comunicação linha-a-linha.
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Modo filho para pipes (será implementado depois)
    if (argc > 1 && std::string(argv[1]) == "pipe_child") {
        // Modo eco simples para o processo filho
        std::string line;
        while (std::getline(std::cin, line)) {
            std::cout << "ECHO: " << line << std::endl;
        }
        return 0;
    }

    IPCManager manager;

    // Log inicial para sinalizar que o backend iniciou
    std::cout << make_simple_event("backend_started") << std::endl;

    std::string line;
    while (std::getline(std::cin, line)) {
        // Tenta parsear a linha de entrada como JSON
        auto maybe_json = parse_json_command(line);
        if (!maybe_json) {
            // Se falhar o parse, emite um evento de erro e continua.
            std::cout << make_error_event("parse_input", "Failed to parse JSON input: " + line) << std::endl;
            continue;
        }

        manager.handle_command(maybe_json.value());
    }

    std::cout << make_simple_event("backend_stopped") << std::endl;
    return 0;
}