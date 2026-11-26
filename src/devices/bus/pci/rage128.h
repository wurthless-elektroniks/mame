// license:BSD-3-Clause
// copyright-holders:<author>
#ifndef MAME_BUS_PCI_RAGE128_H
#define MAME_BUS_PCI_RAGE128_H

#pragma once

#include "pci_slot.h"

#include "video/pc_vga.h"
#include "video/puml.h"

#include "screen.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


// VGA core of the Rage 128.  The palette, DAC mask and VGA aperture banking are shared
// with the native display controller, so this subclass exposes them to the card.
class ati_rage128_vga_device : public svga_device
{
public:
	ati_rage128_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	virtual uint8_t mem_r(offs_t offset) override;
	virtual void mem_w(offs_t offset, uint8_t data) override;

	u8 *vram() { return vga.memory.get(); }
	u32 vram_size() const { return u32(vga.svga_intf.vram_size); }

	// shared DAC: two 256-entry palettes, the second is for CRTC2
	u32 palette_entry(int pal, int index) const { return m_palette[pal & 1][index & 0xff]; }
	void palette_write_index(u8 data);
	void palette_read_index(u8 data);
	u8 palette_write_index_r() const { return m_dac_windex; }
	u8 palette_read_index_r() const { return m_dac_rindex; }
	void palette_data_w(u32 data);
	u32 palette_data_r();
	void set_dac_cntl(u32 data);
	u8 dac_mask() const { return m_dac_mask; }

	void set_banks(u32 wp_sel, u32 rp_sel) { m_wp_sel = wp_sel; m_rp_sel = rp_sel; }
	void set_native(bool native);
	u8 vga_mode_vblank() { return vga_vblank(); }

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// vga_device implementation
	virtual void io_3cx_map(address_map &map) override ATTR_COLD;
	virtual void palette_update() override;
	virtual void recompute_params() override;

private:
	u8 dac_mask_r();
	void dac_mask_w(u8 data);
	u8 dac_state_r();
	void dac_read_index_w(u8 data);
	u8 dac_write_index_r();
	void dac_write_index_w(u8 data);
	u8 dac_data_r();
	void dac_data_w(u8 data);

	u32 bank_offset(offs_t offset, u32 sel) const;

	u32 m_palette[2][256];
	u8 m_dac_windex, m_dac_rindex, m_dac_state, m_dac_mask;
	u8 m_dac_loading[3];
	bool m_dac_reading;
	bool m_dac_8bit;
	bool m_dac_pal2;
	u32 m_wp_sel, m_rp_sel;
	bool m_native;
};


class ati_rage128_device : public pci_card_device
{
public:
	// 2D engine, display, overlay and CCE are emulated; the 3D pipeline is approximate
	static constexpr feature_type imperfect_features() { return feature::GRAPHICS; }

	void set_vram_size(u32 size) { m_vram_size = size; }

protected:
	enum : u32
	{
		CHIP_AGP        = 1U << 0,  // AGP interface (otherwise 33/66 MHz PCI)
		CHIP_AGP4X      = 1U << 1   // AGP 4x capable (Rage 128 Pro)
	};

	ati_rage128_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock,
			u16 device_id, u32 subsystem, u32 flags, u32 vram_size);

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_config_complete() override ATTR_COLD;
	virtual void device_post_load() override ATTR_COLD;
	virtual void device_pre_save() override ATTR_COLD;
	virtual void device_stop() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;

	// pci_device implementation
	virtual void map_extra(uint64_t memory_window_start, uint64_t memory_window_end, uint64_t memory_offset, address_space *memory_space,
			uint64_t io_window_start, uint64_t io_window_end, uint64_t io_offset, address_space *io_space) override;
	virtual void config_map(address_map &map) override ATTR_COLD;
	virtual uint8_t capptr_r() override;

private:
	// a pixel walker for host data transfers that are waiting to be fed
	struct host_xfer
	{
		bool active;
		bool mono;
		bool byte_aligned;
		s32 x, y;               // current destination pixel
		s32 start_x;
		s32 col, row;           // position within the rectangle
		s32 width, height;
		s32 x_step, y_step;
		u32 byte_acc;           // byte accumulator for 24bpp colour transfers
		u8 byte_count;
		u32 span_lo, span_hi;   // VRAM the rectangle can reach, for waiting on 3D
	};

	// a resolved 3D vertex
	struct vertex3d
	{
		float x, y, z, w;
		float r, g, b, a;       // 0..255
		float sr, sg, sb, sf;   // specular and fog
		float s0, t0, s1, t1;
	};

	// A resolved texture unit.  Levels live in offset slots 0-10: the largest level
	// is in slot TEX_SIZE - TEX_MIN_SIZE and each lower slot holds the next smaller
	// one, down to TEX_MIN_SIZE in slot 0.
	struct texunit
	{
		static constexpr int LEVELS = 11;

		bool enabled;
		bool sec_st;            // secondary unit reading the second coordinate set
		bool linear_mag;
		bool mip_disable;
		bool need_lod;          // the minification and magnification filters differ
		int min_filter;
		int fmt, pal_fmt;
		int s_mode, t_mode;
		int top;                // slot of the largest level
		int width_l2, height_l2;    // size of the largest level
		u32 cntl, combine;
		u32 offset[LEVELS];
		u32 border;
	};

	// 3D state, captured once per primitive packet rather than read back per pixel
	enum : int
	{
		SHADE_SOLID = 0,
		SHADE_FLAT,
		SHADE_GOURAUD
	};

	struct raster_state
	{
		texunit tex[2];
		bool textured;
		bool perspective;
		bool premultiply;       // the setup engine multiplies S and T by RHW
		u32 tex_amask;          // forced texture alpha when TEX_MAP_ALPHA_IN_TEXTURE is clear
		int shade;              // SHADE_SOLID, SHADE_FLAT or SHADE_GOURAUD
		int flat_vertex;        // vertex giving the colour in flat shading
		u32 solid_color;        // CONSTANT_COLOR_C, for solid shading
		u32 fpu_setup;

		u32 dst_base, dst_pitch;
		int dst_type, dst_bpp;
		bool dst_tiled;
		u32 write_mask;
		s32 sc_left, sc_top, sc_right, sc_bottom;
		u32 aux_cntl;
		s32 aux[3][4];
		bool stipple;
		int stipple_x, stipple_y;
		u32 stipple_rows[32];

		u32 tex_cntl, misc;
		bool z_test, z_write, stencil;
		int z_func, z_width, z_bytes, z_shift;
		bool z_tiled;
		u32 z_base, z_pitch, z_max;     // z_pitch in bytes
		u8 sten_ref, sten_mask, sten_wmask;
		int sten_func, sten_fail_op, sten_zpass_op, sten_zfail_op;

		float const_r, const_g, const_b, const_a;
		bool specular, fog;
		float fog_r, fog_g, fog_b;
		bool alpha_test;
		int alpha_func;
		u32 alpha_ref;
		bool blend, blend_subtract;
		int blend_src, blend_dst;
		bool chroma_key;
		bool alpha_mask;        // TEX_CNTL_C bit 13: texels with alpha 0 aren't drawn
		u8 clr_cmp_fn;          // MISC_3D_STATE_CNTL_REG CLR_CMP_FCN_3D
		u32 clr_cmp_clr, clr_cmp_msk;
		u32 chroma_clr, chroma_msk;

		bool sync;              // samples system memory, so can't leave the emulation thread
		u32 palette[256];       // texture palette, for palettised textures only
	};

	// what a triangle's PUML block reads (region TRI): the planes of draw_triangle,
	// with row holding this row's values at x = 0
	struct puml_tri
	{
		static constexpr int PLANES = 14;
		float row[PLANES];
		float dx[PLANES], dy[PLANES];
		float tex_w[2], tex_h[2];       // largest level, for the level of detail
		// the largest level as sample_level() addresses it, for blocks that fetch texels themselves
		u32 tex_vbase[2], tex_pitch[2]; // VRAM byte address, bytes per line (16-bit texels)
		u32 tex_wi[2], tex_hi[2];       // size in texels
		u32 tex_direct[2];              // 16-bit and wholly in VRAM, so read directly
	};

	// 3D primitives waiting for the render threads, each with its state
	enum : u8
	{
		PRIM_TRIANGLE = 0,
		PRIM_LINE,
		PRIM_POINT,
		PRIM_FILL               // a 2D fill; state indexes the batch's fills
	};

	struct queued_prim
	{
		u8 kind;
		u32 state;
		vertex3d v[3];
	};

	// A few spans of VRAM byte addresses, kept apart so that the colour buffer,
	// the Z buffer and the textures don't merge into one range covering the
	// command buffers between them.  Past MAX spans, the one that grows least
	// absorbs the new one.
	struct vram_ranges
	{
		static constexpr int MAX = 4;
		u32 lo[MAX], hi[MAX];
		int count = 0;

		void add(u32 start, u32 end)
		{
			int best = 0;
			u32 best_growth = ~0U;
			for (int i = 0; i < count; i++)
			{
				// touching or overlapping spans merge for nothing
				const u32 growth = ((start <= hi[i]) && (end >= lo[i])) ? 0 : ((std::max(hi[i], end) - std::min(lo[i], start)) - (hi[i] - lo[i]));
				if (growth < best_growth)
				{
					best = i;
					best_growth = growth;
				}
			}
			if (best_growth && (count < MAX))
			{
				lo[count] = start;
				hi[count] = end;
				count++;
				return;
			}
			lo[best] = std::min(lo[best], start);
			hi[best] = std::max(hi[best], end);
		}

		bool overlaps(u32 start, u32 end) const
		{
			for (int i = 0; i < count; i++)
				if ((start < hi[i]) && (end > lo[i]))
					return true;
			return false;
		}

		bool overlaps(const vram_ranges &other) const
		{
			for (int i = 0; i < other.count; i++)
				if (overlaps(other.lo[i], other.hi[i]))
					return true;
			return false;
		}
	};

	// A SCALING packet's scaler block, decoded.  fast means plot() reduces to
	// the colour compare and write mask inside the clip rectangle.
	struct scale_job
	{
		bool replicate, blend, raw, fast, direct16, compare;
		u32 wmask;
		int src_fmt, sf, df, dtype, bpp;
		u32 src_base, src_pitch;
		int sx0, sy0;
		u32 x_phase, y_phase, x_inc, y_inc;
		s32 dst_x, dst_y;
		int w, h;
		u32 dpitch;
		u32 dst_offset;
		bool dst_tile;
		s32 clip_l, clip_r, clip_t, clip_b;
	};

	// destination rows [first, last) of a scale job for one render thread
	struct scale_band
	{
		ati_rage128_device *device;
		const scale_job *job;
		int first, last;
	};

	// A 2D rectangle fill reduced to a constant source and pattern, as
	// make_fill() works it out: plot() for every pixel inside the scissor.
	struct fill_job
	{
		u32 base, pitch;
		int bpp;
		bool tiled;
		u8 rop;
		u32 pat, src, wmask, value;
		bool uses_dst, read_dst;
		s32 left, right, top, bottom;
		int x_step, y_step;
	};

	// A colour HOSTDATA_BLT whose pixels all came in one packet: what
	// host_data_w() and plot() do, from a copy of the data.
	struct host_job
	{
		u32 base, pitch;
		int bpp;
		bool tiled, swap;
		u8 rop;
		u32 pat, wmask;
		bool read_dst;
		s32 x0, y0;
		int w, h;
		s32 left, right, top, bottom;
		std::vector<u32> data;
	};

	// A blit from a colour source in memory, as copy_rect() works it out: the
	// columns and rows whose destination is inside the scissor, and what plot()
	// does to each pixel.
	struct copy_job
	{
		u32 src_base, spitch, dst_base, pitch;
		int bpp;
		bool src_tiled, dst_tiled;
		u8 rop;
		u32 pat, wmask;
		bool compare, read_dst;
		s32 dst_x, dst_y, src_x, src_y;
		int x_step, y_step;
		int col0, col1, row0, row1;
		s32 src_right, src_bottom;
	};

	// LOG_PROBE: what happened to one pixel, recorded by the render threads
	struct probe_event
	{
		char why;               // z: depth fail, k: keyed or masked out, a: alpha test, w: written
		u32 misc, tex_cntl, tex_base;
		float s, t;
		u32 texel, old, res, z, zstored;
	};

	struct render_batch;

	// One render work item: rows in 16-line stripes are dealt out to the
	// bands round robin, so the bands never share a byte of a linear or
	// tiled surface and each sees its primitives in order.  A scale job's
	// rows are split into contiguous runs instead.
	struct render_band
	{
		ati_rage128_device *device;
		render_batch *batch;
		int index;
		int count;
	};

	// Primitives with the states they use, or one scale job, and the VRAM
	// they read (textures, the scaler source) and write (colour, Z and
	// stencil, the scaler destination).  Batches render one after another,
	// each started by the last band of the one before; done tells the
	// emulation thread that it may reuse the batch.
	struct render_batch
	{
		std::vector<raster_state> states;
		std::vector<const puml::compiled *> blocks;     // each state's PUML block, or nullptr
		std::vector<u32> perf_ids;          // LOG_PERF: each state's statistics slot
		std::vector<queued_prim> prims;
		vram_ranges reads, writes;
		std::vector<fill_job> fills;
		vram_ranges fill_writes;    // what the fills write, which other bands mustn't read
		bool is_scale = false, is_host = false, is_copy = false;
		scale_job scale;
		host_job host;
		copy_job copy;              // a present: a copy of a 3D frame to the screen
		render_band bands[16];
		std::atomic<int> bands_left{ 0 };
		std::atomic<bool> done{ false };
		osd_ticks_t launched = 0;           // LOG_PERF: when the first band started
		std::atomic<u64> band_ticks{ 0 };   // LOG_PERF: time the bands spent working
		std::atomic<u64> band_cpu_ns{ 0 };  // LOG_PERF: thread CPU time the bands got while working

		bool empty() const { return prims.empty() && !is_scale && !is_host && !is_copy; }
		void clear() { states.clear(); blocks.clear(); perf_ids.clear(); band_ticks.store(0); band_cpu_ns.store(0); prims.clear(); fills.clear(); fill_writes.count = 0; reads.count = 0; writes.count = 0; is_scale = is_host = is_copy = false; host.data.clear(); done.store(false); }
	};

	// PCI GART translations, valid for one command buffer or primitive
	struct gart_entry
	{
		u32 tag;                // (page << 1) | 1 when valid
		u32 bus;
	};

	// bus master clients, which BM_CHUNK_0_VAL can route through the PCI GART separately
	enum bm_client
	{
		BM_DATA,                // textures and vertex buffers
		BM_PM4,                 // command fetch: ring and indirect buffers
		BM_PTR                  // ring read pointer writeback
	};

	static constexpr u32 MMIO_SIZE = 0x4000;       // register file
	static constexpr u32 REG_BAR_SIZE = 0x8000;    // BAR2 decode, the registers twice; see the TODO list
	static constexpr u32 PACKET_MAX = 0x4000;
	static constexpr u32 I2C_FIFO_SIZE = 16;
	static constexpr u32 GART_CACHE_SIZE = 64;
	static constexpr size_t RENDER_BATCH_PRIMS = 2048;   // queued primitives that start a render

	required_device<ati_rage128_vga_device> m_vga;
	required_device<screen_device> m_screen;
	required_memory_region m_bios;
	required_ioport m_monitor;

	const u32 m_chip_flags;
	u32 m_vram_size;
	u8 *m_vram;
	u32 m_vram_mask;

	// register file (byte offset / 4) for everything that doesn't need decoding on access
	u32 m_regs[MMIO_SIZE / 4];
	u32 m_pll[0x40];

	// PCI capabilities
	u32 m_agp_command;
	u16 m_pmcsr;

	// display
	u32 m_crtc_offset_latched;
	u32 m_frame_count;
	u32 m_int_status;
	bool m_crtc_vblank_save;
	bool m_irq_state;
	bool m_native_mode;
	s32 m_cfg_htotal, m_cfg_vtotal, m_cfg_hdisp, m_cfg_vdisp;  // last screen configuration, rebuilt after loading
	u32 m_cfg_pclk;
	emu_timer *m_vline_timer;

	// cursor
	u32 m_cursor_offset;

	// DDC monitor (an I2C EEPROM at 0x50 holding a generated EDID block)
	u8 m_edid[128];
	u8 m_ddc_scl, m_ddc_sda_in, m_ddc_sda_out;
	u8 m_ddc_state, m_ddc_bits, m_ddc_shift, m_ddc_ptr;
	bool m_ddc_read;
	u32 m_i2c_cntl0, m_i2c_cntl1;
	u8 m_i2c_fifo[I2C_FIFO_SIZE];
	u8 m_i2c_fifo_count, m_i2c_fifo_rd;

	// GUI engine state
	u32 m_dst_offset, m_dst_pitch, m_src_offset, m_src_pitch;
	bool m_dst_tile, m_src_tile;
	s32 m_dst_x, m_dst_y, m_src_x, m_src_y;
	u32 m_dst_width, m_dst_height;
	s32 m_sc_left, m_sc_right, m_sc_top, m_sc_bottom;
	s32 m_src_sc_right, m_src_sc_bottom;
	u32 m_gmc;
	u32 m_dp_cntl;
	u32 m_dp_datatype;
	u32 m_dp_mix;
	host_xfer m_host;

	// CCE / PM4
	u32 m_pm4_microcode[256][2];
	u32 m_pm4_ucode_addr, m_pm4_ucode_raddr;
	u32 m_packet[PACKET_MAX];
	u32 m_packet_count;
	u32 m_packet_header;
	u32 m_packet_remaining;
	int m_cce_depth;            // recursion guard, always 0 between accesses
	u32 m_tex_palette[256];
	u8 m_tex_pal_index;         // TEX_PALETTE_WR_INDEX, advanced by TEX_PALETTE_WR
	gart_entry m_gart_cache[GART_CACHE_SIZE];

	// 3D
	raster_state m_rs;
	std::vector<vertex3d> m_verts;
	osd_work_queue *m_render_queue;
	int m_band_count;                   // render work items per batch
	std::unique_ptr<render_batch> m_pending;    // being queued by the emulation thread
	std::deque<std::unique_ptr<render_batch>> m_outstanding;    // submitted, oldest first
	std::vector<std::unique_ptr<render_batch>> m_spare;
	std::mutex m_chain_lock;            // guards m_chain and m_chain_busy
	std::deque<render_batch *> m_chain; // submitted but not started
	bool m_chain_busy;                  // a batch is rendering
	std::shared_mutex m_scanout_lock;   // queued presents hold it shared, screen_update() exclusive
	std::mutex m_probe_lock;
	std::vector<probe_event> m_probe;
	u32 m_probe_frame;
	bool m_tile_warned;
	u64 m_perf_prims, m_perf_batches, m_perf_waits, m_perf_sync;
	std::unique_ptr<puml::jit> m_puml;  // pixel pipeline code generation
	std::unordered_set<std::string> m_puml_refused;     // keys the generator or backends turned down
	std::atomic<u32> m_puml_mismatches; // PUML_VERIFY differences
	std::vector<std::string> m_puml_verify_log;         // guarded by m_probe_lock
	osd_ticks_t m_perf_wait_ticks;
	u32 m_perf_second;

	// LOG_PERF detail: why batches were submitted and waited for (by call site),
	// how busy the render threads were, and where their time went by state
	struct perf_site
	{
		const char *file;
		u32 line;
		std::string func;
		u32 kicks, waits;
		u64 kick_prims;
		osd_ticks_t wait_ticks;
	};
	static constexpr u32 PERF_STATES = 1024;    // the last slot collects the rest
	std::vector<perf_site> m_perf_sites;
	std::unordered_map<std::string, u32> m_perf_state_ids;
	std::vector<std::string> m_perf_state_desc;
	std::atomic<u64> m_perf_state_ticks[PERF_STATES];
	std::atomic<u64> m_perf_state_pixels[PERF_STATES];
	std::atomic<u64> m_perf_state_prims[PERF_STATES];
	std::atomic<u64> m_perf_fill_ticks, m_perf_band_busy, m_perf_band_slots, m_perf_batch_wall, m_perf_band_cpu_ns;
	u32 m_perf_flips, m_perf_present_blits, m_perf_last_vblank;     // frames the guest shows
	u32 m_perf_presents, m_perf_throttled, m_perf_queued;

	// present throttle (a hack standing in for 3D engine timing): the bytes the
	// last 3D state draws into, and the vblank count at the last present
	u32 m_3d_dst_lo, m_3d_dst_hi;
	u32 m_present_frame;
	bool m_surf_on;             // a SURFACEn window is enabled
	bool m_dst3d_tile_logged;
	u32 m_zpitch_logged;

	// register access
	u32 mmio_r(offs_t offset, u32 mem_mask = ~0);
	void mmio_w(offs_t offset, u32 data, u32 mem_mask = ~0);
	u32 reg_r(u32 reg, u32 mem_mask);
	void reg_w(u32 reg, u32 data, u32 mem_mask);
	u32 config_mirror_r(u32 reg);

	// framebuffer apertures and address maps
	u32 fb_r(offs_t offset, u32 mem_mask = ~0);
	void fb_w(offs_t offset, u32 data, u32 mem_mask = ~0);
	u32 aperture_r(u32 byte_addr, bool surfaces);
	void aperture_w(u32 byte_addr, u32 data, u32 mem_mask, bool surfaces);
	u32 surface_xlate(u32 addr) const;
	void update_surfaces();
	void fb_map(address_map &map) ATTR_COLD;
	void mmio_map(address_map &map) ATTR_COLD;
	void io_bar_map(address_map &map) ATTR_COLD;
	void legacy_io_map(address_map &map) ATTR_COLD;
	u8 vga_mem_r(offs_t offset);
	void vga_mem_w(offs_t offset, u8 data);

	// PLL and display timing
	u32 pll_r(int index);
	void pll_w(int index, u32 data);
	double pixel_clock() const;
	void update_mode();
	void update_irq();
	void vblank_w(int state);
	TIMER_CALLBACK_MEMBER(vline_tick);
	void arm_vline_timer();
	bool display_enabled() const;
	u32 screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void draw_native(bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void draw_overlay(bitmap_rgb32 &bitmap, const rectangle &cliprect, int bits, u32 base, u32 pitch);
	void draw_cursor(bitmap_rgb32 &bitmap, const rectangle &cliprect);

	// DDC / I2C / GPIO
	void build_edid() ATTR_COLD;
	void ddc_update();
	void ddc_clock(int scl, int sda);
	int monid_pins();
	void i2c_go();

	// GUI engine
	void gui_reset();
	void gmc_w(u32 data);
	u32 gmc_r() const;
	int dst_bpp() const;
	u32 dst_pitch_bytes() const;
	u32 src_pitch_bytes() const;
	u32 read_pixel(u32 base, u32 pitch, int bpp, int x, int y, bool tiled = false) const;
	void write_pixel(u32 base, u32 pitch, int bpp, int x, int y, u32 data, bool tiled = false);
	bool clip_test(int x, int y) const;
	bool pattern_pixel(int x, int y, u32 &color, int line_index) const;
	bool color_compare_pass(u32 src, u32 dst) const;
	void plot(int x, int y, u32 src, bool src_valid, int line_index);
	void gui_trigger_rect();
	bool make_fill(int w, int h, int x_step, int y_step, u32 src, fill_job &f, bool &draws);
	void do_fill(const fill_job &f, int band, int bands);
	bool queue_host_blit(const u32 *data, u32 avail);
	void host_rows(const host_job &j, int first, int last);
	bool copy_rect(int w, int h, int x_step, int y_step);
	bool make_copy(int w, int h, int x_step, int y_step, copy_job &j) const;
	void copy_rows(const copy_job &j, int band, int bands);
	void queue_copy(const copy_job &j, u32 src_lo, u32 src_hi, u32 dst_lo, u32 dst_hi);
	void gui_trigger_line();
	void host_data_w(u32 data, bool last);
	void host_pixel(u32 src, bool draw = true);
	static u32 rop3(u8 rop, u32 p, u32 s, u32 d);
	void scaler_blit();
	void scale_block(const u32 *b);
	void scale_rows(const scale_job &j, int first, int last);
	static void *scale_band_callback(void *param, int threadid);
	void gui_dma_run(u32 table);

	// CCE
	void cce_write(u32 data);
	void cce_packet3(u8 opcode, const u32 *data, u32 count);
	void cce_run_ring();
	void cce_run_indirect(u32 addr, u32 dwords);
	bool cce_pio_mode() const;
	bool cce_bm_mode() const;
	u32 pm4_fifo_depth() const;
	u32 card_read32(u32 addr, bm_client client = BM_DATA);
	void card_write32(u32 addr, u32 data, bm_client client = BM_DATA);
	bool card_to_bus(u32 addr, u32 &bus, bm_client client);

	// 3D
	void render_prim(u32 vc_format, u32 vc_cntl, const u32 *inline_data, u32 inline_count, u32 vb_addr, const u32 *indices, u32 index_count);
	bool fetch_vertex(u32 vc_format, const u32 *&src, const u32 *end, vertex3d &v);
	bool fetch_vertex_mem(u32 vc_format, u32 addr, vertex3d &v);
	static u32 vertex_dwords(u32 vc_format);
	void setup_raster_state();
	void setup_texunit(texunit &unit, int which);
	void queue_prim(u8 kind, const vertex3d &v0, const vertex3d &v1, const vertex3d &v2);
	void kick_render(const std::source_location &where = std::source_location::current());
	void launch_batch(render_batch *batch);
	void retire_batches();
	std::unique_ptr<render_batch> spare_batch();
	void wait_for_render(const std::source_location &where = std::source_location::current());
	void wait_for_outstanding(const std::source_location &where = std::source_location::current());
	void wait_for_batch(render_batch *batch, const std::source_location &where = std::source_location::current());
	render_batch *last_batch_touching(u32 start, u32 end, bool write, bool skip_presents);
	void wait_for_vram_span(u32 start, u32 end, bool write, const std::source_location &where = std::source_location::current());
	void wait_for_vram_read(u32 addr, const std::source_location &where = std::source_location::current()) { wait_for_vram_span(addr, addr + 4, false, where); }
	void wait_for_vram_write(u32 addr, const std::source_location &where = std::source_location::current()) { wait_for_vram_span(addr, addr + 4, true, where); }
	perf_site &perf_site_for(const std::source_location &where);
	void note_3d_destination(const raster_state &rs);
	static u64 thread_cpu_ns();
	u32 perf_state_id(const raster_state &rs, const puml::compiled *block);
	void perf_log_detail();
	void rows_span(u32 base, u32 pitch, int bpp, s32 top, s32 bottom, s32 right, u32 &lo, u32 &hi) const;
	void note_vram_use(render_batch &batch, const raster_state &rs);
	void texture_spans(const raster_state &rs, vram_ranges &ranges) const;
	void queue_fill(const fill_job &f);
	void perf_tick();
	static void *render_band_callback(void *param, int threadid);
	void render_batch_band(const render_batch &batch, int band, int bands);
	static void apply_solid_color(const raster_state &rs, vertex3d &v);
	static void direct_st(const raster_state &rs, vertex3d &v);
	u32 draw_triangle(const raster_state &rs, const vertex3d &v0, const vertex3d &v1, const vertex3d &v2, int band, int bands, const puml::compiled *block = nullptr);
	std::string puml_key(const raster_state &rs) const;
	bool puml_generate(const raster_state &rs, puml::block &b) const;
	const puml::compiled *puml_block(const raster_state &rs);
	static void puml_texture(const puml::ctx &c, puml::frame &f, u64 param);
	static void puml_fetch_texture(puml::builder &g, int unit, const texunit &tu, int s, int t, int texel, int nearest, int fzero, int fone);
	void puml_verify_pixel(const puml::compiled &code, puml::ctx &pc, puml::frame &pf, const raster_state &rs, int x, int y, const std::function<void ()> &cpp);
	void puml_flush_log();
	void draw_point(const raster_state &rs, const vertex3d &v, int band, int bands);
	void draw_line3d(const raster_state &rs, const vertex3d &v0, const vertex3d &v1, int band, int bands);
	void shade_pixel(const raster_state &rs, int x, int y, const vertex3d &p, const float *lod, bool polygon);
	u32 sample_texture(const texunit &unit, const u32 *palette, float s, float t, float lod, u32 *nearest);
	u32 sample_level(const texunit &unit, const u32 *palette, int slot, float s, float t, bool linear, u32 *nearest);
	u32 read_texel(u32 base, u32 pitch, int x, int y, int fmt, int pal_fmt, bool tiled = false, const u32 *palette = nullptr);
	u32 decode_texel(int fmt, int pal_fmt, u32 raw, const u32 *palette) const;
	u8 card_read8(u32 addr);
	void gart_flush();
	static void combine(u32 cntl, bool first, float tr, float tg, float tb, float ta, float ir, float ig, float ib, float ia,
			float cr, float cg, float cb, float ca, float &or_, float &og, float &ob, float &oa);
	static void blend_factors(int mode, const float *s, const float *d, float *f);
	static float blend_factor(int mode, bool alpha, float sr, float sg, float sb, float sa, float dr, float dg, float db, float da, int chan);
	static u32 pack_color(int dtype, float r, float g, float b, float a);
	static void unpack_color(int dtype, u32 pix, float &r, float &g, float &b, float &a);
};


class ati_rage128_gl_pci_device : public ati_rage128_device
{
public:
	ati_rage128_gl_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

// Apple OEM Rage 128 GL, 16 MiB (ATY,Rage128y): the stock card in the Power Mac G3 (Blue & White)
// and Power Mac G4 (PCI Graphics).  Its option ROM carries Open Firmware FCode and the Mac OS ndrv.
class ati_rage128_gl_pci_mac_device : public ati_rage128_device
{
public:
	ati_rage128_gl_pci_mac_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

protected:
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
};

class ati_rage128_gl_agp_device : public ati_rage128_device
{
public:
	ati_rage128_gl_agp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class ati_rage128_vr_pci_device : public ati_rage128_device
{
public:
	ati_rage128_vr_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class ati_rage128_vr_agp_device : public ati_rage128_device
{
public:
	ati_rage128_vr_agp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class ati_rage128_pro_pci_device : public ati_rage128_device
{
public:
	ati_rage128_pro_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class ati_rage128_pro_agp_device : public ati_rage128_device
{
public:
	ati_rage128_pro_agp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

DECLARE_DEVICE_TYPE(ATI_RAGE128_VGA,     ati_rage128_vga_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_GL_PCI,  ati_rage128_gl_pci_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_GL_PCI_MAC, ati_rage128_gl_pci_mac_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_GL_AGP,  ati_rage128_gl_agp_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_VR_PCI,  ati_rage128_vr_pci_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_VR_AGP,  ati_rage128_vr_agp_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_PRO_PCI, ati_rage128_pro_pci_device)
DECLARE_DEVICE_TYPE(ATI_RAGE128_PRO_AGP, ati_rage128_pro_agp_device)

#endif // MAME_BUS_PCI_RAGE128_H
