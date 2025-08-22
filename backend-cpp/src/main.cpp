#include <iostream>
#include <string>
#include "ipc_common.hpp" // Para a definição das funções de protocolo

int main(int argc, char* argv[]) {
    // Configuração inicial para evitar buffering no stdin/stdout
    // Isso é CRUCIAL para a comunicação linha-a-linha.
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

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

        auto j = maybe_json.value();

        // Processa o comando recebido
        try {
            std::string command = j.at("cmd").get<std::string>();

            if (command == "status") {
                // Responde com um status simples por enquanto
                std::cout << make_simple_event("status", "Backend running, no mechanism active") << std::endl;
            }
            else {
                // Comando não reconhecido
                std::cout << make_error_event("unknown_command", "Command not implemented: " + command) << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cout << make_error_event("process_command", e.what()) << std::endl;
        }
    }

    std::cout << make_simple_event("backend_stopped") << std::endl;
    return 0;
}