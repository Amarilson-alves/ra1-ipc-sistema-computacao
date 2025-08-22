cmake_minimum_required(VERSION 3.20)
project(ra1_ipc_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Política para colocar binários em uma pasta 'bin'
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =======================================================================
# CONFIGURAÇÃO PARA BAIXAR A BIBLIOTECA JSON (ÚNICA NECESSÁRIA AGORA)
# =======================================================================
include(FetchContent)

# Configuração da biblioteca nlohmann/json (CRUCIAL para compilar)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# =======================================================================
# CONFIGURAÇÃO DO NOSSO EXECUTÁVEL
# =======================================================================

# Inclui a pasta 'include' para encontrar nossos headers
include_directories(include)

# Configuração do executável principal
add_executable(ra1_ipc_backend
    src/main.cpp
    src/json_codec.cpp
    src/pipe_module.cpp  # <- NOVO ARQUIVO ADICIONADO
)

# Linka a biblioteca JSON ao nosso executável
target_link_libraries(ra1_ipc_backend PRIVATE nlohmann_json)

# Configurações específicas para Windows
if(WIN32)
    target_link_libraries(ra1_ipc_backend 
        PRIVATE 
            ws2_32      # Para sockets (futuro)
    )
endif()