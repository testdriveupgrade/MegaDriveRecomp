#include "analyzer.h"

#include <cassert>
#include <cstdint>
#include <queue>
#include <set>
#include <spdlog/spdlog.h>

#include "lib/common/memory/device.h"
#include "lib/common/memory/types.h"
#include "lib/m68k/common/context.h"
#include "lib/m68k/instruction/instruction.h"
#include "lib/m68k/registers/registers.h"
#include "lib/m68k/target/target.h"
#include "lib/sega/rom_loader/rom_loader.h"

// ─────────────────────────────────────────────────────────────────────────────
// Minimal Device that reads from ROM bytes (used for decoding only)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

class RomAnalysisDevice final : public Device {
public:
    explicit RomAnalysisDevice(const std::vector<uint8_t>& rom) : rom_(rom) {}

    std::optional<Error> read(AddressType addr, MutableDataView data) override {
        for (std::size_t i = 0; i < data.size(); ++i) {
            uint32_t a = (addr + i) & 0xFFFFFF;
            data[i] = (a < rom_.size()) ? rom_[a] : 0;
        }
        return std::nullopt;
    }

    std::optional<Error> write(AddressType, DataView) override { return std::nullopt; }

private:
    const std::vector<uint8_t>& rom_;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

Analyzer::Analyzer(const std::vector<uint8_t>& rom, const RecompConfig& cfg)
    : rom_(rom), cfg_(cfg) {
    for (const auto& st : cfg_.switch_tables)
        switch_map_[st.address] = &st;
}

// ─────────────────────────────────────────────────────────────────────────────

std::map<uint32_t, RecompFunction> Analyzer::run() {
    // Seed with vector table entry points
    if (rom_.size() >= 8) {
        const auto& hdr = *reinterpret_cast<const sega::Header*>(rom_.data());
        const uint32_t reset_pc = hdr.vector_table.reset_pc.get();
        if (in_rom(reset_pc))
            enqueue_function(reset_pc, "reset");
        const uint32_t vblank_pc = hdr.vector_table.vblank_pc.get();
        if (in_rom(vblank_pc))
            enqueue_function(vblank_pc, "vblank_handler");
        const uint32_t hblank_pc = hdr.vector_table.hblank_pc.get();
        if (in_rom(hblank_pc))
            enqueue_function(hblank_pc, "hblank_handler");
    }

    // Seed switch table targets as function entry points
    for (const auto& st : cfg_.switch_tables) {
        for (uint32_t addr : read_switch_table(st)) {
            if (in_rom(addr))
                enqueue_function(addr);
        }
    }

    // Seed known functions from config
    for (const auto& kf : cfg_.known_functions)
        enqueue_function(kf.address, kf.name);

    // Process the queue
    while (!pending_.empty()) {
        auto [addr, name] = pending_.back();
        pending_.pop_back();
        trace_function(addr);
        if (!name.empty())
            functions_[addr].name = name;
    }

    return functions_;
}

// ─────────────────────────────────────────────────────────────────────────────

void Analyzer::enqueue_function(uint32_t addr, const std::string& name) {
    if (!in_rom(addr)) return;
    if (addr & 1) return; // must be word-aligned
    if (!seen_entries_.insert(addr).second) return; // already queued
    function_entries_.insert(addr);
    pending_.emplace_back(addr, name);
}

// ─────────────────────────────────────────────────────────────────────────────

void Analyzer::trace_function(uint32_t entry_addr) {
    auto& func = functions_[entry_addr];
    func.entry_addr = entry_addr;
    if (func.name.empty())
        func.name = fmt::format("func_{:06X}", entry_addr);

    std::queue<uint32_t> worklist;
    worklist.push(entry_addr);

    while (!worklist.empty()) {
        uint32_t block_start = worklist.front();
        worklist.pop();

        if (func.blocks.count(block_start)) continue;
        if (!in_rom(block_start)) continue;
        if (block_start & 1) continue;
        // If this address is another function's entry and not the current one, stop.
        if (block_start != entry_addr && function_entries_.count(block_start)) continue;

        BasicBlock block = trace_block(block_start);
        func.blocks[block_start] = block;

        // Enqueue successors
        switch (block.exit) {
        case BlockExit::Fallthrough:
            worklist.push(block.fallthrough);
            break;
        case BlockExit::Branch:
            worklist.push(block.fallthrough);
            worklist.push(block.branch_target);
            break;
        case BlockExit::Jump:
            if (block.branch_target != 0)
                worklist.push(block.branch_target);
            break;
        case BlockExit::Call:
            enqueue_function(block.call_target);
            worklist.push(block.fallthrough);
            break;
        case BlockExit::IndirectCall:
            worklist.push(block.fallthrough);
            break;
        case BlockExit::IndirectJump:
            for (uint32_t t : block.switch_targets) {
                worklist.push(t);
                enqueue_function(t);
            }
            break;
        case BlockExit::Return:
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

BasicBlock Analyzer::trace_block(uint32_t start_addr) {
    BasicBlock block;
    block.start_addr = start_addr;

    RomAnalysisDevice device(rom_);
    uint32_t addr = start_addr;

    while (true) {
        if (!in_rom(addr) || addr & 1) {
            block.exit = BlockExit::Return; // treat as terminal
            break;
        }

        m68k::Registers regs{};
        regs.pc = addr;
        regs.sr.supervisor = true;
        auto result = m68k::Instruction::decode({regs, device});
        if (!result) {
            spdlog::warn("decode error at {:06x}: {}", addr, result.error().what());
            block.exit = BlockExit::Return;
            break;
        }

        const uint32_t pc_before = addr;
        const uint32_t pc_after  = regs.pc;

        DecodedInst di;
        di.pc_before = pc_before;
        di.pc_after  = pc_after;
        di.inst      = *result;

        // Compute static EAs for PC-relative/absolute src and dst
        auto& inst = di.inst;
        using Kind = m68k::Instruction::Kind;
        if (inst.get_has_src()) {
            // For MOVE/MOVEA, data_ is the PC saved after parsing src ext words
            uint32_t src_pc = (inst.get_kind() == Kind::MoveKind ||
                               inst.get_kind() == Kind::MoveaKind)
                              ? inst.get_data()
                              : pc_after;
            di.src_ea = static_ea(inst.get_src(), src_pc);
        }
        if (inst.get_has_dst()) {
            di.dst_ea = static_ea(inst.get_dst(), pc_after);
        }

        block.insts.push_back(di);
        addr = pc_after;

        // Detect block terminators
        using Kind2 = m68k::Instruction::Kind;
        const auto kind = inst.get_kind();

        if (kind == Kind2::RtsKind || kind == Kind2::RteKind || kind == Kind2::RtrKind) {
            block.exit = BlockExit::Return;
            break;
        }

        if (kind == Kind2::BccKind) {
            const uint32_t target = branch_target(inst, pc_before, pc_after);
            if (inst.get_cond() == m68k::Instruction::TrueCond) {
                // BRA: unconditional
                block.exit = BlockExit::Jump;
                block.branch_target = target;
            } else {
                block.exit = BlockExit::Branch;
                block.branch_target = target;
                block.fallthrough   = pc_after;
            }
            break;
        }

        if (kind == Kind2::BsrKind) {
            const uint32_t target = branch_target(inst, pc_before, pc_after);
            block.exit        = BlockExit::Call;
            block.call_target = target;
            block.fallthrough = pc_after;
            break;
        }

        if (kind == Kind2::DbccKind) {
            const uint32_t target = branch_target(inst, pc_before, pc_after);
            block.exit = BlockExit::Branch;
            block.branch_target = target;
            block.fallthrough   = pc_after;
            break;
        }

        if (kind == Kind2::JmpKind) {
            using TK = m68k::Target::Kind;
            auto& dst = inst.get_dst();
            if (dst.kind() == TK::AddressKind || dst.kind() == TK::AddressIndexKind) {
                // Indirect jump — look up switch table
                // The switch table is keyed by the ROM address of the table (from config)
                // We can't resolve further at this point without knowing An
                block.exit = BlockExit::IndirectJump;
                // Populate switch_targets from all configured tables (heuristic: pick any match)
                for (const auto& [tbl_addr, st] : switch_map_) {
                    for (uint32_t t : read_switch_table(*st))
                        block.switch_targets.push_back(t);
                }
            } else {
                uint32_t target = static_jump_target(inst, pc_after);
                block.exit = BlockExit::Jump;
                block.branch_target = target;
            }
            break;
        }

        if (kind == Kind2::JsrKind) {
            using TK = m68k::Target::Kind;
            auto& dst = inst.get_dst();
            if (dst.kind() == TK::AddressKind || dst.kind() == TK::AddressIndexKind) {
                block.exit = BlockExit::IndirectCall;
            } else {
                uint32_t target = static_jump_target(inst, pc_after);
                block.exit = BlockExit::Call;
                block.call_target = target;
            }
            block.fallthrough = pc_after;
            break;
        }

        // Otherwise, continue to next instruction
        block.exit      = BlockExit::Fallthrough;
        block.fallthrough = pc_after;

        // If we've reached another known function entry, stop here
        if (pc_after != start_addr && function_entries_.count(pc_after)) {
            block.exit      = BlockExit::Jump;
            block.branch_target = pc_after;
            break;
        }
    }

    return block;
}

// ─────────────────────────────────────────────────────────────────────────────

uint32_t Analyzer::branch_target(const m68k::Instruction& inst,
                                  uint32_t pc_before, uint32_t /*pc_after*/) const {
    // For BCC/BSR/DBCC: target = pc_before + 2 + sign_extended_displacement
    // (same formula for both byte and word displacement sizes)
    int32_t disp = (inst.get_size() == m68k::Instruction::ByteSize)
                 ? (int32_t)(int8_t)(inst.get_data() & 0xFF)
                 : (int32_t)(int16_t)(inst.get_data() & 0xFFFF);
    return (uint32_t)((int32_t)(pc_before + 2) + disp);
}

uint32_t Analyzer::static_jump_target(const m68k::Instruction& inst, uint32_t pc_after) const {
    using TK = m68k::Target::Kind;
    const auto& dst = inst.get_dst();
    switch (dst.kind()) {
    case TK::AbsoluteLongKind:
        return ((uint32_t)dst.get_ext_word0() << 16) | dst.get_ext_word1();
    case TK::AbsoluteShortKind:
        return (uint32_t)(int32_t)(int16_t)dst.get_ext_word0();
    case TK::ProgramCounterDisplacementKind:
        return (uint32_t)((int32_t)pc_after - 2 + (int16_t)dst.get_ext_word0());
    default:
        return 0;
    }
}

std::vector<uint32_t> Analyzer::read_switch_table(const SwitchTable& st) const {
    std::vector<uint32_t> targets;
    for (uint32_t i = 0; i < st.count; ++i) {
        uint32_t off = st.address + i * st.entry_size;
        uint32_t target = rom_read(off, (int)st.entry_size);
        if (in_rom(target))
            targets.push_back(target);
    }
    return targets;
}

bool Analyzer::in_rom(uint32_t addr) const {
    return addr < rom_.size();
}

uint32_t Analyzer::rom_read(uint32_t addr, int bytes) const {
    uint32_t val = 0;
    for (int i = 0; i < bytes; ++i) {
        val = (val << 8) | (addr + i < rom_.size() ? rom_[addr + i] : 0);
    }
    return val;
}

uint32_t Analyzer::static_ea(const m68k::Target& t, uint32_t pc_after) const {
    using TK = m68k::Target::Kind;
    switch (t.kind()) {
    case TK::ProgramCounterDisplacementKind:
        return (uint32_t)((int32_t)pc_after - 2 + (int16_t)t.get_ext_word0());
    case TK::AbsoluteShortKind:
        return (uint32_t)(int32_t)(int16_t)t.get_ext_word0();
    case TK::AbsoluteLongKind:
        return ((uint32_t)t.get_ext_word0() << 16) | t.get_ext_word1();
    case TK::ImmediateKind:
        return t.get_address(); // address in ROM where the immediate value lives
    default:
        return 0;
    }
}
