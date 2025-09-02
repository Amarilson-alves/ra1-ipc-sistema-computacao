#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>
#include <nlohmann/json.hpp>

class IPCManager; // fwd

class SharedMemoryModule {
public:
    explicit SharedMemoryModule(IPCManager* manager);
    ~SharedMemoryModule();

    bool start();                       // cria mapeamento + eventos + threads
    bool send(const std::string& msg);  // escreve no buffer P→C e sinaliza evento
    void stop();                        // encerra threads/handles e emite "stopped"
    nlohmann::json status_json() const; // opcional: usado pelo IPCManager
    bool is_running() const;            // ADICIONADO: método para verificar se está rodando

private:
    // Layout do mapeamento: dois canais fixos, protocolo [u32 len][payload]
    static constexpr size_t SHM_MAX_MSG = 32 * 1024; // 32 KiB por canal

#pragma pack(push, 1)
    struct Channel {
        volatile uint32_t len;                 // 0 = vazio; >0 = bytes válidos em data
        char data[SHM_MAX_MSG];                // payload (JSON line)
    };
    struct ShmLayout {
        Channel p2c; // Parent -> Child
        Channel c2p; // Child  -> Parent
    };
#pragma pack(pop)

    // helpers
    std::wstring make_name(const wchar_t* base) const;
    void clear_channel(Channel& ch);
    bool write_channel(Channel& ch, const std::string& s);
    std::string read_channel(Channel& ch);

    // threads
    void child_echo_loop();    // "lado filho": espera P→C e responde em C→P (ECHO)
    void parent_reader_loop(); // "lado pai": espera C→P e imprime JSON "received"

    // eventos JSON
    nlohmann::json base_event(const std::string& type) const;
    void log_json(const nlohmann::json& j) const;
    void log_error(const std::string& where, const std::string& what) const;

private:
    IPCManager* manager_{ nullptr };

    std::atomic<bool> running_{ false };

    // Identificadores do OS
    HANDLE hMap_{ nullptr };
    ShmLayout* layout_{ nullptr };

    // Eventos (auto-reset): quem escreve sinaliza para o outro lado
    HANDLE ev_p2c_{ nullptr };  // Parent sinaliza para Child
    HANDLE ev_c2p_{ nullptr };  // Child sinaliza para Parent
    HANDLE ev_stop_{ nullptr }; // sinalização de parada interna

    // Nomes (para potencial evolução p/ multi-processo)
    std::wstring map_name_;
    std::wstring ev_p2c_name_;
    std::wstring ev_c2p_name_;
    std::wstring ev_stop_name_;

    // Threads
    std::thread child_thread_;
    std::thread reader_thread_;

    // Métricas simples
    std::atomic<int> messages_sent_{ 0 };
    std::atomic<int> messages_received_{ 0 };
};