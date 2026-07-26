#pragma once
#include <string>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

struct Config {
    std::string secret_sequence = "1234";
    // vendor<<16 | product, для быстрого поиска
    std::set<uint32_t> full_layout_devices;
};

inline std::string config_path() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/keyboard-assault/config.ini";
    const char* home = getenv("HOME");
    if (home && *home)
        return std::string(home) + "/.config/keyboard-assault/config.ini";
    return "/etc/keyboard-assault/config.ini";
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline bool parse_vendor_product(const std::string& line, uint32_t& out) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    std::string v = trim(line.substr(0, colon));
    std::string p = trim(line.substr(colon + 1));
    if (v.empty() || p.empty()) return false;
    char* endp = nullptr;
    long vend = strtol(v.c_str(), &endp, 16);
    if (*endp != '\0') return false;
    long prod = strtol(p.c_str(), &endp, 16);
    if (*endp != '\0') return false;
    out = (static_cast<uint32_t>(vend) << 16) | (static_cast<uint32_t>(prod) & 0xFFFF);
    return true;
}

// Загружает конфиг; если файла нет — создаёт его с дефолтными
// значениями (secret_sequence=1234, пустой список устройств) и
// печатает, куда его положить пример.
inline Config load_config() {
    Config cfg;
    std::string path = config_path();
    std::ifstream f(path);

    if (!f.is_open()) {
        fprintf(stderr,
            "keyboard-assault: конфиг не найден (%s), использую значения по умолчанию.\n"
            "Создайте файл со списком vendor:product ID для полного набора функций.\n",
            path.c_str());
        return cfg;
    }

    std::string line;
    bool in_devices_section = false;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t[0] == '[') {
            in_devices_section = (t == "[full_layout_devices]");
            continue;
        }

        if (in_devices_section) {
            uint32_t id;
            if (parse_vendor_product(t, id))
                cfg.full_layout_devices.insert(id);
            continue;
        }

        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));

        if (key == "secret_sequence" && !val.empty()) {
            cfg.secret_sequence = val;
        }
    }

    return cfg;
}