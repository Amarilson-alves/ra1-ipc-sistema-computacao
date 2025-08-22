#pragma once

#include <nlohmann/json.hpp> // Iremos usar a biblioteca JSON for Modern C++
#include <string>
#include <optional>

// Usaremos a biblioteca JSON for Modern C++.
// Ela ser� baixada pelo CMake mais tarde. Por enquanto, declaramos as fun��es.
using json = nlohmann::json;

// Fun��es para criar mensagens de evento JSON
json create_base_event(const std::string& event_type);
std::string make_simple_event(const std::string& event_type, const std::string& details = "");
std::string make_error_event(const std::string& where, const std::string& message);

// Fun��o para parsear uma string em um comando JSON
std::optional<json> parse_json_command(const std::string& input);