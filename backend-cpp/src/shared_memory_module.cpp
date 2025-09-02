#include "shared_memory_module.hpp"
#include <chrono>
#include <iostream>
#include <sstream>

using nlohmann::json;

static std::wstring to_wstr(DWORD v) {
    wchar_t buf[32];
    _itow_s(static_cast<int>(v), buf, 10);
    return std::wstring(buf);
}

SharedMemoryModule::SharedMemoryModule(IPCManager* manager)
    : manager_(manager) {
}

SharedMemoryModule::~SharedMemoryModule() {
    stop();
}

bool SharedMemoryModule::is_running() const {
    return running_.load();
}

std::wstring SharedMemoryModule::make_name(const wchar_t* base) const {
    // Gera nomes únicos por PID (evita colisão quando roda múltiplas instâncias)
    const auto pid = GetCurrentProcessId();
    return std::wstring(L"Local\\RA1_IPC_SHM_") + base + L"_" + to_wstr(pid);
}

void SharedMemoryModule::clear_channel(Channel& ch) {
    ch.len = 0;
}

bool SharedMemoryModule::write_channel(Channel& ch, const std::string& s) {
    if (s.size() > SHM_MAX_MSG) return false;
    // protocolo: primeiro grava len, depois copia bytes
    // Para evitar o leitor ver len>0 com dados incompletos, zere, copie, depois set len
    ch.len = 0;
    memcpy(ch.data, s.data(), s.size());
    // memory barrier "coarse" (melhoraria com _mm_sfence em x86, mas ok p/ skeleton)
    ch.len = static_cast<uint32_t>(s.size());
    return true;
}

std::string SharedMemoryModule::read_channel(Channel& ch) {
    const uint32_t n = ch.len;
    if (n == 0 || n > SHM_MAX_MSG) return {};
    std::string out(ch.data, ch.data + n);
    ch.len = 0; // esvazia
    return out;
}

json SharedMemoryModule::base_event(const std::string& type) const {
    json j;
    j["event"] = type;
    j["mechanism"] = "shm";
    j["timestamp"] = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    return j;
}

void SharedMemoryModule::log_json(const json& j) const {
    std::cout << j.dump() << std::endl;
}

void SharedMemoryModule::log_error(const std::string& where, const std::string& what) const {
    auto j = base_event("error");
    j["where"] = where;
    j["message"] = what;
    log_json(j);
}

bool SharedMemoryModule::start() {
    if (running_.load()) return true;

    map_name_ = make_name(L"MAP");
    ev_p2c_name_ = make_name(L"EV_P2C");
    ev_c2p_name_ = make_name(L"EV_C2P");
    ev_stop_name_ = make_name(L"EV_STOP");

    // 1) CreateFileMapping + MapViewOfFile
    hMap_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, static_cast<DWORD>(sizeof(ShmLayout)),
        map_name_.c_str());
    if (!hMap_) {
        log_error("shm_start", "CreateFileMapping failed: " + std::to_string(GetLastError()));
        return false;
    }
    layout_ = reinterpret_cast<ShmLayout*>(MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ShmLayout)));
    if (!layout_) {
        log_error("shm_start", "MapViewOfFile failed: " + std::to_string(GetLastError()));
        CloseHandle(hMap_); hMap_ = nullptr;
        return false;
    }

    clear_channel(layout_->p2c);
    clear_channel(layout_->c2p);

    // 2) Eventos (auto-reset)
    ev_p2c_ = CreateEventW(nullptr, FALSE, FALSE, ev_p2c_name_.c_str());
    ev_c2p_ = CreateEventW(nullptr, FALSE, FALSE, ev_c2p_name_.c_str());
    ev_stop_ = CreateEventW(nullptr, TRUE, FALSE, ev_stop_name_.c_str()); // manual-reset para broadcast de stop
    if (!ev_p2c_ || !ev_c2p_ || !ev_stop_) {
        log_error("shm_start", "CreateEvent failed: " + std::to_string(GetLastError()));
        stop();
        return false;
    }

    running_.store(true);
    messages_sent_.store(0);
    messages_received_.store(0);

    // 3) Threads:
    //    - child_echo_loop: simula o "filho", consumindo p2c e produzindo c2p
    //    - parent_reader_loop: consome c2p e imprime JSON "received"
    child_thread_ = std::thread(&SharedMemoryModule::child_echo_loop, this);
    reader_thread_ = std::thread(&SharedMemoryModule::parent_reader_loop, this);

    // evento "started"
    auto j = base_event("started");
    j["message"] = "Shared memory started";
    log_json(j);
    return true;
}

bool SharedMemoryModule::send(const std::string& msg) {
    if (!running_.load()) return false;

    // Grava no canal P→C e sinaliza
    if (!write_channel(layout_->p2c, msg)) {
        log_error("shm_send", "message too large");
        return false;
    }
    ++messages_sent_;
    SetEvent(ev_p2c_);

    // log "sent"
    auto ev = base_event("sent");
    ev["text"] = msg;
    ev["message_number"] = messages_sent_.load();
    log_json(ev);
    return true;
}

void SharedMemoryModule::stop() {
    if (!running_.load()) return;

    running_.store(false);
    if (ev_stop_) SetEvent(ev_stop_); // acorda threads

    if (child_thread_.joinable())  child_thread_.join();
    if (reader_thread_.joinable()) reader_thread_.join();

    if (layout_) { UnmapViewOfFile(layout_); layout_ = nullptr; }
    if (hMap_) { CloseHandle(hMap_); hMap_ = nullptr; }

    if (ev_p2c_) { CloseHandle(ev_p2c_); ev_p2c_ = nullptr; }
    if (ev_c2p_) { CloseHandle(ev_c2p_); ev_c2p_ = nullptr; }
    if (ev_stop_) { CloseHandle(ev_stop_); ev_stop_ = nullptr; }

    auto ev = base_event("stopped");
    ev["message"] = "Shared memory mechanism stopped";
    ev["messages_sent"] = messages_sent_.load();
    ev["messages_received"] = messages_received_.load();
    ev["running"] = false;
    log_json(ev);
}

nlohmann::json SharedMemoryModule::status_json() const {
    auto j = base_event("status");
    j["shm_running"] = running_.load();
    j["messages_sent"] = messages_sent_.load();
    j["messages_received"] = messages_received_.load();
    return j;
}

// ---------------------- Threads ----------------------

void SharedMemoryModule::child_echo_loop() {
    // Espera "mensagem do pai" (ev_p2c_) OU "parar" (ev_stop_)
    HANDLE waits[2] = { ev_p2c_, ev_stop_ };

    while (running_.load()) {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 1) break; // ev_stop_

        // Chegou dado em P→C
        std::string incoming = read_channel(layout_->p2c);
        if (incoming.empty()) continue;

        // Monte resposta (sempre JSON de evento "received" com from:"shm_server")
        json resp = base_event("received");
        resp["from"] = "shm_server";
        // Se veio JSON com {"text": "..."} preserva, senão ecoa a linha
        try {
            auto j = json::parse(incoming);
            resp["text"] = std::string("ECHO: ") + (j.contains("text") ? j["text"].get<std::string>() : incoming);
        }
        catch (...) {
            resp["text"] = std::string("ECHO: ") + incoming;
        }
        resp["message_number"] = messages_received_.load() + 1;

        // Escreve resposta no canal C→P e sinaliza
        const std::string out = resp.dump();
        write_channel(layout_->c2p, out);
        SetEvent(ev_c2p_);
    }
}

void SharedMemoryModule::parent_reader_loop() {
    HANDLE waits[2] = { ev_c2p_, ev_stop_ };

    while (running_.load()) {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0 + 1) break; // ev_stop_

        // Chegou resposta do "filho"
        std::string s = read_channel(layout_->c2p);
        if (s.empty()) continue;

        try {
            auto j = json::parse(s);
            ++messages_received_;
            log_json(j);
        }
        catch (...) {
            // fallback: se não for JSON, embrulhe
            ++messages_received_;
            auto j = base_event("received");
            j["from"] = "shm_server";
            j["text"] = s;
            j["message_number"] = messages_received_.load();
            log_json(j);
        }
    }
}