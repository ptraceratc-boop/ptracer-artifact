// ptdecode.h -- Intel PT instruction-flow decoding (libipt) for the PTracer offline stage.
// The "control flow log" is the raw AUX buffer saved by pt_capture2 --aux-out; the sideband JSON
// (--sideband) supplies the process memory map from which the libipt image is built: EVERY
// executable file-backed mapping is added as-is (E9Patch's loader maps the rewritten code pages
// and trampolines from the rewritten file at runtime, so /proc/PID/maps is the ground truth, not
// the ELF program headers).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <intel-pt.h>
#include "json.h"
#include "e9phase.h"

struct MapEnt { uint64_t start = 0, end = 0, off = 0; std::string perms, path; bool exec() const { return perms.size() > 2 && perms[2] == 'x'; } };
struct Sideband {
    int pid = 0, exit_code = 0; uint64_t aux_bytes = 0; bool wrapped = false; int mtc_period = 3;
    int family = 6, model = 0, stepping = 0; uint32_t cpuid15_eax = 0, cpuid15_ebx = 0;
    uint32_t time_mult = 0, time_shift = 0; uint64_t time_zero = 0;
    // TLS base of the traced (single) thread, read with ptrace by pt_capture2.  It is a process
    // constant, and it is the one piece of machine state a parallel chunk cannot re-derive from
    // its own logged values (the analyzer anchors `fs_base' once, at `main').
    uint64_t fs_base = 0;
    std::vector<MapEnt> maps;
    // ---- per-CPU capture (sideband version 2) ---------------------------------------------
    // `cpus': one entry per `pt_capture2 --cpu N' event -- that core's AUX file and the binary
    // context-switch record stream drained from its perf data ring.  Empty for a per-task
    // capture.  `threads': every thread ptrace saw in the traced process, with the TLS base the
    // kernel gave it at creation (CLONE_SETTLS); the main thread has tid == pid.
    int sb_version = 1;
    struct Cpu { int cpu = -1; std::string aux, switch_file; uint64_t aux_bytes = 0, lost = 0, trunc = 0, n_switch = 0, data_lost = 0; };
    struct Thread { uint32_t tid = 0; uint64_t fs_base = 0; };
    std::vector<Cpu> cpus; std::vector<Thread> threads;
    bool per_cpu() const { return !cpus.empty(); }
    static Sideband load(const std::string& path);
};

struct PtEvents {
    std::function<void(uint64_t payload, int size, uint64_t ip)> on_ptwrite;
    std::function<void(uint64_t tsc)> on_overflow;      // PT lost packets: state must be considered unknown
    std::function<void(uint64_t ip, bool enabled)> on_enable; // tracing enabled/disabled (e.g. kernel entry/exit)
    std::function<void(int err, uint64_t off)> on_resync; // decoder lost sync and re-synchronized at a PSB
};

// Parallel reconstruction (ptrecon --jobs N, see recon_par.cpp): a chunk = [PSB_i, PSB_j) of the
// AUX buffer, given here as [skip, end); each chunk gets its own PtDecoder (pt_insn_sync_forward
// finds the first PSB at or after `skip' via pt_config.begin/end) and its own VexInterp starting
// from an all-unknown state, and the chunk outputs are concatenated in order.  Nothing in
// PtDecoder is shared between instances.  The AUX file is mmap()ed, so N chunk decoders of the
// same file share one physical copy.
class PtDecoder {
public:
    // A decoder that starts at byte 0 covers the process's start-up and therefore begins in the
    // PRE-INIT view of every E9Patch-rewritten image (e9phase.h): the original bytes at the
    // original addresses, swapped for the patched overlay when that image's loader segment
    // executes.  A decoder that starts mid-stream (skip != 0) starts after all DT_INITs and
    // uses the patched bytes throughout.
    PtDecoder(const std::string& auxfile, const Sideband& sb, uint64_t skip = 0, uint64_t end = 0);
    // Offsets (in the AUX file) of every PSB packet, for splitting a trace into chunks.
    static std::vector<uint64_t> psb_offsets(const std::string& auxfile);
    static uint64_t aux_size(const std::string& auxfile);
    ~PtDecoder();
    // Next executed instruction. Returns false at end of trace. Events fire through `ev`.
    bool next(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev);
    uint64_t n_insn = 0, n_ovf = 0, n_resync = 0, n_ptw = 0; bool no_time = false;
    uint64_t n_sync_fail = 0;               // pt_insn_sync_forward() errors we stepped past
    uint64_t n_psb_repaired = 0;            // truncated PSB+ headers given a PSBEND
    static constexpr int kMaxResync = 64;   // consecutive sync failures before we call it the end
    int sync_fail_run_ = 0;
    const std::vector<MapEnt>& maps() const { return maps_; }
    // ---- time-aware images (e9phase.h) ------------------------------------------------------
    bool preinit() const { return preinit_; }
    uint64_t n_e9_swap = 0;            // images swapped from original to patched bytes
    uint64_t n_e9_swap_trig = 0;       // ... of them because their own loader segment executed
    uint64_t n_e9_preinit_insn = 0;    // instructions decoded while any image was still original
    bool e9_aborted = false;           // a resync ended the pre-init phase early (safety net)
    uint64_t e9_abort_off = 0;
    std::string e9_json() const;
    // Absolute offset in the AUX FILE of the decoder's current position.  Used by the chunk
    // warm-up (recon.cpp) to tell "still decoding the previous chunk's tail" from "inside my own
    // range"; libipt reports it relative to cfg_.begin, hence + skip_.
    uint64_t offset() const { uint64_t o = 0; if (dec_ && pt_insn_get_offset(dec_, &o) < 0) o = 0; return skip_ + o; }
    // Why the decode ended, for the "ended before the end of the file" diagnostic.
    const char* eos_why = "";      // "" = still running / normal end of buffer
    uint64_t eos_off = 0; int eos_status = 0;
private:
    // First PSB strictly after `off' (both relative to cfg_.begin), ~0ull if there is none.
    uint64_t next_psb_after(uint64_t off) const;
    bool step(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev);   // one decode step
    void alloc_decoder();
    uint64_t skip_ = 0;
    const uint8_t* aux_ = nullptr; size_t aux_len_ = 0; void* aux_map_ = nullptr; size_t aux_map_len_ = 0;
    struct pt_config cfg_;
    struct pt_image* image_ = nullptr;
    // ---- time-aware images (e9phase.h) ----
    E9Phases e9_; bool preinit_ = false; bool uncached_ = false; size_t n_uncached_adds_ = 0;
    size_t e9_unswapped_ = 0;
    std::vector<std::vector<E9Sec>> e9_pending_;   // withheld patched sections, per image
    int add_section(const char* path, uint64_t off, uint64_t len, uint64_t vaddr);
    void e9_swap(size_t i, uint64_t ip, bool forced = false);
    void e9_abort(uint64_t off);                   // a resync while still pre-init: swap everything
    struct pt_image_section_cache* iscache_ = nullptr;   // keeps file sections mapped (pt_image_add_file alone remaps per read: ~1M insn/s)
    struct pt_insn_decoder* dec_ = nullptr;
    std::vector<MapEnt> maps_;
    int status_ = 0; bool synced_ = false, eos_ = false;
    bool drain_events(PtEvents& ev);
};
