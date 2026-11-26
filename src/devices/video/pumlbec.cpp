// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    pumlbec.cpp

    PUML C backend: a portable interpreter for PUML blocks, used where
    there is no native backend, when -drc_use_c is given, and for blocks
    a native backend refuses.  It is also the reference the native
    backends are checked against.

***************************************************************************/

#include "emu.h"
#include "puml.h"

#include "video/rgbutil.h"

#include <cmath>
#include <cstdio>


namespace puml {

namespace {

//**************************************************************************
//  EXACT CONVERSIONS
//**************************************************************************

// saturating, truncating, NaN to 0 (what arm64 FCVTZS/FCVTZU do)
u64 float_to_int(double v, u8 mode)
{
	if (v != v)
		return 0;
	switch (mode)
	{
	case CV_S32:
		if (v >= 2147483648.0) return 0x7fff'ffff;
		if (v <= -2147483649.0) return 0x8000'0000;
		return u32(s32(v));
	case CV_U32:
		if (v >= 4294967296.0) return 0xffff'ffff;
		if (v <= -1.0) return 0;
		return u32(v);
	case CV_S64:
		if (v >= 9223372036854775808.0) return u64(0x7fff'ffff'ffff'ffffULL);
		if (v < -9223372036854775808.0) return u64(0x8000'0000'0000'0000ULL);
		return u64(s64(v));
	default:
		if (v >= 18446744073709551616.0) return ~u64(0);
		if (v <= -1.0) return 0;
		return u64(v);
	}
}

template <typename T> T int_to_float(u64 v, u8 mode)
{
	switch (mode)
	{
	case CV_S32: return T(s32(u32(v)));
	case CV_U32: return T(u32(v));
	case CV_S64: return T(s64(v));
	default: return T(v);
	}
}

inline u64 load_le(const u8 *p, u32 bytes)
{
	u64 v = 0;
	for (u32 n = 0; n < bytes; n++)
		v |= u64(p[n]) << (8 * n);
	return v;
}

inline void store_le(u8 *p, u32 bytes, u64 v)
{
	for (u32 n = 0; n < bytes; n++)
		p[n] = u8(v >> (8 * n));
}


//**************************************************************************
//  INTERPRETER
//**************************************************************************

class c_compiled : public compiled
{
public:
	c_compiled(const block &b) : m_code(b.code), m_helpers(b.helpers)
	{
		backend_name = "C backend";
		m_jump.resize(m_code.size(), 0);
		std::vector<u32> stack;
		for (u32 n = 0; n < m_code.size(); n++)
		{
			switch (m_code[n].opcode)
			{
			case op::PIXEL: m_pixel = n; break;
			case op::IF: stack.push_back(n); break;
			case op::ELSE: m_jump[stack.back()] = n; stack.back() = n; break;
			case op::ENDIF: m_jump[stack.back()] = n; stack.pop_back(); break;
			default: break;
			}
		}
	}

	virtual void run(const ctx &c, frame &f) const override
	{
		f.x = c.x0;
		execute(c, f, 0, m_pixel);
		for (s32 x = c.x0; x < c.x1; x++)
		{
			f.x = x;
			execute(c, f, m_pixel + 1, u32(m_code.size()));
		}
	}

private:
	static u64 ival(const frame &f, const opnd &o, type t)
	{
		const u64 v = o.is_imm() ? o.value : f.i[o.index];
		return (t == type::I64) ? v : u64(u32(v));
	}

	static void iset(frame &f, int n, type t, u64 v)
	{
		f.i[n] = (t == type::I64) ? v : u64(u32(v));
	}

	void execute(const ctx &c, frame &f, u32 pc, u32 end) const
	{
		while (pc < end)
		{
			const inst &i = m_code[pc];
			const type t = i.t;
			const bool dbl = (t == type::F64);
			auto fa = [&] (int n) { return dbl ? f.fd(i.src[n].index) : double(f.fs(i.src[n].index)); };
			auto fs = [&] (int n) { return f.fs(i.src[n].index); };
			auto fd = [&] (int n) { return f.fd(i.src[n].index); };
			auto a = [&] () { return ival(f, i.src[0], t); };
			auto b = [&] () { return ival(f, i.src[1], t); };
			auto p = [&] (int n) { return f.p[i.src[n].index] != 0; };
			auto pset = [&] (bool v) { f.p[i.dst.index] = v ? 1 : 0; };
			const u32 shmask = (t == type::I64) ? 63 : 31;

			switch (i.opcode)
			{
			case op::NOP: case op::COMMENT: case op::PIXEL: case op::ORDER: case op::ENDIF:
				break;
			case op::IF:
				if (!p(0))
					pc = m_jump[pc];
				break;
			case op::ELSE:
				pc = m_jump[pc];
				break;
			case op::KILL:
				if (p(0))
					return;
				break;
			case op::INPUT:
			{
				u32 v = 0;
				switch (i.cond)
				{
				case IN_X: v = u32(f.x); break;
				case IN_Y: v = u32(c.y); break;
				case IN_X0: v = u32(c.x0); break;
				case IN_X1: v = u32(c.x1); break;
				default: v = c.thread; break;
				}
				f.i[i.dst.index] = v;
				break;
			}
			case op::CALL:
				m_helpers[i.src[0].value].fn(c, f, i.src[1].value);
				break;

			case op::LDI:
				if (i.dst.kind == okind::FREG)
					f.f[i.dst.index] = (t == type::F64) ? i.src[0].value : u64(u32(i.src[0].value));
				else
					iset(f, i.dst.index, t, i.src[0].value);
				break;
			case op::MOV:
				if (i.dst.kind == okind::FREG)
					f.f[i.dst.index] = f.f[i.src[0].index];
				else
					iset(f, i.dst.index, t, a());
				break;
			case op::SEL:
				if (t == type::P)
					f.p[i.dst.index] = p(0) ? f.p[i.src[1].index] : f.p[i.src[2].index];
				else if (i.dst.kind == okind::FREG)
					f.f[i.dst.index] = p(0) ? f.f[i.src[1].index] : f.f[i.src[2].index];
				else
					iset(f, i.dst.index, t, p(0) ? ival(f, i.src[1], t) : ival(f, i.src[2], t));
				break;
			case op::BITCAST:
			{
				const bool wide = (t == type::I64) || (t == type::F64);
				if (i.dst.kind == okind::IREG)
					f.i[i.dst.index] = wide ? f.f[i.src[0].index] : u64(u32(f.f[i.src[0].index]));
				else
					f.f[i.dst.index] = wide ? f.i[i.src[0].index] : u64(u32(f.i[i.src[0].index]));
				break;
			}

			case op::ADD: iset(f, i.dst.index, t, a() + b()); break;
			case op::SUB: iset(f, i.dst.index, t, a() - b()); break;
			case op::MUL: iset(f, i.dst.index, t, a() * b()); break;
			case op::AND: iset(f, i.dst.index, t, a() & b()); break;
			case op::OR:  iset(f, i.dst.index, t, a() | b()); break;
			case op::XOR: iset(f, i.dst.index, t, a() ^ b()); break;
			case op::SHL: iset(f, i.dst.index, t, a() << (b() & shmask)); break;
			case op::SHR: iset(f, i.dst.index, t, a() >> (b() & shmask)); break;
			case op::SAR:
				if (t == type::I64)
					iset(f, i.dst.index, t, u64(s64(a()) >> (b() & 63)));
				else
					iset(f, i.dst.index, t, u32(s32(u32(a())) >> (b() & 31)));
				break;
			case op::MINU: iset(f, i.dst.index, t, std::min(a(), b())); break;
			case op::MAXU: iset(f, i.dst.index, t, std::max(a(), b())); break;
			case op::MINS: case op::MAXS:
			{
				const s64 x = (t == type::I64) ? s64(a()) : s64(s32(u32(a())));
				const s64 y = (t == type::I64) ? s64(b()) : s64(s32(u32(b())));
				iset(f, i.dst.index, t, u64((i.opcode == op::MINS) ? std::min(x, y) : std::max(x, y)));
				break;
			}
			case op::SEXT:
			{
				const s64 v = (i.cond == 16) ? s64(s16(u16(a()))) : s64(s8(u8(a())));
				iset(f, i.dst.index, t, u64(v));
				break;
			}
			case op::CLZ:
			{
				const u64 v = a();
				int n = 0;
				const int bits = (t == type::I64) ? 64 : 32;
				while ((n < bits) && !BIT(v, bits - 1 - n))
					n++;
				iset(f, i.dst.index, t, n);
				break;
			}
			case op::CMP:
			{
				const u64 x = a(), y = b();
				const s64 sx = (t == type::I64) ? s64(x) : s64(s32(u32(x)));
				const s64 sy = (t == type::I64) ? s64(y) : s64(s32(u32(y)));
				bool r = false;
				switch (i.cond)
				{
				case C_EQ: r = x == y; break;
				case C_NE: r = x != y; break;
				case C_LTU: r = x < y; break;
				case C_LEU: r = x <= y; break;
				case C_GTU: r = x > y; break;
				case C_GEU: r = x >= y; break;
				case C_LT: r = sx < sy; break;
				case C_LE: r = sx <= sy; break;
				case C_GT: r = sx > sy; break;
				case C_GE: r = sx >= sy; break;
				}
				pset(r);
				break;
			}
			case op::TST: pset((a() & b()) != 0); break;
			case op::CSET: f.i[i.dst.index] = p(0) ? 1 : 0; break;

			case op::FADD: if (dbl) f.set_fd(i.dst.index, fd(0) + fd(1)); else f.set_fs(i.dst.index, fs(0) + fs(1)); break;
			case op::FSUB: if (dbl) f.set_fd(i.dst.index, fd(0) - fd(1)); else f.set_fs(i.dst.index, fs(0) - fs(1)); break;
			case op::FMUL: if (dbl) f.set_fd(i.dst.index, fd(0) * fd(1)); else f.set_fs(i.dst.index, fs(0) * fs(1)); break;
			case op::FDIV: if (dbl) f.set_fd(i.dst.index, fd(0) / fd(1)); else f.set_fs(i.dst.index, fs(0) / fs(1)); break;
			case op::FMIN:      // std::min: (b < a) ? b : a
				if (dbl) f.set_fd(i.dst.index, (fd(1) < fd(0)) ? fd(1) : fd(0)); else f.set_fs(i.dst.index, (fs(1) < fs(0)) ? fs(1) : fs(0));
				break;
			case op::FMAX:      // std::max: (a < b) ? b : a
				if (dbl) f.set_fd(i.dst.index, (fd(0) < fd(1)) ? fd(1) : fd(0)); else f.set_fs(i.dst.index, (fs(0) < fs(1)) ? fs(1) : fs(0));
				break;
			case op::FCLAMP:
				if (dbl)
				{
					const double v = fd(0), lo = fd(1), hi = fd(2);
					f.set_fd(i.dst.index, (v < lo) ? lo : (hi < v) ? hi : v);
				}
				else
				{
					const float v = fs(0), lo = fs(1), hi = fs(2);
					f.set_fs(i.dst.index, (v < lo) ? lo : (hi < v) ? hi : v);
				}
				break;
			case op::FABS: if (dbl) f.set_fd(i.dst.index, std::fabs(fd(0))); else f.set_fs(i.dst.index, std::fabs(fs(0))); break;
			case op::FNEG: if (dbl) f.set_fd(i.dst.index, -fd(0)); else f.set_fs(i.dst.index, -fs(0)); break;
			case op::FSQRT: if (dbl) f.set_fd(i.dst.index, std::sqrt(fd(0))); else f.set_fs(i.dst.index, std::sqrt(fs(0))); break;
			case op::FCMP:
			{
				const double x = fa(0), y = fa(1);     // f32 widens exactly
				bool r = false;
				switch (i.cond)
				{
				case C_EQ: r = x == y; break;
				case C_NE: r = x != y; break;
				case C_LT: r = x < y; break;
				case C_LE: r = x <= y; break;
				case C_GT: r = x > y; break;
				case C_GE: r = x >= y; break;
				}
				pset(r);
				break;
			}
			case op::FTOI:
				f.i[i.dst.index] = float_to_int(fa(0), i.cond);
				break;
			case op::ITOF:
				if (dbl)
					f.set_fd(i.dst.index, int_to_float<double>(f.i[i.src[0].index], i.cond));
				else
					f.set_fs(i.dst.index, int_to_float<float>(f.i[i.src[0].index], i.cond));
				break;
			case op::FCVT:
				if (dbl)
					f.set_fd(i.dst.index, double(f.fs(i.src[0].index)));
				else
					f.set_fs(i.dst.index, float(f.fd(i.src[0].index)));
				break;

			case op::CBILERP:
			{
				const int c = i.src[0].index;
				f.i[i.dst.index] = rgbaint_t::bilinear_filter(u32(f.i[c]), u32(f.i[c + 1]), u32(f.i[c + 2]), u32(f.i[c + 3]),
						u8(f.i[i.src[1].index]), u8(f.i[i.src[2].index]));
				break;
			}
			case op::PAND: pset(p(0) && p(1)); break;
			case op::POR:  pset(p(0) || p(1)); break;
			case op::PXOR: pset(p(0) != p(1)); break;
			case op::PNOT: pset(!p(0)); break;

			case op::LD: case op::ST:
			{
				u32 addr = u32(i.src[1].value);
				if (!i.src[0].is_none())
					addr = (addr + u32(f.i[i.src[0].index])) & c.mask[i.region];
				u8 *const ptr = c.base[i.region] + addr;
				const bool fl = (t == type::F32) || dbl;
				const u32 bytes = (t == type::F32) ? 4 : dbl ? 8 :
						(i.cond == SZ_U8 || i.cond == SZ_S8) ? 1 : (i.cond == SZ_U16 || i.cond == SZ_S16) ? 2 : (i.cond == SZ_U32) ? 4 : 8;
				if (i.opcode == op::LD)
				{
					u64 v = load_le(ptr, bytes);
					if (fl)
						f.f[i.dst.index] = v;
					else
					{
						if (i.cond == SZ_S8) v = u64(s64(s8(u8(v))));
						else if (i.cond == SZ_S16) v = u64(s64(s16(u16(v))));
						iset(f, i.dst.index, t, v);
					}
				}
				else
					store_le(ptr, bytes, fl ? f.f[i.src[2].index] : f.i[i.src[2].index]);
				break;
			}

			default:
				// refused at compile time
				break;
			}
			pc++;
		}
	}

	std::vector<inst> m_code;
	std::vector<u32> m_jump;
	std::vector<helper> m_helpers;
	u32 m_pixel = 0;
};


class c_backend : public backend
{
public:
	virtual const char *name() const override { return "C backend"; }

	virtual u32 optional_paths() const override { return 0; }

	virtual std::unique_ptr<compiled> compile(const block &b, const capabilities &, std::string &why, FILE *) override
	{
		for (const inst &i : b.code)
		{
			if (!info(i.opcode).c_impl)
			{
				why = std::string("C backend: ") + info(i.opcode).name + " not implemented yet";
				return nullptr;
			}
		}
		return std::make_unique<c_compiled>(b);
	}
};

} // anonymous namespace


std::unique_ptr<backend> make_c_backend()
{
	return std::make_unique<c_backend>();
}

} // namespace puml
