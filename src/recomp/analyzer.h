#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "lib/m68k/instruction/instruction.h"

// ─────────────────────────────────────────────────────────────────────────────

struct DecodedInst {
    uint32_t pc_before;   // ROM address of this instruction
    uint32_t pc_after;    // ROM address of the next instruction
    m68k::Instruction inst;
    uint32_t src_ea = 0;  // static effective address (PC-relative / absolute src), 0 = runtime
    uint32_t dst_ea = 0;  // static effective address for dst, 0 = runtime
};

enum class BlockExit {
    Fallthrough,    // next sequential block
    Branch,         // conditional branch — has fallthrough + branch_target
    Jump,           // unconditional BRA / JMP to known address
    IndirectJump,   // JMP (An) — targets from switch table
    Call,           // JSR/BSR — calls function, fallthrough continues here
    IndirectCall,   // JSR (An) — indirect, targets unknown at recompile time
    Return,         // RTS / RTE / RTR
};

struct BasicBlock {
    uint32_t start_addr = 0;
    std::vector<DecodedInst> insts;

    BlockExit exit = BlockExit::Return;
    uint32_t branch_target  = 0; // for Branch, Jump
    uint32_t fallthrough    = 0; // for Fallthrough, Branch, Call
    uint32_t call_target    = 0; // for Call (0 = unknown/indirect)
    std::vector<uint32_t> switch_targets; // for IndirectJump
};

struct RecompFunction {
    uint32_t entry_addr = 0;
    std::string name;
    std::map<uint32_t, BasicBlock> blocks; // keyed by start_addr
};

// ─────────────────────────────────────────────────────────────────────────────

class Analyzer {
public:
    Analyzer(const std::vector<uint8_t>& rom, const RecompConfig& cfg);

    // Analyse the ROM and return all discovered functions.
    std::map<uint32_t, RecompFunction> run();

private:
    // Add a function entry point to be processed.
    void enqueue_function(uint32_t addr, const std::string& name = "");

    // Trace all basic blocks belonging to one function starting at entry_addr.
    void trace_function(uint32_t entry_addr);

    // Trace one basic block.  Returns the completed block.
    BasicBlock trace_block(uint32_t start_addr);

    // Compute the branch target for BCC/BSR/DBCC.
    uint32_t branch_target(const m68k::Instruction& inst, uint32_t pc_before, uint32_t pc_after) const;

    // Read the jump target of a JMP/JSR with a statically-known destination.
    // Returns 0 if the target cannot be determined statically.
    uint32_t static_jump_target(const m68k::Instruction& inst, uint32_t pc_after) const;

    // Resolve a switch table at rom_addr and return the list of absolute targets.
    std::vector<uint32_t> read_switch_table(const SwitchTable& st) const;

    // True if addr falls inside the ROM image.
    bool in_rom(uint32_t addr) const;

    // Read a big-endian value from ROM.
    uint32_t rom_read(uint32_t addr, int bytes) const;

    // Compute a static effective address for a target (PC-relative / absolute modes).
    // Returns 0 for runtime-computed modes.
    uint32_t static_ea(const m68k::Target& t, uint32_t pc_after) const;

    const std::vector<uint8_t>& rom_;
    const RecompConfig& cfg_;

    // Map: switch-table address → table config (for quick lookup)
    std::map<uint32_t, const SwitchTable*> switch_map_;

    // Discovered functions (output)
    std::map<uint32_t, RecompFunction> functions_;

    // Queue of entry points waiting to be traced
    std::vector<std::pair<uint32_t, std::string>> pending_;
    std::set<uint32_t> seen_entries_;

    // Blocks that are already known to be entry points of OTHER functions
    // (so we don't absorb them into the current function during tracing)
    std::set<uint32_t> function_entries_;
};
