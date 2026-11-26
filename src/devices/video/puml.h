// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    puml.h

    PUML ("pixel UML"): a small code generation frontend for MAME's
    software rasterisers.  A device describes the per-pixel work for one
    draw state as a block of PUML instructions; the frontend validates it,
    disassembles and logs it if asked, and hands it to a backend that
    turns it into something that runs: native code (arm64 now, amd64
    later) or the portable C backend, which interprets it.

    Execution model (the "span" driver):
      - a block runs over one horizontal span [ctx.x0, ctx.x1) of row ctx.y
      - instructions before PIXEL run once per span (the span prologue)
      - instructions after PIXEL run once per pixel, left to right
      - KILL p abandons the current pixel and moves on to the next one
      - stores are only allowed after ORDER, which marks the start of the
        per-pixel ordered (side effect) region
      - registers live in a puml::frame; a backend may keep some of them
        in host registers (a static map, as UML's backends do), the rest
        stay in the frame ("cold"): I0-I31, F0-F31, V0-V15, P0-P15

    Arithmetic is defined exactly so that every backend gives identical
    results: no fused multiply-add, IEEE round-to-nearest single and
    double operations, FMIN/FMAX with C++ std::min/std::max NaN and zero
    behaviour, saturating float to integer conversion with NaN giving 0,
    and shift counts masked to the operand width.

***************************************************************************/

#ifndef MAME_VIDEO_PUML_H
#define MAME_VIDEO_PUML_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace puml {

//**************************************************************************
//  LIMITS
//**************************************************************************

// Registers a backend doesn't map to host registers live in the frame, so a
// generous register file costs only frame space; generators stay simple.
constexpr int REG_I = 32;           // integer registers I0-I31 (32 or 64 bits)
constexpr int REG_F = 32;           // scalar float registers F0-F31 (f32 or f64)
constexpr int REG_V = 16;           // vector registers V0-V15 (c4/f4, reserved)
constexpr int REG_P = 16;           // predicate registers P0-P15
constexpr int MAX_REGIONS = 8;      // memory regions per block
constexpr int MAX_HELPERS = 16;     // C helpers per block


//**************************************************************************
//  TYPES AND OPCODES
//**************************************************************************

enum class type : u8
{
	I32, I64,       // integers
	F32, F64,       // IEEE floats
	P,              // predicate
	C4,             // four u8 channels (reserved: packed colour ops)
	F4,             // four f32 channels (reserved: vector colour ops)
	NONE
};

// operand kinds
enum class okind : u8
{
	NONE, IREG, FREG, VREG, PREG, IMM
};

// integer and float comparison conditions (the cond field of CMP/FCMP)
enum cond : u8
{
	C_EQ, C_NE,
	C_LTU, C_LEU, C_GTU, C_GEU,     // integer only
	C_LT, C_LE, C_GT, C_GE          // signed integer, or ordered float
};

// FTOI destination / ITOF source formats (the cond field)
enum conv : u8
{
	CV_S32, CV_U32, CV_S64, CV_U64
};

// LD/ST access sizes (the cond field); F32/F64 types always move 4/8 bytes
enum msize : u8
{
	SZ_U8, SZ_U16, SZ_U32, SZ_U64, SZ_S8, SZ_S16
};

// INPUT selectors (the cond field)
enum insel : u8
{
	IN_X, IN_Y, IN_X0, IN_X1, IN_THREAD
};

// opcode list: name, number of sources, whether it has a destination,
// whether the C backend implements it, whether the arm64 backend does.
// Unimplemented ones are declared so that generators can be written and
// blocks disassembled; a backend that meets one refuses the block.
#define PUML_OPCODES(X) \
	/* control */ \
	X(NOP,      0, 0, 1, 1) \
	X(COMMENT,  0, 0, 1, 1) \
	X(PIXEL,    0, 0, 1, 1) \
	X(ORDER,    0, 0, 1, 1) \
	X(IF,       1, 0, 1, 1) \
	X(ELSE,     0, 0, 1, 1) \
	X(ENDIF,    0, 0, 1, 1) \
	X(KILL,     1, 0, 1, 1) \
	X(INPUT,    0, 1, 1, 1) \
	X(CALL,     2, 0, 1, 1) \
	/* moves */ \
	X(LDI,      1, 1, 1, 1) \
	X(MOV,      1, 1, 1, 1) \
	X(SEL,      3, 1, 1, 1) \
	X(BITCAST,  1, 1, 1, 1) \
	/* integer */ \
	X(ADD,      2, 1, 1, 1) \
	X(SUB,      2, 1, 1, 1) \
	X(MUL,      2, 1, 1, 1) \
	X(AND,      2, 1, 1, 1) \
	X(OR,       2, 1, 1, 1) \
	X(XOR,      2, 1, 1, 1) \
	X(SHL,      2, 1, 1, 1) \
	X(SHR,      2, 1, 1, 1) \
	X(SAR,      2, 1, 1, 1) \
	X(MINU,     2, 1, 1, 1) \
	X(MAXU,     2, 1, 1, 1) \
	X(MINS,     2, 1, 1, 1) \
	X(MAXS,     2, 1, 1, 1) \
	X(SEXT,     1, 1, 1, 1) \
	X(CMP,      2, 1, 1, 1) \
	X(TST,      2, 1, 1, 1) \
	X(CSET,     1, 1, 1, 1) \
	X(CLZ,      1, 1, 1, 1) \
	/* float */ \
	X(FADD,     2, 1, 1, 1) \
	X(FSUB,     2, 1, 1, 1) \
	X(FMUL,     2, 1, 1, 1) \
	X(FDIV,     2, 1, 1, 1) \
	X(FMIN,     2, 1, 1, 1) \
	X(FMAX,     2, 1, 1, 1) \
	X(FABS,     1, 1, 1, 1) \
	X(FNEG,     1, 1, 1, 1) \
	X(FSQRT,    1, 1, 1, 1) \
	X(FCLAMP,   3, 1, 1, 1)  /* std::clamp(a, lo, hi): (a < lo) ? lo : (hi < a) ? hi : a */ \
	X(FCMP,     2, 1, 1, 1) \
	X(FTOI,     1, 1, 1, 1) \
	X(ITOF,     1, 1, 1, 1) \
	X(FCVT,     1, 1, 1, 1) \
	X(FRCP,     1, 1, 0, 0)  /* reserved: table reciprocal (Voodoo, N64 RDP) */ \
	X(FLOG2,    1, 1, 0, 0)  /* reserved: table log2 for LOD (Voodoo, Model 2) */ \
	X(FEXP2,    1, 1, 0, 0)  /* reserved: fog curves (NV2A, Flipper) */ \
	/* predicates */ \
	X(PAND,     2, 1, 1, 1) \
	X(POR,      2, 1, 1, 1) \
	X(PXOR,     2, 1, 1, 1) \
	X(PNOT,     1, 1, 1, 1) \
	/* memory */ \
	X(LD,       2, 1, 1, 1) \
	X(ST,       3, 0, 1, 1) \
	X(STM,      3, 0, 0, 0)  /* reserved: masked store (write masks, PowerVR tile buffers) */ \
	/* packed colour (reserved for the integer-colour families: Voodoo, N64, Saturn, Model 2/3) */ \
	X(CUNPACK,  1, 1, 0, 0) \
	X(CPACK,    1, 1, 0, 0) \
	X(CMUL8,    2, 1, 0, 0) \
	X(CLERP8,   3, 1, 0, 0) \
	X(CADDS,    2, 1, 0, 0) \
	X(CSUBS,    2, 1, 0, 0) \
	X(CMIN,     2, 1, 0, 0) \
	X(CMAX,     2, 1, 0, 0) \
	X(DITHER,   2, 1, 0, 0)  /* reserved: 4x4 ordered dither (Voodoo, Saturn, N64) */ \
	/* vector float (reserved for the float-colour families: NV2A, Flipper, PS2 GS) */ \
	X(F4ADD,    2, 1, 0, 0) \
	X(F4MUL,    2, 1, 0, 0) \
	X(F4DOT3,   2, 1, 0, 0) \
	X(F4DOT4,   2, 1, 0, 0) \
	X(F4SPLAT,  1, 1, 0, 0) \
	X(F4EXTRACT,1, 1, 0, 0) \
	/* texturing (reserved; Rage 128 calls its existing sampler through CALL) */ \
	X(TFETCH,   3, 1, 0, 0) \
	X(TBILERP,  3, 1, 0, 0) \
	X(TPAL,     2, 1, 0, 0) \
	X(TYUV,     1, 1, 0, 0)  /* PowerVR, Flipper YUV textures */ \
	X(TMORTON,  2, 1, 0, 0)  /* swizzled layouts: NV2A, PS2, Flipper */ \
	X(REDUCE,   1, 1, 0, 0)  /* reserved: span reductions (occlusion counts) */

enum class op : u16
{
#define PUML_ENUM(name, nsrc, dst, c, a64) name,
	PUML_OPCODES(PUML_ENUM)
#undef PUML_ENUM
	COUNT
};

struct op_info
{
	const char *name;
	u8 nsrc;
	bool dst;
	bool c_impl;
	bool a64_impl;
};

const op_info &info(op o);
const char *type_name(type t);


//**************************************************************************
//  INSTRUCTIONS AND BLOCKS
//**************************************************************************

struct opnd
{
	okind kind = okind::NONE;
	u8 index = 0;
	u64 value = 0;

	static opnd none() { return opnd(); }
	static opnd i(int n) { opnd o; o.kind = okind::IREG; o.index = u8(n); return o; }
	static opnd f(int n) { opnd o; o.kind = okind::FREG; o.index = u8(n); return o; }
	static opnd v(int n) { opnd o; o.kind = okind::VREG; o.index = u8(n); return o; }
	static opnd p(int n) { opnd o; o.kind = okind::PREG; o.index = u8(n); return o; }
	static opnd imm(u64 v) { opnd o; o.kind = okind::IMM; o.value = v; return o; }
	bool is_imm() const { return kind == okind::IMM; }
	bool is_none() const { return kind == okind::NONE; }
};

struct inst
{
	op opcode = op::NOP;
	type t = type::NONE;
	u8 cond = 0;                // condition, conversion, size or selector
	u8 region = 0;              // LD/ST region
	opnd dst;
	opnd src[3];
};

// C helper called by CALL.  It sees the I and F registers in the frame: it may
// read those in its call_info's read sets and write those in its write sets.
// Predicates aren't visible to helpers.
struct frame;
struct ctx;
using helper_fn = void (*)(const ctx &c, frame &f, u64 param);

struct call_info
{
	u32 read_i = ~0U, read_f = ~0U;     // bit n: In / Fn
	u32 write_i = ~0U, write_f = ~0U;
};

struct helper
{
	helper_fn fn = nullptr;
	std::string name;
};

// a memory region a block may access; LD/ST addresses are (reg + imm) & mask,
// or a bare imm checked against size when the block is validated
struct region_desc
{
	std::string name;
	u32 size = 0;               // 0: region unused
	bool masked = false;        // addresses with a register part are masked by ctx.mask
};

class block
{
public:
	std::string name;
	std::vector<inst> code;
	std::vector<std::string> comments;
	std::vector<helper> helpers;
	std::vector<call_info> calls;       // CALL's third operand indexes this
	region_desc regions[MAX_REGIONS];

	// debugging symbols for region offsets, shown by the disassembler
	struct symbol { u8 region; u32 offset, length; std::string name; };
	std::vector<symbol> symbols;

	std::string disassemble() const;
	std::string disassemble(const inst &i) const;
	const symbol *find_symbol(u8 region, u32 offset) const;
};


//**************************************************************************
//  RUNTIME STATE
//**************************************************************************

// per-call context, filled in by the device for each span
struct ctx
{
	u8 *base[MAX_REGIONS];
	u32 mask[MAX_REGIONS];
	void *user;
	s32 x0, x1, y;
	u32 thread;
};

// register file; one per render thread
struct alignas(16) frame
{
	u64 i[REG_I];
	u64 f[REG_F];               // f32 lives in the low four bytes
	u8 p[REG_P];
	s32 x;                      // current pixel, for helpers
	u32 pad[3];
	u64 v[REG_V][2];

	float fs(int n) const { float v; std::memcpy(&v, &f[n], sizeof(v)); return v; }
	double fd(int n) const { double v; std::memcpy(&v, &f[n], sizeof(v)); return v; }
	void set_fs(int n, float v) { std::memcpy(&f[n], &v, sizeof(v)); }
	void set_fd(int n, double v) { std::memcpy(&f[n], &v, sizeof(v)); }
};

// a compiled block, ready to run
class compiled
{
public:
	virtual ~compiled() = default;
	virtual void run(const ctx &c, frame &f) const = 0;
	const char *backend_name = "";
};


//**************************************************************************
//  BUILDER
//**************************************************************************

// Generators use this to write blocks.  Register allocation is a simple stack
// per class: alloc_*() takes the next free register, mark()/release() free
// temporaries.  Macros (tile_address, clamp01, ...) expand to primitives.
class builder
{
public:
	builder(block &b) : m_block(b) { }

	// registers
	struct regmark { int i, f, p; };
	int alloc_i();
	int alloc_f();
	int alloc_p();
	regmark mark() const { return regmark{ m_next_i, m_next_f, m_next_p }; }
	regmark peak() const { return regmark{ m_peak_i, m_peak_f, m_peak_p }; }     // high-water marks
	void release(const regmark &m) { m_next_i = m.i; m_next_f = m.f; m_next_p = m.p; }
	bool overflow() const { return m_overflow; }

	// declarations
	void region(int n, const char *name, u32 size, bool masked);
	void symbol(int region, u32 offset, u32 length, const char *name);
	int add_helper(helper_fn fn, const char *name);

	// raw emission
	inst &emit(op o, type t, opnd dst, opnd a = opnd(), opnd b = opnd(), opnd c = opnd(), u8 cond = 0);
	void comment(const std::string &text);

	// control
	void pixel() { emit(op::PIXEL, type::NONE, opnd()); }
	void order() { emit(op::ORDER, type::NONE, opnd()); }
	void if_(int p) { emit(op::IF, type::P, opnd(), opnd::p(p)); }
	void else_() { emit(op::ELSE, type::NONE, opnd()); }
	void endif() { emit(op::ENDIF, type::NONE, opnd()); }
	void kill(int p) { emit(op::KILL, type::P, opnd(), opnd::p(p)); }
	void input(int dst, insel sel) { emit(op::INPUT, type::I32, opnd::i(dst), opnd(), opnd(), opnd(), sel); }
	void call(int index, u64 param, const call_info &regs = call_info());

	// integer (b may be a register or immediate)
	void ldi(int dst, u32 v) { emit(op::LDI, type::I32, opnd::i(dst), opnd::imm(v)); }
	void mov(int dst, int src) { emit(op::MOV, type::I32, opnd::i(dst), opnd::i(src)); }
	void alu(op o, int dst, int a, opnd b, type t = type::I32) { emit(o, t, opnd::i(dst), opnd::i(a), b); }
	void alui(op o, int dst, int a, u64 b, type t = type::I32) { emit(o, t, opnd::i(dst), opnd::i(a), opnd::imm(b)); }
	void cmp(cond c, int p, int a, opnd b, type t = type::I32) { emit(op::CMP, t, opnd::p(p), opnd::i(a), b, opnd(), c); }
	void tst(int p, int a, opnd b) { emit(op::TST, type::I32, opnd::p(p), opnd::i(a), b); }
	void cset(int dst, int p) { emit(op::CSET, type::I32, opnd::i(dst), opnd::p(p)); }
	void sel(int dst, int p, opnd a, opnd b) { emit(op::SEL, type::I32, opnd::i(dst), opnd::p(p), a, b); }

	// float (f32 unless given)
	void fldi(int dst, float v);
	void fldi64(int dst, double v);
	void fmov(int dst, int src, type t = type::F32) { emit(op::MOV, t, opnd::f(dst), opnd::f(src)); }
	void fop(op o, int dst, int a, int b, type t = type::F32) { emit(o, t, opnd::f(dst), opnd::f(a), opnd::f(b)); }
	void fop1(op o, int dst, int a, type t = type::F32) { emit(o, t, opnd::f(dst), opnd::f(a)); }
	void fcmp(cond c, int p, int a, int b, type t = type::F32) { emit(op::FCMP, t, opnd::p(p), opnd::f(a), opnd::f(b), opnd(), c); }
	void fsel(int dst, int p, int a, int b, type t = type::F32) { emit(op::SEL, t, opnd::f(dst), opnd::p(p), opnd::f(a), opnd::f(b)); }
	void ftoi(int dst, int src, conv c, type from = type::F32) { emit(op::FTOI, from, opnd::i(dst), opnd::f(src), opnd(), opnd(), c); }
	void itof(int dst, int src, conv c, type to = type::F32) { emit(op::ITOF, to, opnd::f(dst), opnd::i(src), opnd(), opnd(), c); }
	void fcvt(int dst, int src, type to) { emit(op::FCVT, to, opnd::f(dst), opnd::f(src)); }

	// predicates
	void pop(op o, int dst, int a, int b) { emit(o, type::P, opnd::p(dst), opnd::p(a), opnd::p(b)); }
	void pnot(int dst, int a) { emit(op::PNOT, type::P, opnd::p(dst), opnd::p(a)); }

	// memory: address is (reg + offset) & region mask, or just offset with reg < 0
	void ld(int dst, int region, int reg, u32 offset, msize sz);
	void fld(int dst, int region, int reg, u32 offset, type t = type::F32);
	void st(int region, int reg, u32 offset, int src, msize sz);

	// macros
	void clamp01(int dst, int src, int zero, int one);
	void fmin_c(int dst, int a, int b) { fop(op::FMIN, dst, a, b); }
	void tile_address(int dst, int xb, int y, int pitch, int tile_w_l2, int tile_h_l2);
	void linear_address(int dst, int xb, int y, int pitch);
	void morton_address(int dst, int x, int y);     // stub: emits TMORTON

private:
	block &m_block;
	int m_next_i = 0, m_next_f = 0, m_next_p = 0;
	int m_peak_i = 0, m_peak_f = 0, m_peak_p = 0;
	bool m_overflow = false;
};


//**************************************************************************
//  VALIDATION AND BACKENDS
//**************************************************************************

// checks structure, types and register ranges; returns an empty string if OK
std::string validate(const block &b);

// What the host offers, tracked by the mid-end (puml::jit) only: backends get
// the enabled set when they compile, devices may query it, and nothing else
// asks the CPU.  Optional paths are only ever used when enabled; a mask can
// switch detected ones off so every fallback can be tested on any machine.
struct capabilities
{
	enum : u32
	{
		// arm64 optional extension paths
		A64_DOTPROD = 1U << 0,      // packed colour multiplies
		A64_I8MM    = 1U << 1,
		A64_FP16    = 1U << 2,      // half-float textures and colour
		A64_FRINTTS = 1U << 3,
		A64_LSE     = 1U << 4,      // atomic counters (REDUCE)
		A64_SVE     = 1U << 5,      // whole-span execution
		A64_SVE2    = 1U << 6,

		// amd64 optional extension paths (the amd64 backend's baseline is SSE4.1)
		X64_AVX2    = 1U << 16,
		X64_BMI2    = 1U << 17,
		X64_F16C    = 1U << 18,
		X64_AVX512F = 1U << 19,
		X64_AVX512BW = 1U << 20,
		X64_GFNI    = 1U << 21,

		// GPGPU: reserved.  Never enabled unless a device asks for it and a
		// GPGPU backend is compiled in (none is), so software stays the default.
		GPGPU       = 1U << 31
	};

	std::string cpu;                        // vendor and brand
	unsigned threads = 0;
	std::vector<std::string> features;      // everything the host reports
	u32 detected = 0;                       // optional paths the host could run
	u32 enabled = 0;                        // detected, minus the config mask
	std::vector<std::string> compute_devices;   // GPGPU devices found (none: detection not compiled in)

	bool has(u32 path) const { return (enabled & path) == path; }
	static std::string names(u32 paths);
};

capabilities detect_host();

class backend
{
public:
	virtual ~backend() = default;
	virtual const char *name() const = 0;

	// optional paths this backend has code for (a subset of capabilities)
	virtual u32 optional_paths() const = 0;

	// nullptr with a reason if the block can't be compiled by this backend
	virtual std::unique_ptr<compiled> compile(const block &b, const capabilities &caps, std::string &why, FILE *native_log) = 0;
};

std::unique_ptr<backend> make_c_backend();
std::unique_ptr<backend> make_arm64_backend();     // nullptr where unavailable
bool arm64_backend_available();


//**************************************************************************
//  THE FRONTEND
//**************************************************************************

// options taken from MAME's -drc_* options (read only) by the device
struct config
{
	bool force_c = false;           // -drc_use_c
	bool log_ir = false;            // -drc_log_uml: IR disassembly to puml_<tag>.asm
	bool log_native = false;        // -drc_log_native: host code to puml_<backend>_<tag>.asm
	std::string tag;                // device shortname and tag, for log file names
	size_t max_blocks = 4096;
	u32 disable_paths = 0;          // capabilities to switch off (testing fallbacks)
	u32 request_paths = 0;          // opt-in paths the device asks for (GPGPU)
	std::function<void (const std::string &)> log;      // device logerror
};

class jit
{
public:
	jit(const config &cfg);
	~jit();

	const char *backend_name() const;
	const capabilities &caps() const { return m_caps; }

	// blocks are keyed by an opaque byte string built by the device from its
	// draw state's modes (not its data)
	const compiled *find(const std::string &key) const;
	const compiled *add(const std::string &key, block &&b);
	bool full() const { return m_blocks.size() >= m_cfg.max_blocks; }

	// drop every block; the caller guarantees no block is running
	void flush();

	// statistics
	struct stats { u64 compiled = 0, native = 0, fallback_c = 0, refused = 0, flushes = 0, lookups = 0, hits = 0; };
	const stats &statistics() const { return m_stats; }
	void log_statistics();
	void log(const std::string &text) const { if (m_cfg.log) m_cfg.log(text); }

private:
	config m_cfg;
	capabilities m_caps;
	std::unique_ptr<backend> m_native, m_c;
	std::unordered_map<std::string, std::unique_ptr<compiled>> m_blocks;
	mutable stats m_stats;
	FILE *m_ir_log = nullptr, *m_native_log = nullptr;
};

} // namespace puml

#endif // MAME_VIDEO_PUML_H
