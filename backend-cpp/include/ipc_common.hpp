#pragma once

#include <nlohmann/json.hpp> // Iremos usar a biblioteca JSON for Modern C++
#include <string>
#include <optional>

// Usaremos a biblioteca JSON for Modern C++.
// Ela será baixada pelo CMake mais tarde. Por enquanto, declaramos as funções.
using json = nlohmann::json;

// Funções para criar mensagens de evento JSON
json create_base_event(const std::string& event_type);
std::string make_simple_event(const std::string& event_type, const std::string& details = "");
std::string make_error_event(const std::string& where, const std::string& message);

// Função para parsear uma string em um comando JSON
std::optional<json> parse_json_command(const std::string& input);