#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SwitchTable {
    uint32_t address;    // ROM address of the jump table
    uint32_t count;      // number of entries
    uint32_t entry_size; // 2 (word) or 4 (long) bytes per entry
};

struct KnownFunction {
    uint32_t address;
    std::string name;
};

struct RecompConfig {
    std::string rom_path;
    std::string output_dir;
    std::string namespace_name = "game";
    bool split_files = true; // one .cpp per function

    std::vector<SwitchTable> switch_tables;
    std::vector<KnownFunction> known_functions;

    static RecompConfig parse(const std::string& path);
};
