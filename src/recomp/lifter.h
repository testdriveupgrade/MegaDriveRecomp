#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "analyzer.h"
#include "config.h"

// Translates a map of RecompFunctions into C++ source files.
class Lifter {
public:
    Lifter(const std::vector<uint8_t>& rom, const RecompConfig& cfg,
           const std::map<uint32_t, RecompFunction>& functions);

    // Write all output files to cfg.output_dir.
    void emit();

private:
    // Emit a single function's C++ source.
    std::string emit_function(const RecompFunction& func) const;

    // Emit a single basic block's C++ lines (not including the label).
    std::string emit_block(const BasicBlock& block, const RecompFunction& func) const;

    // Translate one decoded instruction into C++ source lines.
    std::string translate_inst(const DecodedInst& di) const;

    // ── Operand helpers ──────────────────────────────────────────────────────

    // Expression that reads src/dst operand as uint32_t.
    // Pre/post side-effects (inc/dec) must be emitted separately via side_effects_*.
    std::string read_operand(const m68k::Target& t, uint32_t static_ea, int bytes) const;

    // C++ statement(s) to perform writes; val_expr is the value to write.
    std::string write_operand(const m68k::Target& t, uint32_t static_ea, int bytes,
                              const std::string& val_expr) const;

    // Pre-side-effect (pre-decrement) — returns C++ statement or "".
    std::string pre_effect(const m68k::Target& t, int bytes) const;

    // Post-side-effect (post-increment) — returns C++ statement or "".
    std::string post_effect(const m68k::Target& t, int bytes) const;

    // Effective address expression for LEA/PEA (may be static or runtime).
    std::string ea_expr(const m68k::Target& t, uint32_t static_ea, uint32_t pc_after) const;

    // Condition expression string for a Condition enum value.
    static const char* cond_expr(m68k::Instruction::Condition c);

    // Resolve a static (compile-time-known) effective address for a target.
    uint32_t static_ea(const m68k::Target& t, uint32_t pc_after) const;

    // Read an integer from ROM bytes.
    uint32_t rom_read(uint32_t addr, int bytes) const;

    // A[N] register expression (handles A7 normally).
    static std::string areg(int n);

    const std::vector<uint8_t>& rom_;
    const RecompConfig& cfg_;
    const std::map<uint32_t, RecompFunction>& functions_;
};
