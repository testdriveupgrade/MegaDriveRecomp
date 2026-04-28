#include "config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// Minimal TOML parser for the subset used by smd_recomp configs.
// Supports: [section], [[array_section]], key = "str", key = 0xHEX, key = DEC, key = true/false

namespace {

static std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

static uint32_t parse_int(const std::string& val) {
    if (starts_with(val, "0x") || starts_with(val, "0X"))
        return (uint32_t)std::stoul(val, nullptr, 16);
    return (uint32_t)std::stoul(val, nullptr, 10);
}

static std::string parse_string(const std::string& val) {
    // val should be "quoted string"
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
        return val.substr(1, val.size() - 2);
    return val;
}

} // namespace

RecompConfig RecompConfig::parse(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open config: " + path);

    RecompConfig cfg;
    std::string line;
    std::string cur_section;
    bool in_switch_table = false;
    bool in_function = false;

    SwitchTable cur_st{};
    KnownFunction cur_fn{};

    const auto flush_current = [&] {
        if (in_switch_table) { cfg.switch_tables.push_back(cur_st); cur_st = {}; }
        if (in_function)     { cfg.known_functions.push_back(cur_fn); cur_fn = {}; }
        in_switch_table = false;
        in_function = false;
    };

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // Strip inline comment
        auto hash = line.find(" #");
        if (hash != std::string::npos) line = trim(line.substr(0, hash));

        // [[array_section]]
        if (starts_with(line, "[[")) {
            flush_current();
            auto end = line.find("]]");
            if (end != std::string::npos) {
                cur_section = trim(line.substr(2, end - 2));
                if (cur_section == "switch_tables") { in_switch_table = true; cur_st = {}; }
                else if (cur_section == "functions") { in_function = true; cur_fn = {}; }
            }
            continue;
        }
        // [section]
        if (starts_with(line, "[")) {
            flush_current();
            auto end = line.find(']');
            if (end != std::string::npos) cur_section = trim(line.substr(1, end - 1));
            in_switch_table = false;
            in_function = false;
            continue;
        }

        // key = value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (cur_section == "rom") {
            if (key == "path")      cfg.rom_path = parse_string(val);
        } else if (cur_section == "output") {
            if (key == "directory") cfg.output_dir = parse_string(val);
            if (key == "namespace") cfg.namespace_name = parse_string(val);
            if (key == "split_files") cfg.split_files = (val == "true");
        } else if (in_switch_table) {
            if (key == "address")    cur_st.address    = parse_int(val);
            if (key == "count")      cur_st.count      = parse_int(val);
            if (key == "entry_size") cur_st.entry_size = parse_int(val);
        } else if (in_function) {
            if (key == "address") cur_fn.address = parse_int(val);
            if (key == "name")    cur_fn.name    = parse_string(val);
        }
    }
    flush_current();
    return cfg;
}
