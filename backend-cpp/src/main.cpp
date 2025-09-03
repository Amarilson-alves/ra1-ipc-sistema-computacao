#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include "ipc_common.hpp"
#include "pipe_module.hpp"
#include "socket_module.hpp"
#include "ipc_manager.hpp"

int main(int argc, char* argv[]) {
    // Modo filho para pipes - DEVE SER A PRIMEIRA COISA
    if (argc > 1 && std::string(argv[1]) == "pipe_child") {
        // Processo filho: modo eco SIMPLES
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty()) {
                // Responde com JSON formatado
                json response;
                response["event"] = "received";
                response["text"] = "ECHO: " + line;
                response["from"] = "child";
                std::cout << response.dump() << std::endl;
                std::cout.flush();
            }
        }
        return 0;
    }

    // Configuração inicial para evitar buffering no stdin/stdout
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    IPCManager manager;

    // REMOVIDO: backend_started duplicado (já é emitido no construtor do IPCManager)

    std::string line;
    while (std::getline(std::cin, line)) {
        // DEBUG: Log da linha recebida
        std::cerr << "DEBUG [INPUT]: " << line << std::endl;

        // Tenta parsear a linha de entrada como JSON
        auto maybe_json = parse_json_command(line);
        if (!maybe_json) {
            std::cerr << make_error_event("parse_input", "Failed to parse JSON input: " + line) << std::endl;
            continue;
        }

        // Handle command usando o IPCManager
        try {
            json command = maybe_json.value();
            std::string cmd = command.at("cmd").get<std::string>();

            // DEBUG: Log do comando recebido
            std::cerr << "DEBUG [COMANDO]: " << cmd << std::endl;

            if (cmd == "start") {
                std::string mechanism = command.at("mechanism").get<std::string>();
                std::cerr << "DEBUG [MECANISMO]: " << mechanism << std::endl;

                if (manager.start(mechanism)) {
                    std::cerr << "DEBUG [START SUCESSO]: Mecanismo " << mechanism << " iniciado" << std::endl;
                }
                else {
                    std::cerr << "DEBUG [START FALHA]: Falha ao iniciar mecanismo " << mechanism << std::endl;
                }
            }
            else if (cmd == "stop") {
                std::cerr << "DEBUG [STOP]: Parando mecanismo" << std::endl;
                manager.stop();
                std::cerr << "DEBUG [STOP COMPLETO]: Mecanismo parado" << std::endl;
            }
            else if (cmd == "send") {
                std::cerr << "DEBUG [SEND]: Entrou no comando send" << std::endl;

                std::string text = command.at("text").get<std::string>();
                std::cerr << "DEBUG [SEND TEXT]: " << text << std::endl;

                if (manager.send(text)) {
                    std::cerr << "DEBUG [SEND SUCESSO]: Mensagem enviada" << std::endl;
                }
                else {
                    std::cerr << "DEBUG [SEND FALHA]: Falha ao enviar mensagem" << std::endl;
                    std::cerr << "DEBUG [STATUS ATUAL]: " << manager.get_status() << std::endl;
                }
            }
            else if (cmd == "status") {
                std::cerr << "DEBUG [STATUS]: Solicitando status" << std::endl;
                std::string status = manager.get_status();
                std::cerr << "DEBUG [STATUS RESULTADO]: " << status << std::endl;
                std::cout << status << std::endl;
            }
            else {
                std::cerr << "DEBUG [COMANDO DESCONHECIDO]: " << cmd << std::endl;
                std::cerr << make_error_event("unknown_command", "Command not implemented: " + cmd) << std::endl;
            }

        }
        catch (const std::exception& e) {
            std::cerr << "DEBUG [EXCEÇÃO]: " << e.what() << std::endl;
            std::cerr << make_error_event("process_command", e.what()) << std::endl;
        }
    }

    std::cout << make_simple_event("backend_stopped") << std::endl;
    return 0;
}