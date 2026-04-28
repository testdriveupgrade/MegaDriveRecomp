#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "lib/recomp/analyzer.h"
#include "lib/recomp/config.h"
#include "lib/recomp/lifter.h"

static std::vector<uint8_t> load_rom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open ROM: " + path);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: smd_recomp <config.toml>\n";
        return 1;
    }

    try {
        const std::string config_path = argv[1];
        spdlog::info("SMDRecomp — loading config: {}", config_path);

        RecompConfig cfg = RecompConfig::parse(config_path);
        spdlog::info("ROM: {}", cfg.rom_path);
        spdlog::info("Output: {}", cfg.output_dir);
        spdlog::info("Namespace: {}", cfg.namespace_name);

        auto rom = load_rom(cfg.rom_path);
        spdlog::info("ROM size: {} bytes ({} KB)", rom.size(), rom.size() / 1024);

        // Phase 1: analyse
        spdlog::info("Analysing ROM...");
        Analyzer analyzer(rom, cfg);
        auto functions = analyzer.run();
        spdlog::info("Discovered {} functions", functions.size());

        // Phase 2: generate C++
        spdlog::info("Generating C++...");
        Lifter lifter(rom, cfg, functions);
        lifter.emit();
        spdlog::info("Done — output written to: {}", cfg.output_dir);

    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        return 1;
    }
    return 0;
}
