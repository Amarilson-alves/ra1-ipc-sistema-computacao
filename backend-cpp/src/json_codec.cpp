#include "ipc_common.hpp"
#include <iostream>

// Cria um evento JSON base com timestamp e PID
json create_base_event(const std::string& event_type) {
    json event;
    event["event"] = event_type;
    // TODO (Dia 3): Adicionar timestamp ISO 8601 e PID real.
    event["ts"] = "2024-01-01T12:00:00Z"; // Placeholder
    event["pid"] = 0; // Placeholder
    return event;
}

std::string make_simple_event(const std::string& event_type, const std::string& details) {
    json event = create_base_event(event_type);
    if (!details.empty()) {
        event["details"] = details;
    }
    return event.dump(); // Retorna a string JSON
}

std::string make_error_event(const std::string& where, const std::string& message) {
    json event = create_base_event("error");
    event["where"] = where;
    event["message"] = message;
    return event.dump();
}

std::optional<json> parse_json_command(const std::string& input) {
    try {
        return json::parse(input);
    }
    catch (const json::parse_error& e) {
        // Log do erro de parse (opcional, já que o main vai emitir um evento de erro)
        // std::cerr << "JSON parse error: " << e.what() << '\n';
        return std::nullopt;
    }
}