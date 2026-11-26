// license:BSD-3-Clause
// copyright-holders:<author>
/***************************************************************************

    ATI Rage 128 (R128) PCI/AGP graphics

    The Rage 128 replaced the mach64-derived Rage Pro with a new register
    layout that was later carried forward into the original Radeon.
    Chip codes are ASCII in the PCI device ID, as with the other ATI parts:

        RE/RF   Rage 128 GL         (PCI / AGP 2x)
        RK/RL   Rage 128 VR         (PCI / AGP 2x)
        PD/PF   Rage 128 Pro        (PCI / AGP 4x)

    ATI's Windows 2000 miniport knows 43 IDs in four families: Rage 128
    (RE/RF, RK/RL/RM), Rage 128 4X (SE-SH, SK-SN), Rage 128 Pro (PA-PX,
    cycling PCI / AGP 2x / AGP 4x) and Rage 128 Pro II/Ultra (TF, TL,
    TR-TU).  The Mac OS X 10.4 accelerator matches RE, RF, RK, RL, PE, PF,
    PR, TR and the Mobility LE/LF only, so it won't load on the PD.

    PCI resources:
        BAR0    64 MiB prefetchable framebuffer window.  Two 32 MiB
                apertures, each with its own big-endian swapper
                (CONFIG_CNTL bits 1:0 and 3:2).
        BAR1    256 byte I/O window onto MMIO 0x00-0xff, including the
                MM_INDEX/MM_DATA pair that reaches every register.
        BAR2    Register aperture: 16 KiB of registers, decoded as 32 KiB
                with the registers again in the upper half (see TODO).
                PCI config space is mirrored at 0xf00, read-only apart from
                the command register.
        ROM     Option ROM: x86 VGA BIOS on PC boards; Open Firmware
                FCode with the Mac OS "ndrv" embedded on Mac boards.  The
                Apple OEM GL ROM image is 74 KiB, so that board decodes at
                least 128 KiB.

    Emulated:
        - Standard VGA (pc_vga core) with ATI linear aperture banking.
        - Native CRTC: all pixel formats, CRTC_OFFSET flipping, double
          scan, the pixel PLL, DPMS blanking and the 64x64 cursor.
        - Shared 8-bit DAC with direct-colour gamma through the palette.
        - Interrupts: vertical blank, vertical line and vertical sync.
        - DDC: GPIO_MONID bit-banging and the I2C engine, talking to an
          internally generated EDID that offers 640x480 only.
        - Overlay scaler: RGB, packed YUV 4:2:2 and planar YUV 4:2:0
          sources with graphics and video colour keys.
        - GUI engine: solid, pattern, mono expansion, screen-to-screen,
          host data and Bresenham line operations, all 256 ROP3s,
          scissors including the three auxiliary rectangles, colour
          compare and write mask.
        - CCE (PM4): PIO and bus mastering modes, ring buffer, indirect
          buffers, read pointer writeback, PCI GART and AGP translation,
          including the BM_CHUNK_0_VAL "force to PCI" routing that Mac OS X
          relies on.  The microcode is accepted but packets are interpreted
          directly.
        - 3D: Type 3 vertex packets rendered with a software rasteriser:
          flat/Gouraud shading, two texture units with the combiners,
          perspective correction, mipmapping with per-pixel LOD, nearest,
          bilinear and trilinear filtering, Z test/write,
          stencil, vertex fog, polygon stipple, alpha test and alpha
          blending.
        - Scaler blits (SCALE_* registers) with alpha blending, which is
          how the Mac OS X accelerator draws the mouse cursor.
        - Bus-master GUI DMA from the frame buffer to system memory.

    TODO:
        - Tiled scanout (CRTC_OFFSET_CNTL TILE_EN, CRTC2_TILE_EN); textures,
          2D source/destination, the 3D colour and Z buffers and the SURFACEn
          aperture windows use the 86Box tile layout
        - CRTC2, LVDS/TMDS panel scaling (registers are kept)
        - Dithering, table fog, LOD bias, VQ/S3TC textures (datatype 0)
        - Direct (non-CCE) setup engine: the GDI GradientFill path in the
          Windows NT driver programs RED_X_INC...ALPHA_START (0x1a40-0x1a6c)
          and a trigger that hasn't been decoded
        - Trapezoid and small text GUI primitives; NEXT_CHAR (0x19)
        - Scaler blocks (CNTL_SCALING/CNTL_TRANS_SCALING): source tiling,
          the source start phase in the filtered path, YUV sources
        - WAIT_UNTIL doesn't stall the CCE.  Drivers queue vline and flip
          waits (0x1, 0x2, 0x4, 0x8, 0x400, and bit 31 for CRTC2) before
          flips and cursor updates.
        - DVD sub-picture (0x540-0x588) and motion compensation
          (0x1840-0x1898, IDCT at 0x1f80-0x1fbc) blocks, used by the Mac OS X
          DVD player
        - Option ROMs: only the Apple OEM Rage 128 GL (113-57407-204) is
          dumped; the other variants have a blank region
        - Verify against hardware; these follow the reference drivers'
          usage rather than documentation:
            - MEM_VGA_WP_SEL/RP_SEL as two 32 KiB page numbers
            - pattern origin (BRUSH_Y_X) added to screen coordinates
            - direction selection for CCE BITBLT_MULTI blits
            - LOAD_PALETTE payload layout
            - float colour components as 0.0-1.0 in vertex formats
            - cursor vertical offset shortening rather than skipping rows
            - PM4_STAT free count (64 in mode 7?) and the FIFO depth per mode
            - AGP_APER_OFFSET as an offset into the AGP aperture, with the
              card's AGP/GART window fixed at 0x02000000
            - POLYSCANLINES spans as (x_right << 16) | x_left, right edge
              exclusive
            - SCALE_PITCH units, SCALE_3D_DATATYPE (0x1a20) as the source
              format, and blending only when MISC_3D_STATE_CNTL_REG sets a
              blend function
            - GUI DMA register names (BM_QUEUE_STATUS 0xa10, 0xa50 table
              pointer) and the descriptor direction
            - polygon stipple bit and row order
            - TEX_PALETTE_WR_INDEX (0x1968) taking the entry number in its
              low byte; Mac OS 9 writes 0x2000 there
            - the texture level layout, taken from 86Box: the base level in
              PRIM_TEX_n_OFFSET_C slot TEX_SIZE - TEX_MIN_SIZE (Mac OS X's
              "reverse order"), 2^TEX_PITCH texels wide
            - CI4 nibble order
            - chip revisions: the Win9x driver only knows Rage 128 GL/VR
              revisions 2 and 3, but the Apple card reports revision 0
            - HOST_BIG_ENDIAN_EN swapping colour host data per pixel and
              leaving mono host data alone
            - the CNTL_TRANS_BITBLT payload: CLR_CMP_CNTL, CLR_CMP_CLR_SRC and
              CLR_CMP_CLR_DST between the GMC state and the blits
            - CNTL packet brush operands: one colour for FG_LA mono brushes and
              a BRUSH_Y_X dword for GMC bit 31; PAINT taking two corners where
              PAINT_MULTI takes a position and size
            - the PIO FIFO window as 0x1000-0x11ff, and 0x740 reporting free
              FIFO entries in its low byte
            - CUR_OFFSET ignoring its low four bits
            - POLYLINE points as (y << 16) | x, with DP_CNTL DST_LAST_PEL
              deciding whether the final point is drawn
            - memory-source rectangles ignoring SRC_X/SRC_Y and the source
              scissor when the ROP doesn't use the source
            - the size of BAR2 and what lives at its offset 0x4000.  On the
              Beige G3 a Mac OS 9 driver writes PIO FIFO packets to 0x5000 and
              host data to HOST_DATA0-7 at 0x57c0-0x57dc, byte-reversed like
              its other register writes, so the upper half is taken to repeat
              the registers unswapped
            - the name of TEX_CNTL_C bit 13, taken from the Rage Pro's
              TEX_AMASK_AEN.  Its effect (texels with alpha 0 aren't drawn)
              matches real hardware for Nanosaur's leaves; Unreal's masked
              textures set it too

    Looks wrong but matches real hardware (don't chase these):
        - Unreal under Mac OS 9 RAVE, in the intro flyby at 320x240: a black
          triangle along the right edge of the screen as the camera turns past
          the rock face beside the castle, with the texture next to it
          flickering.  Both happen on real hardware, with the retail release
          and with the 224 patch, and the black is probably just the reduced
          lighting setting.  Traced with LOG_PROBE: the depth order
          and both texture passes are right, and the texels read are dark.
        - Unreal under RAVE has other renderer problems of its own, such as
          missing fog; compare with real hardware before treating one as an
          emulation bug.

    Hardware references used:
        - XFree86/X.Org r128 driver (r128_reg.h is derived from ATI's
          "RAGE 128 VR/RAGE 128 GL Register Reference Manual",
          RRG-G04100-C, and "RAGE 128 Software Development Manual")
        - Linux aty128fb and the r128 DRM driver
        - ATI's own drivers, disassembled for the packets and registers they
          use: the Windows 9x/NT4/2000/XP "Rage 128 driver 1.02" CD (whose
          OpenGL ICD carries ATI's register name table), the Mac OS 9
          "ATI Rage 128 3D Accelerator" RAVE engine, "ATI Graphics
          Accelerator" (QuickDraw acceleration) and "ATI Driver Update"
          ndrvs, and the Mac OS X 10.4
          ATIRage128 kext, GL, 2D and DVD drivers (1.4.4)

***************************************************************************/

#include "emu.h"
#include "rage128.h"

#include "video/rgbutil.h"

#include "corefloat.h"
#include "emuopts.h"
#include "multibyte.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#define LOG_REG      (1U << 1)
#define LOG_CRTC     (1U << 2)
#define LOG_ENGINE   (1U << 3)
#define LOG_CCE      (1U << 4)
#define LOG_DDC      (1U << 5)
#define LOG_3D       (1U << 6)
#define LOG_UNIMPL   (1U << 7)
#define LOG_PACKET   (1U << 8)
#define LOG_TILE     (1U << 9)
#define LOG_PERF     (1U << 10)
#define LOG_PROBE    (1U << 11)

#define VERBOSE (0)
#include "logmacro.h"

// The pixel pipeline is the reference PUML blocks are checked against byte for
// byte, so the compiler mustn't fuse multiplies and adds here (GCC and clang
// both do by default on arm64); PUML never does.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#define LOGREG(...)     LOGMASKED(LOG_REG, __VA_ARGS__)
#define LOGCRTC(...)    LOGMASKED(LOG_CRTC, __VA_ARGS__)
#define LOGENGINE(...)  LOGMASKED(LOG_ENGINE, __VA_ARGS__)
#define LOGCCE(...)     LOGMASKED(LOG_CCE, __VA_ARGS__)
#define LOGDDC(...)     LOGMASKED(LOG_DDC, __VA_ARGS__)
#define LOG3D(...)      LOGMASKED(LOG_3D, __VA_ARGS__)
#define LOGUNIMPL(...)  LOGMASKED(LOG_UNIMPL, __VA_ARGS__)
#define LOGPACKET(...)  LOGMASKED(LOG_PACKET, __VA_ARGS__)
#define LOGPERF(...)    LOGMASKED(LOG_PERF, __VA_ARGS__)
#define LOGPROBE(...)   LOGMASKED(LOG_PROBE, __VA_ARGS__)

// LOG_PROBE logs every 3D write or rejection at this pixel of the 3D destination
constexpr int PROBE_X = 316, PROBE_Y = 190;
#define LOGTILE(...)    LOGMASKED(LOG_TILE, __VA_ARGS__)


DEFINE_DEVICE_TYPE(ATI_RAGE128_VGA,      ati_rage128_vga_device,      "rage128_vga",   "ATI Rage 128 VGA core")
DEFINE_DEVICE_TYPE(ATI_RAGE128_GL_PCI,   ati_rage128_gl_pci_device,   "rage128gl",     "ATI Rage 128 GL PCI")
DEFINE_DEVICE_TYPE(ATI_RAGE128_GL_PCI_MAC, ati_rage128_gl_pci_mac_device, "rage128gl_mac", "ATI Rage 128 GL PCI (Apple OEM)")
DEFINE_DEVICE_TYPE(ATI_RAGE128_GL_AGP,   ati_rage128_gl_agp_device,   "rage128gl_agp", "ATI Rage 128 GL AGP")
DEFINE_DEVICE_TYPE(ATI_RAGE128_VR_PCI,   ati_rage128_vr_pci_device,   "rage128vr",     "ATI Rage 128 VR PCI")
DEFINE_DEVICE_TYPE(ATI_RAGE128_VR_AGP,   ati_rage128_vr_agp_device,   "rage128vr_agp", "ATI Rage 128 VR AGP")
DEFINE_DEVICE_TYPE(ATI_RAGE128_PRO_PCI,  ati_rage128_pro_pci_device,  "rage128pro",    "ATI Rage 128 Pro PCI")
DEFINE_DEVICE_TYPE(ATI_RAGE128_PRO_AGP,  ati_rage128_pro_agp_device,  "rage128pro_agp", "ATI Rage 128 Pro AGP 4x")


namespace {

// register byte offsets
enum : u32
{
	MM_INDEX                = 0x0000,
	MM_DATA                 = 0x0004,
	CLOCK_CNTL_INDEX        = 0x0008,
	CLOCK_CNTL_DATA         = 0x000c,
	BIOS_0_SCRATCH          = 0x0010,
	BIOS_7_SCRATCH          = 0x002c,
	BUS_CNTL                = 0x0030,
	BUS_CNTL1               = 0x0034,
	MEM_VGA_WP_SEL          = 0x0038,
	MEM_VGA_RP_SEL          = 0x003c,
	GEN_INT_CNTL            = 0x0040,
	GEN_INT_STATUS          = 0x0044,
	CRTC_GEN_CNTL           = 0x0050,
	CRTC_EXT_CNTL           = 0x0054,
	DAC_CNTL                = 0x0058,
	CRTC_STATUS             = 0x005c,
	GPIO_MONID              = 0x0068,
	GPIO_MONIDB             = 0x006c,
	I2C_CNTL_0              = 0x0090,
	I2C_CNTL_1              = 0x0094,
	I2C_DATA                = 0x0098,
	PALETTE_INDEX           = 0x00b0,
	PALETTE_DATA            = 0x00b4,
	CONFIG_CNTL             = 0x00e0,
	CONFIG_XSTRAP           = 0x00e4,
	CONFIG_BONDS            = 0x00e8,
	GEN_RESET_CNTL          = 0x00f0,
	CONFIG_MEMSIZE          = 0x00f8,
	CONFIG_APER_0_BASE      = 0x0100,
	CONFIG_APER_1_BASE      = 0x0104,
	CONFIG_APER_SIZE        = 0x0108,
	CONFIG_REG_1_BASE       = 0x010c,
	CONFIG_REG_APER_SIZE    = 0x0110,
	CONFIG_MEMSIZE_EMBEDDED = 0x0114,
	MEM_CNTL                = 0x0140,
	AGP_BASE                = 0x0170,
	AGP_CNTL                = 0x0174,
	AGP_APER_OFFSET         = 0x0178,
	PCI_GART_PAGE           = 0x017c,
	PC_NGUI_CTLSTAT         = 0x0184,
	CRTC_H_TOTAL_DISP       = 0x0200,
	CRTC_H_SYNC_STRT_WID    = 0x0204,
	CRTC_V_TOTAL_DISP       = 0x0208,
	CRTC_V_SYNC_STRT_WID    = 0x020c,
	CRTC_VLINE_CRNT_VLINE   = 0x0210,
	CRTC_CRNT_FRAME         = 0x0214,
	CRTC_GUI_TRIG_VLINE     = 0x0218,
	CRTC_OFFSET             = 0x0224,
	CRTC_OFFSET_CNTL        = 0x0228,
	CRTC_PITCH              = 0x022c,
	SNAPSHOT_VH_COUNTS      = 0x0240,
	SNAPSHOT_F_COUNT        = 0x0244,
	CUR_OFFSET              = 0x0260,
	CUR_HORZ_VERT_POSN      = 0x0264,
	CUR_HORZ_VERT_OFF       = 0x0268,
	CUR_CLR0                = 0x026c,
	CUR_CLR1                = 0x0270,
	OV0_Y_X_START           = 0x0400,
	OV0_Y_X_END             = 0x0404,
	OV0_REG_LOAD_CNTL       = 0x0410,
	OV0_SCALE_CNTL          = 0x0420,
	OV0_V_INC               = 0x0424,
	OV0_P1_V_ACCUM_INIT     = 0x0428,
	OV0_VID_BUF0_BASE_ADRS  = 0x0440,
	OV0_VID_BUF1_BASE_ADRS  = 0x0444,
	OV0_VID_BUF2_BASE_ADRS  = 0x0448,
	OV0_VID_BUF_PITCH0_VALUE = 0x0460,
	OV0_VID_BUF_PITCH1_VALUE = 0x0464,
	OV0_H_INC               = 0x0480,
	OV0_STEP_BY             = 0x0484,
	OV0_P1_H_ACCUM_INIT     = 0x0488,
	OV0_P1_X_START_END      = 0x0494,
	OV0_VIDEO_KEY_CLR       = 0x04e4,
	OV0_VIDEO_KEY_MSK       = 0x04e8,
	OV0_GRAPHICS_KEY_CLR    = 0x04ec,
	OV0_GRAPHICS_KEY_MSK    = 0x04f0,
	OV0_KEY_CNTL            = 0x04f4,

	PM4_BUFFER_OFFSET       = 0x0700,
	PM4_BUFFER_CNTL         = 0x0704,
	PM4_BUFFER_WM_CNTL      = 0x0708,
	PM4_BUFFER_DL_RPTR_ADDR = 0x070c,
	PM4_BUFFER_DL_RPTR      = 0x0710,
	PM4_BUFFER_DL_WPTR      = 0x0714,
	PM4_IW_INDOFF           = 0x0738,
	PM4_IW_INDSIZE          = 0x073c,
	PM4_FIFO_FREE           = 0x0740,       // name unknown; see reg_r
	PM4_STAT                = 0x07b8,
	PM4_MICROCODE_ADDR      = 0x07d4,
	PM4_MICROCODE_RADDR     = 0x07d8,
	PM4_MICROCODE_DATAH     = 0x07dc,
	PM4_MICROCODE_DATAL     = 0x07e0,
	PM4_BUFFER_ADDR         = 0x07f0,
	PM4_MICRO_CNTL          = 0x07fc,

	// bus master unit; 0xa10 bit 27 is polled as "busy" and 0xa50 takes the
	// GUI DMA descriptor table (names after r128_reg.h, 0xa50 unconfirmed)
	BM_QUEUE_STATUS         = 0x0a10,
	BM_CHUNK_0_VAL          = 0x0a18,
	BM_GUI_TABLE            = 0x0a50,

	// named in ATI's register table; Mac OS 9 programs them, nothing else does
	SURFACE_DELAY           = 0x0b00,
	SURFACE0_LOWER_BOUND    = 0x0b04,
	SURFACE0_UPPER_BOUND    = 0x0b08,
	SURFACE0_INFO           = 0x0b0c,

	PCI_MIRROR              = 0x0f00,

	PM4_FIFO_DATA_EVEN      = 0x1000,       // start of the PIO FIFO window, see reg_w
	PM4_FIFO_DATA_ODD       = 0x1004,
	PM4_FIFO_WINDOW_END     = 0x1200,

	DST_OFFSET              = 0x1404,
	DST_PITCH               = 0x1408,
	DST_WIDTH               = 0x140c,
	DST_HEIGHT              = 0x1410,
	SRC_X                   = 0x1414,
	SRC_Y                   = 0x1418,
	DST_X                   = 0x141c,
	DST_Y                   = 0x1420,
	SRC_PITCH_OFFSET        = 0x1428,
	DST_PITCH_OFFSET        = 0x142c,
	SRC_Y_X                 = 0x1434,
	DST_Y_X                 = 0x1438,
	DST_HEIGHT_WIDTH        = 0x143c,
	DP_GUI_MASTER_CNTL      = 0x146c,
	BRUSH_SCALE             = 0x1470,
	BRUSH_Y_X               = 0x1474,
	DP_BRUSH_BKGD_CLR       = 0x1478,
	DP_BRUSH_FRGD_CLR       = 0x147c,
	BRUSH_DATA0             = 0x1480,
	BRUSH_DATA63            = 0x157c,
	DST_WIDTH_X             = 0x1588,
	DST_HEIGHT_WIDTH_8      = 0x158c,
	SRC_X_Y                 = 0x1590,
	DST_X_Y                 = 0x1594,
	DST_WIDTH_HEIGHT        = 0x1598,
	DST_WIDTH_X_INCY        = 0x159c,
	DST_HEIGHT_Y            = 0x15a0,
	DST_X_SUB               = 0x15a4,
	DST_Y_SUB               = 0x15a8,
	SRC_OFFSET              = 0x15ac,
	SRC_PITCH               = 0x15b0,
	DST_HEIGHT_WIDTH_BW     = 0x15b4,
	CLR_CMP_CNTL            = 0x15c0,
	CLR_CMP_CLR_SRC         = 0x15c4,
	CLR_CMP_CLR_DST         = 0x15c8,
	CLR_CMP_MASK            = 0x15cc,
	DP_SRC_FRGD_CLR         = 0x15d8,
	DP_SRC_BKGD_CLR         = 0x15dc,
	GUI_SCRATCH_REG0        = 0x15e0,       // Mac OS X writes a fence stamp here after each buffer
	GUI_SCRATCH_REG5        = 0x15f4,
	DST_BRES_ERR            = 0x1628,
	DST_BRES_INC            = 0x162c,
	DST_BRES_DEC            = 0x1630,
	DST_BRES_LNTH           = 0x1634,
	DST_BRES_LNTH_SUB       = 0x1638,
	SC_LEFT                 = 0x1640,
	SC_RIGHT                = 0x1644,
	SC_TOP                  = 0x1648,
	SC_BOTTOM               = 0x164c,
	SRC_SC_RIGHT            = 0x1654,
	SRC_SC_BOTTOM           = 0x165c,
	AUX_SC_CNTL             = 0x1660,
	AUX1_SC_LEFT            = 0x1664,
	DP_CNTL                 = 0x16c0,
	DP_DATATYPE             = 0x16c4,
	DP_MIX                  = 0x16c8,
	DP_WRITE_MASK           = 0x16cc,
	DP_CNTL_XDIR_YDIR_YMAJOR = 0x16d0,
	DEFAULT_OFFSET          = 0x16e0,
	DEFAULT_PITCH           = 0x16e4,
	DEFAULT_SC_BOTTOM_RIGHT = 0x16e8,
	SC_TOP_LEFT             = 0x16ec,
	SC_BOTTOM_RIGHT         = 0x16f0,
	SRC_SC_BOTTOM_RIGHT     = 0x16f4,
	// FLUSH_1-FLUSH_7: the drivers write them as pipeline delays (35 writes of
	// FLUSH_1 before each primitive on the first Rage 128, FLUSH_7 = 0 after
	// every PC_GUI_CTLSTAT flush); nothing visible is known to happen
	FLUSH_1                 = 0x1704,
	FLUSH_7                 = 0x171c,
	WAIT_UNTIL              = 0x1720,
	CACHE_CNTL              = 0x1724,
	GUI_STAT                = 0x1740,
	PC_GUI_MODE             = 0x1744,       // name unconfirmed; Mac OS X writes 0x122
	PC_GUI_CTLSTAT          = 0x1748,
	HOST_DATA0              = 0x17c0,
	HOST_DATA7              = 0x17dc,
	HOST_DATA_LAST          = 0x17e0,

	TEX_CNTL                = 0x1800,
	TRI_LIN_CNTL            = 0x1804,
	TEX_PALETTE_WR_INDEX    = 0x1968,
	TEX_PALETTE_WR          = 0x196c,
	SCALE_SRC_HEIGHT_WIDTH  = 0x1994,
	SCALE_OFFSET_0          = 0x1998,
	SCALE_PITCH             = 0x199c,
	SCALE_X_INC             = 0x19a0,
	SCALE_Y_INC             = 0x19a4,
	SCALE_HACC              = 0x19a8,
	SCALE_VACC              = 0x19ac,
	SCALE_DST_X_Y           = 0x19b0,
	SCALE_DST_HEIGHT_WIDTH  = 0x19b4,
	SCALE_3D_CNTL           = 0x1a00,
	SCALE_3D_DATATYPE       = 0x1a20,       // name assumed; the Mac OS X cursor puts the source format here
	CLR_CMP_CLR_3D          = 0x1a24,
	CLR_CMP_MSK_3D          = 0x1a28,
	SETUP_CNTL              = 0x1bc4,
	SOLID_COLOR             = 0x1bc8,
	WINDOW_XY_OFFSET        = 0x1bcc,
	SETUP_CNTL_PM4          = 0x1bd4,
	DST_PITCH_OFFSET_C      = 0x1c80,
	DP_GUI_MASTER_CNTL_C    = 0x1c84,
	SC_TOP_LEFT_C           = 0x1c88,
	SC_BOTTOM_RIGHT_C       = 0x1c8c,
	Z_OFFSET_C              = 0x1c90,
	Z_PITCH_C               = 0x1c94,
	Z_STEN_CNTL_C           = 0x1c98,
	TEX_CNTL_C              = 0x1c9c,
	MISC_3D_STATE_CNTL_REG  = 0x1ca0,
	TEXTURE_CLR_CMP_CLR_C   = 0x1ca4,
	TEXTURE_CLR_CMP_MSK_C   = 0x1ca8,
	FOG_COLOR_C             = 0x1cac,
	PRIM_TEX_CNTL_C         = 0x1cb0,
	PRIM_TEXTURE_COMBINE_CNTL_C = 0x1cb4,
	TEX_SIZE_PITCH_C        = 0x1cb8,
	PRIM_TEX_0_OFFSET_C     = 0x1cbc,
	SEC_TEX_CNTL_C          = 0x1d00,
	SEC_TEX_COMBINE_CNTL_C  = 0x1d04,
	SEC_TEX_0_OFFSET_C      = 0x1d08,
	CONSTANT_COLOR_C        = 0x1d34,
	PRIM_TEXTURE_BORDER_COLOR_C = 0x1d38,
	SEC_TEXTURE_BORDER_COLOR_C = 0x1d3c,
	STEN_REF_MASK_C         = 0x1d40,
	PLANE_3D_MASK_C         = 0x1d44,
	PM4_VC_FPU_SETUP        = 0x071c
};

// PLL register indices
enum : int
{
	PLL_CLK_PIN_CNTL        = 0x01,
	PLL_PPLL_CNTL           = 0x02,
	PLL_PPLL_REF_DIV        = 0x03,
	PLL_PPLL_DIV_0          = 0x04,
	PLL_PPLL_DIV_3          = 0x07,
	PLL_VCLK_ECP_CNTL       = 0x08,
	PLL_HTOTAL_CNTL         = 0x09,
	PLL_X_MPLL_REF_FB_DIV   = 0x0a,
	PLL_XPLL_CNTL           = 0x0b,
	PLL_XDLL_CNTL           = 0x0c,
	PLL_XCLK_CNTL           = 0x0d,
	PLL_MPLL_CNTL           = 0x0e,
	PLL_MCLK_CNTL           = 0x0f,
	PLL_P2PLL_CNTL          = 0x2a,
	PLL_P2PLL_REF_DIV       = 0x2b,
	PLL_P2PLL_DIV_0         = 0x2c
};

// GEN_INT_CNTL / GEN_INT_STATUS
constexpr u32 INT_CRTC_VBLANK = 1U << 0;
constexpr u32 INT_CRTC_VLINE  = 1U << 1;
constexpr u32 INT_CRTC_VSYNC  = 1U << 2;

// DP_GUI_MASTER_CNTL
constexpr u32 GMC_SRC_PITCH_OFFSET_CNTL = 1U << 0;
constexpr u32 GMC_DST_PITCH_OFFSET_CNTL = 1U << 1;
constexpr u32 GMC_SRC_CLIPPING          = 1U << 2;
constexpr u32 GMC_DST_CLIPPING          = 1U << 3;
constexpr u32 GMC_CLR_CMP_CNTL_DIS      = 1U << 28;
constexpr u32 GMC_AUX_CLIP_DIS          = 1U << 29;
constexpr u32 GMC_WR_MSK_DIS            = 1U << 30;
constexpr u32 GMC_LD_BRUSH_Y_X          = 1U << 31;

// DP_DATATYPE
constexpr u32 DP_HOST_BIG_ENDIAN_EN = 1U << 29;

// DP_CNTL
constexpr u32 DP_DST_LAST_PEL = 1U << 5;

// DP_SRC_SOURCE
enum : u32
{
	SRC_SOURCE_MEMORY       = 2,
	SRC_SOURCE_HOST_DATA    = 3,
	SRC_SOURCE_HOST_DATA_BYTE_ALIGNED = 4
};

// brush types
enum : u32
{
	BRUSH_8X8_MONO_FG_BG    = 0,
	BRUSH_8X8_MONO_FG_LA    = 1,
	BRUSH_1X8_MONO_FG_BG    = 4,
	BRUSH_1X8_MONO_FG_LA    = 5,
	BRUSH_32X1_MONO_FG_BG   = 6,
	BRUSH_32X1_MONO_FG_LA   = 7,
	BRUSH_32X32_MONO_FG_BG  = 8,
	BRUSH_32X32_MONO_FG_LA  = 9,
	BRUSH_8X8_COLOR         = 10,
	BRUSH_1X8_COLOR         = 12,
	BRUSH_SOLID_COLOR       = 13,
	BRUSH_NONE              = 15
};

// PM4_BUFFER_CNTL modes
constexpr u32 PM4_BUFFER_CNTL_NOUPDATE = 1U << 27;
constexpr u32 PM4_MICRO_FREERUN = 1U << 30;

constexpr u32 PCI_GART_PAGE_SIZE = 0x1000;

// BM_CHUNK_0_VAL: route bus master traffic through the PCI GART rather than AGP.
// Mac OS X sets PM4_RD for PCI cards; GUI DMA sets PM4_RD and GLOBAL.
constexpr u32 BM_PTR_FORCE_TO_PCI    = 1U << 21;
constexpr u32 BM_PM4_RD_FORCE_TO_PCI = 1U << 22;
constexpr u32 BM_GLOBAL_FORCE_TO_PCI = 1U << 23;

// GUI DMA descriptor command word
constexpr u32 BM_CMD_EOL = 1U << 31;

// PLL reference: the 29.50 MHz value drivers assume when no BIOS PLL block is found.
// ATI's NT miniport falls back to reference divider 64 and a 118-236 MHz VCO for it.
constexpr u32 REF_CLOCK = 29'500'000;
constexpr u32 REF_DIV_DEFAULT = 0x40;
constexpr double VCO_MIN = 118'000'000.0, VCO_MAX = 236'000'000.0;

// Card address of the AGP/GART window.  Linux (R128_AGP_OFFSET) and Mac OS X both
// put GART memory at 32 MiB in the card's address space.
constexpr u32 AGP_WINDOW_BASE = 0x0200'0000;

// PPLL post divider encoding; value 5 is reserved (assumed /16 as on later ATI parts)
constexpr int PLL_POST_DIV[8] = { 1, 2, 4, 8, 3, 16, 6, 12 };

// bytes per pixel for each engine datatype (0 means unsupported)
constexpr int DTYPE_BYTES[16] = { 0, 0, 1, 2, 2, 3, 4, 1, 1, 1, 0, 2, 2, 0, 4, 2 };

// bits per texel for each texture datatype; 13 is the undocumented 8:8 alpha/intensity
// format (86Box found it in Serious Sam), 0 is VQ/S3TC and isn't supported
constexpr int TEXEL_BITS[16] = { 0, 4, 8, 16, 16, 24, 32, 8, 8, 8, 16, 16, 16, 16, 32, 16 };

// texture offsets: bits 25:0 address the card (bit 25 selects the AGP/GART half),
// bits 31:30 select a tiled layout
constexpr u32 TEX_OFFSET_MASK = 0x03ff'ffff;

// render batches queued or rendering before the emulation thread waits
constexpr size_t MAX_OUTSTANDING_BATCHES = 8;

// Debugging switches for the render pipeline: wait for every batch as soon as
// it is submitted, and do fills, host data and the scaler on the emulation
// thread instead of queuing them.  Neither should change what is drawn.
constexpr bool DEBUG_SYNC_RENDER = false;
constexpr bool DEBUG_NO_QUEUED_2D = false;

// signed fields packed as 16:16 in coordinate registers
inline s32 lo16s(u32 v) { return s16(v & 0xffff); }
inline s32 hi16s(u32 v) { return s16(v >> 16); }

// byte swap for the big-endian apertures
inline u32 aperture_swap(u32 data, int mode)
{
	switch (mode)
	{
	case 1: return ((data & 0x00ff'00ff) << 8) | ((data >> 8) & 0x00ff'00ff);
	case 2: return swapendian_int32(data);
	default: return data;
	}
}

// Tiled surfaces: 64-byte x 16-line tiles of 1 KiB, stored row by row, so byte xb of
// line y lands at 16 * pitch * (y / 16) + 1024 * (xb / 64) + 64 * (y % 16) + xb % 64
// from the surface base.  Only defined for a pitch that is a nonzero multiple of 64
// bytes; anything else is linear.  After 86Box, which notes that the programming
// guide misprints the third term.
inline u32 tile_offset(u32 xb, u32 y, u32 pitch)
{
	return 16 * pitch * (y / 16) + 1024 * (xb / 64) + 64 * (y % 16) + (xb % 64);
}

inline bool tile_pitch_ok(u32 pitch)
{
	return pitch && !(pitch & 63);
}

// byte offset of byte xb of line y in a surface, tiled or linear
inline u32 surface_offset(u32 xb, u32 y, u32 pitch, bool tiled)
{
	return (tiled && tile_pitch_ok(pitch)) ? tile_offset(xb, y, pitch) : (y * pitch + xb);
}

// SURFACEn_INFO PITCHSEL (bits 4:0): bytes per line; 0 turns the window off
constexpr u32 SURFACE_PITCH[32] = {
	0, 64, 128, 256, 512, 1024, 2048, 4096, 640, 1280, 2560, 5120, 1600, 3200, 6400, 832,
	1664, 3328, 1920, 3840, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

inline u8 clamp8(float v)
{
	return u8(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

// whether a ROP3 depends on the source: bit (P<<2 | S<<1 | D) gives the result,
// so compare the S=1 bits with the S=0 bits
constexpr bool rop_uses_source(u8 rop)
{
	return ((rop >> 2) ^ rop) & 0x33;
}

// the start of a packet's payload, for logging packets that aren't understood yet
std::string dump_dwords(const u32 *data, u32 count)
{
	std::string result;
	for (u32 i = 0; (i < count) && (i < 20); i++)
		result += util::string_format(" %08x", data[i]);
	if (count > 20)
		result += " ...";
	return result;
}

} // anonymous namespace


/***************************************************************************
    VGA core
***************************************************************************/

ati_rage128_vga_device::ati_rage128_vga_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: svga_device(mconfig, ATI_RAGE128_VGA, tag, owner, clock)
{
}

void ati_rage128_vga_device::device_start()
{
	svga_device::device_start();

	for (auto &pal : m_palette)
		std::fill(std::begin(pal), std::end(pal), 0);
	std::fill(std::begin(m_dac_loading), std::end(m_dac_loading), 0);
	m_dac_windex = m_dac_rindex = 0;
	m_dac_mask = 0xff;
	m_dac_8bit = false;
	m_dac_pal2 = false;
	m_wp_sel = m_rp_sel = 0;

	save_item(NAME(m_palette));
	save_item(NAME(m_dac_windex));
	save_item(NAME(m_dac_rindex));
	save_item(NAME(m_dac_state));
	save_item(NAME(m_dac_mask));
	save_item(NAME(m_dac_loading));
	save_item(NAME(m_dac_reading));
	save_item(NAME(m_dac_8bit));
	save_item(NAME(m_dac_pal2));
	save_item(NAME(m_wp_sel));
	save_item(NAME(m_rp_sel));
	save_item(NAME(m_native));
}

void ati_rage128_vga_device::device_reset()
{
	svga_device::device_reset();
	m_dac_state = 0;
	m_dac_reading = false;
	m_native = false;
}

void ati_rage128_vga_device::io_3cx_map(address_map &map)
{
	svga_device::io_3cx_map(map);
	map(0x06, 0x06).rw(FUNC(ati_rage128_vga_device::dac_mask_r), FUNC(ati_rage128_vga_device::dac_mask_w));
	map(0x07, 0x07).rw(FUNC(ati_rage128_vga_device::dac_state_r), FUNC(ati_rage128_vga_device::dac_read_index_w));
	map(0x08, 0x08).rw(FUNC(ati_rage128_vga_device::dac_write_index_r), FUNC(ati_rage128_vga_device::dac_write_index_w));
	map(0x09, 0x09).rw(FUNC(ati_rage128_vga_device::dac_data_r), FUNC(ati_rage128_vga_device::dac_data_w));
}

void ati_rage128_vga_device::palette_update()
{
	for (int i = 0; i < 256; i++)
	{
		const u32 c = m_palette[0][i & m_dac_mask];
		set_pen_color(i, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
	}
}

void ati_rage128_vga_device::set_dac_cntl(u32 data)
{
	m_dac_8bit = BIT(data, 8);
	m_dac_pal2 = BIT(data, 5);
	if (m_dac_mask != (data >> 24))
	{
		m_dac_mask = data >> 24;
		vga.dac.mask = m_dac_mask;
		vga.dac.dirty = 1;
	}
}

u8 ati_rage128_vga_device::dac_mask_r() { return m_dac_mask; }

void ati_rage128_vga_device::dac_mask_w(u8 data)
{
	m_dac_mask = data;
	vga.dac.mask = data;
	vga.dac.dirty = 1;
}

u8 ati_rage128_vga_device::dac_state_r() { return m_dac_reading ? 3 : 0; }

void ati_rage128_vga_device::dac_read_index_w(u8 data) { palette_read_index(data); }

u8 ati_rage128_vga_device::dac_write_index_r() { return m_dac_windex; }

void ati_rage128_vga_device::dac_write_index_w(u8 data) { palette_write_index(data); }

u8 ati_rage128_vga_device::dac_data_r()
{
	const u32 c = m_palette[m_dac_pal2 ? 1 : 0][m_dac_rindex];
	u8 v = u8(c >> (16 - 8 * m_dac_state));
	if (!m_dac_8bit)
		v >>= 2;
	if (!machine().side_effects_disabled() && ++m_dac_state == 3)
	{
		m_dac_state = 0;
		m_dac_rindex++;
	}
	return v;
}

void ati_rage128_vga_device::dac_data_w(u8 data)
{
	m_dac_loading[m_dac_state] = m_dac_8bit ? data : pal6bit(data & 0x3f);
	if (++m_dac_state == 3)
	{
		const int pal = m_dac_pal2 ? 1 : 0;
		m_palette[pal][m_dac_windex] = (u32(m_dac_loading[0]) << 16) | (u32(m_dac_loading[1]) << 8) | m_dac_loading[2];
		vga.dac.dirty = 1;
		m_dac_state = 0;
		m_dac_windex++;
	}
}

void ati_rage128_vga_device::palette_write_index(u8 data)
{
	m_dac_windex = data;
	m_dac_state = 0;
	m_dac_reading = false;
}

void ati_rage128_vga_device::palette_read_index(u8 data)
{
	m_dac_rindex = data;
	m_dac_state = 0;
	m_dac_reading = true;
}

// PALETTE_DATA writes a whole 24-bit entry at the write index and advances it
void ati_rage128_vga_device::palette_data_w(u32 data)
{
	u8 r = (data >> 16) & 0xff, g = (data >> 8) & 0xff, b = data & 0xff;
	if (!m_dac_8bit)
	{
		r = pal6bit(r & 0x3f);
		g = pal6bit(g & 0x3f);
		b = pal6bit(b & 0x3f);
	}
	const int pal = m_dac_pal2 ? 1 : 0;
	m_palette[pal][m_dac_windex] = (u32(r) << 16) | (u32(g) << 8) | b;
	vga.dac.dirty = 1;
	m_dac_windex++;
}

u32 ati_rage128_vga_device::palette_data_r()
{
	u32 c = m_palette[m_dac_pal2 ? 1 : 0][m_dac_rindex];
	if (!m_dac_8bit)
		c = (c >> 2) & 0x003f'3f3f;
	if (!machine().side_effects_disabled())
		m_dac_rindex++;
	return c;
}

void ati_rage128_vga_device::set_native(bool native)
{
	if (m_native == native)
		return;
	m_native = native;
	if (!native)
		recompute_params();
}

void ati_rage128_vga_device::recompute_params()
{
	// the native CRTC owns the screen while extended display mode is on
	if (!m_native)
		svga_device::recompute_params();
}

// MEM_VGA_WP_SEL/RP_SEL are taken as two 32 KiB page numbers, for the lower
// and upper halves of the 64 KiB VGA window, as on mach64.  They only take
// effect in packed pixel (chain 4) modes; planar modes use the pc_vga layout.
u32 ati_rage128_vga_device::bank_offset(offs_t offset, u32 sel) const
{
	const u32 page = BIT(offset, 15) ? (sel >> 16) : (sel & 0xffff);
	return ((page << 15) | (offset & 0x7fff)) % vga.svga_intf.vram_size;
}

uint8_t ati_rage128_vga_device::mem_r(offs_t offset)
{
	if (!BIT(vga.sequencer.data[4], 3) || (!m_wp_sel && !m_rp_sel))
		return svga_device::mem_r(offset);
	return vga.memory[bank_offset(offset & 0xffff, m_rp_sel)];
}

void ati_rage128_vga_device::mem_w(offs_t offset, uint8_t data)
{
	if (!BIT(vga.sequencer.data[4], 3) || (!m_wp_sel && !m_rp_sel))
	{
		svga_device::mem_w(offset, data);
		return;
	}
	vga.memory[bank_offset(offset & 0xffff, m_wp_sel)] = data;
}


/***************************************************************************
    Card construction and configuration
***************************************************************************/

ati_rage128_device::ati_rage128_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock,
		u16 device_id, u32 subsystem, u32 flags, u32 vram_size)
	: pci_card_device(mconfig, type, tag, owner, clock)
	, m_vga(*this, "vga")
	, m_screen(*this, "screen")
	, m_bios(*this, "bios")
	, m_monitor(*this, "MONITOR")
	, m_chip_flags(flags)
	, m_vram_size(vram_size)
{
	set_ids(0x1002'0000 | device_id, 0x00, 0x03'0000, subsystem);
}

// All variants report revision 0 for now; see the TODO about revisions.
ati_rage128_gl_pci_device::ati_rage128_gl_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_GL_PCI, tag, owner, clock, 0x5245, 0x1002'0008, 0, 16 * 1024 * 1024)
{
}

// IDs from a Power Mac G4 (PCI Graphics) ioreg dump: 1002:5245 rev 00, subsystem b530:0408
ati_rage128_gl_pci_mac_device::ati_rage128_gl_pci_mac_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_GL_PCI_MAC, tag, owner, clock, 0x5245, 0xb530'0408, 0, 16 * 1024 * 1024)
{
}

ati_rage128_gl_agp_device::ati_rage128_gl_agp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_GL_AGP, tag, owner, clock, 0x5246, 0x1002'0008, CHIP_AGP, 16 * 1024 * 1024)
{
}

ati_rage128_vr_pci_device::ati_rage128_vr_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_VR_PCI, tag, owner, clock, 0x524b, 0x1002'0008, 0, 8 * 1024 * 1024)
{
}

ati_rage128_vr_agp_device::ati_rage128_vr_agp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_VR_AGP, tag, owner, clock, 0x524c, 0x1002'0008, CHIP_AGP, 8 * 1024 * 1024)
{
}

ati_rage128_pro_pci_device::ati_rage128_pro_pci_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_PRO_PCI, tag, owner, clock, 0x5044, 0x1002'0008, 0, 32 * 1024 * 1024)
{
}

ati_rage128_pro_agp_device::ati_rage128_pro_agp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: ati_rage128_device(mconfig, ATI_RAGE128_PRO_AGP, tag, owner, clock, 0x5046, 0x1002'0008, CHIP_AGP | CHIP_AGP4X, 32 * 1024 * 1024)
{
}

// No PC Rage 128 option ROM has been dumped for MAME yet.  PC cards carry an x86
// VGA BIOS; Mac cards carry Open Firmware FCode and the Mac OS ndrv.
ROM_START( rage128 )
	ROM_REGION32_LE( 0x10000, "bios", ROMREGION_ERASEFF )
	ROM_LOAD( "rage128.bin", 0x0000, 0x10000, NO_DUMP )
ROM_END

// Apple OEM Rage 128 GL: ATY,Rom# 113-57407-204, ATY,Card# 109-57400-00, ATY,Fcode 1.65.
// A single FCode image (PCIR code type 1, no x86 code) of 0x12800 bytes, with the Mac OS
// ndrv .Display_Rage128 1.0.1 embedded.  Open Firmware sizes the expansion ROM BAR at
// 128 KiB on real hardware; the dump stops at the image length given in the PCIR header.
// TODO: confirm that nothing follows the image in the upper part of the EEPROM.
ROM_START( rage128gl_mac )
	ROM_REGION32_LE( 0x20000, "bios", ROMREGION_ERASEFF )
	ROM_LOAD( "113-57407-204.bin", 0x00000, 0x12800, CRC(ab00bb87) SHA1(6c0153db8e0316c1c59a42b8f6c4d4c312c598b0) )
ROM_END

const tiny_rom_entry *ati_rage128_device::device_rom_region() const
{
	return ROM_NAME(rage128);
}

const tiny_rom_entry *ati_rage128_gl_pci_mac_device::device_rom_region() const
{
	return ROM_NAME(rage128gl_mac);
}

static INPUT_PORTS_START( rage128 )
	PORT_START("MONITOR")
	PORT_CONFNAME( 0x03, 0x01, "Monitor" )
	PORT_CONFSETTING(    0x00, "None" )
	PORT_CONFSETTING(    0x01, "VGA (DDC, 640x480 only)" )
	PORT_CONFSETTING(    0x02, "VGA without DDC" )
INPUT_PORTS_END

ioport_constructor ati_rage128_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(rage128);
}

void ati_rage128_device::device_add_mconfig(machine_config &config)
{
	SCREEN(config, m_screen);
	m_screen->set_raw(XTAL(25'174'800), 800, 0, 640, 525, 0, 480);
	m_screen->set_screen_update(FUNC(ati_rage128_device::screen_update));
	m_screen->screen_vblank().set(FUNC(ati_rage128_device::vblank_w));

	ATI_RAGE128_VGA(config, m_vga);
	m_vga->set_screen(m_screen);
	m_vga->set_vram_size(m_vram_size);
}

void ati_rage128_device::device_config_complete()
{
	// the VRAM size may have been changed after the VGA core was configured
	if (auto *const vga = m_vga.lookup())
		vga->set_vram_size(m_vram_size);
}

void ati_rage128_device::device_start()
{
	if (!m_vga->started())
		throw device_missing_dependencies();

	pci_card_device::device_start();

	add_map(0x400'0000, M_MEM | M_PREF, FUNC(ati_rage128_device::fb_map));
	add_map(0x100, M_IO, FUNC(ati_rage128_device::io_bar_map));
	add_map(REG_BAR_SIZE, M_MEM, FUNC(ati_rage128_device::mmio_map));
	add_rom(m_bios->base(), m_bios->bytes());

	intr_pin = 1;
	minimum_grant = 0x08;
	maximum_latency = 0x00;

	m_vram = m_vga->vram();
	m_vram_size = m_vga->vram_size();
	m_vram_mask = m_vram_size - 1;
	if (m_vram_size & m_vram_mask)
		fatalerror("%s: VRAM size must be a power of 2\n", tag());

	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	std::fill(std::begin(m_pll), std::end(m_pll), 0);
	for (auto &w : m_pm4_microcode)
		w[0] = w[1] = 0;
	std::fill(std::begin(m_tex_palette), std::end(m_tex_palette), 0);
	m_tex_pal_index = 0;
	std::fill(std::begin(m_edid), std::end(m_edid), 0);
	std::fill(std::begin(m_packet), std::end(m_packet), 0);
	std::fill(std::begin(m_i2c_fifo), std::end(m_i2c_fifo), 0);
	m_cce_depth = 0;
	gart_flush();
	m_tile_warned = false;
	m_surf_on = false;
	m_dst3d_tile_logged = false;
	m_zpitch_logged = ~0U;
	m_verts.reserve(256);

	// one band per render thread; with no threads the queue runs the items itself
	m_render_queue = osd_work_queue_alloc(WORK_QUEUE_FLAG_MULTI | WORK_QUEUE_FLAG_HIGH_FREQ | WORK_QUEUE_FLAG_COARSE);
	const int bands = std::clamp(osd_work_queue_thread_count(m_render_queue), 1, 16);
	m_band_count = bands;
	m_pending = spare_batch();
	m_chain_busy = false;
	m_probe_frame = 0;
	m_host.span_lo = m_host.span_hi = 0;
	m_perf_prims = m_perf_batches = m_perf_waits = m_perf_sync = 0;
	m_perf_wait_ticks = 0;
	m_perf_second = 0;

	m_vline_timer = timer_alloc(FUNC(ati_rage128_device::vline_tick), this);

	// pixel pipeline code generation; the -drc_* options choose and log the backend
	{
		puml::config cfg;
		cfg.force_c = machine().options().drc_use_c();
		cfg.log_ir = machine().options().drc_log_uml();
		cfg.log_native = machine().options().drc_log_native();
		cfg.tag = std::string(shortname()) + tag();
		std::replace(cfg.tag.begin(), cfg.tag.end(), ':', '_');
		cfg.log = [this] (const std::string &text) { logerror("%s", text); };
		m_puml = std::make_unique<puml::jit>(cfg);
		m_puml_refused.clear();
		m_puml_mismatches = 0;
	}

	build_edid();

	save_item(NAME(m_regs));
	save_item(NAME(m_pll));
	save_item(NAME(m_agp_command));
	save_item(NAME(m_pmcsr));
	save_item(NAME(m_crtc_offset_latched));
	save_item(NAME(m_frame_count));
	save_item(NAME(m_int_status));
	save_item(NAME(m_crtc_vblank_save));
	save_item(NAME(m_irq_state));
	save_item(NAME(m_native_mode));
	save_item(NAME(m_cursor_offset));
	save_item(NAME(m_ddc_scl));
	save_item(NAME(m_ddc_sda_in));
	save_item(NAME(m_ddc_sda_out));
	save_item(NAME(m_ddc_state));
	save_item(NAME(m_ddc_bits));
	save_item(NAME(m_ddc_shift));
	save_item(NAME(m_ddc_ptr));
	save_item(NAME(m_ddc_read));
	save_item(NAME(m_i2c_cntl0));
	save_item(NAME(m_i2c_cntl1));
	save_item(NAME(m_i2c_fifo));
	save_item(NAME(m_i2c_fifo_count));
	save_item(NAME(m_i2c_fifo_rd));
	save_item(NAME(m_dst_offset));
	save_item(NAME(m_dst_pitch));
	save_item(NAME(m_src_offset));
	save_item(NAME(m_src_pitch));
	save_item(NAME(m_dst_tile));
	save_item(NAME(m_src_tile));
	save_item(NAME(m_dst_x));
	save_item(NAME(m_dst_y));
	save_item(NAME(m_src_x));
	save_item(NAME(m_src_y));
	save_item(NAME(m_dst_width));
	save_item(NAME(m_dst_height));
	save_item(NAME(m_sc_left));
	save_item(NAME(m_sc_right));
	save_item(NAME(m_sc_top));
	save_item(NAME(m_sc_bottom));
	save_item(NAME(m_src_sc_right));
	save_item(NAME(m_src_sc_bottom));
	save_item(NAME(m_gmc));
	save_item(NAME(m_dp_cntl));
	save_item(NAME(m_dp_datatype));
	save_item(NAME(m_dp_mix));
	save_item(NAME(m_edid));
	save_item(NAME(m_host.active));
	save_item(NAME(m_host.mono));
	save_item(NAME(m_host.byte_aligned));
	save_item(NAME(m_host.x));
	save_item(NAME(m_host.y));
	save_item(NAME(m_host.start_x));
	save_item(NAME(m_host.col));
	save_item(NAME(m_host.row));
	save_item(NAME(m_host.width));
	save_item(NAME(m_host.height));
	save_item(NAME(m_host.x_step));
	save_item(NAME(m_host.y_step));
	save_item(NAME(m_host.byte_acc));
	save_item(NAME(m_host.byte_count));
	save_item(NAME(m_pm4_microcode));
	save_item(NAME(m_pm4_ucode_addr));
	save_item(NAME(m_pm4_ucode_raddr));
	save_item(NAME(m_packet));
	save_item(NAME(m_packet_count));
	save_item(NAME(m_packet_header));
	save_item(NAME(m_packet_remaining));
	save_item(NAME(m_tex_palette));
	save_item(NAME(m_tex_pal_index));
}

void ati_rage128_device::device_pre_save()
{
	pci_card_device::device_pre_save();

	// get everything rendered into the saved VRAM
	wait_for_render();
}

void ati_rage128_device::device_stop()
{
	wait_for_render();
	if (m_puml)
	{
		puml_flush_log();
		if (m_puml_mismatches)
			logerror("PUML verify: %u mismatching pixels\n", m_puml_mismatches.load());
		m_puml->log_statistics();
	}
	osd_work_queue_free(m_render_queue);
	m_render_queue = nullptr;
	pci_card_device::device_stop();
}

// Primitives queued before the load no longer belong to this timeline.
// TODO: a batch still rendering when the state is restored can land on
// top of the restored VRAM; the screen update waits every frame, so the
// window is short.
void ati_rage128_device::device_post_load()
{
	pci_card_device::device_post_load();
	m_pending->clear();
	wait_for_render();
	m_host.span_lo = 0;
	m_host.span_hi = ~0U;

	// screen timing is derived from the restored CRTC and PLL registers
	m_cfg_htotal = 0;
	update_mode();
	gart_flush();
	update_surfaces();
}

void ati_rage128_device::device_reset()
{
	m_pending->clear();
	wait_for_render();
	pci_card_device::device_reset();

	command = 0x0000;
	// Capabilities list, medium DEVSEL.  On the PCI Rage 128 GL, Open Firmware reports
	// fast-back-to-back but not 66mhz-capable; the PCI VR and Pro are assumed to match.
	status = (m_chip_flags & CHIP_AGP) ? 0x0230 : 0x0290;
	m_agp_command = 0;
	m_pmcsr = 0;

	std::fill(std::begin(m_regs), std::end(m_regs), 0);
	std::fill(std::begin(m_pll), std::end(m_pll), 0);

	// configuration straps and read-only registers
	m_regs[CONFIG_CNTL / 4] = 0;
	m_regs[CONFIG_XSTRAP / 4] = 0;
	m_regs[CONFIG_BONDS / 4] = 0;
	m_regs[MEM_CNTL / 4] = 0x0000'0001;      // SDR SGRAM
	m_regs[BUS_CNTL / 4] = 1U << 6;         // bus mastering disabled
	m_regs[AGP_APER_OFFSET / 4] = 0;
	m_regs[PCI_GART_PAGE / 4] = (m_chip_flags & CHIP_AGP) ? 1 : 0;
	m_regs[BM_CHUNK_0_VAL / 4] = 0;
	m_regs[DAC_CNTL / 4] = 0xff00'0000;      // DAC mask all on
	m_regs[CRTC_EXT_CNTL / 4] = 0;
	m_regs[CRTC_GEN_CNTL / 4] = 0;          // VGA mode
	m_regs[PM4_BUFFER_CNTL / 4] = 0;        // non-PM4
	m_regs[PLANE_3D_MASK_C / 4] = 0xffff'ffff;
	m_regs[DP_WRITE_MASK / 4] = 0xffff'ffff;
	m_regs[CLR_CMP_MASK / 4] = 0xffff'ffff;
	m_regs[DEFAULT_SC_BOTTOM_RIGHT / 4] = 0x1fff'1fff;
	m_regs[GPIO_MONID / 4] = 0;

	// PLL defaults resembling a BIOS-initialised card, so that drivers which
	// read the dividers back (Linux on PowerPC does) get sane numbers.  The
	// reference divider is the one ATI's NT miniport uses for a 29.5 MHz crystal.
	m_pll[PLL_PPLL_REF_DIV] = REF_DIV_DEFAULT;
	m_pll[PLL_PPLL_DIV_3] = 0x0003'01b5;     // VCO 201.4 MHz / 8 = 25.18 MHz
	m_pll[PLL_VCLK_ECP_CNTL] = 0x0000'0003;  // VCLK from PPLL, ECP divider 1
	m_pll[PLL_X_MPLL_REF_FB_DIV] = 0x004c'4c1e;
	m_pll[PLL_XCLK_CNTL] = 0x0000'0002;
	m_pll[PLL_MCLK_CNTL] = 0x0000'0000;
	m_pll[PLL_P2PLL_REF_DIV] = REF_DIV_DEFAULT;

	m_vga->set_dac_cntl(m_regs[DAC_CNTL / 4]);
	m_vga->set_banks(0, 0);
	m_vga->set_native(false);

	m_crtc_offset_latched = 0;
	m_frame_count = 0;
	m_int_status = 0;
	m_crtc_vblank_save = false;
	m_irq_state = false;
	m_native_mode = false;
	m_cfg_htotal = m_cfg_vtotal = m_cfg_hdisp = m_cfg_vdisp = 0;
	m_cfg_pclk = 0;
	m_cursor_offset = 0;

	m_ddc_scl = m_ddc_sda_in = m_ddc_sda_out = 1;
	m_ddc_state = 0;
	m_ddc_bits = m_ddc_shift = m_ddc_ptr = 0;
	m_ddc_read = false;
	m_i2c_cntl0 = m_i2c_cntl1 = 0;
	m_i2c_fifo_count = m_i2c_fifo_rd = 0;

	m_pm4_ucode_addr = m_pm4_ucode_raddr = 0;
	m_tex_pal_index = 0;
	m_packet_count = 0;
	m_packet_header = 0;
	m_packet_remaining = 0;
	gart_flush();
	update_surfaces();

	gui_reset();
	m_vline_timer->adjust(attotime::never);
	update_irq();
}


/***************************************************************************
    PCI configuration space
***************************************************************************/

uint8_t ati_rage128_device::capptr_r()
{
	return (m_chip_flags & CHIP_AGP) ? 0x50 : 0x5c;
}

void ati_rage128_device::config_map(address_map &map)
{
	pci_card_device::config_map(map);

	// AGP capability (AGP parts only) and power management capability
	map(0x50, 0x57).lr32(NAME([this] (offs_t offset) -> u32 { return config_mirror_r(0x50 + offset * 4); }));
	map(0x58, 0x5b).lrw32(
		NAME([this] () -> u32 { return config_mirror_r(0x58); }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) {
			if (m_chip_flags & CHIP_AGP)
			{
				COMBINE_DATA(&m_agp_command);
				m_agp_command &= 0x1f00'0307;
			}
		}));
	map(0x5c, 0x5f).lr32(NAME([this] () -> u32 { return config_mirror_r(0x5c); }));
	map(0x60, 0x63).lrw32(
		NAME([this] () -> u32 { return m_pmcsr; }),
		NAME([this] (offs_t offset, u32 data, u32 mem_mask) {
			if (ACCESSING_BITS_0_15)
				m_pmcsr = data & 0x0003;
		}));
}

// the register aperture mirrors the configuration header at 0xf00
u32 ati_rage128_device::config_mirror_r(u32 reg)
{
	switch (reg & 0xfc)
	{
	case 0x00: return main_id;
	case 0x04: return command | (u32(status) << 16);
	case 0x08: return (pclass << 8) | revision;
	case 0x0c: return u32(header_type_r()) << 16;
	case 0x10: case 0x14: case 0x18: case 0x1c: case 0x20: case 0x24:
		return address_base_r((reg & 0xfc) / 4 - 4);
	case 0x2c: return subsystem_id;
	case 0x30: return expansion_rom_base;
	case 0x34: return capptr_r();
	case 0x3c: return intr_line | (u32(intr_pin) << 8) | (u32(minimum_grant) << 16) | (u32(maximum_latency) << 24);
	case 0x50: return (m_chip_flags & CHIP_AGP) ? (0x02 | (0x5c << 8) | (0x10 << 16)) : 0;
	case 0x54: return (m_chip_flags & CHIP_AGP) ? ((0x1fU << 24) | (1U << 9) | ((m_chip_flags & CHIP_AGP4X) ? 0x7 : 0x3)) : 0;
	case 0x58: return (m_chip_flags & CHIP_AGP) ? m_agp_command : 0;
	case 0x5c: return 0x01 | (0x0602U << 16);      // PM, end of list, v1.1, D1 and D2 supported
	case 0x60: return m_pmcsr;
	default: return 0;
	}
}


/***************************************************************************
    Address maps and apertures
***************************************************************************/

void ati_rage128_device::fb_map(address_map &map)
{
	map(0x000'0000, 0x3ff'ffff).rw(FUNC(ati_rage128_device::fb_r), FUNC(ati_rage128_device::fb_w));
}

void ati_rage128_device::mmio_map(address_map &map)
{
	map(0x0000, REG_BAR_SIZE - 1).rw(FUNC(ati_rage128_device::mmio_r), FUNC(ati_rage128_device::mmio_w));
	map(0x03b0, 0x03df).m(m_vga, FUNC(ati_rage128_vga_device::io_map));
}

// BAR1 mirrors the first 256 bytes of the register aperture
void ati_rage128_device::io_bar_map(address_map &map)
{
	map(0x00, 0xff).rw(FUNC(ati_rage128_device::mmio_r), FUNC(ati_rage128_device::mmio_w));
}

void ati_rage128_device::legacy_io_map(address_map &map)
{
	map(0x00, 0x2f).m(m_vga, FUNC(ati_rage128_vga_device::io_map));
}

void ati_rage128_device::map_extra(uint64_t memory_window_start, uint64_t memory_window_end, uint64_t memory_offset, address_space *memory_space,
		uint64_t io_window_start, uint64_t io_window_end, uint64_t io_offset, address_space *io_space)
{
	// a VGA-class device decodes the legacy ranges whenever its decoders are enabled
	if (BIT(command, 1))
	{
		memory_space->install_readwrite_handler(memory_offset + 0xa'0000, memory_offset + 0xb'ffff,
				read8sm_delegate(*this, FUNC(ati_rage128_device::vga_mem_r)), write8sm_delegate(*this, FUNC(ati_rage128_device::vga_mem_w)));
	}

	if (BIT(command, 0))
		io_space->install_device(io_offset + 0x3b0, io_offset + 0x3df, *this, &ati_rage128_device::legacy_io_map);
}

// BAR0: two 32 MiB apertures onto the same memory, each with its own swapper
u32 ati_rage128_device::fb_r(offs_t offset, u32 mem_mask)
{
	return aperture_r(offset << 2, true);
}

void ati_rage128_device::fb_w(offs_t offset, u32 data, u32 mem_mask)
{
	aperture_w(offset << 2, data, mem_mask, true);
}

// the VGA window sees VRAM too, so it waits for 3D rendering like the apertures
u8 ati_rage128_device::vga_mem_r(offs_t offset)
{
	wait_for_render();
	return m_vga->mem_r(offset);
}

void ati_rage128_device::vga_mem_w(offs_t offset, u8 data)
{
	wait_for_render();
	m_vga->mem_w(offset, data);
}

// The SURFACEn windows only apply to the linear apertures, not to MM_DATA.
u32 ati_rage128_device::aperture_r(u32 byte_addr, bool surfaces)
{
	const int swap = BIT(m_regs[CONFIG_CNTL / 4], BIT(byte_addr, 25) ? 2 : 0, 2);
	u32 addr = byte_addr & m_vram_mask & ~3U;
	if (surfaces && m_surf_on)
		addr = surface_xlate(addr);
	wait_for_vram_read(addr);
	return aperture_swap(get_u32le(&m_vram[addr]), swap);
}

void ati_rage128_device::aperture_w(u32 byte_addr, u32 data, u32 mem_mask, bool surfaces)
{
	const int swap = BIT(m_regs[CONFIG_CNTL / 4], BIT(byte_addr, 25) ? 2 : 0, 2);
	u32 addr = byte_addr & m_vram_mask & ~3U;
	if (surfaces && m_surf_on)
		addr = surface_xlate(addr);
	wait_for_vram_write(addr);
	data = aperture_swap(data, swap);
	mem_mask = aperture_swap(mem_mask, swap);
	u32 v = get_u32le(&m_vram[addr]);
	COMBINE_DATA(&v);
	put_u32le(&m_vram[addr], v);
}

// A CPU access inside an enabled SURFACEn window (lower bound inclusive, upper
// exclusive) sees the tiled surface as linear at the window's pitch.  Dword
// accesses never cross a 64-byte tile column, since every pitch is a multiple of 64.
u32 ati_rage128_device::surface_xlate(u32 addr) const
{
	for (int n = 0; n < 4; n++)
	{
		const u32 pitch = SURFACE_PITCH[m_regs[(SURFACE0_INFO + n * 0x10) / 4] & 0x1f];
		const u32 lower = m_regs[(SURFACE0_LOWER_BOUND + n * 0x10) / 4] & 0x03ff'ffff;
		const u32 upper = m_regs[(SURFACE0_UPPER_BOUND + n * 0x10) / 4] & 0x03ff'ffff;
		if (!pitch || (addr < lower) || (addr >= upper))
			continue;
		const u32 off = addr - lower;
		return (lower + tile_offset(off % pitch, off / pitch, pitch)) & m_vram_mask;
	}
	return addr;
}

void ati_rage128_device::update_surfaces()
{
	m_surf_on = false;
	for (int n = 0; n < 4; n++)
	{
		const u32 info = m_regs[(SURFACE0_INFO + n * 0x10) / 4];
		if (SURFACE_PITCH[info & 0x1f])
			m_surf_on = true;
		LOGTILE("SURFACE%d: %08x-%08x, info %08x (%u bytes per line%s)\n", n,
				m_regs[(SURFACE0_LOWER_BOUND + n * 0x10) / 4], m_regs[(SURFACE0_UPPER_BOUND + n * 0x10) / 4], info,
				SURFACE_PITCH[info & 0x1f], SURFACE_PITCH[info & 0x1f] ? ", tiled" : ", off");
	}
}

u32 ati_rage128_device::mmio_r(offs_t offset, u32 mem_mask)
{
	return reg_r(offset << 2, mem_mask);
}

void ati_rage128_device::mmio_w(offs_t offset, u32 data, u32 mem_mask)
{
	reg_w(offset << 2, data, mem_mask);
}

/***************************************************************************
    Register file
***************************************************************************/

u32 ati_rage128_device::reg_r(u32 reg, u32 mem_mask)
{
	reg &= (MMIO_SIZE - 1) & ~3;

	if (reg >= PCI_MIRROR && reg < PCI_MIRROR + 0x100)
		return config_mirror_r(reg - PCI_MIRROR);

	if (reg >= BRUSH_DATA0 && reg <= BRUSH_DATA63)
		return m_regs[reg / 4];

	switch (reg)
	{
	case MM_INDEX:
		return m_regs[MM_INDEX / 4];

	case MM_DATA:
	{
		const u32 index = m_regs[MM_INDEX / 4];
		if (BIT(index, 31))
			return aperture_r(index & 0x7fff'ffff, false);
		if ((index & ~3) == MM_DATA || (index & ~3) == MM_INDEX)
			return 0;
		return reg_r(index & 0x7fff'fffc, mem_mask);
	}

	case CLOCK_CNTL_DATA:
		return pll_r(m_regs[CLOCK_CNTL_INDEX / 4] & 0x3f);

	case GEN_INT_STATUS:
		return m_int_status;

	case CRTC_STATUS:
	{
		const bool vblank = m_native_mode ? m_screen->vblank() : m_vga->vga_mode_vblank();
		return (vblank ? 1 : 0) | (m_crtc_vblank_save ? 2 : 0);
	}

	case CRTC_VLINE_CRNT_VLINE:
	{
		const u32 vpos = m_screen->vpos();
		return (m_regs[reg / 4] & 0x7ff) | ((vpos & 0x7ff) << 16);
	}

	case CRTC_CRNT_FRAME:
		return m_frame_count & 0x1f'ffff;

	case SNAPSHOT_VH_COUNTS:
		return (m_screen->hpos() & 0x7ff) | ((m_screen->vpos() & 0x7ff) << 16);

	case SNAPSHOT_F_COUNT:
		return m_frame_count;

	case GPIO_MONID:
	{
		const int pins = monid_pins();
		return (m_regs[reg / 4] & ~0x0000'0f00) | (pins << 8);
	}

	case I2C_CNTL_0:
		return m_i2c_cntl0;

	case I2C_CNTL_1:
		return m_i2c_cntl1;

	case I2C_DATA:
	{
		u8 data = 0xff;
		if (m_i2c_fifo_rd < m_i2c_fifo_count)
		{
			data = m_i2c_fifo[m_i2c_fifo_rd];
			if (!machine().side_effects_disabled())
				m_i2c_fifo_rd++;
		}
		return data;
	}

	case PALETTE_INDEX:
		return m_vga->palette_write_index_r() | (u32(m_vga->palette_read_index_r()) << 16);

	case PALETTE_DATA:
		return m_vga->palette_data_r();

	case CONFIG_CNTL:
		// chip revision in bits 20:16
		return (m_regs[reg / 4] & 0x0000'ffff) | (u32(revision & 0x1f) << 16);

	case CONFIG_MEMSIZE:
		return m_vram_size;

	case CONFIG_MEMSIZE_EMBEDDED:
		return 0;

	case CONFIG_APER_0_BASE:
		return bank_infos[0].adr & ~0x3ff'ffffU;

	case CONFIG_APER_1_BASE:
		return (bank_infos[0].adr & ~0x3ff'ffffU) + 0x200'0000;

	case CONFIG_APER_SIZE:
		return 0x200'0000;

	case CONFIG_REG_1_BASE:
		return bank_infos[2].adr & ~(REG_BAR_SIZE - 1);

	case CONFIG_REG_APER_SIZE:
		return MMIO_SIZE;

	case PC_NGUI_CTLSTAT:
		// flushes complete instantly
		return m_regs[reg / 4] & ~0x8000'00ffU;

	case PC_GUI_CTLSTAT:
		return m_regs[reg / 4] & ~0x8000'00ffU;

	case GUI_STAT:
		// the engine executes immediately; report 64 free FIFO slots and idle
		return 64;

	case PM4_FIFO_FREE:
		// The Mac OS 9 resource manager polls bits 7:0 (or 8:0) here for free PIO
		// FIFO entries before each submission, waiting for at least 6 or for the
		// size of what it's about to send.  It writes 0 here when it resets the CCE.
		return (m_regs[reg / 4] & ~0x1ffU) | std::min<u32>(pm4_fifo_depth(), 0xff);

	case PM4_STAT:
		// Bits 11:0 are the free FIFO entries and bit 31 is busy.  Packets run
		// as soon as they arrive, so the whole FIFO is always free and the CCE
		// idle.  Mac OS X waits for at least 6 free entries in mode 7 before each
		// submission and silently drops the buffer if it never sees them.
		return pm4_fifo_depth();

	case PM4_BUFFER_ADDR:
		return m_regs[PM4_BUFFER_OFFSET / 4] + m_regs[PM4_BUFFER_DL_RPTR / 4] * 4;

	case PM4_MICROCODE_ADDR:
		return m_pm4_ucode_addr;

	case PM4_MICROCODE_RADDR:
		return m_pm4_ucode_raddr;

	case PM4_MICROCODE_DATAH:
	{
		const u32 data = m_pm4_microcode[m_pm4_ucode_raddr & 0xff][0];
		return data;
	}

	case PM4_MICROCODE_DATAL:
	{
		const u32 data = m_pm4_microcode[m_pm4_ucode_raddr & 0xff][1];
		if (!machine().side_effects_disabled())
			m_pm4_ucode_raddr++;
		return data;
	}

	case OV0_REG_LOAD_CNTL:
		// LOCK_READBACK follows LOCK, the scaler accepts updates immediately.
		// Mac OS X spins on it before programming the overlay.
		return (m_regs[reg / 4] & 0x7) | (BIT(m_regs[reg / 4], 0) ? 0x8 : 0);

	case BM_QUEUE_STATUS:
		// GUI DMA transfers complete immediately, so busy (bit 27) never reads set
		return m_regs[reg / 4] & ~(1U << 27);

	case TEX_PALETTE_WR_INDEX:
		return (m_regs[reg / 4] & ~0xffU) | m_tex_pal_index;

	case DST_OFFSET:            return m_dst_offset;
	case DST_PITCH:             return m_dst_pitch;
	case SRC_OFFSET:            return m_src_offset;
	case SRC_PITCH:             return m_src_pitch;
	case DST_PITCH_OFFSET:      return (m_dst_offset >> 5) | (m_dst_pitch << 21) | (m_dst_tile ? 0x8000'0000 : 0);
	case SRC_PITCH_OFFSET:      return (m_src_offset >> 5) | (m_src_pitch << 21) | (m_src_tile ? 0x8000'0000 : 0);
	case DST_X:                 return m_dst_x & 0x3fff;
	case DST_Y:                 return m_dst_y & 0x3fff;
	case SRC_X:                 return m_src_x & 0x3fff;
	case SRC_Y:                 return m_src_y & 0x3fff;
	case DST_Y_X:               return (u32(m_dst_y & 0xffff) << 16) | (m_dst_x & 0xffff);
	case DST_X_Y:               return (u32(m_dst_x & 0xffff) << 16) | (m_dst_y & 0xffff);
	case SRC_Y_X:               return (u32(m_src_y & 0xffff) << 16) | (m_src_x & 0xffff);
	case SRC_X_Y:               return (u32(m_src_x & 0xffff) << 16) | (m_src_y & 0xffff);
	case DST_WIDTH:             return m_dst_width;
	case DST_HEIGHT:            return m_dst_height;
	case DST_HEIGHT_WIDTH:      return (m_dst_height << 16) | m_dst_width;
	case DST_WIDTH_HEIGHT:      return (m_dst_width << 16) | m_dst_height;
	case SC_LEFT:               return m_sc_left & 0x3fff;
	case SC_RIGHT:              return m_sc_right & 0x3fff;
	case SC_TOP:                return m_sc_top & 0x3fff;
	case SC_BOTTOM:             return m_sc_bottom & 0x3fff;
	case SC_TOP_LEFT:           return (u32(m_sc_top & 0xffff) << 16) | (m_sc_left & 0xffff);
	case SC_BOTTOM_RIGHT:       return (u32(m_sc_bottom & 0xffff) << 16) | (m_sc_right & 0xffff);
	case SRC_SC_RIGHT:          return m_src_sc_right & 0x3fff;
	case SRC_SC_BOTTOM:         return m_src_sc_bottom & 0x3fff;
	case SRC_SC_BOTTOM_RIGHT:   return (u32(m_src_sc_bottom & 0xffff) << 16) | (m_src_sc_right & 0xffff);
	case DP_GUI_MASTER_CNTL:    return gmc_r();
	case DP_CNTL:               return (m_regs[reg / 4] & ~0x7U) | m_dp_cntl;
	case DP_DATATYPE:           return m_dp_datatype;
	case DP_MIX:                return m_dp_mix;
	case DP_CNTL_XDIR_YDIR_YMAJOR:
		return (BIT(m_dp_cntl, 2) << 2) | (BIT(m_dp_cntl, 1) << 15) | (BIT(m_dp_cntl, 0) << 31);

	default:
		return m_regs[reg / 4];
	}
}

void ati_rage128_device::reg_w(u32 reg, u32 data, u32 mem_mask)
{
	reg &= (MMIO_SIZE - 1) & ~3;

	// The configuration mirror is read-only apart from the command register:
	// Mac OS X sets bus master enable through MMIO 0xf04.
	if (reg >= PCI_MIRROR && reg < PCI_MIRROR + 0x100)
	{
		if ((reg == PCI_MIRROR + 0x04) && ACCESSING_BITS_0_15)
			command_w(0, u16(data), u16(mem_mask));
		return;
	}

	const u32 old = m_regs[reg / 4];
	u32 val = old;
	COMBINE_DATA(&val);

	if (reg >= BRUSH_DATA0 && reg <= BRUSH_DATA63)
	{
		m_regs[reg / 4] = val;
		return;
	}

	if (reg >= HOST_DATA0 && reg <= HOST_DATA_LAST)
	{
		m_regs[reg / 4] = val;
		host_data_w(val, reg == HOST_DATA_LAST);
		return;
	}

	// PIO FIFO window.  Linux and Mac OS X alternate between PM4_FIFO_DATA_EVEN and
	// _ODD, but the Mac OS 9 resource manager also writes packets to consecutive
	// addresses from 0x1000 (up to 0x1014 seen).  The window size is assumed, after
	// the Radeon's 512-byte primary queue aperture at the same address.
	if (reg >= PM4_FIFO_DATA_EVEN && reg < PM4_FIFO_WINDOW_END)
	{
		cce_write(val);
		return;
	}

	switch (reg)
	{
	case MM_DATA:
	{
		const u32 index = m_regs[MM_INDEX / 4];
		if (BIT(index, 31))
			aperture_w(index & 0x7fff'ffff, data, mem_mask, false);
		else if ((index & ~3) != MM_DATA && (index & ~3) != MM_INDEX)
			reg_w(index & 0x7fff'fffc, data, mem_mask);
		return;
	}

	case CLOCK_CNTL_DATA:
		m_regs[reg / 4] = val;
		if (BIT(m_regs[CLOCK_CNTL_INDEX / 4], 7))
			pll_w(m_regs[CLOCK_CNTL_INDEX / 4] & 0x3f, val);
		return;

	case CLOCK_CNTL_INDEX:
		m_regs[reg / 4] = val & 0x3bf;
		if (BIT(old ^ val, 8, 2))
			update_mode();
		return;

	case MEM_VGA_WP_SEL:
	case MEM_VGA_RP_SEL:
		m_regs[reg / 4] = val;
		m_vga->set_banks(m_regs[MEM_VGA_WP_SEL / 4], m_regs[MEM_VGA_RP_SEL / 4]);
		return;

	case GEN_INT_CNTL:
		m_regs[reg / 4] = val;
		update_irq();
		return;

	case GEN_INT_STATUS:
		// write 1 to acknowledge
		m_int_status &= ~(data & mem_mask);
		update_irq();
		return;

	case CRTC_GEN_CNTL:
	case CRTC_EXT_CNTL:
		m_regs[reg / 4] = val;
		// EXT_DISP_EN, CRTC_EN, DISP_REQ_EN_B / HSYNC_DIS, VSYNC_DIS, DISPLAY_DIS
		if ((old ^ val) & ((reg == CRTC_GEN_CNTL) ? 0x0700'0000 : 0x0000'0700))
		{
			LOGCRTC("%s: %s %08x -> %08x (mask %08x), display %s\n", machine().describe_context(),
					(reg == CRTC_GEN_CNTL) ? "CRTC_GEN_CNTL" : "CRTC_EXT_CNTL", old, val, mem_mask, display_enabled() ? "on" : "blanked");
		}
		if (old != val)
			update_mode();
		return;

	case CRTC_H_TOTAL_DISP:
	case CRTC_H_SYNC_STRT_WID:
	case CRTC_V_TOTAL_DISP:
	case CRTC_V_SYNC_STRT_WID:
	case CRTC_PITCH:
		m_regs[reg / 4] = val;
		if (old != val)
			update_mode();
		return;

	case CRTC_OFFSET:
		m_regs[reg / 4] = val;
		// with CRTC_OFFSET_FLIP_CNTL set the new start address waits for vertical blank
		if (!BIT(m_regs[CRTC_OFFSET_CNTL / 4], 16))
			m_crtc_offset_latched = val & 0x07ff'ffff;
		return;

	case CRTC_VLINE_CRNT_VLINE:
		m_regs[reg / 4] = val & 0x7ff;
		arm_vline_timer();
		return;

	case CRTC_STATUS:
		if (BIT(data & mem_mask, 1))
			m_crtc_vblank_save = false;
		return;

	case DAC_CNTL:
		m_regs[reg / 4] = val;
		m_vga->set_dac_cntl(val);
		return;

	case PALETTE_INDEX:
		m_regs[reg / 4] = val;
		if (ACCESSING_BITS_0_7)
			m_vga->palette_write_index(val & 0xff);
		if (ACCESSING_BITS_16_23)
			m_vga->palette_read_index((val >> 16) & 0xff);
		return;

	case PALETTE_DATA:
		m_vga->palette_data_w(val);
		return;

	case GPIO_MONID:
		m_regs[reg / 4] = val;
		ddc_update();
		return;

	case I2C_CNTL_0:
		m_i2c_cntl0 = val & ~0x0000'0003;
		if (BIT(data & mem_mask, 0))     // write 1 clears DONE
			m_i2c_cntl0 &= ~1;
		if (BIT(val, 5))                 // soft reset
			m_i2c_fifo_count = m_i2c_fifo_rd = 0;
		if (BIT(val, 12))
			i2c_go();
		return;

	case I2C_CNTL_1:
		m_i2c_cntl1 = val;
		return;

	case I2C_DATA:
		if (m_i2c_fifo_rd >= m_i2c_fifo_count)
			m_i2c_fifo_count = m_i2c_fifo_rd = 0;
		if (m_i2c_fifo_count < I2C_FIFO_SIZE)
			m_i2c_fifo[m_i2c_fifo_count++] = val & 0xff;
		return;

	case CONFIG_CNTL:
		m_regs[reg / 4] = val & 0x0000'ffff;
		return;

	case CONFIG_MEMSIZE:
	case CONFIG_APER_0_BASE:
	case CONFIG_APER_1_BASE:
	case CONFIG_APER_SIZE:
	case CONFIG_REG_1_BASE:
	case CONFIG_REG_APER_SIZE:
	case CONFIG_XSTRAP:
	case CONFIG_BONDS:
	case GUI_STAT:
	case PM4_STAT:
		return;

	case GEN_RESET_CNTL:
		m_regs[reg / 4] = val;
		if (BIT(val, 0) && !BIT(old, 0))
		{
			LOGENGINE("GUI soft reset\n");
			gui_reset();
		}
		return;

	case CUR_OFFSET:
		m_regs[reg / 4] = val;
		// The cursor is fetched in whole 16-byte rows, so the low four bits are
		// ignored.  Both Mac OS 9 ndrvs put the image at 64 and add a row offset
		// for a cursor clipped at the top that only works if they're dropped: the
		// ROM ndrv adds yoff * 2, the "ATI Driver Update" one (yoff * 16) & 0xff.
		m_cursor_offset = val & 0x03ff'fff0;
		return;

	case OV0_SCALE_CNTL:
		m_regs[reg / 4] = val;
		if (BIT(val, 31))
			m_regs[reg / 4] &= 0x7fff'ffff;  // soft reset is self-clearing
		return;

	// CCE / PM4
	case PM4_BUFFER_CNTL:
		m_regs[reg / 4] = val;
		LOGCCE("%s: PM4_BUFFER_CNTL = %08x (mode %d, size 2^%d qwords)\n", machine().describe_context(), val, val >> 28, val & 0x1f);
		if (!cce_bm_mode() && !cce_pio_mode())
			m_packet_count = m_packet_remaining = 0;
		return;

	case PM4_BUFFER_DL_WPTR:
		m_regs[reg / 4] = val;
		cce_run_ring();
		return;

	case PM4_BUFFER_DL_RPTR:
		m_regs[reg / 4] = val & 0x7fff'ffff;
		return;

	case PM4_MICRO_CNTL:
		m_regs[reg / 4] = val;
		if (val & PM4_MICRO_FREERUN)
			cce_run_ring();
		return;

	case PM4_MICROCODE_ADDR:
		m_pm4_ucode_addr = val & 0xff;
		return;

	case PM4_MICROCODE_RADDR:
		m_pm4_ucode_raddr = val & 0xff;
		return;

	case PM4_MICROCODE_DATAH:
		m_pm4_microcode[m_pm4_ucode_addr & 0xff][0] = val;
		return;

	case PM4_MICROCODE_DATAL:
		m_pm4_microcode[m_pm4_ucode_addr & 0xff][1] = val;
		m_pm4_ucode_addr = (m_pm4_ucode_addr + 1) & 0xff;
		return;

	case PM4_IW_INDOFF:
		m_regs[reg / 4] = val;
		return;

	case PM4_IW_INDSIZE:
		// Mac OS X sends INDOFF and INDSIZE as one two-register Type 0 packet
		// through the PIO FIFO, with the size masked to 23 bits
		m_regs[reg / 4] = val;
		cce_run_indirect(m_regs[PM4_IW_INDOFF / 4], val & 0x007f'ffff);
		return;

	// GUI engine
	case DST_OFFSET:            m_dst_offset = val & 0x03ff'ffff; return;
	case DST_PITCH:             m_dst_pitch = val & 0x3ff; return;
	case SRC_OFFSET:            m_src_offset = val & 0x03ff'ffff; return;
	case SRC_PITCH:             m_src_pitch = val & 0x3ff; return;
	case DST_PITCH_OFFSET:
		m_dst_offset = (val & 0x1f'ffff) << 5;
		m_dst_pitch = (val >> 21) & 0x3ff;
		if (BIT(val, 31) && !m_dst_tile)
			LOGTILE("2D destination tiled: DST_PITCH_OFFSET %08x\n", val);
		m_dst_tile = BIT(val, 31);
		return;
	case SRC_PITCH_OFFSET:
		m_src_offset = (val & 0x1f'ffff) << 5;
		m_src_pitch = (val >> 21) & 0x3ff;
		if (BIT(val, 31) && !m_src_tile)
			LOGTILE("2D source tiled: SRC_PITCH_OFFSET %08x\n", val);
		m_src_tile = BIT(val, 31);
		return;
	case DST_X:                 m_dst_x = s16(val & 0xffff); return;
	case DST_Y:                 m_dst_y = s16(val & 0xffff); return;
	case SRC_X:                 m_src_x = s16(val & 0xffff); return;
	case SRC_Y:                 m_src_y = s16(val & 0xffff); return;
	case DST_X_SUB:             m_dst_x = s32(val) >> 4; return;
	case DST_Y_SUB:             m_dst_y = s32(val) >> 4; return;
	case DST_Y_X:               m_dst_y = hi16s(val); m_dst_x = lo16s(val); return;
	case DST_X_Y:               m_dst_x = hi16s(val); m_dst_y = lo16s(val); return;
	case SRC_Y_X:               m_src_y = hi16s(val); m_src_x = lo16s(val); return;
	case SRC_X_Y:               m_src_x = hi16s(val); m_src_y = lo16s(val); return;
	case DST_WIDTH:             m_dst_width = val & 0x3fff; return;

	case DST_HEIGHT:
		m_dst_height = val & 0x3fff;
		gui_trigger_rect();
		return;
	case DST_HEIGHT_WIDTH:
	case DST_HEIGHT_WIDTH_8:
	case DST_HEIGHT_WIDTH_BW:
		m_dst_height = (val >> 16) & 0x3fff;
		m_dst_width = val & 0x3fff;
		gui_trigger_rect();
		return;
	case DST_WIDTH_HEIGHT:
		m_dst_width = (val >> 16) & 0x3fff;
		m_dst_height = val & 0x3fff;
		gui_trigger_rect();
		return;
	case DST_HEIGHT_Y:
		m_dst_height = (val >> 16) & 0x3fff;
		m_dst_y = lo16s(val);
		gui_trigger_rect();
		return;
	case DST_WIDTH_X:
	case DST_WIDTH_X_INCY:
		m_dst_width = (val >> 16) & 0x3fff;
		m_dst_x = lo16s(val);
		gui_trigger_rect();
		return;

	case DST_BRES_LNTH:
	case DST_BRES_LNTH_SUB:
		m_regs[reg / 4] = val;
		gui_trigger_line();
		return;

	case SC_LEFT:               m_sc_left = s16(val & 0xffff); return;
	case SC_RIGHT:              m_sc_right = s16(val & 0xffff); return;
	case SC_TOP:                m_sc_top = s16(val & 0xffff); return;
	case SC_BOTTOM:             m_sc_bottom = s16(val & 0xffff); return;
	case SC_TOP_LEFT:           m_sc_top = hi16s(val); m_sc_left = lo16s(val); return;
	case SC_BOTTOM_RIGHT:       m_sc_bottom = hi16s(val); m_sc_right = lo16s(val); return;
	case SRC_SC_RIGHT:          m_src_sc_right = s16(val & 0xffff); return;
	case SRC_SC_BOTTOM:         m_src_sc_bottom = s16(val & 0xffff); return;
	case SRC_SC_BOTTOM_RIGHT:   m_src_sc_bottom = hi16s(val); m_src_sc_right = lo16s(val); return;

	case DP_GUI_MASTER_CNTL:
		gmc_w(val);
		return;

	case DP_CNTL:
		// directions and Y major in m_dp_cntl; the rest (DST_LAST_PEL) in m_regs
		m_regs[reg / 4] = val;
		m_dp_cntl = val & 0x7;
		return;

	case DP_CNTL_XDIR_YDIR_YMAJOR:
		m_dp_cntl = (BIT(val, 31) << 0) | (BIT(val, 15) << 1) | (BIT(val, 2) << 2);
		return;

	case DP_DATATYPE:
		m_dp_datatype = val;
		return;

	case DP_MIX:
		m_dp_mix = val;
		return;

	case DEFAULT_PITCH:
		m_regs[reg / 4] = val & 0x3ff;
		return;

	case WAIT_UNTIL:
		// Engine idle, vline and flip waits.  Everything has already finished,
		// and the CCE isn't stalled for display events yet.
		m_regs[reg / 4] = val;
		LOGCCE("WAIT_UNTIL %08x\n", val);
		return;

	// texture palette: an entry number, then data that auto-increments it
	case TEX_PALETTE_WR_INDEX:
		m_regs[reg / 4] = val;
		m_tex_pal_index = val & 0xff;
		return;

	case TEX_PALETTE_WR:
		m_regs[reg / 4] = val;
		m_tex_palette[m_tex_pal_index++] = val;
		return;

	case SCALE_DST_HEIGHT_WIDTH:
		m_regs[reg / 4] = val;
		scaler_blit();
		return;

	case BM_QUEUE_STATUS:
		return;

	case SURFACE0_LOWER_BOUND: case SURFACE0_UPPER_BOUND: case SURFACE0_INFO:
	case SURFACE0_LOWER_BOUND + 0x10: case SURFACE0_UPPER_BOUND + 0x10: case SURFACE0_INFO + 0x10:
	case SURFACE0_LOWER_BOUND + 0x20: case SURFACE0_UPPER_BOUND + 0x20: case SURFACE0_INFO + 0x20:
	case SURFACE0_LOWER_BOUND + 0x30: case SURFACE0_UPPER_BOUND + 0x30: case SURFACE0_INFO + 0x30:
		m_regs[reg / 4] = val;
		if (old != val)
			update_surfaces();
		return;

	case CRTC_OFFSET_CNTL:
		m_regs[reg / 4] = val;
		if (BIT(val, 15) && !BIT(old, 15))
			LOGTILE("CRTC_OFFSET_CNTL = %08x: tiled scanout not supported\n", val);
		return;

	case PCI_GART_PAGE:
	case AGP_BASE:
	case AGP_APER_OFFSET:
		m_regs[reg / 4] = val;
		gart_flush();
		return;

	case BM_GUI_TABLE:
		m_regs[reg / 4] = val;
		if (BIT(val, 0))
			gui_dma_run(val & ~0xfU);
		return;

	default:
		m_regs[reg / 4] = val;
		return;
	}
}


/***************************************************************************
    Clock synthesiser
***************************************************************************/

u32 ati_rage128_device::pll_r(int index)
{
	return m_pll[index & 0x3f];
}

void ati_rage128_device::pll_w(int index, u32 data)
{
	index &= 0x3f;
	LOGCRTC("%s: PLL[%02x] = %08x\n", machine().describe_context(), index, data);
	switch (index)
	{
	case PLL_PPLL_REF_DIV:
	case PLL_P2PLL_REF_DIV:
		// atomic updates complete immediately, so the update bit never reads back as set
		m_pll[index] = data & ~0x8000;
		break;

	default:
		m_pll[index] = data;
		break;
	}

	if ((index >= PLL_PPLL_CNTL && index <= PLL_HTOTAL_CNTL))
		update_mode();
}

// pixel clock in Hz for the primary CRTC
double ati_rage128_device::pixel_clock() const
{
	const double ref = REF_CLOCK;
	if ((m_pll[PLL_VCLK_ECP_CNTL] & 3) != 3)
		return ref;

	const int sel = BIT(m_regs[CLOCK_CNTL_INDEX / 4], 8, 2);
	const u32 div = m_pll[PLL_PPLL_DIV_0 + sel];
	const u32 fb = div & 0x7ff;
	const int post = PLL_POST_DIV[BIT(div, 16, 3)];
	const u32 ref_div = m_pll[PLL_PPLL_REF_DIV] & 0x3ff;
	if (!fb || !ref_div)
		return 25'175'000.0;
	const double vco = ref * double(fb) / double(ref_div);
	if (vco < VCO_MIN || vco > VCO_MAX)
		LOGCRTC("PPLL VCO %.2f MHz is outside the 118-236 MHz range\n", vco / 1e6);
	return vco / double(post);
}


/***************************************************************************
    Display timing and interrupts
***************************************************************************/

void ati_rage128_device::update_mode()
{
	const u32 gen = m_regs[CRTC_GEN_CNTL / 4];
	const bool native = BIT(gen, 24);

	if (native != m_native_mode)
		LOGCRTC("%s display mode\n", native ? "native" : "VGA");
	m_native_mode = native;
	m_vga->set_native(native);

	if (!native)
	{
		// the VGA core has taken the screen back; force a reconfigure next time
		m_cfg_htotal = 0;
		return;
	}

	const u32 h = m_regs[CRTC_H_TOTAL_DISP / 4];
	const u32 v = m_regs[CRTC_V_TOTAL_DISP / 4];
	const int htotal = (BIT(h, 0, 9) + 1) * 8;
	const int hdisp = (BIT(h, 16, 8) + 1) * 8;
	const int vtotal = BIT(v, 0, 11) + 1;
	const int vdisp = BIT(v, 16, 11) + 1;

	if (htotal <= 8 || vtotal <= 1 || hdisp > htotal || vdisp > vtotal)
		return;

	const double pclk = pixel_clock();
	if (pclk <= 0.0)
		return;

	// cursor and blanking bits share CRTC_GEN_CNTL, so only reconfigure on a real timing change
	const u32 pclk_hz = u32(pclk);
	if (htotal == m_cfg_htotal && vtotal == m_cfg_vtotal && hdisp == m_cfg_hdisp && vdisp == m_cfg_vdisp && pclk_hz == m_cfg_pclk)
		return;
	m_cfg_htotal = htotal;
	m_cfg_vtotal = vtotal;
	m_cfg_hdisp = hdisp;
	m_cfg_vdisp = vdisp;
	m_cfg_pclk = pclk_hz;

	const rectangle visarea(0, hdisp - 1, 0, vdisp - 1);
	m_screen->configure(htotal, vtotal, visarea, attotime::from_ticks(u64(htotal) * vtotal, pclk_hz));
	LOGCRTC("native mode %dx%d (total %dx%d), %.3f MHz, format %d\n", hdisp, vdisp, htotal, vtotal, pclk / 1e6, BIT(gen, 8, 3));

	arm_vline_timer();
}

void ati_rage128_device::update_irq()
{
	const bool state = (m_int_status & m_regs[GEN_INT_CNTL / 4]) != 0;
	if (state != m_irq_state)
	{
		m_irq_state = state;
		irq_pin_w(0, state);
	}
}

void ati_rage128_device::vblank_w(int state)
{
	if (!state)
		return;

	m_frame_count++;
	m_crtc_vblank_save = true;

	// latch a pending flip
	if (BIT(m_regs[CRTC_OFFSET_CNTL / 4], 16))
		m_crtc_offset_latched = m_regs[CRTC_OFFSET / 4] & 0x07ff'ffff;

	m_int_status |= INT_CRTC_VBLANK | INT_CRTC_VSYNC;
	update_irq();
}

void ati_rage128_device::arm_vline_timer()
{
	const int line = m_regs[CRTC_VLINE_CRNT_VLINE / 4] & 0x7ff;
	if (line >= m_screen->height())
	{
		m_vline_timer->adjust(attotime::never);
		return;
	}
	m_vline_timer->adjust(m_screen->time_until_pos(line));
}

TIMER_CALLBACK_MEMBER(ati_rage128_device::vline_tick)
{
	m_int_status |= INT_CRTC_VLINE;
	update_irq();
	// the same line comes round once per frame
	m_vline_timer->adjust(m_screen->frame_period());
}


/***************************************************************************
    Screen update
***************************************************************************/

// The display is blanked with CRTC_EN clear (CRTC_GEN_CNTL bit 25) or
// CRTC_DISPLAY_DIS set (CRTC_EXT_CNTL bit 10), which is what aty128fb and the
// X.Org r128 driver use for blanking and DPMS.  CRTC_DISP_REQ_EN_B (CRTC_GEN_CNTL
// bit 26) only gates the CRTC's memory requests and isn't treated as a blank.
bool ati_rage128_device::display_enabled() const
{
	return BIT(m_regs[CRTC_GEN_CNTL / 4], 25) && !BIT(m_regs[CRTC_EXT_CNTL / 4], 10);
}

u32 ati_rage128_device::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	perf_tick();
	if (VERBOSE & LOG_PROBE)
	{
		std::vector<probe_event> events;
		{
			std::lock_guard<std::mutex> lock(m_probe_lock);
			events.swap(m_probe);
		}
		if (!events.empty())
			LOGPROBE("probe %d,%d, screen update %u:\n", PROBE_X, PROBE_Y, m_probe_frame++);
		for (const probe_event &e : events)
		{
			LOGPROBE("  %c MISC %08x TEX_CNTL_C %08x tex %08x st %f,%f texel %08x z %04x/%04x dst %08x -> %08x\n",
					e.why, e.misc, e.tex_cntl, e.tex_base, e.s, e.t, e.texel, e.z, e.zstored, e.old, e.res);
		}
	}

	if (!m_native_mode)
	{
		wait_for_render();
		return m_vga->screen_update(screen, bitmap, cliprect);
	}

	// wait only for queued drawing into the lines shown and the cursor image
	{
		const u32 pitch = BIT(m_regs[CRTC_PITCH / 4], 0, 10) * 8 * 4;     // at up to 32 bpp
		const s32 last = BIT(m_regs[CRTC_GEN_CNTL / 4], 0) ? (cliprect.bottom() >> 1) : cliprect.bottom();
		u32 lo, hi;
		rows_span(m_crtc_offset_latched, pitch, 1, 0, last, 0, lo, hi);
		wait_for_vram_span(lo, hi, false);
		wait_for_vram_span(m_cursor_offset & m_vram_mask, (m_cursor_offset & m_vram_mask) + 64 * 16, false);
	}

	// TODO: blank when !display_enabled().  Not done for now: the Mac OS 9 ROM ndrv
	// saves and restores CRTC_GEN_CNTL byte 3 with an unaligned 32-bit read around
	// its monitor sense, and this appears to be mishandled outside the device (CRTC_EN
	// ends up clear).  Changes are still logged under LOG_CRTC.
	draw_native(bitmap, cliprect);
	draw_cursor(bitmap, cliprect);
	return 0;
}

void ati_rage128_device::draw_native(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	const u32 gen = m_regs[CRTC_GEN_CNTL / 4];
	const int format = BIT(gen, 8, 3);
	const bool dblscan = BIT(gen, 0);
	const u32 base = m_crtc_offset_latched;
	const u32 pitch_px = BIT(m_regs[CRTC_PITCH / 4], 0, 10) * 8;
	const u8 mask = m_vga->dac_mask();

	int bits;
	switch (format)
	{
	case 1: bits = 4; break;
	case 2: case 7: bits = 8; break;
	case 3: case 4: bits = 16; break;
	case 5: bits = 24; break;
	case 6: bits = 32; break;
	default:
		bitmap.fill(rgb_t::black(), cliprect);
		return;
	}
	const u32 pitch = pitch_px * bits / 8;

	// gamma: in the direct colour modes each component indexes the palette
	auto gamma = [this] (u8 r, u8 g, u8 b) -> u32 {
		return (m_vga->palette_entry(0, r) & 0xff'0000) | (m_vga->palette_entry(0, g) & 0x00'ff00) | (m_vga->palette_entry(0, b) & 0x00'00ff);
	};

	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		const int sy = dblscan ? (y >> 1) : y;
		const u32 line = base + sy * pitch;
		u32 *dst = &bitmap.pix(y, cliprect.left());

		for (int x = cliprect.left(); x <= cliprect.right(); x++)
		{
			u32 rgb = 0;
			switch (format)
			{
			case 1:
			{
				const u8 pix = m_vram[(line + (x >> 1)) & m_vram_mask];
				rgb = m_vga->palette_entry(0, (BIT(x, 0) ? (pix & 0x0f) : (pix >> 4)) & mask);
				break;
			}
			case 2:
				rgb = m_vga->palette_entry(0, m_vram[(line + x) & m_vram_mask] & mask);
				break;
			case 7:
			{
				// direct 3:3:2
				const u8 pix = m_vram[(line + x) & m_vram_mask];
				rgb = gamma(pal3bit(pix >> 5), pal3bit((pix >> 2) & 7), pal2bit(pix & 3));
				break;
			}
			case 3:
			{
				const u32 pix = get_u16le(&m_vram[(line + x * 2) & m_vram_mask]);
				rgb = gamma(BIT(pix, 10, 5) << 3, BIT(pix, 5, 5) << 3, BIT(pix, 0, 5) << 3);
				break;
			}
			case 4:
			{
				const u32 pix = get_u16le(&m_vram[(line + x * 2) & m_vram_mask]);
				rgb = gamma(BIT(pix, 11, 5) << 3, BIT(pix, 5, 6) << 2, BIT(pix, 0, 5) << 3);
				break;
			}
			case 5:
			{
				const u32 pix = get_u24le(&m_vram[(line + x * 3) & m_vram_mask]);
				rgb = gamma(pix >> 16, pix >> 8, pix);
				break;
			}
			case 6:
			{
				const u32 pix = get_u32le(&m_vram[(line + x * 4) & m_vram_mask]);
				rgb = gamma(pix >> 16, pix >> 8, pix);
				break;
			}
			}
			*dst++ = rgb;
		}
	}

	if (BIT(m_regs[OV0_SCALE_CNTL / 4], 30))
		draw_overlay(bitmap, cliprect, bits, base, pitch);
}

// Overlay scaler: composites a scaled RGB or YUV surface over the graphics
// using the graphics/video colour key functions.
void ati_rage128_device::draw_overlay(bitmap_rgb32 &bitmap, const rectangle &cliprect, int bits, u32 base, u32 pitch)
{
	const u32 cntl = m_regs[OV0_SCALE_CNTL / 4];
	const int srcfmt = BIT(cntl, 8, 4);
	const u32 start = m_regs[OV0_Y_X_START / 4];
	const u32 end = m_regs[OV0_Y_X_END / 4];
	const int x0 = start & 0x7ff, y0 = (start >> 16) & 0x7ff;
	const int x1 = end & 0x7ff, y1 = (end >> 16) & 0x7ff;
	if (x1 <= x0 || y1 <= y0)
		return;

	const u32 buf0 = m_regs[OV0_VID_BUF0_BASE_ADRS / 4] & 0x03ff'fff0;
	const u32 buf1 = m_regs[OV0_VID_BUF1_BASE_ADRS / 4] & 0x03ff'fff0;
	const u32 buf2 = m_regs[OV0_VID_BUF2_BASE_ADRS / 4] & 0x03ff'fff0;
	const u32 pitch0 = m_regs[OV0_VID_BUF_PITCH0_VALUE / 4] & 0xf'ffff;
	const u32 pitch1 = m_regs[OV0_VID_BUF_PITCH1_VALUE / 4] & 0xf'ffff;
	const int step_by = std::max<int>(1, m_regs[OV0_STEP_BY / 4] & 0x7);
	const u32 h_inc = (m_regs[OV0_H_INC / 4] & 0x3fff) << (step_by - 1);  // 4.12
	const u32 v_inc = m_regs[OV0_V_INC / 4] & 0x03ff'ffff;                 // 6.20
	const int src_left = BIT(m_regs[OV0_P1_X_START_END / 4], 16, 4);

	const u32 key = m_regs[OV0_KEY_CNTL / 4];
	const u32 gkey = m_regs[OV0_GRAPHICS_KEY_CLR / 4], gmsk = m_regs[OV0_GRAPHICS_KEY_MSK / 4];
	const u32 vkey = m_regs[OV0_VIDEO_KEY_CLR / 4], vmsk = m_regs[OV0_VIDEO_KEY_MSK / 4];

	auto keyfn = [] (int fn, u32 a, u32 b, u32 m) -> bool {
		switch (fn & 7)
		{
		case 0: return false;
		case 1: return true;
		case 4: return (a & m) == (b & m);
		case 5: return (a & m) != (b & m);
		default: return false;
		}
	};

	auto yuv = [] (int y, int u, int v) -> u32 {
		const float yy = 1.164f * (y - 16);
		const float uu = u - 128.0f, vv = v - 128.0f;
		return (u32(clamp8(yy + 1.596f * vv)) << 16) | (u32(clamp8(yy - 0.813f * vv - 0.391f * uu)) << 8) | clamp8(yy + 2.018f * uu);
	};

	const int ymin = std::max(y0, cliprect.top()), ymax = std::min(y1 - 1, cliprect.bottom());
	const int xmin = std::max(x0, cliprect.left()), xmax = std::min(x1 - 1, cliprect.right());

	for (int y = ymin; y <= ymax; y++)
	{
		const u32 sy = u32((u64(y - y0) * v_inc) >> 20);
		const u32 gline = base + y * pitch;
		u32 *dst = &bitmap.pix(y);

		for (int x = xmin; x <= xmax; x++)
		{
			const u32 sx = src_left + u32((u64(x - x0) * h_inc) >> 12);

			// raw graphics pixel for the key comparison
			u32 graw = 0;
			switch (bits)
			{
			case 4: graw = (m_vram[(gline + (x >> 1)) & m_vram_mask] >> (BIT(x, 0) ? 0 : 4)) & 0xf; break;
			case 8: graw = m_vram[(gline + x) & m_vram_mask]; break;
			case 16: graw = get_u16le(&m_vram[(gline + x * 2) & m_vram_mask]); break;
			case 24: graw = get_u24le(&m_vram[(gline + x * 3) & m_vram_mask]); break;
			case 32: graw = get_u32le(&m_vram[(gline + x * 4) & m_vram_mask]) & 0xff'ffff; break;
			}

			u32 video = 0;
			switch (srcfmt)
			{
			case 3:
			{
				const u32 p = get_u16le(&m_vram[(buf0 + sy * pitch0 + sx * 2) & m_vram_mask]);
				video = (u32(pal5bit(p >> 10)) << 16) | (u32(pal5bit(p >> 5)) << 8) | pal5bit(p);
				break;
			}
			case 4:
			{
				const u32 p = get_u16le(&m_vram[(buf0 + sy * pitch0 + sx * 2) & m_vram_mask]);
				video = (u32(pal5bit(p >> 11)) << 16) | (u32(pal6bit(p >> 5)) << 8) | pal5bit(p);
				break;
			}
			case 6:
				video = get_u32le(&m_vram[(buf0 + sy * pitch0 + sx * 4) & m_vram_mask]) & 0xff'ffff;
				break;
			case 0xb:   // Y0 U Y1 V
			case 0xc:   // U Y0 V Y1
			{
				const u32 pair = buf0 + sy * pitch0 + (sx & ~1) * 2;
				const u8 *p = &m_vram[pair & m_vram_mask & ~3];
				int y_, u_, v_;
				if (srcfmt == 0xb)
				{
					y_ = BIT(sx, 0) ? p[2] : p[0];
					u_ = p[1];
					v_ = p[3];
				}
				else
				{
					y_ = BIT(sx, 0) ? p[3] : p[1];
					u_ = p[0];
					v_ = p[2];
				}
				video = yuv(y_, u_, v_);
				break;
			}
			case 0xa:   // planar 4:2:0, Y in buffer 0, U and V in buffers 1 and 2
			case 0x9:   // planar 4:1:0
			{
				const int sub = (srcfmt == 0xa) ? 1 : 2;
				const int y_ = m_vram[(buf0 + sy * pitch0 + sx) & m_vram_mask];
				const u32 cofs = (sy >> sub) * pitch1 + (sx >> sub);
				const int u_ = m_vram[(buf1 + cofs) & m_vram_mask];
				const int v_ = m_vram[(buf2 + cofs) & m_vram_mask];
				video = yuv(y_, u_, v_);
				break;
			}
			default:
				LOGUNIMPL("overlay source format %x not supported\n", srcfmt);
				return;
			}

			const bool gres = keyfn(BIT(key, 4, 3), graw, gkey, gmsk);
			const bool vres = keyfn(BIT(key, 0, 3), video, vkey, vmsk);
			const bool show_graphics = BIT(key, 8) ? (gres && vres) : (gres || vres);
			if (!show_graphics)
				dst[x] = video;
		}
	}
}

// 64x64 two-plane cursor: each 16 byte row is 64 AND bits then 64 XOR bits, MSB first
void ati_rage128_device::draw_cursor(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	if (!BIT(m_regs[CRTC_GEN_CNTL / 4], 16))
		return;

	const u32 posn = m_regs[CUR_HORZ_VERT_POSN / 4];
	const u32 off = m_regs[CUR_HORZ_VERT_OFF / 4];
	const int px = (posn >> 16) & 0x7ff, py = posn & 0x7ff;
	const int xoff = (off >> 16) & 0x3f, yoff = off & 0x3f;
	const u32 clr0 = m_regs[CUR_CLR0 / 4] & 0xff'ffff;
	const u32 clr1 = m_regs[CUR_CLR1 / 4] & 0xff'ffff;
	const bool dblscan = BIT(m_regs[CRTC_GEN_CNTL / 4], 0);

	for (int row = 0; row < 64 - yoff; row++)
	{
		const u32 rowaddr = m_cursor_offset + row * 16;
		for (int rep = 0; rep < (dblscan ? 2 : 1); rep++)
		{
			const int y = py + (dblscan ? (row * 2 + rep) : row);
			if (y < cliprect.top() || y > cliprect.bottom())
				continue;
			u32 *const line = &bitmap.pix(y);

			for (int col = xoff; col < 64; col++)
			{
				const int x = px + col - xoff;
				if (x < cliprect.left() || x > cliprect.right())
					continue;
				const int bit = ~col & 7;
				const bool and_bit = BIT(m_vram[(rowaddr + (col >> 3)) & m_vram_mask], bit);
				const bool xor_bit = BIT(m_vram[(rowaddr + 8 + (col >> 3)) & m_vram_mask], bit);
				if (!and_bit)
					line[x] = xor_bit ? clr1 : clr0;
				else if (xor_bit)
					line[x] ^= 0x00ff'ffff;
			}
		}
	}
}


/***************************************************************************
    DDC monitor, GPIO_MONID and the I2C engine
***************************************************************************/

// A generic EDID 1.3 block for a fixed-frequency VGA monitor that only accepts
// 640x480; there is no specific monitor to copy.
void ati_rage128_device::build_edid()
{
	u8 *const e = m_edid;
	std::fill_n(e, 128, 0);

	constexpr u8 header[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	std::copy(std::begin(header), std::end(header), e);

	// manufacturer "MAM" (compressed ASCII), product 0x0128, serial 1, week 1 of 2000
	const u16 mfg = ((('M' - '@') << 10) | (('A' - '@') << 5) | ('M' - '@'));
	e[8] = mfg >> 8;
	e[9] = mfg & 0xff;
	e[10] = 0x28; e[11] = 0x01;
	e[12] = 0x01;
	e[16] = 1;
	e[17] = 2000 - 1990;
	e[18] = 1; e[19] = 3;           // EDID 1.3
	e[20] = 0x0e;                   // analog, 0.7/0.0 V, separate/composite sync supported
	e[21] = 32; e[22] = 24;         // 32x24 cm
	e[23] = 120;                    // gamma 2.20
	e[24] = 0xea;                   // DPMS standby/suspend/off, RGB colour, preferred timing in DTD 1

	// chromaticity (sRGB-like)
	constexpr u8 chroma[10] = { 0x5e, 0xc0, 0xa4, 0x59, 0x4a, 0x98, 0x25, 0x20, 0x50, 0x54 };
	std::copy(std::begin(chroma), std::end(chroma), &e[25]);

	// established timings: 640x480 at 60, 72 and 75 Hz only
	e[35] = 0x2c;
	e[36] = 0x00;
	e[37] = 0x00;

	// standard timings: all unused
	std::fill_n(&e[38], 16, 0x01);

	// detailed timing 1: 640x480@60 Hz (VESA DMT), 25.175 MHz, H 800 total, V 525 total
	{
		u8 *d = &e[54];
		const int clk = 2518, ha = 640, hb = 160, va = 480, vb = 45, hso = 16, hsw = 96, vso = 10, vsw = 2;
		d[0] = clk & 0xff; d[1] = clk >> 8;
		d[2] = ha & 0xff; d[3] = hb & 0xff; d[4] = ((ha >> 8) << 4) | (hb >> 8);
		d[5] = va & 0xff; d[6] = vb & 0xff; d[7] = ((va >> 8) << 4) | (vb >> 8);
		d[8] = hso & 0xff; d[9] = hsw & 0xff;
		d[10] = ((vso & 0xf) << 4) | (vsw & 0xf);
		d[11] = ((hso >> 8) << 6) | ((hsw >> 8) << 4) | ((vso >> 4) << 2) | (vsw >> 4);
		d[12] = 0x40; d[13] = 0x2c; d[14] = 0x21;     // 320x300 mm
		d[15] = 0; d[16] = 0;
		d[17] = 0x18;                                 // separate sync, -H -V
	}

	// Range limits just wide enough for the three 640x480 modes (31.5-37.9 kHz),
	// so that drivers deriving modes from them can't go above 640x480:
	// 59-76 Hz vertical, 31-38 kHz horizontal, 40 MHz
	{
		u8 *d = &e[72];
		d[3] = 0xfd;
		d[5] = 59; d[6] = 76; d[7] = 31; d[8] = 38; d[9] = 4;
		d[10] = 0x00; d[11] = 0x0a;
		for (int i = 12; i < 18; i++)
			d[i] = 0x20;
	}

	// monitor name
	{
		u8 *d = &e[90];
		d[3] = 0xfc;
		constexpr char name[] = "MAME MONITOR";
		int i = 0;
		for (; name[i] && i < 13; i++)
			d[5 + i] = name[i];
		if (i < 13)
			d[5 + i++] = 0x0a;
		for (; i < 13; i++)
			d[5 + i] = 0x20;
	}

	// dummy descriptor
	e[108 + 3] = 0x10;

	u8 sum = 0;
	for (int i = 0; i < 127; i++)
		sum += e[i];
	e[127] = u8(0x100 - sum);
}

// level on each of the four MONID pins: driven low when the output is enabled
// with A=0, otherwise pulled up (and the monitor may pull the DDC data line low)
int ati_rage128_device::monid_pins()
{
	const u32 r = m_regs[GPIO_MONID / 4];
	int pins = 0;
	for (int i = 0; i < 4; i++)
	{
		const bool driven = BIT(r, 16 + i);
		pins |= (driven ? BIT(r, i) : 1) << i;
	}

	if ((m_monitor->read() & 3) == 1)
	{
		// VGA DDC: SDA on MONID1, SCL on MONID2
		if (!m_ddc_sda_out)
			pins &= ~0x2;
	}
	return pins;
}

void ati_rage128_device::ddc_update()
{
	const u32 r = m_regs[GPIO_MONID / 4];
	const int sda = BIT(r, 17) ? BIT(r, 1) : 1;
	const int scl = BIT(r, 18) ? BIT(r, 2) : 1;
	if ((m_monitor->read() & 3) == 1)
		ddc_clock(scl, sda);
}

// I2C slave at 0x50: random read of the EDID EEPROM
void ati_rage128_device::ddc_clock(int scl, int sda)
{
	enum { IDLE = 0, ADDR, WDATA, RDATA, WAIT_STOP };

	if (scl && m_ddc_scl && sda != m_ddc_sda_in)
	{
		// SDA changing with SCL high is a start or stop
		if (!sda)
		{
			m_ddc_state = ADDR;
			m_ddc_bits = 0;
			m_ddc_shift = 0;
		}
		else
			m_ddc_state = IDLE;
		m_ddc_sda_out = 1;
	}
	else if (scl && !m_ddc_scl)
	{
		// rising edge: sample
		switch (m_ddc_state)
		{
		case ADDR:
		case WDATA:
			if (m_ddc_bits < 8)
			{
				m_ddc_shift = (m_ddc_shift << 1) | (sda ? 1 : 0);
				m_ddc_bits++;
			}
			break;

		case RDATA:
			if (m_ddc_bits < 8)
				m_ddc_bits++;
			else if (sda)
				m_ddc_state = WAIT_STOP;    // master NAK ends the read
			else
				m_ddc_bits = 9;             // master ACK, another byte follows
			break;
		}
	}
	else if (!scl && m_ddc_scl)
	{
		// falling edge: the slave changes SDA
		switch (m_ddc_state)
		{
		case ADDR:
			if (m_ddc_bits == 8)
			{
				if ((m_ddc_shift >> 1) == 0x50)
				{
					m_ddc_read = BIT(m_ddc_shift, 0);
					m_ddc_sda_out = 0;
					m_ddc_bits = 9;
					LOGDDC("DDC address %02x (%s)\n", m_ddc_shift, m_ddc_read ? "read" : "write");
				}
				else
				{
					m_ddc_state = WAIT_STOP;
					m_ddc_sda_out = 1;
				}
			}
			else if (m_ddc_bits == 9)
			{
				m_ddc_bits = 0;
				m_ddc_shift = 0;
				if (m_ddc_read)
				{
					m_ddc_state = RDATA;
					m_ddc_shift = m_edid[m_ddc_ptr & 0x7f];
					m_ddc_ptr = (m_ddc_ptr + 1) & 0xff;
					m_ddc_sda_out = BIT(m_ddc_shift, 7);
				}
				else
				{
					m_ddc_state = WDATA;
					m_ddc_sda_out = 1;
				}
			}
			break;

		case WDATA:
			if (m_ddc_bits == 8)
			{
				m_ddc_ptr = m_ddc_shift & 0xff;
				m_ddc_sda_out = 0;
				m_ddc_bits = 9;
			}
			else if (m_ddc_bits == 9)
			{
				m_ddc_sda_out = 1;
				m_ddc_bits = 0;
				m_ddc_shift = 0;
			}
			break;

		case RDATA:
			if (m_ddc_bits < 8)
				m_ddc_sda_out = BIT(m_ddc_shift, 7 - m_ddc_bits);
			else if (m_ddc_bits == 8)
				m_ddc_sda_out = 1;          // release for the master's ACK
			else
			{
				m_ddc_shift = m_edid[m_ddc_ptr & 0x7f];
				m_ddc_ptr = (m_ddc_ptr + 1) & 0xff;
				m_ddc_bits = 0;
				m_ddc_sda_out = BIT(m_ddc_shift, 7);
			}
			break;

		default:
			m_ddc_sda_out = 1;
			break;
		}
	}

	m_ddc_scl = scl;
	m_ddc_sda_in = sda;
}

// I2C engine: a transaction is described by the FIFO contents and the
// address/data counts.  With START the first FIFO byte is the slave address.
void ati_rage128_device::i2c_go()
{
	const bool start = BIT(m_i2c_cntl0, 8);
	const bool receive = BIT(m_i2c_cntl0, 10);
	const u32 data_count = m_i2c_cntl1 & 0xf;
	const bool present = (m_monitor->read() & 3) == 1;

	m_i2c_cntl0 &= ~((1U << 12) | 0x3);

	// bytes queued by the host, consumed by this transaction
	u8 pos = m_i2c_fifo_rd;
	const u8 end = m_i2c_fifo_count;
	m_i2c_fifo_count = m_i2c_fifo_rd = 0;

	if (start && pos < end)
	{
		const u8 addr = m_i2c_fifo[pos++];
		if (!present || (addr >> 1) != 0x50)
		{
			LOGDDC("I2C engine: no ACK for address %02x\n", addr);
			m_i2c_cntl0 |= 0x3;     // DONE | NACK
			return;
		}
	}

	if (receive)
	{
		const u32 count = data_count ? data_count : I2C_FIFO_SIZE;
		for (u32 i = 0; i < count; i++)
		{
			m_i2c_fifo[m_i2c_fifo_count++] = m_edid[m_ddc_ptr & 0x7f];
			m_ddc_ptr++;
		}
	}
	else if (pos < end)
	{
		// a byte written after the address sets the EEPROM word address
		m_ddc_ptr = m_i2c_fifo[pos];
	}

	LOGDDC("I2C engine: %s complete\n", receive ? "read" : "write");
	m_i2c_cntl0 |= 1;   // DONE
}


/***************************************************************************
    GUI (2D) engine

    Every operation runs to completion when its initiator register is
    written, so the FIFO always reports free and the engine idle.
***************************************************************************/

void ati_rage128_device::gui_reset()
{
	m_dst_offset = m_dst_pitch = m_src_offset = m_src_pitch = 0;
	m_dst_tile = m_src_tile = false;
	m_dst_x = m_dst_y = m_src_x = m_src_y = 0;
	m_dst_width = m_dst_height = 0;
	m_sc_left = m_sc_top = 0;
	m_sc_right = m_sc_bottom = 0x1fff;
	m_src_sc_right = m_src_sc_bottom = 0x1fff;
	m_gmc = 0;
	m_dp_cntl = 3;
	m_dp_datatype = 0;
	m_dp_mix = 0;
	m_host = host_xfer();
	m_host.active = false;
	m_packet_count = 0;
	m_packet_remaining = 0;
}

// DP_GUI_MASTER_CNTL is an alias for fields of DP_DATATYPE and DP_MIX, plus
// "load defaults" controls: a clear SRC/DST pitch-offset or clipping bit
// reloads that state from the DEFAULT_* registers.
void ati_rage128_device::gmc_w(u32 data)
{
	m_gmc = data;

	m_dp_datatype = (m_dp_datatype & ~0x4003'0f0fU)
			| BIT(data, 8, 4)
			| (BIT(data, 4, 4) << 8)
			| (BIT(data, 12, 2) << 16)
			| (BIT(data, 14) << 30);
	m_dp_mix = (m_dp_mix & ~0x00ff'0700U) | (BIT(data, 24, 3) << 8) | (BIT(data, 16, 8) << 16);

	const u32 def_ofs = m_regs[DEFAULT_OFFSET / 4] & 0x03ff'ffff;
	const u32 def_pitch = m_regs[DEFAULT_PITCH / 4] & 0x3ff;
	const u32 def_br = m_regs[DEFAULT_SC_BOTTOM_RIGHT / 4];

	if (!(data & GMC_SRC_PITCH_OFFSET_CNTL))
	{
		m_src_offset = def_ofs;
		m_src_pitch = def_pitch;
		m_src_tile = false;
	}
	if (!(data & GMC_DST_PITCH_OFFSET_CNTL))
	{
		m_dst_offset = def_ofs;
		m_dst_pitch = def_pitch;
		m_dst_tile = false;
	}
	if (!(data & GMC_SRC_CLIPPING))
	{
		m_src_sc_right = def_br & 0x1fff;
		m_src_sc_bottom = (def_br >> 16) & 0x1fff;
	}
	if (!(data & GMC_DST_CLIPPING))
	{
		m_sc_left = 0;
		m_sc_top = 0;
		m_sc_right = def_br & 0x1fff;
		m_sc_bottom = (def_br >> 16) & 0x1fff;
	}

	LOGENGINE("GMC = %08x: brush %d dst %d src %d rop %02x source %d\n", data,
			BIT(data, 4, 4), BIT(data, 8, 4), BIT(data, 12, 2), BIT(data, 16, 8), BIT(data, 24, 3));
}

u32 ati_rage128_device::gmc_r() const
{
	return (m_gmc & 0xf800'800fU)
			| (BIT(m_dp_datatype, 8, 4) << 4)
			| (BIT(m_dp_datatype, 0, 4) << 8)
			| (BIT(m_dp_datatype, 16, 2) << 12)
			| (BIT(m_dp_datatype, 30) << 14)
			| (BIT(m_dp_mix, 16, 8) << 16)
			| (BIT(m_dp_mix, 8, 3) << 24);
}

int ati_rage128_device::dst_bpp() const
{
	return DTYPE_BYTES[m_dp_datatype & 0xf];
}

// pitch is in units of 8 pixels; 24 bpp surfaces are programmed in units of 8 bytes
u32 ati_rage128_device::dst_pitch_bytes() const
{
	const int bpp = dst_bpp();
	return (bpp == 3) ? m_dst_pitch * 8 : m_dst_pitch * 8 * bpp;
}

u32 ati_rage128_device::src_pitch_bytes() const
{
	const int bpp = dst_bpp();
	return (bpp == 3) ? m_src_pitch * 8 : m_src_pitch * 8 * bpp;
}

// 24-bit pixels can straddle a tile column, so they're addressed a byte at a time
u32 ati_rage128_device::read_pixel(u32 base, u32 pitch, int bpp, int x, int y, bool tiled) const
{
	const u32 xb = u32(x) * bpp;
	const u32 addr = base + surface_offset(xb, u32(y), pitch, tiled);
	switch (bpp)
	{
	case 1: return m_vram[addr & m_vram_mask];
	case 2: return get_u16le(&m_vram[addr & m_vram_mask & ~1]);
	case 3:
		return m_vram[addr & m_vram_mask]
				| (m_vram[(base + surface_offset(xb + 1, u32(y), pitch, tiled)) & m_vram_mask] << 8)
				| (m_vram[(base + surface_offset(xb + 2, u32(y), pitch, tiled)) & m_vram_mask] << 16);
	case 4: return get_u32le(&m_vram[addr & m_vram_mask & ~3]);
	default: return 0;
	}
}

void ati_rage128_device::write_pixel(u32 base, u32 pitch, int bpp, int x, int y, u32 data, bool tiled)
{
	const u32 xb = u32(x) * bpp;
	const u32 addr = base + surface_offset(xb, u32(y), pitch, tiled);
	switch (bpp)
	{
	case 1: m_vram[addr & m_vram_mask] = u8(data); break;
	case 2: put_u16le(&m_vram[addr & m_vram_mask & ~1], data); break;
	case 3:
		m_vram[addr & m_vram_mask] = u8(data);
		m_vram[(base + surface_offset(xb + 1, u32(y), pitch, tiled)) & m_vram_mask] = u8(data >> 8);
		m_vram[(base + surface_offset(xb + 2, u32(y), pitch, tiled)) & m_vram_mask] = u8(data >> 16);
		break;
	case 4: put_u32le(&m_vram[addr & m_vram_mask & ~3], data); break;
	}
}

bool ati_rage128_device::clip_test(int x, int y) const
{
	if (x < m_sc_left || x > m_sc_right || y < m_sc_top || y > m_sc_bottom)
		return false;
	if (x < 0 || y < 0)
		return false;

	if (!(m_gmc & GMC_AUX_CLIP_DIS))
	{
		const u32 cntl = m_regs[AUX_SC_CNTL / 4];
		bool any_or = false, or_hit = false;
		for (int i = 0; i < 3; i++)
		{
			if (!BIT(cntl, i * 2))
				continue;
			const u32 base = (AUX1_SC_LEFT + i * 0x10) / 4;
			const s32 l = s16(m_regs[base + 0]), r = s16(m_regs[base + 1]);
			const s32 t = s16(m_regs[base + 2]), b = s16(m_regs[base + 3]);
			const bool inside = (x >= l) && (x <= r) && (y >= t) && (y <= b);
			if (BIT(cntl, i * 2 + 1))
			{
				// NAND: draw outside the rectangle only
				if (inside)
					return false;
			}
			else
			{
				any_or = true;
				or_hit |= inside;
			}
		}
		if (any_or && !or_hit)
			return false;
	}
	return true;
}

// Returns false if the pattern leaves this pixel alone (transparent mono brush bit).
bool ati_rage128_device::pattern_pixel(int x, int y, u32 &color, int line_index) const
{
	const u32 type = BIT(m_dp_datatype, 8, 4);
	const bool lsb_first = BIT(m_dp_datatype, 30);
	const u32 brush_yx = m_regs[BRUSH_Y_X / 4];
	const int bx = brush_yx & 0x1f;
	const int by = (brush_yx >> 8) & 0x1f;
	const u32 *const data = &m_regs[BRUSH_DATA0 / 4];

	auto mono_byte_bit = [lsb_first] (u8 byte, int col) -> bool {
		return lsb_first ? BIT(byte, col) : BIT(byte, ~col & 7);
	};

	int bit = -1;
	switch (type)
	{
	case BRUSH_SOLID_COLOR:
		color = m_regs[DP_BRUSH_FRGD_CLR / 4];
		return true;

	case BRUSH_NONE:
		color = 0;
		return true;

	case BRUSH_8X8_MONO_FG_BG:
	case BRUSH_8X8_MONO_FG_LA:
	{
		const int row = (y + by) & 7, col = (x + bx) & 7;
		const u8 byte = u8(data[row >> 2] >> ((row & 3) * 8));
		bit = mono_byte_bit(byte, col);
		break;
	}

	case BRUSH_1X8_MONO_FG_BG:
	case BRUSH_1X8_MONO_FG_LA:
		bit = mono_byte_bit(u8(data[0]), (x + bx) & 7);
		break;

	case BRUSH_32X1_MONO_FG_BG:
	case BRUSH_32X1_MONO_FG_LA:
	{
		const int idx = ((line_index >= 0) ? (line_index + bx) : (x + bx)) & 31;
		bit = lsb_first ? BIT(data[0], idx) : BIT(data[0], 31 - idx);
		break;
	}

	case BRUSH_32X32_MONO_FG_BG:
	case BRUSH_32X32_MONO_FG_LA:
	{
		const u32 d = data[(y + by) & 31];
		const int idx = (x + bx) & 31;
		bit = lsb_first ? BIT(d, idx) : BIT(d, 31 - idx);
		break;
	}

	case BRUSH_8X8_COLOR:
	case BRUSH_1X8_COLOR:
	{
		const int bpp = std::max(1, dst_bpp());
		const int row = (type == BRUSH_1X8_COLOR) ? 0 : ((y + by) & 7);
		const int col = (x + bx) & 7;
		u32 c = 0;
		for (int i = 0; i < bpp; i++)
		{
			// brush data registers are little-endian dwords
			const int ofs = (row * 8 + col) * bpp + i;
			c |= u32(u8(data[ofs >> 2] >> ((ofs & 3) * 8))) << (i * 8);
		}
		color = c;
		return true;
	}

	default:
		color = m_regs[DP_BRUSH_FRGD_CLR / 4];
		return true;
	}

	if (bit)
	{
		color = m_regs[DP_BRUSH_FRGD_CLR / 4];
		return true;
	}
	// odd mono brush types leave background pixels alone
	if (type & 1)
		return false;
	color = m_regs[DP_BRUSH_BKGD_CLR / 4];
	return true;
}

// CLR_CMP_CNTL functions select when the pixel is written:
// 0 always, 1 never, 4 when equal to the compare colour, 5 when different
bool ati_rage128_device::color_compare_pass(u32 src, u32 dst) const
{
	const u32 cntl = m_regs[CLR_CMP_CNTL / 4];
	const u32 mask = m_regs[CLR_CMP_MASK / 4];

	auto test = [mask] (int fn, u32 value, u32 ref) -> bool {
		switch (fn)
		{
		case 0: return true;
		case 1: return false;
		case 4: return (value & mask) == (ref & mask);
		case 5: return (value & mask) != (ref & mask);
		default: return true;
		}
	};

	const int fn_src = cntl & 7;
	const int fn_dst = (cntl >> 8) & 7;
	bool pass = true;
	if (fn_src)
		pass = BIT(cntl, 24) ? test(fn_src, src, m_regs[CLR_CMP_CLR_SRC / 4]) : test(fn_src, dst, m_regs[CLR_CMP_CLR_SRC / 4]);
	if (pass && fn_dst)
		pass = test(fn_dst, dst, m_regs[CLR_CMP_CLR_DST / 4]);
	return pass;
}

// ROP3: bit (P<<2 | S<<1 | D) of the code gives the result
u32 ati_rage128_device::rop3(u8 rop, u32 p, u32 s, u32 d)
{
	switch (rop)
	{
	case 0x00: return 0;
	case 0xff: return ~0U;
	case 0xcc: return s;
	case 0xf0: return p;
	case 0xaa: return d;
	case 0x33: return ~s;
	case 0x0f: return ~p;
	case 0x55: return ~d;
	case 0x66: return s ^ d;
	case 0x5a: return p ^ d;
	case 0x88: return s & d;
	case 0xee: return s | d;
	case 0xa0: return p & d;
	case 0xfa: return p | d;
	default:
	{
		u32 r = 0;
		for (int i = 0; i < 8; i++)
		{
			if (!BIT(rop, i))
				continue;
			r |= (BIT(i, 2) ? p : ~p) & (BIT(i, 1) ? s : ~s) & (BIT(i, 0) ? d : ~d);
		}
		return r;
	}
	}
}

void ati_rage128_device::plot(int x, int y, u32 src, bool src_valid, int line_index)
{
	if (!clip_test(x, y))
		return;

	u32 pat;
	if (!pattern_pixel(x, y, pat, line_index))
		return;

	const int bpp = dst_bpp();
	if (!bpp)
		return;
	const u32 pitch = dst_pitch_bytes();
	const u32 dst = read_pixel(m_dst_offset, pitch, bpp, x, y, m_dst_tile);

	if (!(m_gmc & GMC_CLR_CMP_CNTL_DIS) && !color_compare_pass(src, dst))
		return;

	u32 res = rop3(BIT(m_dp_mix, 16, 8), pat, src_valid ? src : m_regs[DP_SRC_FRGD_CLR / 4], dst);
	if (!(m_gmc & GMC_WR_MSK_DIS))
	{
		const u32 wmask = m_regs[DP_WRITE_MASK / 4];
		res = (res & wmask) | (dst & ~wmask);
	}
	write_pixel(m_dst_offset, pitch, bpp, x, y, res, m_dst_tile);
}

void ati_rage128_device::gui_trigger_rect()
{
	const int w = m_dst_width, h = m_dst_height;
	const int bpp = dst_bpp();
	const u32 source = BIT(m_dp_mix, 8, 3);
	const u32 src_dtype = BIT(m_dp_datatype, 16, 2);
	const int x_step = BIT(m_dp_cntl, 0) ? 1 : -1;
	const int y_step = BIT(m_dp_cntl, 1) ? 1 : -1;

	if (m_host.active)
	{
		LOGENGINE("rectangle started with a host transfer pending, %d pixels dropped\n",
				(m_host.height - m_host.row) * m_host.width - m_host.col);
		m_host.active = false;
	}

	if (!w || !h)
		return;
	if (!bpp)
	{
		LOGUNIMPL("rectangle with unsupported destination datatype %d\n", m_dp_datatype & 0xf);
		return;
	}

	LOGENGINE("rect %dx%d at %d,%d src %d,%d%s source %d dtype %d rop %02x dir %d/%d\n", w, h, m_dst_x, m_dst_y,
			m_src_x, m_src_y, rop_uses_source(BIT(m_dp_mix, 16, 8)) ? "" : " (unused)", source, m_dp_datatype & 0xf,
			BIT(m_dp_mix, 16, 8), x_step, y_step);

	// A fill joins the queued 3D: each render thread fills the rows of its own
	// stripes after the primitives before it, so nothing has to wait.  Fills
	// whose rows overlap through a narrow pitch are done here, in order.
	const bool host_source = (source == SRC_SOURCE_HOST_DATA) || (source == SRC_SOURCE_HOST_DATA_BYTE_ALIGNED);
	if (!DEBUG_NO_QUEUED_2D && !host_source && !((source == SRC_SOURCE_MEMORY) && rop_uses_source(BIT(m_dp_mix, 16, 8))))
	{
		fill_job f;
		bool draws;
		if (make_fill(w, h, x_step, y_step, m_regs[DP_SRC_FRGD_CLR / 4], f, draws))
		{
			if (draws)
			{
				if (u64(f.right + 1) * f.bpp <= f.pitch)
				{
					queue_fill(f);
				}
				else
				{
					u32 lo, hi;
					rows_span(f.base, f.pitch, f.bpp, f.top, f.bottom, f.right, lo, hi);
					wait_for_vram_span(lo, hi, true);
					do_fill(f, 0, 1);
				}
			}
			m_dst_y += h * y_step;
			return;
		}
	}

	// Wait only for queued 3D that touches the rows the rectangle can reach on
	// the destination, and for a blit the rows it can read from the source.
	const s32 dst_y0 = (y_step > 0) ? m_dst_y : (m_dst_y - h + 1);
	u32 dst_lo, dst_hi;
	rows_span(m_dst_offset, dst_pitch_bytes(), bpp, std::max<s32>(dst_y0, m_sc_top), std::min<s32>(dst_y0 + h - 1, m_sc_bottom), m_sc_right, dst_lo, dst_hi);
	wait_for_vram_span(dst_lo, dst_hi, true);

	if (source == SRC_SOURCE_HOST_DATA || source == SRC_SOURCE_HOST_DATA_BYTE_ALIGNED)
	{
		m_host.span_lo = dst_lo;
		m_host.span_hi = dst_hi;
		m_host.active = true;
		m_host.mono = src_dtype != 3;
		m_host.byte_aligned = source == SRC_SOURCE_HOST_DATA_BYTE_ALIGNED;
		m_host.x = m_host.start_x = m_dst_x;
		m_host.y = m_dst_y;
		m_host.col = m_host.row = 0;
		m_host.width = w;
		m_host.height = h;
		m_host.x_step = x_step;
		m_host.y_step = y_step;
		m_host.byte_acc = 0;
		m_host.byte_count = 0;
		return;
	}

	// The source is only read, and only clipped against the source scissor, when the
	// ROP uses it.  The Mac OS 9 accelerator paints with DP_SRC_SOURCE set to memory
	// and ROP 0xf0 (PATCOPY), leaving SRC_X/SRC_Y wherever the last blit finished;
	// clipping those against SRC_SC_BOTTOM_RIGHT cut the following fills short.
	const bool from_memory = (source == SRC_SOURCE_MEMORY) && rop_uses_source(BIT(m_dp_mix, 16, 8));
	if (from_memory)
	{
		const s32 src_y0 = (y_step > 0) ? m_src_y : (m_src_y - h + 1);
		u32 src_lo, src_hi;
		if (src_dtype == 3)
			rows_span(m_src_offset, src_pitch_bytes(), bpp, src_y0, std::min<s32>(src_y0 + h - 1, m_src_sc_bottom), m_src_sc_right, src_lo, src_hi);
		else
			rows_span(m_src_offset, m_src_pitch * 8, 1, src_y0, std::min<s32>(src_y0 + h - 1, m_src_sc_bottom), (m_src_sc_right + 7) / 8, src_lo, src_hi);
		wait_for_vram_span(src_lo, src_hi, false);
	}
	const u32 fg = m_regs[DP_SRC_FRGD_CLR / 4], bg = m_regs[DP_SRC_BKGD_CLR / 4];
	const bool lsb_first = BIT(m_dp_datatype, 30);
	const u32 spitch = src_pitch_bytes();
	const u32 mono_pitch_bits = m_src_pitch * 8 * 8;

	if (from_memory && (src_dtype == 3) && copy_rect(w, h, x_step, y_step))
	{
		m_dst_y += h * y_step;
		return;
	}

	for (int row = 0; row < h; row++)
	{
		const int dy = m_dst_y + row * y_step;
		const int sy = m_src_y + row * y_step;
		for (int col = 0; col < w; col++)
		{
			const int dx = m_dst_x + col * x_step;
			const int sx = m_src_x + col * x_step;
			u32 src = fg;
			if (from_memory)
			{
				if (sx < 0 || sy < 0 || sx > m_src_sc_right || sy > m_src_sc_bottom)
					continue;
				if (src_dtype == 3)
					src = read_pixel(m_src_offset, spitch, bpp, sx, sy, m_src_tile);
				else
				{
					// monochrome source expanded through the source colours
					const u32 bitpos = u32(sy) * mono_pitch_bits + u32(sx);
					const u8 byte = m_vram[(m_src_offset + (bitpos >> 3)) & m_vram_mask];
					const bool bit = lsb_first ? BIT(byte, bitpos & 7) : BIT(byte, ~bitpos & 7);
					if (!bit && src_dtype == 1)
						continue;
					src = bit ? fg : bg;
				}
			}
			plot(dx, dy, src, from_memory, -1);
		}
	}

	m_dst_y += h * y_step;
}

// A rectangle whose source and pattern are the same for every pixel, with no
// auxiliary scissors and a colour compare that doesn't look at the destination,
// is filled a row at a time; plot() gives the same result for each pixel.
// Returns false when that doesn't apply, and clears draws when the rectangle
// draws nothing at all.
bool ati_rage128_device::make_fill(int w, int h, int x_step, int y_step, u32 src, fill_job &f, bool &draws)
{
	draws = false;
	const int brush = BIT(m_dp_datatype, 8, 4);
	if ((brush != BRUSH_SOLID_COLOR) && (brush != BRUSH_NONE))
		return false;
	if (!(m_gmc & GMC_AUX_CLIP_DIS) && (m_regs[AUX_SC_CNTL / 4] & 0x15))
		return false;

	// with the destination test off, the colour compare only sees the source
	const u32 cmp = m_regs[CLR_CMP_CNTL / 4];
	const bool compare = !(m_gmc & GMC_CLR_CMP_CNTL_DIS) && (cmp & 0x0707);
	if (compare && (BIT(cmp, 8, 3) || (BIT(cmp, 0, 3) && !BIT(cmp, 24))))
		return false;
	if (compare && !color_compare_pass(src, 0))
		return true;

	f.bpp = dst_bpp();
	f.pitch = dst_pitch_bytes();
	f.base = m_dst_offset;
	f.tiled = m_dst_tile;
	f.rop = BIT(m_dp_mix, 16, 8);
	f.pat = (brush == BRUSH_SOLID_COLOR) ? m_regs[DP_BRUSH_FRGD_CLR / 4] : 0;
	f.src = src;
	const u32 pixel_mask = (f.bpp >= 4) ? ~0U : ((1U << (f.bpp * 8)) - 1);
	f.wmask = ((m_gmc & GMC_WR_MSK_DIS) ? ~0U : m_regs[DP_WRITE_MASK / 4]) | ~pixel_mask;
	f.uses_dst = ((f.rop >> 1) ^ f.rop) & 0x55;
	f.read_dst = f.uses_dst || (f.wmask != ~0U);
	f.value = rop3(f.rop, f.pat, src, 0);
	f.x_step = x_step;
	f.y_step = y_step;

	// the pixels covered, whichever way the rectangle is drawn, inside the scissor
	const s32 x0 = (x_step > 0) ? m_dst_x : (m_dst_x - w + 1), y0 = (y_step > 0) ? m_dst_y : (m_dst_y - h + 1);
	f.left = std::max<s32>({ x0, m_sc_left, 0 });
	f.right = std::min<s32>(x0 + w - 1, m_sc_right);
	f.top = std::max<s32>({ y0, m_sc_top, 0 });
	f.bottom = std::min<s32>(y0 + h - 1, m_sc_bottom);
	draws = (f.left <= f.right) && (f.top <= f.bottom);
	return true;
}

// The rows of a fill in this band's 16-line stripes (all of them for one band).
void ati_rage128_device::do_fill(const fill_job &f, int band, int bands)
{
	auto mine = [band, bands] (s32 y) { return (bands == 1) || (((y >> 4) % bands) == band); };

	// plain writes to a linear surface wholly in VRAM go a row at a time
	const u64 last = u64(f.base) + u64(f.bottom) * f.pitch + u64(f.right + 1) * f.bpp;
	if (!f.read_dst && (!f.tiled || !tile_pitch_ok(f.pitch)) && (f.bpp != 3) && !((f.base | f.pitch) & (f.bpp - 1)) && (last <= u64(m_vram_mask) + 1))
	{
		for (s32 y = f.top; y <= f.bottom; y++)
		{
			if (!mine(y))
				continue;
			u8 *p = &m_vram[f.base + u32(y) * f.pitch + u32(f.left) * f.bpp];
			const s32 n = f.right - f.left + 1;
			switch (f.bpp)
			{
			case 1: std::fill_n(p, n, u8(f.value)); break;
			case 2: for (s32 i = 0; i < n; i++) put_u16le(p + i * 2, u16(f.value)); break;
			default: for (s32 i = 0; i < n; i++) put_u32le(p + i * 4, f.value); break;
			}
		}
		return;
	}

	// in the rectangle's own direction, in case a narrow pitch makes rows overlap
	for (s32 yi = f.top; yi <= f.bottom; yi++)
	{
		const s32 y = (f.y_step > 0) ? yi : (f.top + f.bottom - yi);
		if (!mine(y))
			continue;
		for (s32 xi = f.left; xi <= f.right; xi++)
		{
			const s32 x = (f.x_step > 0) ? xi : (f.left + f.right - xi);
			u32 res = f.value;
			if (f.read_dst)
			{
				const u32 dst = read_pixel(f.base, f.pitch, f.bpp, x, y, f.tiled);
				if (f.uses_dst)
					res = rop3(f.rop, f.pat, f.src, dst);
				res = (res & f.wmask) | (dst & ~f.wmask);
			}
			write_pixel(f.base, f.pitch, f.bpp, x, y, res, f.tiled);
		}
	}
}

// A colour HOSTDATA_BLT with all of its data in the packet goes to the render
// threads as a batch of its own: the CPU often replaces a texture that queued
// primitives still read.  Same conditions as copy_rect(), with no colour
// compare, a left-to-right top-to-bottom rectangle and 16 or 32 bpp.  Returns
// false, doing nothing, when it doesn't apply.
bool ati_rage128_device::queue_host_blit(const u32 *data, u32 avail)
{
	const int w = m_dst_width, h = m_dst_height;
	const int bpp = dst_bpp();
	const u32 source = BIT(m_dp_mix, 8, 3);
	const u8 rop = BIT(m_dp_mix, 16, 8);
	const int brush = BIT(m_dp_datatype, 8, 4);
	const bool uses_pat = ((rop >> 4) ^ rop) & 0x0f;
	const bool constant_pat = (brush == BRUSH_SOLID_COLOR) || (brush == BRUSH_NONE);
	if (!w || !h || ((bpp != 2) && (bpp != 4)) || (BIT(m_dp_datatype, 16, 2) != 3) || ((m_dp_cntl & 3) != 3))
		return false;
	if ((source != SRC_SOURCE_HOST_DATA) && (source != SRC_SOURCE_HOST_DATA_BYTE_ALIGNED))
		return false;
	if (((brush < BRUSH_8X8_COLOR) && (brush & 1)) || (uses_pat && !constant_pat))
		return false;
	if (!(m_gmc & GMC_AUX_CLIP_DIS) && (m_regs[AUX_SC_CNTL / 4] & 0x15))
		return false;
	if (!(m_gmc & GMC_CLR_CMP_CNTL_DIS) && (m_regs[CLR_CMP_CNTL / 4] & 0x0707))
		return false;
	const u32 dwords = u32((u64(w) * u64(h) * u64(bpp) + 3) / 4);
	if (avail < dwords)
		return false;

	if (m_host.active)
		m_host.active = false;
	LOGENGINE("host blit %dx%d at %d,%d queued, dtype %d rop %02x\n", w, h, m_dst_x, m_dst_y, m_dp_datatype & 0xf, rop);

	kick_render();
	render_batch &batch = *m_pending;
	host_job &j = batch.host;
	j.base = m_dst_offset;
	j.pitch = dst_pitch_bytes();
	j.bpp = bpp;
	j.tiled = m_dst_tile;
	j.swap = m_dp_datatype & DP_HOST_BIG_ENDIAN_EN;
	j.rop = rop;
	j.pat = (brush == BRUSH_SOLID_COLOR) ? m_regs[DP_BRUSH_FRGD_CLR / 4] : 0;
	const u32 pixel_mask = (bpp >= 4) ? ~0U : ((1U << (bpp * 8)) - 1);
	j.wmask = ((m_gmc & GMC_WR_MSK_DIS) ? ~0U : m_regs[DP_WRITE_MASK / 4]) | ~pixel_mask;
	j.read_dst = (((rop >> 1) ^ rop) & 0x55) || (j.wmask != ~0U);
	j.x0 = m_dst_x;
	j.y0 = m_dst_y;
	j.w = w;
	j.h = h;
	j.left = std::max<s32>(m_sc_left, 0);
	j.right = m_sc_right;
	j.top = std::max<s32>(m_sc_top, 0);
	j.bottom = m_sc_bottom;
	j.data.assign(data, data + dwords);

	u32 lo, hi;
	rows_span(j.base, j.pitch, bpp, std::max<s32>(j.y0, j.top), std::min<s32>(j.y0 + h - 1, j.bottom), j.right, lo, hi);
	batch.writes.add(lo, hi);
	batch.is_host = true;
	kick_render();

	// where the engine leaves the destination after the last row
	m_dst_y = m_dst_y + h;
	return true;
}

void ati_rage128_device::host_rows(const host_job &j, int first, int last)
{
	const int swap = (j.bpp == 2) ? 1 : 2;
	for (int row = first; row < last; row++)
	{
		const s32 y = j.y0 + row;
		if ((y < j.top) || (y > j.bottom))
			continue;
		for (int col = 0; col < j.w; col++)
		{
			const s32 x = j.x0 + col;
			if ((x < j.left) || (x > j.right))
				continue;
			const u32 i = u32(row) * u32(j.w) + u32(col);
			u32 src;
			if (j.bpp == 2)
			{
				const u32 d = j.swap ? aperture_swap(j.data[i >> 1], swap) : j.data[i >> 1];
				src = (i & 1) ? (d >> 16) : (d & 0xffff);
			}
			else
			{
				src = j.swap ? aperture_swap(j.data[i], swap) : j.data[i];
			}

			u32 res = src;
			if (j.read_dst)
			{
				const u32 dst = read_pixel(j.base, j.pitch, j.bpp, x, y, j.tiled);
				res = (rop3(j.rop, j.pat, src, dst) & j.wmask) | (dst & ~j.wmask);
			}
			else if (j.rop != 0xcc)
			{
				res = rop3(j.rop, j.pat, src, 0);
			}
			write_pixel(j.base, j.pitch, j.bpp, x, y, res, j.tiled);
		}
	}
}

// A blit from a colour source in memory: what plot() does, with the scissor
// and the pattern worked out once per rectangle.  Pixels are visited in the
// same order as the general loop, so overlapping copies come out the same.
// Returns false, drawing nothing, when it doesn't apply.
bool ati_rage128_device::copy_rect(int w, int h, int x_step, int y_step)
{
	const u8 rop = BIT(m_dp_mix, 16, 8);
	const int brush = BIT(m_dp_datatype, 8, 4);
	const bool uses_pat = ((rop >> 4) ^ rop) & 0x0f;
	const bool constant_pat = (brush == BRUSH_SOLID_COLOR) || (brush == BRUSH_NONE);
	if (((brush < BRUSH_8X8_COLOR) && (brush & 1)) || (uses_pat && !constant_pat))
		return false;
	if (!(m_gmc & GMC_AUX_CLIP_DIS) && (m_regs[AUX_SC_CNTL / 4] & 0x15))
		return false;

	const int bpp = dst_bpp();
	const u32 pitch = dst_pitch_bytes(), spitch = src_pitch_bytes();
	const u32 pat = (brush == BRUSH_SOLID_COLOR) ? m_regs[DP_BRUSH_FRGD_CLR / 4] : 0;
	const u32 pixel_mask = (bpp >= 4) ? ~0U : ((1U << (bpp * 8)) - 1);
	const u32 wmask = ((m_gmc & GMC_WR_MSK_DIS) ? ~0U : m_regs[DP_WRITE_MASK / 4]) | ~pixel_mask;
	const bool compare = !(m_gmc & GMC_CLR_CMP_CNTL_DIS) && (m_regs[CLR_CMP_CNTL / 4] & 0x0707);
	const bool uses_dst = ((rop >> 1) ^ rop) & 0x55;
	const bool read_dst = uses_dst || compare || (wmask != ~0U);

	// the columns and rows whose destination is inside the scissor
	const s32 left = std::max<s32>(m_sc_left, 0), right = m_sc_right;
	const s32 top = std::max<s32>(m_sc_top, 0), bottom = m_sc_bottom;
	auto range = [] (s32 start, int step, int n, s32 lo, s32 hi, int &first, int &end) {
		if (step > 0)
		{
			first = std::max<s32>(0, lo - start);
			end = std::min<s32>(n, hi - start + 1);
		}
		else
		{
			first = std::max<s32>(0, start - hi);
			end = std::min<s32>(n, start - lo + 1);
		}
	};
	int col0, col1, row0, row1;
	range(m_dst_x, x_step, w, left, right, col0, col1);
	range(m_dst_y, y_step, h, top, bottom, row0, row1);

	for (int row = row0; row < row1; row++)
	{
		const int dy = m_dst_y + row * y_step;
		const int sy = m_src_y + row * y_step;
		if ((sy < 0) || (sy > m_src_sc_bottom))
			continue;
		for (int col = col0; col < col1; col++)
		{
			const int dx = m_dst_x + col * x_step;
			const int sx = m_src_x + col * x_step;
			if ((sx < 0) || (sx > m_src_sc_right))
				continue;
			const u32 src = read_pixel(m_src_offset, spitch, bpp, sx, sy, m_src_tile);
			u32 res = src;
			if (read_dst)
			{
				const u32 dst = read_pixel(m_dst_offset, pitch, bpp, dx, dy, m_dst_tile);
				if (compare && !color_compare_pass(src, dst))
					continue;
				res = (rop3(rop, pat, src, dst) & wmask) | (dst & ~wmask);
			}
			else if (rop != 0xcc)
				res = rop3(rop, pat, src, 0);
			write_pixel(m_dst_offset, pitch, bpp, dx, dy, res, m_dst_tile);
		}
	}
	return true;
}

void ati_rage128_device::gui_trigger_line()
{
	wait_for_render();

	const u32 lnth = m_regs[DST_BRES_LNTH / 4];
	const int len = lnth & 0xffff;
	s32 err = util::sext(m_regs[DST_BRES_ERR / 4], 18);
	const s32 inc = util::sext(m_regs[DST_BRES_INC / 4], 18);
	const s32 dec = util::sext(m_regs[DST_BRES_DEC / 4], 18);
	const int x_step = BIT(m_dp_cntl, 0) ? 1 : -1;
	const int y_step = BIT(m_dp_cntl, 1) ? 1 : -1;
	const bool y_major = BIT(m_dp_cntl, 2);

	LOGENGINE("line from %d,%d len %d err %d inc %d dec %d %s-major\n", m_dst_x, m_dst_y, len, err, inc, dec, y_major ? "y" : "x");

	if (!dst_bpp())
		return;

	int x = m_dst_x, y = m_dst_y;
	for (int i = 0; i < len; i++)
	{
		plot(x, y, m_regs[DP_SRC_FRGD_CLR / 4], false, i);
		if (y_major)
			y += y_step;
		else
			x += x_step;
		err += inc;
		if (err >= 0)
		{
			if (y_major)
				x += x_step;
			else
				y += y_step;
			err += dec;
		}
	}
	m_dst_x = x;
	m_dst_y = y;
	m_regs[DST_BRES_ERR / 4] = u32(err) & 0x3'ffff;
}

// one pixel of a host data transfer; advances the walker
void ati_rage128_device::host_pixel(u32 src, bool draw)
{
	if (!m_host.active)
		return;

	if (draw)
		plot(m_host.x, m_host.y, src, true, -1);

	m_host.x += m_host.x_step;
	if (++m_host.col >= m_host.width)
	{
		m_host.col = 0;
		m_host.x = m_host.start_x;
		m_host.y += m_host.y_step;
		if (++m_host.row >= m_host.height)
		{
			m_host.active = false;
			m_dst_y = m_host.y;
		}
	}
}

void ati_rage128_device::host_data_w(u32 data, bool last)
{
	wait_for_vram_span(m_host.span_lo, m_host.span_hi, true);

	if (!m_host.active)
	{
		LOGENGINE("host data %08x with no transfer in progress\n", data);
		return;
	}

	if (m_host.mono)
	{
		const bool lsb_first = BIT(m_dp_datatype, 30);
		const bool transparent = BIT(m_dp_datatype, 16, 2) == 1;
		const u32 fg = m_regs[DP_SRC_FRGD_CLR / 4], bg = m_regs[DP_SRC_BKGD_CLR / 4];

		// Mono data is a byte stream in memory order whatever HOST_BIG_ENDIAN_EN says:
		// the Mac OS 9 accelerator sends the same unswapped glyph and icon bitmaps at
		// 8 bpp (bit clear) and at 16 and 32 bpp (bit set).
		for (int byte = 0; byte < 4 && m_host.active; byte++)
		{
			const u8 b = u8(data >> (byte * 8));
			for (int i = 0; i < 8 && m_host.active; i++)
			{
				const bool bit = lsb_first ? BIT(b, i) : BIT(b, 7 - i);
				const int row = m_host.row;
				// a clear bit with the transparent source type leaves the pixel alone
				host_pixel(bit ? fg : bg, bit || !transparent);
				// byte aligned transfers start each row on a fresh byte
				if (m_host.byte_aligned && m_host.row != row)
					break;
			}
		}
	}
	else
	{
		// HOST_BIG_ENDIAN_EN swaps colour data within each pixel.  The Mac OS 9
		// accelerator copies big-endian pixels into its command buffers unswapped and
		// sets the bit at 16 and 32 bpp only.
		// TODO: what it does at 24 bpp
		const int bpp = dst_bpp();
		if (m_dp_datatype & DP_HOST_BIG_ENDIAN_EN)
			data = aperture_swap(data, (bpp == 2) ? 1 : (bpp == 4) ? 2 : 0);

		switch (bpp)
		{
		case 1:
			for (int i = 0; i < 4; i++)
				host_pixel((data >> (i * 8)) & 0xff);
			break;
		case 2:
			host_pixel(data & 0xffff);
			host_pixel(data >> 16);
			break;
		case 3:
			for (int i = 0; i < 4; i++)
			{
				m_host.byte_acc |= ((data >> (i * 8)) & 0xff) << (m_host.byte_count * 8);
				if (++m_host.byte_count == 3)
				{
					host_pixel(m_host.byte_acc);
					m_host.byte_acc = 0;
					m_host.byte_count = 0;
				}
			}
			break;
		case 4:
			host_pixel(data);
			break;
		}
	}

	if (last && m_host.active)
	{
		m_host.active = false;
		m_dst_y = m_host.y + (m_host.col ? m_host.y_step : 0);
	}
}


/***************************************************************************
    Scaler

    A scaled copy from a texture-format source to the 2D destination,
    started by writing SCALE_DST_HEIGHT_WIDTH.  The Mac OS X accelerator
    draws its 32-bit ARGB cursor this way, blended with
    MISC_3D_STATE_CNTL_REG = 0x00540100 (source alpha, inverse source
    alpha) and clipped to the cursor rectangle by SC_TOP_LEFT_C and
    SC_BOTTOM_RIGHT_C.  Increments and accumulators are 16.16.
***************************************************************************/

void ati_rage128_device::scaler_blit()
{
	wait_for_render();

	const u32 src_hw = m_regs[SCALE_SRC_HEIGHT_WIDTH / 4];
	const u32 dst_xy = m_regs[SCALE_DST_X_Y / 4];
	const u32 dst_hw = m_regs[SCALE_DST_HEIGHT_WIDTH / 4];
	const int src_w = src_hw & 0x3fff, src_h = (src_hw >> 16) & 0x3fff;
	const int dst_w = dst_hw & 0x3fff, dst_h = (dst_hw >> 16) & 0x3fff;
	const s32 dst_x = hi16s(dst_xy), dst_y = lo16s(dst_xy);
	const int src_fmt = m_regs[SCALE_3D_DATATYPE / 4] & 0xf;
	const u32 src_base = m_regs[SCALE_OFFSET_0 / 4] & 0x03ff'ffff;
	const u32 src_pitch = (m_regs[SCALE_PITCH / 4] & 0x3ff) * 8;
	const u32 x_inc = m_regs[SCALE_X_INC / 4], y_inc = m_regs[SCALE_Y_INC / 4];
	const u32 hacc = m_regs[SCALE_HACC / 4], vacc = m_regs[SCALE_VACC / 4];

	// the destination format comes from the 3D context's GUI master control
	int dtype = BIT(m_regs[DP_GUI_MASTER_CNTL_C / 4], 8, 4);
	if (!dtype)
		dtype = m_dp_datatype & 0xf;
	const int bpp = DTYPE_BYTES[dtype];

	const u32 misc = m_regs[MISC_3D_STATE_CNTL_REG / 4];
	const int sf = BIT(misc, 16, 4), df = BIT(misc, 20, 4);
	const bool blend = sf || df;

	LOGENGINE("scaler %dx%d format %d at %08x -> %dx%d at %d,%d dtype %d, blend %d/%d\n",
			src_w, src_h, src_fmt, src_base, dst_w, dst_h, dst_x, dst_y, dtype, sf, df);

	if (!src_w || !src_h || !dst_w || !dst_h || !bpp || !TEXEL_BITS[src_fmt])
		return;
	gart_flush();

	const u32 tl = m_regs[SC_TOP_LEFT_C / 4], br = m_regs[SC_BOTTOM_RIGHT_C / 4];
	const u32 dpitch = m_dst_pitch * 8 * ((bpp == 3) ? 1 : bpp);
	const u32 wmask = m_regs[PLANE_3D_MASK_C / 4] & m_regs[DP_WRITE_MASK / 4];

	for (int row = 0; row < dst_h; row++)
	{
		const s32 y = dst_y + row;
		if ((y < 0) || (y < hi16s(tl)) || (y > hi16s(br)))
			continue;
		const int sy = std::min<int>(src_h - 1, int((u64(vacc) + u64(row) * y_inc) >> 16));

		for (int col = 0; col < dst_w; col++)
		{
			const s32 x = dst_x + col;
			if ((x < 0) || (x < lo16s(tl)) || (x > lo16s(br)))
				continue;
			const int sx = std::min<int>(src_w - 1, int((u64(hacc) + u64(col) * x_inc) >> 16));

			const u32 texel = read_texel(src_base, src_pitch, sx, sy, src_fmt, 0);
			const float sa = float(texel >> 24) / 255.0f, sr = float((texel >> 16) & 0xff) / 255.0f;
			const float sg = float((texel >> 8) & 0xff) / 255.0f, sb = float(texel & 0xff) / 255.0f;
			const u32 old = read_pixel(m_dst_offset, dpitch, bpp, x, y, m_dst_tile);

			float out[4] = { sr, sg, sb, sa };
			if (blend)
			{
				float dr, dg, db, da;
				unpack_color(dtype, old, dr, dg, db, da);
				const float dv[4] = { dr / 255.0f, dg / 255.0f, db / 255.0f, da / 255.0f };
				const float sv[4] = { sr, sg, sb, sa };
				for (int ch = 0; ch < 4; ch++)
				{
					const float s = sv[ch] * blend_factor(sf, ch == 3, sr, sg, sb, sa, dv[0], dv[1], dv[2], dv[3], ch);
					const float d = dv[ch] * blend_factor(df, ch == 3, sr, sg, sb, sa, dv[0], dv[1], dv[2], dv[3], ch);
					out[ch] = std::clamp(s + d, 0.0f, 1.0f);
				}
			}

			const u32 res = pack_color(dtype, out[0] * 255.0f, out[1] * 255.0f, out[2] * 255.0f, out[3] * 255.0f);
			write_pixel(m_dst_offset, dpitch, bpp, x, y, (res & wmask) | (old & ~wmask), m_dst_tile);
		}
	}
}


/***************************************************************************
    Scaler block of the SCALING and TRANS_SCALING packets

    Eleven dwords, after 86Box: MISC_3D_STATE_CNTL_REG (bit 10 replicates
    pixels instead of filtering, bits 23:16 are the blend functions),
    TEX_CNTL_C (bit 9 enables alpha blending), a combine control that isn't
    used, the source datatype, source offset, source pitch in units of 8
    pixels (bits 8:0), the source start in half pixels (Y in bits 29:16, X in
    bits 13:0), the X and Y increments as 4.12 in bits 19:4, then
    (x << 16) | y and (height << 16) | width of the destination.  The
    destination, clipping, ROP, colour compare and write mask come from the
    GUI master control header, as for a blit.
***************************************************************************/

void ati_rage128_device::scale_block(const u32 *b)
{
	scale_job j;
	const u32 misc = b[0];
	j.blend = BIT(b[1], 9);
	j.replicate = BIT(misc, 10);
	j.src_fmt = b[3] & 0xf;
	j.src_base = b[4] & TEX_OFFSET_MASK;
	j.src_pitch = (b[5] & 0x1ff) * 8;
	j.sx0 = (b[6] & 0x3fff) >> 1;
	j.sy0 = ((b[6] >> 16) & 0x3fff) >> 1;
	j.x_phase = (b[6] & 1) << 15;
	j.y_phase = BIT(b[6], 16) << 15;
	j.x_inc = b[7] & 0x000f'fff0;
	j.y_inc = b[8] & 0x000f'fff0;
	j.dst_x = util::sext(b[9] >> 16, 14);
	j.dst_y = util::sext(b[9], 14);
	j.w = b[10] & 0x3fff;
	j.h = (b[10] >> 16) & 0x3fff;

	j.dtype = m_dp_datatype & 0xf;
	j.bpp = DTYPE_BYTES[j.dtype];
	LOGENGINE("scale %s%s format %d at %08x pitch %u from %d,%d step %05x/%05x -> %dx%d at %d,%d dtype %d rop %02x\n",
			j.replicate ? "replicated" : "filtered", j.blend ? " blended" : "", j.src_fmt, j.src_base, j.src_pitch, j.sx0, j.sy0, j.x_inc, j.y_inc,
			j.w, j.h, j.dst_x, j.dst_y, j.dtype, BIT(m_dp_mix, 16, 8));
	if (!j.w || !j.h || !j.bpp || !TEXEL_BITS[j.src_fmt] || !j.src_pitch)
		return;
	if (m_host.active)
		m_host.active = false;

	// a source in the destination's own format is copied without conversion
	j.raw = j.replicate && !j.blend && (j.src_fmt == j.dtype) && (TEXEL_BITS[j.src_fmt] == j.bpp * 8);
	j.sf = BIT(misc, 16, 4);
	j.df = BIT(misc, 20, 4);
	j.dpitch = dst_pitch_bytes();
	j.dst_offset = m_dst_offset;
	j.dst_tile = m_dst_tile;

	// For a source copy with no auxiliary scissors and no "leave alone" mono
	// brush, plot() comes down to the scissor, the colour compare and the write
	// mask.  The Mac OS 9 driver leaves the compare enabled after TRANS_BITBLT.
	const int brush = BIT(m_dp_datatype, 8, 4);
	const u32 pixel_mask = (j.bpp >= 4) ? ~0U : ((1U << (j.bpp * 8)) - 1);
	j.fast = (BIT(m_dp_mix, 16, 8) == 0xcc)
			&& ((m_gmc & GMC_AUX_CLIP_DIS) || !(m_regs[AUX_SC_CNTL / 4] & 0x15))
			&& !((brush < BRUSH_8X8_COLOR) && (brush & 1));
	j.compare = !(m_gmc & GMC_CLR_CMP_CNTL_DIS) && (m_regs[CLR_CMP_CNTL / 4] & 0x0707);
	j.wmask = ((m_gmc & GMC_WR_MSK_DIS) ? ~0U : m_regs[DP_WRITE_MASK / 4]) | ~pixel_mask;
	j.clip_l = std::max<s32>(m_sc_left, 0);
	j.clip_r = m_sc_right;
	j.clip_t = std::max<s32>(m_sc_top, 0);
	j.clip_b = m_sc_bottom;

	// The rows are independent, so a large job is shared out among the render
	// threads, as long as the source is all in VRAM and doesn't overlap the
	// rows written.  The extents are conservative: whole 16-line tiles of the
	// destination, and the extra source row and column the filter reads.
	const u64 vram_size = u64(m_vram_mask) + 1;
	const u64 src_pitch_bytes = std::max<u64>((u64(j.src_pitch) * TEXEL_BITS[j.src_fmt]) >> 3, 1);
	const u64 src_last_row = u64(j.sy0) + ((u64(j.y_phase) + u64(j.h - 1) * j.y_inc) >> 16) + 1;
	const u64 src_start = u64(j.src_base) + u64(j.sy0) * src_pitch_bytes;
	const u64 src_end = u64(j.src_base) + (src_last_row + 2) * src_pitch_bytes;
	j.direct16 = (TEXEL_BITS[j.src_fmt] == 16) && (src_end <= AGP_WINDOW_BASE) && (src_end <= vram_size);

	bool parallel = (m_band_count > 1) && ((u64(j.w) * u64(j.h)) >= 4096) && (src_end <= AGP_WINDOW_BASE) && (src_end <= vram_size);
	const s32 dy0 = std::max<s32>(j.dst_y, 0), dy1 = j.dst_y + j.h - 1;
	u64 dst_start = 0, dst_end = 0;
	if (parallel && (dy1 >= 0))
	{
		dst_start = u64(m_dst_offset) + u64(j.dpitch) * u64(dy0 & ~15);
		dst_end = u64(m_dst_offset) + u64(j.dpitch) * u64((dy1 + 16) & ~15) + u64(std::max<s32>(j.dst_x + j.w, 0)) * j.bpp;
		if ((dst_end > vram_size) || ((src_start < dst_end) && (dst_start < src_end)))
			parallel = false;
	}

	// A job that needs nothing from the engine state but what it has captured
	// is queued behind the 3D before it, and emulation carries on.  That rules
	// out the general plot() path, the colour compare and palettised sources.
	const bool palettised = (j.src_fmt == 1) || (j.src_fmt == 2) || (j.src_fmt == 10);
	if (!DEBUG_NO_QUEUED_2D && parallel && (dy1 >= 0) && j.fast && !j.compare && !palettised)
	{
		kick_render();
		render_batch &batch = *m_pending;
		batch.is_scale = true;
		batch.scale = j;
		batch.reads.add(u32(src_start), u32(src_end));
		batch.writes.add(u32(dst_start), u32(dst_end));
		kick_render();
		return;
	}

	wait_for_render();
	if (!parallel)
	{
		scale_rows(j, 0, j.h);
		return;
	}

	// the render threads are idle after wait_for_render()
	const int bands = m_band_count;
	scale_band items[16];
	for (int i = 0; i < bands; i++)
		items[i] = scale_band{ this, &j, j.h * i / bands, j.h * (i + 1) / bands };
	osd_work_item_queue_multiple(m_render_queue, scale_band_callback, bands, &items[0], sizeof(scale_band), WORK_ITEM_FLAG_AUTO_RELEASE);
	while (!osd_work_queue_wait(m_render_queue, osd_ticks_per_second() * 10)) { }
}

void *ati_rage128_device::scale_band_callback(void *param, int threadid)
{
	const scale_band &band = *reinterpret_cast<const scale_band *>(param);
	band.device->scale_rows(*band.job, band.first, band.last);
	return nullptr;
}

void ati_rage128_device::scale_rows(const scale_job &j, int first, int last)
{
	auto fetch = [this, &j] (int x, int y) -> u32 {
		if (j.direct16)
		{
			const u32 addr = j.src_base + u32(y) * j.src_pitch * 2 + u32(x) * 2;
			return decode_texel(j.src_fmt, 0, get_u16le(&m_vram[addr & m_vram_mask & ~1U]), m_tex_palette);
		}
		return read_texel(j.src_base, j.src_pitch, x, y, j.src_fmt, 0);
	};
	auto put = [this, &j] (int x, int y, u32 color) {
		if (!j.fast)
		{
			plot(x, y, color, true, -1);
			return;
		}
		if (j.compare || (j.wmask != ~0U))
		{
			const u32 dst = read_pixel(j.dst_offset, j.dpitch, j.bpp, x, y, j.dst_tile);
			if (j.compare && !color_compare_pass(color, dst))
				return;
			color = (color & j.wmask) | (dst & ~j.wmask);
		}
		write_pixel(j.dst_offset, j.dpitch, j.bpp, x, y, color, j.dst_tile);
	};

	// with the fast path only the columns inside the scissor are computed
	int col0 = 0, col1 = j.w - 1;
	if (j.fast)
	{
		col0 = std::max<s32>(col0, j.clip_l - j.dst_x);
		col1 = std::min<s32>(col1, j.clip_r - j.dst_x);
	}

	for (int row = first; row < last; row++)
	{
		const int y = j.dst_y + row;
		if (j.fast && ((y < j.clip_t) || (y > j.clip_b)))
			continue;
		const u32 v_acc = j.y_phase + u32(row) * j.y_inc;
		const int sy = j.sy0 + int(v_acc >> 16);
		u32 h_acc = j.x_phase + u32(col0) * j.x_inc;
		for (int col = col0; col <= col1; col++, h_acc += j.x_inc)
		{
			const int x = j.dst_x + col;
			const int sx = j.sx0 + int(h_acc >> 16);
			if (j.raw)
			{
				put(x, y, read_pixel(j.src_base, j.src_pitch * j.bpp, j.bpp, sx, sy));
				continue;
			}

			u32 texel;
			if (j.replicate)
				texel = fetch(sx, sy);
			else
				texel = rgbaint_t::bilinear_filter(fetch(sx, sy), fetch(sx + 1, sy), fetch(sx, sy + 1), fetch(sx + 1, sy + 1), u8(h_acc >> 8), u8(v_acc >> 8));

			float r = float((texel >> 16) & 0xff), g = float((texel >> 8) & 0xff), bl = float(texel & 0xff), a = float(texel >> 24);
			if (j.blend && (j.fast || clip_test(x, y)))
			{
				float dr, dg, db, da;
				unpack_color(j.dtype, read_pixel(j.dst_offset, j.dpitch, j.bpp, x, y, j.dst_tile), dr, dg, db, da);
				const float sv[4] = { r / 255.0f, g / 255.0f, bl / 255.0f, a / 255.0f };
				const float dv[4] = { dr / 255.0f, dg / 255.0f, db / 255.0f, da / 255.0f };
				float out[4];
				for (int ch = 0; ch < 4; ch++)
				{
					const float s = sv[ch] * blend_factor(j.sf, ch == 3, sv[0], sv[1], sv[2], sv[3], dv[0], dv[1], dv[2], dv[3], ch);
					const float d = dv[ch] * blend_factor(j.df, ch == 3, sv[0], sv[1], sv[2], sv[3], dv[0], dv[1], dv[2], dv[3], ch);
					out[ch] = std::clamp(s + d, 0.0f, 1.0f) * 255.0f;
				}
				r = out[0]; g = out[1]; bl = out[2]; a = out[3];
			}
			put(x, y, pack_color(j.dtype, r, g, bl, a));
		}
	}
}


/***************************************************************************
    Bus-master GUI DMA

    Mac OS X copies surfaces from VRAM to system memory with a table of
    16-byte little-endian descriptors: frame buffer offset, system memory
    address, command (byte count in bits 20:0, 0x28000000, end of list in
    bit 31) and a status word.  It waits for BM_QUEUE_STATUS bit 27 to clear,
    writes the table address to 0xa50 with bit 0 set, and waits again.  The
    copy direction for other command values isn't known.
***************************************************************************/

void ati_rage128_device::gui_dma_run(u32 table)
{
	wait_for_render();

	address_space *const space = get_pci_busmaster_space();
	if (!space)
	{
		LOGUNIMPL("GUI DMA with no bus master space\n");
		return;
	}
	if (BIT(m_regs[BUS_CNTL / 4], 6))
		LOGENGINE("GUI DMA with BUS_MASTER_DIS set\n");

	for (int entry = 0; entry < 4096; entry++, table += 16)
	{
		const u32 fb = space->read_dword(table + 0) & 0x03ff'ffff;
		const u32 sys = space->read_dword(table + 4);
		const u32 cmd = space->read_dword(table + 8);
		const u32 bytes = cmd & 0x1f'ffff;
		LOGENGINE("GUI DMA %06x bytes from VRAM %08x to %08x, command %08x\n", bytes, fb, sys, cmd);

		for (u32 i = 0; i < bytes; i++)
			space->write_byte(sys + i, m_vram[(fb + i) & m_vram_mask]);

		if (cmd & BM_CMD_EOL)
			break;
	}
}


/***************************************************************************
    CCE (PM4) command processor

    The real CCE runs microcode loaded by the driver; here the packet
    formats that the microcode implements are interpreted directly.

    Type 0: register writes, count+1 dwords, optionally all to one register
    Type 1: two register writes packed into one header
    Type 2: filler
    Type 3: opcode in bits 15:8, count+1 dwords of payload
***************************************************************************/

bool ati_rage128_device::cce_pio_mode() const
{
	switch (m_regs[PM4_BUFFER_CNTL / 4] >> 28)
	{
	case 1: case 3: case 5: case 7: case 15: return true;
	default: return false;
	}
}

bool ati_rage128_device::cce_bm_mode() const
{
	switch (m_regs[PM4_BUFFER_CNTL / 4] >> 28)
	{
	case 2: case 4: case 6: case 8: return true;
	default: return false;
	}
}

// Depth of the primary command FIFO for each PM4_BUFFER_CNTL mode: 192 entries,
// or 128 or 64 when the rest is given to the indirect and vertex FIFOs.  Mac OS X
// runs mode 7 (64 PIO, 64 vertex and 64 indirect bus master entries).
u32 ati_rage128_device::pm4_fifo_depth() const
{
	switch (m_regs[PM4_BUFFER_CNTL / 4] >> 28)
	{
	case 1: case 2: return 192;
	case 3: case 4: return 128;
	case 5: case 6: case 7: case 8: case 15: return 64;
	default: return 0;
	}
}

// Translate a card address to a PCI bus address; false means local memory.
//
// The card sees VRAM from 0 and the AGP/GART window from 32 MiB.  Each bus master
// client goes through AGP or the PCI GART: PCI_GART_PAGE bit 0 selects AGP, and
// the BM_CHUNK_0_VAL "force to PCI" bits override that per client.
//
// The CCE only fetches from system memory.  Mac OS X gives indirect buffers as
// plain GART offsets (masked to 25 bits) while Linux ORs in the 32 MiB window
// base, so command fetch addresses are taken as GART offsets either way.
// With BM_PTR_FORCE_TO_PCI the read pointer writeback address is a physical bus
// address, as the Linux DRM programs it for PCI cards.
bool ati_rage128_device::card_to_bus(u32 addr, u32 &bus, bm_client client)
{
	const u32 chunk = m_regs[BM_CHUNK_0_VAL / 4];

	if ((client == BM_PTR) && (chunk & BM_PTR_FORCE_TO_PCI))
	{
		bus = addr;
		return true;
	}

	if ((client != BM_PM4) && (addr < AGP_WINDOW_BASE))
		return false;

	const u32 offset = (addr >= AGP_WINDOW_BASE) ? (addr - AGP_WINDOW_BASE) : addr;
	bool force_pci = (chunk & BM_GLOBAL_FORCE_TO_PCI) != 0;
	if (client == BM_PM4)
		force_pci = force_pci || (chunk & BM_PM4_RD_FORCE_TO_PCI);

	if (!force_pci && BIT(m_regs[PCI_GART_PAGE / 4], 0))
	{
		// AGP: the chipset GART translates the AGP aperture
		bus = m_regs[AGP_BASE / 4] + m_regs[AGP_APER_OFFSET / 4] + offset;
	}
	else
	{
		// PCI GART: a flat table of little-endian page addresses in system memory.
		// Entries are cached for the current command buffer or primitive (see gart_flush).
		const u32 page = offset / PCI_GART_PAGE_SIZE;
		gart_entry &cached = m_gart_cache[page % GART_CACHE_SIZE];
		if (cached.tag != ((page << 1) | 1))
		{
			address_space *const space = get_pci_busmaster_space();
			if (!space)
				return false;
			const u32 table = m_regs[PCI_GART_PAGE / 4] & ~(PCI_GART_PAGE_SIZE - 1);
			cached.bus = space->read_dword(table + page * 4) & ~(PCI_GART_PAGE_SIZE - 1);
			cached.tag = (page << 1) | 1;
		}
		bus = cached.bus | (offset & (PCI_GART_PAGE_SIZE - 1));
	}
	return true;
}

// The CPU can only rewrite GART entries between command buffers and PIO packets, so
// translations are dropped whenever the CCE starts on one, and before each primitive.
void ati_rage128_device::gart_flush()
{
	for (auto &entry : m_gart_cache)
		entry.tag = 0;
}

u32 ati_rage128_device::card_read32(u32 addr, bm_client client)
{
	u32 bus;
	if (!card_to_bus(addr, bus, client))
	{
		// command and vertex buffers in VRAM are fetched while rendering goes on
		const u32 vaddr = addr & m_vram_mask & ~3;
		wait_for_vram_read(vaddr);
		return get_u32le(&m_vram[vaddr]);
	}

	address_space *const space = get_pci_busmaster_space();
	if (!space)
	{
		LOGUNIMPL("bus master read from %08x with no bus master space\n", bus);
		return 0;
	}
	return space->read_dword(bus & ~3);
}

void ati_rage128_device::card_write32(u32 addr, u32 data, bm_client client)
{
	u32 bus;
	if (!card_to_bus(addr, bus, client))
	{
		const u32 vaddr = addr & m_vram_mask & ~3;
		wait_for_vram_write(vaddr);
		put_u32le(&m_vram[vaddr], data);
		return;
	}

	address_space *const space = get_pci_busmaster_space();
	if (space)
		space->write_dword(bus & ~3, data);
}

void ati_rage128_device::cce_write(u32 data)
{
	if (!m_packet_remaining)
	{
		m_packet_header = data;
		m_packet_count = 0;
		switch (data >> 30)
		{
		case 0: m_packet_remaining = BIT(data, 16, 14) + 1; break;
		case 1: m_packet_remaining = 2; break;
		case 2: break;
		case 3: m_packet_remaining = BIT(data, 16, 14) + 1; break;
		}
		if ((data >> 30) == 0)
			LOGPACKET("CCE type 0: %u dword(s) to %04x%s\n", m_packet_remaining, (data & 0x7ff) << 2, BIT(data, 15) ? " (one register)" : "");
		else if ((data >> 30) == 1)
			LOGPACKET("CCE type 1: %04x, %04x\n", (data & 0x7ff) << 2, ((data >> 11) & 0x7ff) << 2);
		return;
	}

	const u32 index = m_packet_count++;
	m_packet_remaining--;

	switch (m_packet_header >> 30)
	{
	case 0:
	{
		// register writes are streamed as they arrive
		const u32 base = (m_packet_header & 0x7ff) << 2;
		const u32 reg = BIT(m_packet_header, 15) ? base : (base + index * 4);
		LOGPACKET("  %04x = %08x\n", reg, data);
		reg_w(reg, data, ~0U);
		break;
	}

	case 1:
		reg_w(index ? (((m_packet_header >> 11) & 0x7ff) << 2) : ((m_packet_header & 0x7ff) << 2), data, ~0U);
		break;

	case 3:
		m_packet[index] = data;
		if (!m_packet_remaining)
		{
			LOGPACKET("CCE packet3 %02x, %u dwords:%s\n", BIT(m_packet_header, 8, 8), m_packet_count, dump_dwords(m_packet, m_packet_count).c_str());
			cce_packet3(BIT(m_packet_header, 8, 8), m_packet, m_packet_count);
		}
		break;
	}
}

void ati_rage128_device::cce_run_ring()
{
	if (!cce_bm_mode() || !(m_regs[PM4_MICRO_CNTL / 4] & PM4_MICRO_FREERUN))
		return;
	if (m_cce_depth > 2)
		return;
	if (BIT(m_regs[BUS_CNTL / 4], 6))
		LOGCCE("ring fetch with BUS_MASTER_DIS set\n");

	const u32 cntl = m_regs[PM4_BUFFER_CNTL / 4];
	const u32 size = 2U << (cntl & 0x1f);
	const u32 base = m_regs[PM4_BUFFER_OFFSET / 4];
	const u32 wptr = m_regs[PM4_BUFFER_DL_WPTR / 4] & (size - 1);
	u32 rptr = m_regs[PM4_BUFFER_DL_RPTR / 4] & (size - 1);

	gart_flush();
	m_cce_depth++;
	u32 guard = size;
	while (rptr != wptr && guard--)
	{
		const u32 data = card_read32(base + rptr * 4, BM_PM4);
		rptr = (rptr + 1) & (size - 1);
		m_regs[PM4_BUFFER_DL_RPTR / 4] = rptr;
		cce_write(data);
	}
	m_cce_depth--;
	if (!m_cce_depth)
		kick_render();

	if (!(cntl & PM4_BUFFER_CNTL_NOUPDATE) && m_regs[PM4_BUFFER_DL_RPTR_ADDR / 4])
		card_write32(m_regs[PM4_BUFFER_DL_RPTR_ADDR / 4], rptr, BM_PTR);
}

void ati_rage128_device::cce_run_indirect(u32 addr, u32 dwords)
{
	if (m_cce_depth > 2)
	{
		LOGCCE("indirect buffer nested too deeply\n");
		return;
	}
	LOGCCE("indirect buffer at %08x, %u dwords\n", addr, dwords);
	gart_flush();

	// An indirect buffer is fired by a Type 0 write to PM4_IW_INDSIZE, so the
	// outer packet is a register write that needs only its header and position.
	const u32 saved_header = m_packet_header;
	const u32 saved_remaining = m_packet_remaining;
	const u32 saved_count = m_packet_count;
	m_packet_remaining = 0;

	m_cce_depth++;
	for (u32 i = 0; i < dwords; i++)
		cce_write(card_read32(addr + i * 4, BM_PM4));
	m_cce_depth--;
	if (!m_cce_depth)
		kick_render();

	if (m_packet_remaining)
		LOGCCE("indirect buffer ended inside a packet\n");
	m_packet_header = saved_header;
	m_packet_remaining = saved_remaining;
	m_packet_count = saved_count;
}

void ati_rage128_device::cce_packet3(u8 opcode, const u32 *data, u32 count)
{
	// 2D packets draw straight into VRAM, after the 3D queued before them
	switch (opcode & 0x7f)
	{
	case 0x10: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x23: case 0x25: case 0x2c: case 0x2d: case 0x2e: case 0x31:
		break;
	// rectangles, host data and the scaler wait for the 3D they overlap themselves
	case 0x11: case 0x12: case 0x14: case 0x16: case 0x17: case 0x1a: case 0x1b: case 0x1c:
		break;
	default:
		wait_for_render();
		break;
	}

	u32 idx = 0;
	auto next = [&idx, data, count] () -> u32 { return (idx < count) ? data[idx++] : 0; };

	// The CNTL_ variants start with GUI master control and the state it selects:
	// pitch/offset words for GMC bits 0 and 1, then clipping words for bits 2
	// and 3 (one for the source, two for the destination), then the brush and
	// source colours.  ATI's NT driver sends CNTL_BITBLT with all four bits set
	// in 9 dwords, and the Mac drivers set the destination clip bit on
	// CNTL_HOSTDATA_BLT.
	//
	// The Mac OS 9 accelerator's 8x8 mono brush paints show the brush layout: the
	// background and foreground colours for an FG_BG brush but only the foreground
	// for an FG_LA one, then the pattern, then BRUSH_Y_X when GMC bit 31
	// (GMC_LD_BRUSH_Y_X) is set.  The other mono brush sizes are assumed to match.
	auto load_header = [this, &next] (bool has_src, bool src_colours) {
		const u32 gmc = next();
		gmc_w(gmc);
		if ((gmc & GMC_SRC_PITCH_OFFSET_CNTL) && has_src)
			reg_w(SRC_PITCH_OFFSET, next(), ~0U);
		if (gmc & GMC_DST_PITCH_OFFSET_CNTL)
			reg_w(DST_PITCH_OFFSET, next(), ~0U);
		if ((gmc & GMC_SRC_CLIPPING) && has_src)
			reg_w(SRC_SC_BOTTOM_RIGHT, next(), ~0U);
		if (gmc & GMC_DST_CLIPPING)
		{
			reg_w(SC_TOP_LEFT, next(), ~0U);
			reg_w(SC_BOTTOM_RIGHT, next(), ~0U);
		}

		const u32 brush = BIT(gmc, 4, 4);
		switch (brush)
		{
		case BRUSH_8X8_MONO_FG_BG:
		case BRUSH_8X8_MONO_FG_LA:
		case BRUSH_1X8_MONO_FG_BG:
		case BRUSH_1X8_MONO_FG_LA:
		case BRUSH_32X1_MONO_FG_BG:
		case BRUSH_32X1_MONO_FG_LA:
		case BRUSH_32X32_MONO_FG_BG:
		case BRUSH_32X32_MONO_FG_LA:
		{
			// the odd (FG_LA) types leave background pixels alone and carry no background colour
			if (!(brush & 1))
				m_regs[DP_BRUSH_BKGD_CLR / 4] = next();
			m_regs[DP_BRUSH_FRGD_CLR / 4] = next();
			const int rows = (brush <= BRUSH_8X8_MONO_FG_LA) ? 2 : (brush <= BRUSH_32X1_MONO_FG_LA) ? 1 : 32;
			for (int i = 0; i < rows; i++)
				m_regs[BRUSH_DATA0 / 4 + i] = next();
			break;
		}
		case BRUSH_8X8_COLOR:
		{
			const int n = 16 * std::max(1, dst_bpp());
			for (int i = 0; i < n && i < 64; i++)
				m_regs[BRUSH_DATA0 / 4 + i] = next();
			break;
		}
		case BRUSH_1X8_COLOR:
		{
			const int n = 2 * std::max(1, dst_bpp());
			for (int i = 0; i < n; i++)
				m_regs[BRUSH_DATA0 / 4 + i] = next();
			break;
		}
		case BRUSH_SOLID_COLOR:
			m_regs[DP_BRUSH_FRGD_CLR / 4] = next();
			break;
		default:
			break;
		}

		if (gmc & GMC_LD_BRUSH_Y_X)
			m_regs[BRUSH_Y_X / 4] = next();

		if (src_colours)
		{
			m_regs[DP_SRC_FRGD_CLR / 4] = next();
			m_regs[DP_SRC_BKGD_CLR / 4] = next();
		}
	};

	const bool cntl = BIT(opcode, 7);

	switch (opcode & 0x7f)
	{
	case 0x10:  // NOP
		break;

	case 0x11:  // PAINT: (top << 16) | left, then the exclusive (bottom << 16) | right
		if (cntl)
			load_header(false, false);
		m_dp_cntl = 3;
		while (count - idx >= 2)
		{
			const u32 tl = next(), br = next();
			m_dst_x = lo16s(tl);
			m_dst_y = hi16s(tl);
			m_dst_width = std::max<s32>(lo16s(br) - m_dst_x, 0) & 0x3fff;
			m_dst_height = std::max<s32>(hi16s(br) - m_dst_y, 0) & 0x3fff;
			gui_trigger_rect();
		}
		break;

	case 0x1a:  // PAINT_MULTI: (x << 16) | y, then (width << 16) | height
		if (cntl)
			load_header(false, false);
		m_dp_cntl = 3;
		while (count - idx >= 2)
		{
			const u32 xy = next(), wh = next();
			m_dst_x = hi16s(xy);
			m_dst_y = lo16s(xy);
			m_dst_width = (wh >> 16) & 0x3fff;
			m_dst_height = wh & 0x3fff;
			gui_trigger_rect();
		}
		break;

	case 0x12:  // BITBLT
	case 0x1b:  // BITBLT_MULTI
	case 0x1c:  // TRANS_BITBLT
	{
		// the non-CNTL forms use the current engine state
		if (cntl && count)
			load_header(true, BIT(data[0], 12, 2) != 3);

		// TRANS_BITBLT puts the colour compare state between the header and the blits.
		// The Mac OS 9 accelerator sends CLR_CMP_CNTL = 0x01000005 (write source pixels
		// that differ from the key) with the port background colour as the key, and its
		// GMC leaves CLR_CMP_CNTL_DIS clear.  The non-CNTL form is assumed to match.
		if ((opcode & 0x7f) == 0x1c)
		{
			reg_w(CLR_CMP_CNTL, next(), ~0U);
			reg_w(CLR_CMP_CLR_SRC, next(), ~0U);
			reg_w(CLR_CMP_CLR_DST, next(), ~0U);
		}

		while (count - idx >= 3)
		{
			const u32 sxy = next(), dxy = next(), wh = next();
			s32 sx = hi16s(sxy), sy = lo16s(sxy);
			s32 dx = hi16s(dxy), dy = lo16s(dxy);
			const int w = (wh >> 16) & 0x3fff, h = wh & 0x3fff;

			// the microcode's direction choice is unknown; pick one that keeps overlapping copies correct
			int dirs = 3;
			if (m_src_offset == m_dst_offset && m_src_pitch == m_dst_pitch)
			{
				if (sx < dx)
				{
					dirs &= ~1;
					sx += w - 1;
					dx += w - 1;
				}
				if (sy < dy)
				{
					dirs &= ~2;
					sy += h - 1;
					dy += h - 1;
				}
			}
			m_dp_cntl = (m_dp_cntl & ~3U) | dirs;
			m_src_x = sx; m_src_y = sy;
			m_dst_x = dx; m_dst_y = dy;
			m_dst_width = w;
			m_dst_height = h;
			gui_trigger_rect();
		}
		m_dp_cntl |= 3;
		break;
	}

	case 0x14:  // HOSTDATA_BLT
	{
		if (cntl)
			load_header(false, true);
		const u32 yx = next(), hw = next();
		const u32 dwords = next();
		m_dp_cntl = 3;
		m_dst_y = hi16s(yx);
		m_dst_x = lo16s(yx);
		m_dst_height = (hw >> 16) & 0x3fff;
		m_dst_width = hw & 0x3fff;
		const u32 avail = std::min<u32>(dwords, count - std::min(idx, count));
		if (!DEBUG_NO_QUEUED_2D && queue_host_blit(data + idx, avail))
			break;
		gui_trigger_rect();
		for (u32 i = 0; i < avail; i++)
			host_data_w(next(), i == avail - 1);
		break;
	}

	case 0x15:  // POLYLINE: a list of (y << 16) | x points joined by Bresenham lines
	{
		// The Mac OS 9 accelerator packs the points as DST_Y_X, and sets
		// DST_LAST_PEL in DP_CNTL around each polyline so the final point is drawn.
		if (cntl)
			load_header(false, false);
		if (idx >= count)
			break;
		u32 p = next();
		s32 x0 = lo16s(p), y0 = hi16s(p);
		while (idx < count)
		{
			p = next();
			const s32 x1 = lo16s(p), y1 = hi16s(p);
			const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
			const int sx = (x1 >= x0) ? 1 : -1, sy = (y1 >= y0) ? 1 : -1;
			int err = dx - dy;
			s32 x = x0, y = y0;
			for (int i = 0; ; i++)
			{
				// the last point of each segment is drawn by the next one
				if (x == x1 && y == y1)
					break;
				plot(x, y, m_regs[DP_SRC_FRGD_CLR / 4], false, i);
				const int e2 = err * 2;
				if (e2 > -dy)
				{
					err -= dy;
					x += sx;
				}
				if (e2 < dx)
				{
					err += dx;
					y += sy;
				}
			}
			x0 = x1;
			y0 = y1;
		}
		if (m_regs[DP_CNTL / 4] & DP_DST_LAST_PEL)
			plot(x0, y0, m_regs[DP_SRC_FRGD_CLR / 4], false, 0);
		break;
	}

	case 0x18:  // POLYSCANLINES
	case 0x1d:  // PLY_NEXTSCAN: more scanlines for the previous POLYSCANLINES
	{
		// Each scanline is (span count << 16) | y followed by its spans as
		// (x_right << 16) | x_left, as ATI's NT driver sends them.  The right
		// edge is taken as exclusive.
		if (cntl)
			load_header(false, false);
		while (idx < count)
		{
			const u32 line = next();
			const s32 y = lo16s(line);
			for (u32 n = line >> 16; n && (idx < count); n--)
			{
				const u32 span = next();
				for (s32 x = lo16s(span); x < hi16s(span); x++)
					plot(x, y, m_regs[DP_SRC_FRGD_CLR / 4], false, -1);
			}
		}
		break;
	}

	case 0x1e:  // SET_SCISSORS: (top << 16) | left, then the inclusive bottom-right corner
		reg_w(SC_TOP_LEFT, next(), ~0U);
		reg_w(SC_BOTTOM_RIGHT, next(), ~0U);
		break;

	case 0x1f:  // one dword (1) closing each NT driver batch, apparently a marker
	case 0x31:  // one dword (0, 0x40, 0x100 or 0x400) around flushes and DVD motion compensation
		LOGCCE("packet3 %02x (%08x) ignored\n", opcode, count ? data[0] : 0);
		break;

	case 0x16:  // SCALING
	case 0x17:  // TRANS_SCALING
	{
		// The GUI master control header, the colour compare state for TRANS_SCALING,
		// then an 11-dword scaler block.  Mac OS 9 presents RAVE frames rendered at
		// a lower resolution with CNTL_SCALING.
		if (cntl && count)
			load_header(true, BIT(data[0], 12, 2) != 3);
		if ((opcode & 0x7f) == 0x17)
		{
			reg_w(CLR_CMP_CNTL, next(), ~0U);
			reg_w(CLR_CMP_CLR_SRC, next(), ~0U);
			reg_w(CLR_CMP_CLR_DST, next(), ~0U);
		}
		if (count - std::min(idx, count) < 11)
		{
			LOGUNIMPL("packet3 %02x: scaler block cut short (%u dwords):%s\n", opcode, count, dump_dwords(data, count).c_str());
			break;
		}
		scale_block(data + idx);
		break;
	}

	case 0x19:  // NEXT_CHAR: (y << 16) | x, a control dword, then glyph bitmaps
	case 0x28:  // vertex buffer draw built by the Windows D3D driver
		LOGUNIMPL("packet3 %02x (%u dwords) not supported:%s\n", opcode, count, dump_dwords(data, count).c_str());
		break;

	case 0x20:  // 3D_SAVE_CONTEXT
	case 0x21:  // 3D_PLAY_CONTEXT
	case 0x2d:  // PURGE
		LOGCCE("packet3 %02x ignored\n", opcode);
		break;

	case 0x23:  // 3D_RNDR_GEN_INDX_PRIM
	{
		const u32 vb = next();
		const u32 vb_size = next();
		const u32 fmt = next();
		const u32 vc = next();
		LOG3D("3D_RNDR_GEN_INDX_PRIM: vertex buffer %08x size %08x format %03x VC_CNTL %08x (prim %d walk %d, %u vertices), %u dwords follow:%s\n",
				vb, vb_size, fmt, vc, vc & 0xf, BIT(vc, 4, 2), vc >> 16, count - std::min(idx, count),
				dump_dwords(data + std::min(idx, count), count - std::min(idx, count)).c_str());
		if (BIT(vc, 4, 2) == 1)
		{
			// indices follow, two per dword
			render_prim(fmt, vc, nullptr, 0, vb, data + idx, (count - idx) * 2);
		}
		else
			render_prim(fmt, vc, nullptr, 0, vb, nullptr, 0);
		break;
	}

	case 0x25:  // 3D_RNDR_GEN_PRIM
	{
		const u32 fmt = next();
		const u32 vc = next();
		LOG3D("3D_RNDR_GEN_PRIM: format %03x VC_CNTL %08x (prim %d walk %d, %u vertices of %u dwords), %u dwords follow\n",
				fmt, vc, vc & 0xf, BIT(vc, 4, 2), vc >> 16, vertex_dwords(fmt), count - std::min(idx, count));
		if (BIT(vc, 4, 2) == 3)
			render_prim(fmt, vc, data + idx, count - idx, 0, nullptr, 0);
		else
			LOGUNIMPL("3D_RNDR_GEN_PRIM walk mode %d\n", BIT(vc, 4, 2));
		break;
	}

	case 0x2c:  // LOAD_PALETTE: texture palette entries (first dword taken as the start index)
	{
		const u32 start = next() & 0xff;
		for (u32 i = 0; idx < count && i < 256; i++)
			m_tex_palette[(start + i) & 0xff] = next();
		break;
	}

	case 0x2e:  // NEXT_VERTEX_BUNDLE
		LOGUNIMPL("NEXT_VERTEX_BUNDLE not supported\n");
		break;

	default:
		LOGUNIMPL("packet3 opcode %02x (%u dwords) not supported\n", opcode, count);
		break;
	}
}

/***************************************************************************
    3D engine

    Vertices arrive through 3D_RNDR_GEN_PRIM (inline) or
    3D_RNDR_GEN_INDX_PRIM (from a vertex buffer, listed or indexed).
    The setup engine and pixel pipeline are modelled in software.  The
    register state is captured once per primitive packet (setup_raster_state)
    and triangles are walked one span per row with plane equations.

    Texturing follows 86Box's Rage 128 (vid_ati_rage128_3d.c), which was
    checked against real games:
    - the largest level is in offset slot TEX_SIZE - TEX_MIN_SIZE, and each
      lower slot holds the next smaller level, so a full mip chain from
      256x256 down to 1x1 puts the base level in PRIM_TEX_8_OFFSET_C and the
      1x1 level in PRIM_TEX_0_OFFSET_C
    - the largest level is 2^TEX_PITCH texels wide (also its pitch) and
      2^TEX_HEIGHT high; TEX_SIZE is the log2 of the larger side
    - S and T are normalised; with SCALE_3D_CNTL bit 30 (TEX_MAP_ALPHA_IN_
      TEXTURE) clear, texture alpha reads as 0xFF after the chroma key test
***************************************************************************/

namespace {

// CCE_VC_FRMT bits
constexpr u32 VC_FRMT_RHW           = 1U << 0;
constexpr u32 VC_FRMT_DIFFUSE_BGR   = 1U << 1;
constexpr u32 VC_FRMT_DIFFUSE_A     = 1U << 2;
constexpr u32 VC_FRMT_DIFFUSE_ARGB  = 1U << 3;
constexpr u32 VC_FRMT_SPEC_BGR      = 1U << 4;
constexpr u32 VC_FRMT_SPEC_F        = 1U << 5;
constexpr u32 VC_FRMT_SPEC_FRGB     = 1U << 6;
constexpr u32 VC_FRMT_S_T           = 1U << 7;
constexpr u32 VC_FRMT_S2_T2         = 1U << 8;
constexpr u32 VC_FRMT_RHW2          = 1U << 9;

// TEX_CNTL_C bits
constexpr u32 TEX_Z_ENABLE          = 1U << 0;
constexpr u32 TEX_Z_WRITE_ENABLE    = 1U << 1;
constexpr u32 TEX_STENCIL_ENABLE    = 1U << 3;
constexpr u32 TEX_TEXMAP_ENABLE     = 1U << 4;
constexpr u32 TEX_SEC_TEXMAP_ENABLE = 1U << 5;
constexpr u32 TEX_FOG_ENABLE        = 1U << 7;
constexpr u32 TEX_ALPHA_ENABLE      = 1U << 9;
constexpr u32 TEX_ALPHA_TEST_ENABLE = 1U << 10;
constexpr u32 TEX_SPEC_LIGHT_ENABLE = 1U << 11;
constexpr u32 TEX_CHROMA_KEY_ENABLE = 1U << 12;
constexpr u32 TEX_AMASK_ENABLE      = 1U << 13;     // name assumed, after the Rage Pro's TEX_AMASK_AEN; effect checked on hardware

// SCALE_3D_CNTL
constexpr u32 SCALE_TEX_MAP_ALPHA_IN_TEXTURE = 1U << 30;

// texture wrap modes (PRIM_TEX_CNTL_C bits 9:8 and 12:11)
enum : int
{
	TEX_WRAP = 0,
	TEX_MIRROR,
	TEX_CLAMP,
	TEX_BORDER
};

inline bool depth_test(int fn, u32 incoming, u32 stored)
{
	switch (fn & 7)
	{
	case 0: return false;
	case 1: return incoming < stored;
	case 2: return incoming <= stored;
	case 3: return incoming == stored;
	case 4: return incoming >= stored;
	case 5: return incoming > stored;
	case 6: return incoming != stored;
	default: return true;
	}
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Z_STEN_CNTL_C stencil operations: keep, zero, replace, increment, decrement, invert
inline u8 stencil_op(int op, u8 value, u8 ref)
{
	switch (op & 7)
	{
	case 1: return 0;
	case 2: return ref;
	case 3: return (value == 0xff) ? value : (value + 1);
	case 4: return value ? (value - 1) : 0;
	case 5: return ~value;
	default: return value;
	}
}

// a texel coordinate wrapped into a level n texels across, or -1 for the border colour
inline int tex_wrap(int c, int n, int mode)
{
	switch (mode)
	{
	case TEX_WRAP:
		return c & (n - 1);
	case TEX_MIRROR:
	{
		const int m = c & (2 * n - 1);
		return (m < n) ? m : (2 * n - 1 - m);
	}
	case TEX_CLAMP:
		return std::clamp(c, 0, n - 1);
	default:
		return ((c < 0) || (c >= n)) ? -1 : c;
	}
}

// keep wild texture coordinates in a range that converts to int safely
inline float tex_coord(float v)
{
	return (v > -16'777'216.0f) ? std::min(v, 16'777'216.0f) : -16'777'216.0f;    // also catches NaN
}

inline u8 frac8(float f)
{
	return u8(std::clamp(int(f * 256.0f), 0, 255));
}

inline void unpack_argb(u32 c, float &r, float &g, float &b, float &a)
{
	a = float(c >> 24) * (1.0f / 255.0f);
	r = float((c >> 16) & 0xff) * (1.0f / 255.0f);
	g = float((c >> 8) & 0xff) * (1.0f / 255.0f);
	b = float(c & 0xff) * (1.0f / 255.0f);
}

} // anonymous namespace

u32 ati_rage128_device::vertex_dwords(u32 f)
{
	// x, y, z then the optional fields in order
	return 3
			+ ((f & VC_FRMT_RHW) ? 1 : 0)
			+ ((f & VC_FRMT_DIFFUSE_BGR) ? 3 : 0)
			+ ((f & VC_FRMT_DIFFUSE_A) ? 1 : 0)
			+ ((f & VC_FRMT_DIFFUSE_ARGB) ? 1 : 0)
			+ ((f & VC_FRMT_SPEC_BGR) ? 3 : 0)
			+ ((f & VC_FRMT_SPEC_F) ? 1 : 0)
			+ ((f & VC_FRMT_SPEC_FRGB) ? 1 : 0)
			+ ((f & VC_FRMT_S_T) ? 2 : 0)
			+ ((f & VC_FRMT_S2_T2) ? 2 : 0)
			+ ((f & VC_FRMT_RHW2) ? 1 : 0);
}

bool ati_rage128_device::fetch_vertex(u32 f, const u32 *&src, const u32 *end, vertex3d &v)
{
	if (end - src < ptrdiff_t(vertex_dwords(f)))
		return false;

	auto flt = [&src] () { return u2f(*src++); };

	v.x = flt();
	v.y = flt();
	v.z = flt();
	v.w = (f & VC_FRMT_RHW) ? flt() : 1.0f;
	v.r = v.g = v.b = v.a = 255.0f;
	v.sr = v.sg = v.sb = 0.0f;
	v.sf = 255.0f;
	v.s0 = v.t0 = v.s1 = v.t1 = 0.0f;

	if (f & VC_FRMT_DIFFUSE_BGR)
	{
		v.b = flt() * 255.0f;
		v.g = flt() * 255.0f;
		v.r = flt() * 255.0f;
	}
	if (f & VC_FRMT_DIFFUSE_A)
		v.a = flt() * 255.0f;
	if (f & VC_FRMT_DIFFUSE_ARGB)
	{
		const u32 c = *src++;
		v.a = float(c >> 24);
		v.r = float((c >> 16) & 0xff);
		v.g = float((c >> 8) & 0xff);
		v.b = float(c & 0xff);
	}
	if (f & VC_FRMT_SPEC_BGR)
	{
		v.sb = flt() * 255.0f;
		v.sg = flt() * 255.0f;
		v.sr = flt() * 255.0f;
	}
	if (f & VC_FRMT_SPEC_F)
		v.sf = flt() * 255.0f;
	if (f & VC_FRMT_SPEC_FRGB)
	{
		const u32 c = *src++;
		v.sf = float(c >> 24);
		v.sr = float((c >> 16) & 0xff);
		v.sg = float((c >> 8) & 0xff);
		v.sb = float(c & 0xff);
	}
	if (f & VC_FRMT_S_T)
	{
		v.s0 = flt();
		v.t0 = flt();
	}
	if (f & VC_FRMT_S2_T2)
	{
		v.s1 = flt();
		v.t1 = flt();
	}
	if (f & VC_FRMT_RHW2)
		src++;

	// window offset: X in bits 31:20, Y in bits 19:4
	const u32 wofs = m_regs[WINDOW_XY_OFFSET / 4];
	v.x += float(s32(wofs) >> 20);
	v.y += float(s16((wofs >> 4) & 0xffff));
	return true;
}

bool ati_rage128_device::fetch_vertex_mem(u32 f, u32 addr, vertex3d &v)
{
	u32 buf[24];
	const u32 n = vertex_dwords(f);
	for (u32 i = 0; i < n; i++)
		buf[i] = card_read32(addr + i * 4);
	const u32 *p = buf;
	return fetch_vertex(f, p, buf + n, v);
}

void ati_rage128_device::render_prim(u32 vc_format, u32 vc_cntl, const u32 *inline_data, u32 inline_count, u32 vb_addr, const u32 *indices, u32 index_count)
{
	const int prim = vc_cntl & 0xf;
	const int walk = BIT(vc_cntl, 4, 2);
	const u32 num = vc_cntl >> 16;
	const u32 stride = vertex_dwords(vc_format) * 4;

	gart_flush();
	setup_raster_state();

	m_verts.clear();
	for (u32 i = 0; i < num; i++)
	{
		vertex3d v;
		bool ok = false;
		switch (walk)
		{
		case 3:     // inline in the packet
		{
			const u32 *p = inline_data + i * (stride / 4);
			ok = (i + 1) * (stride / 4) <= inline_count && fetch_vertex(vc_format, p, inline_data + inline_count, v);
			break;
		}
		case 2:     // sequential from the vertex buffer
			ok = fetch_vertex_mem(vc_format, vb_addr + i * stride, v);
			break;
		case 1:     // indexed
			if (indices && i < index_count)
			{
				const u32 index = (indices[i >> 1] >> ((i & 1) * 16)) & 0xffff;
				ok = fetch_vertex_mem(vc_format, vb_addr + index * stride, v);
			}
			break;
		}
		if (!ok)
			break;
		m_verts.push_back(v);
	}

	LOG3D("prim %d walk %d: %u vertices, format %03x, FPU_SETUP %08x SETUP_CNTL %08x (shade %d) TEX_CNTL_C %08x MISC %08x SCALE_3D_CNTL %08x Z_STEN %08x FOG_COLOR %08x DST %08x Z %08x/%08x\n",
			prim, walk, u32(m_verts.size()), vc_format, m_rs.fpu_setup, m_regs[SETUP_CNTL / 4], m_rs.shade, m_rs.tex_cntl, m_rs.misc,
			m_regs[SCALE_3D_CNTL / 4], m_regs[Z_STEN_CNTL_C / 4], m_regs[FOG_COLOR_C / 4],
			m_regs[DST_PITCH_OFFSET_C / 4], m_regs[Z_OFFSET_C / 4], m_regs[Z_PITCH_C / 4]);
	if (m_verts.size() != num)
		LOG3D("  only %u of %u vertices fetched\n", u32(m_verts.size()), num);
	if ((VERBOSE & LOG_3D) && (walk != 3))
	{
		u32 bus = 0;
		const bool system = card_to_bus(vb_addr, bus, BM_DATA);
		LOG3D("  vertex buffer %08x is %s %08x\n", vb_addr, system ? "system memory at" : "VRAM at", system ? bus : (vb_addr & m_vram_mask));
	}
	if (indices)
		LOG3D("  indices:%s\n", dump_dwords(indices, std::min<u32>((index_count + 1) / 2, 8)).c_str());
	for (size_t i = 0; i < std::min<size_t>(m_verts.size(), 8); i++)
	{
		const vertex3d &v = m_verts[i];
		LOG3D("  v%u: %.2f,%.2f z %f rhw %f argb %.0f,%.0f,%.0f,%.0f spec %.0f,%.0f,%.0f fog %.0f st %f,%f st2 %f,%f\n",
				u32(i), v.x, v.y, v.z, v.w, v.a, v.r, v.g, v.b, v.sr, v.sg, v.sb, v.sf, v.s0, v.t0, v.s1, v.t1);
	}
	if (m_rs.textured)
	{
		for (int unit = 0; unit < 2; unit++)
		{
			const texunit &tu = m_rs.tex[unit];
			if (tu.enabled)
			{
				LOG3D("  tex%d: %dx%d format %d, base slot %d at %08x, PRIM/SEC_TEX_CNTL %08x (min %d, mag %s%s), wrap %d/%d, combine %08x\n",
						unit, 1 << tu.width_l2, 1 << tu.height_l2, tu.fmt, tu.top, tu.offset[tu.top], tu.cntl,
						tu.min_filter, tu.linear_mag ? "linear" : "nearest", tu.mip_disable ? ", no mips" : "", tu.s_mode, tu.t_mode, tu.combine);
			}
		}
	}

	const std::vector<vertex3d> &verts = m_verts;
	const size_t n = verts.size();
	switch (prim)
	{
	case 1:     // points
		for (size_t i = 0; i < n; i++)
			queue_prim(PRIM_POINT, verts[i], verts[i], verts[i]);
		break;
	case 2:     // lines
		for (size_t i = 0; i + 1 < n; i += 2)
			queue_prim(PRIM_LINE, verts[i], verts[i + 1], verts[i + 1]);
		break;
	case 3:     // line strip
		for (size_t i = 0; i + 1 < n; i++)
			queue_prim(PRIM_LINE, verts[i], verts[i + 1], verts[i + 1]);
		break;
	case 4:     // triangle list
	case 7:     // "type 2" triangles behave like a list here
		for (size_t i = 0; i + 2 < n; i += 3)
			queue_prim(PRIM_TRIANGLE, verts[i], verts[i + 1], verts[i + 2]);
		break;
	case 5:     // fan
		for (size_t i = 1; i + 1 < n; i++)
			queue_prim(PRIM_TRIANGLE, verts[0], verts[i], verts[i + 1]);
		break;
	case 6:     // strip
		for (size_t i = 0; i + 2 < n; i++)
		{
			if (i & 1)
				queue_prim(PRIM_TRIANGLE, verts[i + 1], verts[i], verts[i + 2]);
			else
				queue_prim(PRIM_TRIANGLE, verts[i], verts[i + 1], verts[i + 2]);
		}
		break;
	default:
		break;
	}

	// keep the render threads busy while the CPU builds the next batch
	if (m_pending->prims.size() >= RENDER_BATCH_PRIMS)
		kick_render();
}


/***************************************************************************
    3D rendering threads

    After powervr2.cpp: primitives are queued with a copy of the state they
    were issued with, and rendered on the OSD work queue while emulation
    carries on.  Each work item draws every primitive of a batch, but only
    the rows of its own 16-line stripes.  Up to MAX_OUTSTANDING_BATCHES
    batches are in flight; each starts when the one before it is done.  2D
    fills ride along with the primitives (each band fills its own rows), and
    a scaler job or a colour host data blit is a batch of its own, so the
    CPU doesn't wait for them either.  Each batch records the VRAM it
    may read and write, and anything else that touches VRAM (the apertures,
    CCE fetches from local memory, the 2D rectangles, host data, the screen
    update) waits only when it overlaps, so the result doesn't depend on
    host timing.  The VGA window, lines, the register-driven scaler, GUI DMA
    and save states wait for everything.  Primitives that sample system
    memory are drawn on the emulation thread, since only it may touch the bus.
***************************************************************************/

void ati_rage128_device::queue_prim(u8 kind, const vertex3d &v0, const vertex3d &v1, const vertex3d &v2)
{
	if (m_rs.sync)
	{
		m_perf_sync++;
		wait_for_render();
		switch (kind)
		{
		case PRIM_TRIANGLE: draw_triangle(m_rs, v0, v1, v2, 0, 1, puml_block(m_rs)); break;
		case PRIM_LINE: draw_line3d(m_rs, v0, v1, 0, 1); break;
		default: draw_point(m_rs, v0, 0, 1); break;
		}
		return;
	}

	if (m_pending->states.empty() || std::memcmp(&m_pending->states.back(), &m_rs, sizeof(raster_state)))
	{
		// a texture a fill in this batch writes may be in another band's rows
		if (m_pending->fill_writes.count)
		{
			vram_ranges textures;
			texture_spans(m_rs, textures);
			if (m_pending->fill_writes.overlaps(textures))
				kick_render();
		}
		// (compiling may flush the cache, which first waits for everything queued)
		const puml::compiled *const block = puml_block(m_rs);
		m_pending->states.push_back(m_rs);
		m_pending->blocks.push_back(block);
		note_vram_use(*m_pending, m_rs);
	}
	render_batch &batch = *m_pending;

	queued_prim &p = batch.prims.emplace_back();
	p.kind = kind;
	p.state = u32(batch.states.size() - 1);
	p.v[0] = v0;
	p.v[1] = v1;
	p.v[2] = v2;
}

// Submit the queued primitives.  They start at once if nothing else is
// rendering, otherwise when the batches before them are done.
void ati_rage128_device::kick_render()
{
	if (m_pending->empty())
		return;

	// keep the number of batches in flight bounded
	retire_batches();
	if (m_outstanding.size() >= MAX_OUTSTANDING_BATCHES)
		wait_for_outstanding();

	render_batch *const batch = m_pending.get();
	m_perf_prims += batch->prims.size();
	m_perf_batches++;
	for (int i = 0; i < m_band_count; i++)
		batch->bands[i] = render_band{ this, batch, i, m_band_count };
	m_outstanding.push_back(std::move(m_pending));
	m_pending = spare_batch();

	bool start;
	{
		std::lock_guard<std::mutex> lock(m_chain_lock);
		start = !m_chain_busy;
		if (start)
			m_chain_busy = true;
		else
			m_chain.push_back(batch);
	}
	if (start)
		launch_batch(batch);
	if (DEBUG_SYNC_RENDER)
		wait_for_outstanding();
}

// Called without m_chain_lock held: with no render threads the work queue
// runs the items, and so the next batch, on the calling thread.
void ati_rage128_device::launch_batch(render_batch *batch)
{
	batch->bands_left.store(m_band_count, std::memory_order_relaxed);
	osd_work_item_queue_multiple(m_render_queue, render_band_callback, m_band_count, &batch->bands[0], sizeof(render_band), WORK_ITEM_FLAG_AUTO_RELEASE);
}

// finished batches go back to the spare list, oldest first
void ati_rage128_device::retire_batches()
{
	while (!m_outstanding.empty() && m_outstanding.front()->done.load(std::memory_order_acquire))
	{
		m_outstanding.front()->clear();
		m_spare.push_back(std::move(m_outstanding.front()));
		m_outstanding.pop_front();
	}
}

std::unique_ptr<ati_rage128_device::render_batch> ati_rage128_device::spare_batch()
{
	if (m_spare.empty())
	{
		auto batch = std::make_unique<render_batch>();
		batch->prims.reserve(RENDER_BATCH_PRIMS + 1024);
		return batch;
	}
	std::unique_ptr<render_batch> batch = std::move(m_spare.back());
	m_spare.pop_back();
	return batch;
}

// Every submitted batch is in VRAM when this returns.
void ati_rage128_device::wait_for_outstanding()
{
	retire_batches();
	if (m_outstanding.empty())
		return;

	const osd_ticks_t start = osd_ticks();
	while (!osd_work_queue_wait(m_render_queue, osd_ticks_per_second() * 10)) { }
	retire_batches();
	m_perf_waits++;
	m_perf_wait_ticks += osd_ticks() - start;
}

// Everything queued so far is in VRAM when this returns.
void ati_rage128_device::wait_for_render()
{
	kick_render();
	wait_for_outstanding();
}

// An access by anything but the render threads to a span of VRAM waits only
// for batches that may write it (a read) or touch it at all (a write).  The
// ranges are conservative, so the result doesn't depend on host timing.
void ati_rage128_device::wait_for_vram_span(u32 start, u32 end, bool write)
{
	if (m_pending->writes.overlaps(start, end) || (write && m_pending->reads.overlaps(start, end)))
	{
		wait_for_render();
		return;
	}

	retire_batches();
	for (const auto &batch : m_outstanding)
	{
		if (batch->done.load(std::memory_order_acquire))
			continue;
		if (batch->writes.overlaps(start, end) || (write && batch->reads.overlaps(start, end)))
		{
			wait_for_outstanding();
			return;
		}
	}
}

// The VRAM a 2D operation can reach on a surface: rows top to bottom, in whole
// 16-line tiles, out to column right.  A span past the end of VRAM is all of it.
void ati_rage128_device::rows_span(u32 base, u32 pitch, int bpp, s32 top, s32 bottom, s32 right, u32 &lo, u32 &hi) const
{
	top = std::max<s32>(top, 0);
	if ((bottom < top) || (right < 0))
	{
		lo = hi = 0;
		return;
	}
	const u64 vram_size = u64(m_vram_mask) + 1;
	const u64 start = u64(base & m_vram_mask) + u64(pitch) * u64(top & ~15);
	const u64 end = u64(base & m_vram_mask) + u64(pitch) * u64((bottom + 16) & ~15) + u64(right + 1) * u64(std::max(bpp, 1));
	if (end > vram_size)
	{
		lo = 0;
		hi = u32(vram_size);
	}
	else
	{
		lo = u32(start);
		hi = u32(end);
	}
}

// Widen a batch's VRAM ranges to cover what a state can reach: the colour and
// Z buffers down to the scissor's bottom (rounded up to whole 16-line tiles)
// and every texture level the filters can select.  A range that wraps around
// the end of VRAM covers all of it.
void ati_rage128_device::note_vram_use(render_batch &batch, const raster_state &rs)
{
	const u64 vram_size = u64(m_vram_mask) + 1;
	auto add = [vram_size] (vram_ranges &ranges, u32 base, u64 bytes) {
		const u64 end = u64(base) + bytes;
		if (end > vram_size)
			ranges.add(0, u32(vram_size));
		else
			ranges.add(base, u32(end));
	};

	if ((rs.sc_bottom >= 0) && (rs.sc_right >= 0))
	{
		const u64 rows = (u64(rs.sc_bottom) + 16) & ~u64(15);
		if (rs.dst_bpp)
			add(batch.writes, rs.dst_base & m_vram_mask, u64(rs.dst_pitch) * rows + u64(rs.sc_right + 1) * rs.dst_bpp);
		if (rs.z_test || rs.stencil)
			add(batch.writes, rs.z_base & m_vram_mask, u64(rs.z_pitch) * rows + u64(rs.sc_right + 1) * rs.z_bytes);
	}

	texture_spans(rs, batch.reads);
}

// every texture level a state's filters can select, in whole 16-line tiles
void ati_rage128_device::texture_spans(const raster_state &rs, vram_ranges &ranges) const
{
	const u64 vram_size = u64(m_vram_mask) + 1;
	for (const texunit &unit : rs.tex)
	{
		if (!unit.enabled || !TEXEL_BITS[unit.fmt])
			continue;
		const bool mipmapped = !unit.mip_disable && (unit.min_filter >= 2);
		for (int slot = mipmapped ? 0 : unit.top; slot <= unit.top; slot++)
		{
			const u32 base = unit.offset[slot] & TEX_OFFSET_MASK;
			if (base >= AGP_WINDOW_BASE)
				continue;
			const int shift = unit.top - slot;
			const u64 w = u64(1) << std::max(unit.width_l2 - shift, 0);
			const u64 h = u64(1) << std::max(unit.height_l2 - shift, 0);
			const u64 pitch = std::max<u64>((w * TEXEL_BITS[unit.fmt]) >> 3, 1);
			const u64 start = base & m_vram_mask, end = start + pitch * ((h + 15) & ~u64(15));
			if (end > vram_size)
				ranges.add(0, u32(vram_size));
			else
				ranges.add(u32(start), u32(end));
		}
	}
}

// A fill goes in with the primitives, each band filling the rows of its own
// stripes, which is only safe while no other band reads what it writes: not
// as a texture, and not as part of a surface laid out differently from the
// 3D colour or Z buffer.  Otherwise it starts a new batch, after everything
// before it; primitives that later sample it start another (see queue_prim).
void ati_rage128_device::queue_fill(const fill_job &f)
{
	u32 lo, hi;
	rows_span(f.base, f.pitch, f.bpp, f.top, f.bottom, f.right, lo, hi);

	const raster_state &rs = m_rs;
	const u32 base = f.base & m_vram_mask;
	const bool colour = (base == (rs.dst_base & m_vram_mask)) && (f.pitch == rs.dst_pitch) && (f.bpp == rs.dst_bpp) && (f.tiled == rs.dst_tiled);
	const bool depth = (base == (rs.z_base & m_vram_mask)) && (f.pitch == rs.z_pitch) && (f.bpp == rs.z_bytes) && (f.tiled == rs.z_tiled);
	if (m_pending->reads.overlaps(lo, hi) || (m_pending->writes.overlaps(lo, hi) && !colour && !depth))
		kick_render();

	render_batch &batch = *m_pending;
	batch.fills.push_back(f);
	queued_prim &p = batch.prims.emplace_back();
	p.kind = PRIM_FILL;
	p.state = u32(batch.fills.size() - 1);
	batch.writes.add(lo, hi);
	batch.fill_writes.add(lo, hi);
	if (batch.prims.size() >= RENDER_BATCH_PRIMS)
		kick_render();
}

// LOG_PERF: once per emulated second, how much 3D went to the render threads
// and how long emulation was held up waiting for it
void ati_rage128_device::perf_tick()
{
	puml_flush_log();
	const u32 second = machine().time().seconds();
	if (second == m_perf_second)
		return;
	m_perf_second = second;
	if (m_perf_prims || m_perf_waits || m_perf_sync)
	{
		LOGPERF("3D: %u primitives in %u batches, %u drawn on the emulation thread (system memory textures), %u waits for the render threads totalling %.1f ms\n",
				u32(m_perf_prims), u32(m_perf_batches), u32(m_perf_sync), u32(m_perf_waits),
				double(m_perf_wait_ticks) * 1000.0 / double(osd_ticks_per_second()));
	}
	m_perf_prims = m_perf_batches = m_perf_waits = m_perf_sync = 0;
	m_perf_wait_ticks = 0;
}

// PUML_VERIFY findings from the render threads, logged on the emulation thread
void ati_rage128_device::puml_flush_log()
{
	std::vector<std::string> lines;
	{
		std::lock_guard<std::mutex> lock(m_probe_lock);
		lines.swap(m_puml_verify_log);
	}
	for (const std::string &l : lines)
		logerror("%s\n", l);
}

void *ati_rage128_device::render_band_callback(void *param, int threadid)
{
	const render_band &band = *reinterpret_cast<const render_band *>(param);
	ati_rage128_device *const device = band.device;
	render_batch *const batch = band.batch;
	if (batch->is_scale)
	{
		const scale_job &j = batch->scale;
		device->scale_rows(j, j.h * band.index / band.count, j.h * (band.index + 1) / band.count);
	}
	else if (batch->is_host)
	{
		const host_job &j = batch->host;
		device->host_rows(j, j.h * band.index / band.count, j.h * (band.index + 1) / band.count);
	}
	else
	{
		device->render_batch_band(*batch, band.index, band.count);
	}

	// The last band to finish starts the next batch.  Once done is set the
	// emulation thread may reuse the batch, so nothing here touches it after.
	if (batch->bands_left.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		render_batch *next = nullptr;
		{
			std::lock_guard<std::mutex> lock(device->m_chain_lock);
			batch->done.store(true, std::memory_order_release);
			if (device->m_chain.empty())
			{
				device->m_chain_busy = false;
			}
			else
			{
				next = device->m_chain.front();
				device->m_chain.pop_front();
			}
		}
		if (next)
			device->launch_batch(next);
	}
	return nullptr;
}

void ati_rage128_device::render_batch_band(const render_batch &batch, int band, int bands)
{
	for (const queued_prim &p : batch.prims)
	{
		switch (p.kind)
		{
		case PRIM_TRIANGLE: draw_triangle(batch.states[p.state], p.v[0], p.v[1], p.v[2], band, bands, batch.blocks[p.state]); break;
		case PRIM_LINE: draw_line3d(batch.states[p.state], p.v[0], p.v[1], band, bands); break;
		case PRIM_FILL: do_fill(batch.fills[p.state], band, bands); break;
		default: draw_point(batch.states[p.state], p.v[0], band, bands); break;
		}
	}
}

void ati_rage128_device::setup_texunit(texunit &unit, int which)
{
	const u32 size = m_regs[TEX_SIZE_PITCH_C / 4] >> (which ? 16 : 0);
	const u32 cntl = m_regs[(which ? SEC_TEX_CNTL_C : PRIM_TEX_CNTL_C) / 4];
	const u32 first = (which ? SEC_TEX_0_OFFSET_C : PRIM_TEX_0_OFFSET_C) / 4;

	unit.enabled = m_regs[TEX_CNTL_C / 4] & (which ? TEX_SEC_TEXMAP_ENABLE : TEX_TEXMAP_ENABLE);
	unit.cntl = cntl;
	unit.combine = m_regs[(which ? SEC_TEX_COMBINE_CNTL_C : PRIM_TEXTURE_COMBINE_CNTL_C) / 4];
	unit.sec_st = which && BIT(cntl, 0);
	unit.min_filter = BIT(cntl, 1, 3);
	// MAG_BLEND (bits 6:4): odd values are linear; the secondary unit also takes 2.
	// Unreal under Mac OS 9 writes 5.  After 86Box.
	unit.linear_mag = BIT(cntl, 4) || (which && (BIT(cntl, 4, 3) == 2));
	unit.mip_disable = BIT(cntl, 7);
	unit.s_mode = BIT(cntl, 8, 2);
	unit.t_mode = BIT(cntl, 11, 2);
	unit.fmt = BIT(cntl, 16, 4);
	unit.pal_fmt = BIT(cntl, 24, 2);
	unit.width_l2 = BIT(size, 0, 4);
	unit.height_l2 = BIT(size, 8, 4);
	unit.top = std::clamp<int>(int(BIT(size, 4, 4)) - int(BIT(size, 12, 4)), 0, texunit::LEVELS - 1);
	for (int i = 0; i < texunit::LEVELS; i++)
		unit.offset[i] = m_regs[first + i];
	unit.border = m_regs[(which ? SEC_TEXTURE_BORDER_COLOR_C : PRIM_TEXTURE_BORDER_COLOR_C) / 4];

	// LOD is only worth computing when it changes the result
	const bool mipmapped = !unit.mip_disable && (unit.min_filter >= 2);
	unit.need_lod = mipmapped || (BIT(unit.min_filter, 0) != unit.linear_mag);

	if (unit.enabled && !TEXEL_BITS[unit.fmt])
		LOGUNIMPL("texture unit %d: datatype %d not supported\n", which, unit.fmt);
	if (unit.enabled && (unit.offset[unit.top] >> 30) && !m_tile_warned)
	{
		m_tile_warned = true;
		LOGTILE("tiled texture: unit %d offset %08x\n", which, unit.offset[unit.top]);
	}
}

void ati_rage128_device::setup_raster_state()
{
	// cleared first so that queued states can be compared byte for byte
	raster_state &rs = m_rs;
	std::memset(&rs, 0, sizeof(rs));
	const u32 texcntl = m_regs[TEX_CNTL_C / 4];
	const u32 misc = m_regs[MISC_3D_STATE_CNTL_REG / 4];
	const u32 gmc = m_regs[DP_GUI_MASTER_CNTL_C / 4];

	rs.tex_cntl = texcntl;
	rs.misc = misc;

	setup_texunit(rs.tex[0], 0);
	setup_texunit(rs.tex[1], 1);
	rs.textured = rs.tex[0].enabled || rs.tex[1].enabled;
	rs.tex_amask = (m_regs[SCALE_3D_CNTL / 4] & SCALE_TEX_MAP_ALPHA_IN_TEXTURE) ? 0 : 0xff00'0000;

	// Shading for CCE primitives comes from PM4_VC_FPU_SETUP COLOR_FCN (bits 6:5:
	// solid, flat, Gouraud, Gouraud), with SETUP_CNTL COLOR_FCN (bits 5:3) able to
	// pick a flat vertex; solid shading uses CONSTANT_COLOR_C, and flat shading
	// the first vertex, or the third with FLAT_SHADE_VERTEX (bit 14) set.  Mac OS 9
	// writes SETUP_CNTL = 0, which taken alone would mean solid SOLID_COLOR.
	// After 86Box.
	const u32 setup = m_regs[SETUP_CNTL / 4];
	const u32 fpu = m_regs[PM4_VC_FPU_SETUP / 4];
	const int setup_color = BIT(setup, 3, 3);
	const int fpu_color = BIT(fpu, 5, 2);
	if (fpu_color == 0)
		rs.shade = SHADE_SOLID;
	else if ((fpu_color == 1) || ((setup_color >= 1) && (setup_color <= 3)))
		rs.shade = SHADE_FLAT;
	else
		rs.shade = SHADE_GOURAUD;
	if (fpu_color == 1)
		rs.flat_vertex = BIT(fpu, 14) ? 2 : 0;
	else
		rs.flat_vertex = (setup_color == 2) ? 1 : (setup_color == 3) ? 2 : 0;
	rs.solid_color = m_regs[CONSTANT_COLOR_C / 4];
	rs.fpu_setup = fpu;

	// Perspective correction divides the interpolated S and T by the interpolated
	// RHW unless PRIM_TEX_CNTL_C PERSPECTIVE_DIS (bit 14) is set.  The setup engine
	// first multiplies the vertex S and T by RHW, unless SETUP_CNTL TEXTURE_ST_DIRECT
	// (bit 9) says the driver already has; Mac OS 9 sets it.  After 86Box.
	rs.perspective = !BIT(rs.tex[0].cntl, 14);
	rs.premultiply = !BIT(setup, 9);

	// destination
	const u32 dpo = m_regs[DST_PITCH_OFFSET_C / 4];
	rs.dst_type = BIT(gmc, 8, 4);
	rs.dst_bpp = DTYPE_BYTES[rs.dst_type];
	rs.dst_base = (dpo & 0x1f'ffff) << 5;
	rs.dst_pitch = ((dpo >> 21) & 0x3ff) * 8 * ((rs.dst_bpp == 3) ? 1 : rs.dst_bpp);
	rs.dst_tiled = BIT(dpo, 31);
	if ((rs.dst_tiled != m_dst3d_tile_logged) || (m_regs[Z_PITCH_C / 4] != m_zpitch_logged))
	{
		m_dst3d_tile_logged = rs.dst_tiled;
		m_zpitch_logged = m_regs[Z_PITCH_C / 4];
		LOGTILE("3D destination %s: DST_PITCH_OFFSET_C %08x, Z_OFFSET_C %08x, Z_PITCH_C %08x (%u pixels%s)\n",
				rs.dst_tiled ? "tiled" : "linear", dpo, m_regs[Z_OFFSET_C / 4], m_regs[Z_PITCH_C / 4],
				(m_regs[Z_PITCH_C / 4] & 0xfffe) * 8, BIT(m_regs[Z_PITCH_C / 4], 16) ? ", tiled" : "");
	}
	rs.write_mask = m_regs[PLANE_3D_MASK_C / 4] & m_regs[DP_WRITE_MASK / 4];

	const u32 tl = m_regs[SC_TOP_LEFT_C / 4], br = m_regs[SC_BOTTOM_RIGHT_C / 4];
	rs.sc_left = std::max<s32>(lo16s(tl), 0);
	rs.sc_top = std::max<s32>(hi16s(tl), 0);
	rs.sc_right = std::min<s32>(lo16s(br), 4095);
	rs.sc_bottom = std::min<s32>(hi16s(br), 4095);

	// auxiliary scissors (used by the DRM for cliprects)
	rs.aux_cntl = m_regs[AUX_SC_CNTL / 4] & 0x3f;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			rs.aux[i][j] = s16(m_regs[(AUX1_SC_LEFT + i * 0x10) / 4 + j]);

	// Polygon stipple: a 32x32 "leave alone" mono brush in the 3D context's GUI
	// master control, as Mesa's r128 driver selects it.  Mac OS 9 rewrites the
	// BRUSH_Y_X origin (X in bits 4:0, Y in bits 20:16) for every primitive and
	// loads the rows bottom first, as Mesa does.
	rs.stipple = BIT(gmc, 4, 4) == BRUSH_32X32_MONO_FG_LA;
	rs.stipple_x = m_regs[BRUSH_Y_X / 4] & 0x1f;
	rs.stipple_y = (m_regs[BRUSH_Y_X / 4] >> 16) & 0x1f;
	std::copy_n(&m_regs[BRUSH_DATA0 / 4], 32, rs.stipple_rows);

	// depth and stencil; stencil is the top byte of 24-bit Z
	const u32 zcntl = m_regs[Z_STEN_CNTL_C / 4];
	rs.z_test = texcntl & TEX_Z_ENABLE;
	rs.z_write = rs.z_test && (texcntl & TEX_Z_WRITE_ENABLE);
	rs.z_func = BIT(zcntl, 4, 3);
	// Z_STEN_CNTL_C Z_PIX_WIDTH (bits 2:1): 16-bit Z, 24-bit Z with 8-bit stencil
	// above it, or 32 bits holding 24-bit Z in the upper three bytes.  Z_PITCH_C
	// is in units of 8 pixels with bit 0 ignored, and bit 16 tiles the Z buffer.
	// After 86Box.
	const u32 zpitch = m_regs[Z_PITCH_C / 4];
	rs.z_width = BIT(zcntl, 1, 2);
	rs.z_bytes = (rs.z_width == 0) ? 2 : 4;
	rs.z_max = (rs.z_width == 0) ? 0xffff : 0xff'ffff;
	rs.z_shift = (rs.z_width == 2) ? 8 : 0;
	rs.z_base = m_regs[Z_OFFSET_C / 4] & TEX_OFFSET_MASK;
	rs.z_pitch = (zpitch & 0xfffe) * 8 * rs.z_bytes;
	rs.z_tiled = BIT(zpitch, 16);
	rs.stencil = (texcntl & TEX_STENCIL_ENABLE) && (rs.z_width == 1);

	// STEN_REF_MASK_C: reference, compare mask in bits 23:16, write mask in bits
	// 31:24; operations for stencil fail, Z pass and Z fail at bits 16, 20 and 24
	// of Z_STEN_CNTL_C, as on Radeon
	const u32 srm = m_regs[STEN_REF_MASK_C / 4];
	rs.sten_ref = srm & 0xff;
	rs.sten_mask = (srm >> 16) & 0xff;
	rs.sten_wmask = srm >> 24;
	rs.sten_func = BIT(zcntl, 12, 3);
	rs.sten_fail_op = BIT(zcntl, 16, 3);
	rs.sten_zpass_op = BIT(zcntl, 20, 3);
	rs.sten_zfail_op = BIT(zcntl, 24, 3);

	unpack_argb(m_regs[CONSTANT_COLOR_C / 4], rs.const_r, rs.const_g, rs.const_b, rs.const_a);
	rs.specular = texcntl & TEX_SPEC_LIGHT_ENABLE;
	rs.fog = texcntl & TEX_FOG_ENABLE;
	float fog_a;
	unpack_argb(m_regs[FOG_COLOR_C / 4], rs.fog_r, rs.fog_g, rs.fog_b, fog_a);

	rs.alpha_test = texcntl & TEX_ALPHA_TEST_ENABLE;
	rs.alpha_func = BIT(misc, 24, 3);
	rs.alpha_ref = misc & 0xff;

	// blending ONE/ZERO is a plain write (Unreal draws most of its world that way);
	// the destination's INVSRCALPHA-style 11 is taken as 12
	rs.blend_src = BIT(misc, 16, 4);
	rs.blend_dst = BIT(misc, 20, 4);
	if (rs.blend_dst == 11)
		rs.blend_dst = 12;
	// BOTHSRCALPHA and BOTHINVSRCALPHA set the destination factor as well
	if (rs.blend_src == 11)
		rs.blend_dst = 5;
	else if (rs.blend_src == 12)
		rs.blend_dst = 4;
	rs.blend_subtract = BIT(misc, 13);
	rs.blend = (texcntl & TEX_ALPHA_ENABLE) && !((rs.blend_src == 1) && (rs.blend_dst == 0) && !rs.blend_subtract);

	// Palettised textures take a copy of the palette.  Levels in system memory can
	// only be read from the emulation thread.
	rs.sync = false;
	for (const texunit &tu : rs.tex)
	{
		if (!tu.enabled)
			continue;
		if ((tu.fmt == 1) || (tu.fmt == 2) || (tu.fmt == 10))
			std::copy(std::begin(m_tex_palette), std::end(m_tex_palette), std::begin(rs.palette));
		// Only the levels the filters can select are read.  The others hold
		// whatever the driver left there: Unreal under Mac OS 9 leaves garbage
		// such as 4eba0098 in PRIM_TEX_0_OFFSET_C, which looks like system memory.
		const bool mipmapped = !tu.mip_disable && (tu.min_filter >= 2);
		for (int slot = mipmapped ? 0 : tu.top; slot <= tu.top; slot++)
		{
			const int shift = tu.top - slot;
			const u32 texels = u32(1) << (std::max(tu.width_l2 - shift, 0) + std::max(tu.height_l2 - shift, 0));
			if (((tu.offset[slot] & TEX_OFFSET_MASK) + ((texels * TEXEL_BITS[tu.fmt]) >> 3)) > AGP_WINDOW_BASE)
				rs.sync = true;
		}
	}

	rs.chroma_key = (texcntl & TEX_CHROMA_KEY_ENABLE) && rs.tex[0].enabled;

	// Alpha masking: Mac OS 9 cuts out Nanosaur's leaves and Unreal's masked
	// textures (1555, with blending and the alpha test off for Nanosaur) by
	// setting TEX_CNTL_C bit 13.  Like the chroma key it tests the nearest
	// texel of the first unit.
	rs.alpha_mask = (texcntl & TEX_AMASK_ENABLE) && rs.tex[0].enabled;
	rs.chroma_msk = m_regs[TEXTURE_CLR_CMP_MSK_C / 4];
	rs.chroma_clr = m_regs[TEXTURE_CLR_CMP_CLR_C / 4] & rs.chroma_msk;

	// The 3D colour key, MISC_3D_STATE_CNTL_REG bits 31:30: 0 keys nothing, 1
	// everything, 2 texels that differ from CLR_CMP_CLR_3D under CLR_CMP_MSK_3D,
	// 3 texels that match it.  Like the chroma key, it looks at the nearest
	// texel of the first unit, as ARGB8888.
	rs.clr_cmp_fn = rs.tex[0].enabled ? BIT(misc, 30, 2) : 0;
	rs.clr_cmp_msk = m_regs[CLR_CMP_MSK_3D / 4];
	rs.clr_cmp_clr = m_regs[CLR_CMP_CLR_3D / 4] & rs.clr_cmp_msk;
}

u8 ati_rage128_device::card_read8(u32 addr)
{
	return u8(card_read32(addr & ~3U) >> ((addr & 3) * 8));
}

// One texel of a surface in VRAM or GART memory, converted to ARGB8888.
// pal_fmt selects the 16-bit palette entry format for the indexed formats.
u32 ati_rage128_device::read_texel(u32 base, u32 pitch, int x, int y, int fmt, int pal_fmt, bool tiled, const u32 *palette)
{
	const int bits = TEXEL_BITS[fmt & 0xf];
	if (!bits)
		return 0xffff'ffff;

	// bitofs only selects the nibble for 4-bit texels
	const u32 bitofs = u32(x) * bits;
	const u32 addr = (base & TEX_OFFSET_MASK) + surface_offset(bitofs >> 3, u32(y), (pitch * bits) >> 3, tiled);
	u32 raw;
	if (addr < AGP_WINDOW_BASE)
	{
		switch (bits)
		{
		case 4: raw = (m_vram[addr & m_vram_mask] >> (BIT(bitofs, 2) ? 4 : 0)) & 0xf; break;
		case 8: raw = m_vram[addr & m_vram_mask]; break;
		case 16: raw = get_u16le(&m_vram[addr & m_vram_mask & ~1]); break;
		case 24:
			raw = m_vram[addr & m_vram_mask] | (u32(m_vram[(addr + 1) & m_vram_mask]) << 8) | (u32(m_vram[(addr + 2) & m_vram_mask]) << 16);
			break;
		default: raw = get_u32le(&m_vram[addr & m_vram_mask & ~3]); break;
		}
	}
	else
	{
		// texture in AGP / PCI GART memory
		switch (bits)
		{
		case 4: raw = (card_read8(addr) >> (BIT(bitofs, 2) ? 4 : 0)) & 0xf; break;
		case 8: raw = card_read8(addr); break;
		case 16: raw = (card_read32(addr & ~3U) >> ((addr & 2) * 8)) & 0xffff; break;
		case 24: raw = card_read8(addr) | (u32(card_read8(addr + 1)) << 8) | (u32(card_read8(addr + 2)) << 16); break;
		default: raw = card_read32(addr & ~3U); break;
		}
	}

	return decode_texel(fmt, pal_fmt, raw, palette ? palette : m_tex_palette);
}

// a texel's raw bits converted to ARGB8888
u32 ati_rage128_device::decode_texel(int fmt, int pal_fmt, u32 raw, const u32 *palette) const
{
	auto from_palette = [palette, pal_fmt] (u32 index) -> u32 {
		const u32 e = palette[index & 0xff] & 0xffff;
		switch (pal_fmt)
		{
		case 1: return (BIT(e, 15) ? 0xff00'0000 : 0) | (u32(pal5bit(e >> 10)) << 16) | (u32(pal5bit(e >> 5)) << 8) | pal5bit(e);
		case 2: return (u32(pal4bit(e >> 12)) << 24) | (u32(pal4bit(e >> 8)) << 16) | (u32(pal4bit(e >> 4)) << 8) | pal4bit(e);
		default: return 0xff00'0000 | (u32(pal5bit(e >> 11)) << 16) | (u32(pal6bit(e >> 5)) << 8) | pal5bit(e);
		}
	};

	switch (fmt & 0xf)
	{
	case 1: case 2: case 10:
		return from_palette(raw);
	case 3:
		return (BIT(raw, 15) ? 0xff00'0000 : 0) | (u32(pal5bit(raw >> 10)) << 16) | (u32(pal5bit(raw >> 5)) << 8) | pal5bit(raw);
	case 4:
		return 0xff00'0000 | (u32(pal5bit(raw >> 11)) << 16) | (u32(pal6bit(raw >> 5)) << 8) | pal5bit(raw);
	case 5:
		return 0xff00'0000 | raw;
	case 6:
		return raw;
	case 7:
		return 0xff00'0000 | (u32(pal3bit(raw >> 5)) << 16) | (u32(pal3bit(raw >> 2)) << 8) | pal2bit(raw);
	case 8: case 9:
		return 0xff00'0000 | (raw << 16) | (raw << 8) | raw;
	case 11: case 12:
	{
		// 4:2:2, use luma of this texel and nominal chroma
		const u32 yv = (fmt == 11) ? (raw & 0xff) : (raw >> 8);
		return 0xff00'0000 | (yv << 16) | (yv << 8) | yv;
	}
	case 13:
		// alpha in the high byte, intensity in the low byte
		return ((raw & 0xff00) << 16) | ((raw & 0xff) * 0x01'0101);
	case 14:
		return raw;
	case 15:
		return (u32(pal4bit(raw >> 12)) << 24) | (u32(pal4bit(raw >> 8)) << 16) | (u32(pal4bit(raw >> 4)) << 8) | pal4bit(raw);
	default:
		return 0xffff'ffff;
	}
}

// Sample one level with normalised coordinates.  When nearest is given it also
// gets the texel under the sample point, for the chroma key.
u32 ati_rage128_device::sample_level(const texunit &unit, const u32 *palette, int slot, float s, float t, bool linear, u32 *nearest)
{
	const int shift = unit.top - slot;
	const int w_l2 = std::max(unit.width_l2 - shift, 0);
	const int h_l2 = std::max(unit.height_l2 - shift, 0);
	const int w = 1 << w_l2, h = 1 << h_l2;
	const float u = tex_coord(s * float(w));
	const float v = tex_coord(t * float(h));

	// Each level's pitch is its width; a coordinate wrapped to -1 is the border
	// colour.  16-bit levels wholly in VRAM, the usual case, are read directly.
	const u32 base = unit.offset[slot];
	const bool tiled = base >> 30;
	const u32 vbase = base & TEX_OFFSET_MASK;
	const u32 pitch16 = u32(w) * 2;
	const bool direct = (TEXEL_BITS[unit.fmt] == 16) && ((vbase + pitch16 * u32(h)) <= AGP_WINDOW_BASE);
	const bool direct_tiled = tiled && tile_pitch_ok(pitch16);
	auto texel = [this, &unit, palette, base, vbase, w, pitch16, tiled, direct, direct_tiled] (int x, int y) -> u32 {
		if ((x | y) < 0)
			return unit.border;
		if (direct)
		{
			const u32 off = direct_tiled ? tile_offset(u32(x) * 2, u32(y), pitch16) : (u32(y) * pitch16 + u32(x) * 2);
			const u32 raw = get_u16le(&m_vram[(vbase + off) & m_vram_mask & ~1U]);
			// the common 16-bit formats inline, the rest through decode_texel()
			switch (unit.fmt)
			{
			case 3:
				return (BIT(raw, 15) ? 0xff00'0000 : 0) | (u32(pal5bit(raw >> 10)) << 16) | (u32(pal5bit(raw >> 5)) << 8) | pal5bit(raw);
			case 4:
				return 0xff00'0000 | (u32(pal5bit(raw >> 11)) << 16) | (u32(pal6bit(raw >> 5)) << 8) | pal5bit(raw);
			case 15:
				return (u32(pal4bit(raw >> 12)) << 24) | (u32(pal4bit(raw >> 8)) << 16) | (u32(pal4bit(raw >> 4)) << 8) | pal4bit(raw);
			default:
				return decode_texel(unit.fmt, unit.pal_fmt, raw, palette);
			}
		}
		return read_texel(base, w, x, y, unit.fmt, unit.pal_fmt, tiled, palette);
	};

	if (!linear || nearest)
	{
		const u32 c = texel(tex_wrap(int(std::floor(u)), w, unit.s_mode), tex_wrap(int(std::floor(v)), h, unit.t_mode));
		if (nearest)
			*nearest = c;
		if (!linear)
			return c;
	}

	const float fu = u - 0.5f, fv = v - 0.5f;
	const float x0f = std::floor(fu), y0f = std::floor(fv);
	const int x0 = int(x0f), y0 = int(y0f);
	const int xa = tex_wrap(x0, w, unit.s_mode), xb = tex_wrap(x0 + 1, w, unit.s_mode);
	const int ya = tex_wrap(y0, h, unit.t_mode), yb = tex_wrap(y0 + 1, h, unit.t_mode);
	return rgbaint_t::bilinear_filter(texel(xa, ya), texel(xb, ya), texel(xa, yb), texel(xb, yb), frac8(fu - x0f), frac8(fv - y0f));
}

// MIN_BLEND: 0 nearest, 1 linear, 2 nearest on the nearest level, 3 linear on the
// nearest level, 4 nearest blended between levels, 5 trilinear.  lod is log2 of the
// texel footprint on the largest level.
u32 ati_rage128_device::sample_texture(const texunit &unit, const u32 *palette, float s, float t, float lod, u32 *nearest)
{
	const int minf = unit.min_filter;
	if (unit.mip_disable || (minf < 2) || !(lod > 0.0f))
	{
		const bool linear = (lod > 0.0f) ? BIT(minf, 0) : unit.linear_mag;
		return sample_level(unit, palette, unit.top, s, t, linear, nearest);
	}

	const float level = std::min(lod, float(unit.top));
	const bool linear = BIT(minf, 0);
	if (minf >= 4)
	{
		const int l0 = int(level);
		const float f = level - float(l0);
		const u32 c0 = sample_level(unit, palette, unit.top - l0, s, t, linear, nearest);
		if ((l0 >= unit.top) || (f <= 0.0f))
			return c0;
		const u32 c1 = sample_level(unit, palette, unit.top - l0 - 1, s, t, linear, nullptr);
		return rgbaint_t::bilinear_filter(c0, c1, c0, c1, frac8(f), 0);
	}
	return sample_level(unit, palette, unit.top - int(level + 0.5f), s, t, linear, nearest);
}

// texture combiner; all values 0..1.  i* is the interpolated (diffuse) colour
// for the primary stage and the previous stage's output for the secondary one.
// Function 0 ("disable") outputs the texture colour, or with COMB_FCN_MSB (bit 8)
// the colour factor minus the input clamped at 0; its alpha is the texture alpha
// in the first stage and the incoming alpha in the second.  After 86Box.
void ati_rage128_device::combine(u32 cntl, bool first, float tr, float tg, float tb, float ta, float ir, float ig, float ib, float ia,
		float cr, float cg, float cb, float ca, float &or_, float &og, float &ob, float &oa)
{
	const int comb = cntl & 0xf;
	const int cfac = BIT(cntl, 4, 4);
	const int ifac = BIT(cntl, 10, 4);
	const int acomb = BIT(cntl, 14, 4);
	const int afac = BIT(cntl, 18, 4);
	const int iafac = BIT(cntl, 25, 3);

	float fr, fg, fb;
	switch (cfac)
	{
	case 0: fr = cr; fg = cg; fb = cb; break;
	case 1: fr = 1 - cr; fg = 1 - cg; fb = 1 - cb; break;
	case 5: fr = 1 - tr; fg = 1 - tg; fb = 1 - tb; break;
	case 6: fr = fg = fb = ta; break;
	case 7: fr = fg = fb = 1 - ta; break;
	case 8: fr = ir; fg = ig; fb = ib; break;
	default: fr = tr; fg = tg; fb = tb; break;     // 4: texture
	}

	float xr, xg, xb;
	switch (ifac)
	{
	case 2: xr = cr; xg = cg; xb = cb; break;
	case 3: xr = xg = xb = ca; break;
	case 5: case 9: xr = xg = xb = ia; break;
	default: xr = ir; xg = ig; xb = ib; break;    // 4: interpolated, 8: previous
	}

	auto op = [] (int mode, float f, float x, float blend_a, float tex_a, float c) -> float {
		switch (mode)
		{
		case 0: return x;
		case 1: return f;
		case 2: return x;
		case 3: return f * x;
		case 4: return clamp01(2 * f * x);
		case 5: return clamp01(4 * f * x);
		case 6: return clamp01(f + x);
		case 7: return clamp01(f + x - 0.5f);
		case 8: case 9: case 10: case 12: return f * blend_a + x * (1 - blend_a);
		case 11: return clamp01(f + x * (1 - tex_a));
		case 13: return clamp01(f * (1 - tex_a) + x);
		case 14: return clamp01(2 * (f + x - 0.5f));
		case 15: return f * c + x * (1 - c);
		default: return x;
		}
	};

	float blend_a;
	switch (comb)
	{
	case 8: blend_a = ia; break;
	case 9: blend_a = ta; break;
	case 10: blend_a = ca; break;
	default: blend_a = ia; break;
	}

	if (comb == 0)
	{
		if (BIT(cntl, 8))
		{
			or_ = std::max(fr - xr, 0.0f);
			og = std::max(fg - xg, 0.0f);
			ob = std::max(fb - xb, 0.0f);
		}
		else
		{
			or_ = tr;
			og = tg;
			ob = tb;
		}
	}
	else
	{
		or_ = op(comb, fr, xr, blend_a, ta, cr);
		og = op(comb, fg, xg, blend_a, ta, cg);
		ob = op(comb, fb, xb, blend_a, ta, cb);
	}

	const float fa = (afac == 7) ? (1 - ta) : ta;
	float xa;
	switch (iafac)
	{
	case 1: xa = ca; break;
	default: xa = ia; break;      // 2: interpolated, 4: previous
	}
	oa = (acomb == 0) ? (first ? ta : ia) : op(acomb, fa, xa, blend_a, ta, ca);
}

float ati_rage128_device::blend_factor(int mode, bool alpha, float sr, float sg, float sb, float sa, float dr, float dg, float db, float da, int chan)
{
	const float s[4] = { sr, sg, sb, sa };
	const float d[4] = { dr, dg, db, da };
	switch (mode)
	{
	case 0: return 0.0f;
	case 1: return 1.0f;
	case 2: return s[chan];
	case 3: return 1.0f - s[chan];
	case 4: return sa;
	case 5: return 1.0f - sa;
	case 6: return da;
	case 7: return 1.0f - da;
	case 8: return d[chan];
	case 9: return 1.0f - d[chan];
	case 10: return alpha ? 1.0f : std::min(sa, 1.0f - da);
	case 11: return sa;
	case 12: return 1.0f - sa;
	default: return 1.0f;
	}
}

// All four channels of a 3D blend function at once (s and d are RGBA, 0..1).  The
// destination's 11 behaves as 12, as in the per-channel version's callers.
void ati_rage128_device::blend_factors(int mode, const float *s, const float *d, float *f)
{
	switch (mode)
	{
	case 0: f[0] = f[1] = f[2] = f[3] = 0.0f; break;
	case 2: for (int i = 0; i < 4; i++) f[i] = s[i]; break;
	case 3: for (int i = 0; i < 4; i++) f[i] = 1.0f - s[i]; break;
	case 4: case 11: f[0] = f[1] = f[2] = f[3] = s[3]; break;
	case 5: case 12: f[0] = f[1] = f[2] = f[3] = 1.0f - s[3]; break;
	case 6: f[0] = f[1] = f[2] = f[3] = d[3]; break;
	case 7: f[0] = f[1] = f[2] = f[3] = 1.0f - d[3]; break;
	case 8: for (int i = 0; i < 4; i++) f[i] = d[i]; break;
	case 9: for (int i = 0; i < 4; i++) f[i] = 1.0f - d[i]; break;
	case 10: f[0] = f[1] = f[2] = std::min(s[3], 1.0f - d[3]); f[3] = 1.0f; break;
	default: f[0] = f[1] = f[2] = f[3] = 1.0f; break;
	}
}

u32 ati_rage128_device::pack_color(int dtype, float r, float g, float b, float a)
{
	const u8 r8 = clamp8(r), g8 = clamp8(g), b8 = clamp8(b), a8 = clamp8(a);
	switch (dtype)
	{
	case 2: case 9: return r8;
	case 3: return (a8 >= 128 ? 0x8000 : 0) | ((r8 >> 3) << 10) | ((g8 >> 3) << 5) | (b8 >> 3);
	case 4: return ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3);
	case 5: return (u32(r8) << 16) | (u32(g8) << 8) | b8;
	case 6: return (u32(a8) << 24) | (u32(r8) << 16) | (u32(g8) << 8) | b8;
	case 7: return (r8 & 0xe0) | ((g8 >> 3) & 0x1c) | (b8 >> 6);
	case 15: return ((a8 >> 4) << 12) | ((r8 >> 4) << 8) | ((g8 >> 4) << 4) | (b8 >> 4);
	default: return (u32(a8) << 24) | (u32(r8) << 16) | (u32(g8) << 8) | b8;
	}
}

void ati_rage128_device::unpack_color(int dtype, u32 p, float &r, float &g, float &b, float &a)
{
	switch (dtype)
	{
	case 2: case 9: r = g = b = float(p & 0xff); a = 255; break;
	case 3: a = BIT(p, 15) ? 255.0f : 0.0f; r = pal5bit(p >> 10); g = pal5bit(p >> 5); b = pal5bit(p); break;
	case 4: a = 255; r = pal5bit(p >> 11); g = pal6bit(p >> 5); b = pal5bit(p); break;
	case 5: a = 255; r = (p >> 16) & 0xff; g = (p >> 8) & 0xff; b = p & 0xff; break;
	case 7: a = 255; r = pal3bit(p >> 5); g = pal3bit(p >> 2); b = pal2bit(p); break;
	case 15: a = pal4bit(p >> 12); r = pal4bit(p >> 8); g = pal4bit(p >> 4); b = pal4bit(p); break;
	default: a = (p >> 24) & 0xff; r = (p >> 16) & 0xff; g = (p >> 8) & 0xff; b = p & 0xff; break;
	}
}

// One pixel through the 3D pipeline.  lod[n] is texture unit n's level of detail.
void ati_rage128_device::shade_pixel(const raster_state &rs, int x, int y, const vertex3d &p, const float *lod, bool polygon)
{
	// 3D scissor (also clamped to the 0-4095 coordinate range)
	if ((x < rs.sc_left) || (y < rs.sc_top) || (x > rs.sc_right) || (y > rs.sc_bottom))
		return;

	const bool probe = (VERBOSE & LOG_PROBE) && (x == PROBE_X) && (y == PROBE_Y);
	u32 probe_texel = 0;
	auto record = [&] (char why, u32 z, u32 zstored, u32 old, u32 res) {
		if (!probe)
			return;
		std::lock_guard<std::mutex> lock(m_probe_lock);
		m_probe.push_back(probe_event{ why, rs.misc, rs.tex_cntl, rs.tex[0].offset[rs.tex[0].top], p.s0, p.t0, probe_texel, old, res, z, zstored });
	};

	if (polygon && rs.stipple)
	{
		const int sx = (x + rs.stipple_x) & 31, sy = (y + rs.stipple_y) & 31;
		if (!BIT(rs.stipple_rows[sy], (sx & ~7) | (~sx & 7)))
			return;
	}

	if (rs.aux_cntl)
	{
		bool any_or = false, or_hit = false;
		for (int i = 0; i < 3; i++)
		{
			if (!BIT(rs.aux_cntl, i * 2))
				continue;
			const bool inside = (x >= rs.aux[i][0]) && (x <= rs.aux[i][1]) && (y >= rs.aux[i][2]) && (y <= rs.aux[i][3]);
			if (BIT(rs.aux_cntl, i * 2 + 1))
			{
				if (inside)
					return;
			}
			else
			{
				any_or = true;
				or_hit |= inside;
			}
		}
		if (any_or && !or_hit)
			return;
	}

	// depth and stencil buffer
	u32 zaddr = 0, zval = 0, zword = 0, zstored = 0;
	bool zpass = true;
	if (rs.z_test || rs.stencil)
	{
		zaddr = (rs.z_base + surface_offset(u32(x) * rs.z_bytes, u32(y), rs.z_pitch, rs.z_tiled)) & m_vram_mask;
		zval = u32(double(clamp01(p.z)) * double(rs.z_max));
		zword = (rs.z_bytes == 2) ? get_u16le(&m_vram[zaddr & ~1]) : get_u32le(&m_vram[zaddr & ~3]);
		zstored = (zword >> rs.z_shift) & rs.z_max;
		zpass = !rs.z_test || depth_test(rs.z_func, zval, zstored);

		// Without stencil a failed depth test changes nothing, so reject early.  With
		// stencil the fail operations apply only to pixels that survive alpha test.
		if (!rs.stencil && !zpass)
		{
			record('z', zval, zstored, 0, 0);
			return;
		}
	}

	// colour
	float r = p.r * (1.0f / 255.0f), g = p.g * (1.0f / 255.0f), b = p.b * (1.0f / 255.0f), a = p.a * (1.0f / 255.0f);

	if (rs.textured)
	{
		const float dr = r, dg = g, db = b, da = a;
		for (int unit = 0; unit < 2; unit++)
		{
			const texunit &tu = rs.tex[unit];
			if (!tu.enabled)
				continue;

			// the chroma key compares the nearest texel before the alpha override
			u32 nearest = 0;
			const bool key = (rs.chroma_key || rs.clr_cmp_fn || rs.alpha_mask) && !unit;
			u32 texel = sample_texture(tu, rs.palette, tu.sec_st ? p.s1 : p.s0, tu.sec_st ? p.t1 : p.t0, lod[unit], key ? &nearest : nullptr);
			if (key)
			{
				if (rs.alpha_mask && !(nearest >> 24))
				{
					record('k', zval, zstored, nearest, 0);
					return;
				}
				if (rs.chroma_key && ((nearest & rs.chroma_msk) == rs.chroma_clr))
				{
					record('k', zval, zstored, nearest, 1);
					return;
				}
				if (rs.clr_cmp_fn)
				{
					const bool match = (nearest & rs.clr_cmp_msk) == rs.clr_cmp_clr;
					if ((rs.clr_cmp_fn == 1) || (match == (rs.clr_cmp_fn == 3)))
						return;
				}
			}
			texel |= rs.tex_amask;
			if (!unit)
				probe_texel = texel;

			float tr, tg, tb, ta;
			unpack_argb(texel, tr, tg, tb, ta);
			const float ir = unit ? r : dr, ig = unit ? g : dg, ib = unit ? b : db, ia = unit ? a : da;
			combine(tu.combine, !unit, tr, tg, tb, ta, ir, ig, ib, ia, rs.const_r, rs.const_g, rs.const_b, rs.const_a, r, g, b, a);
		}
	}

	if (rs.specular)
	{
		r = clamp01(r + p.sr * (1.0f / 255.0f));
		g = clamp01(g + p.sg * (1.0f / 255.0f));
		b = clamp01(b + p.sb * (1.0f / 255.0f));
	}

	// Vertex fog: the factor arrives in the specular alpha (SPEC_F), 1.0 meaning
	// no fog.  Mac OS 9 computes it on the CPU for every fog mode.
	// TODO: table fog (MISC_3D_STATE_CNTL_REG bit 14)
	if (rs.fog)
	{
		const float f = clamp01(p.sf * (1.0f / 255.0f));
		r = r * f + rs.fog_r * (1.0f - f);
		g = g * f + rs.fog_g * (1.0f - f);
		b = b * f + rs.fog_b * (1.0f - f);
	}

	if (rs.alpha_test && !depth_test(rs.alpha_func, u32(a * 255.0f + 0.5f), rs.alpha_ref))
	{
		record('a', zval, zstored, 0, u32(a * 255.0f + 0.5f));
		return;
	}

	if (rs.stencil)
	{
		const u8 value = zword >> 24;
		const bool spass = depth_test(rs.sten_func, rs.sten_ref & rs.sten_mask, value & rs.sten_mask);
		const int op = !spass ? rs.sten_fail_op : (zpass ? rs.sten_zpass_op : rs.sten_zfail_op);
		const u8 result = (stencil_op(op, value, rs.sten_ref) & rs.sten_wmask) | (value & ~rs.sten_wmask);
		const bool zwrite = spass && zpass && rs.z_write;
		put_u32le(&m_vram[zaddr & ~3], (u32(result) << 24) | (zwrite ? (zval & 0xff'ffff) : zstored));
		if (!spass || !zpass)
			return;
	}

	if (!rs.dst_bpp)
		return;
	const u32 old = (rs.blend || (rs.write_mask != ~0U)) ? read_pixel(rs.dst_base, rs.dst_pitch, rs.dst_bpp, x, y, rs.dst_tiled) : 0;

	float or_ = r * 255.0f, og = g * 255.0f, ob = b * 255.0f, oa = a * 255.0f;
	if (rs.blend)
	{
		float dr, dg, db, da;
		unpack_color(rs.dst_type, old, dr, dg, db, da);
		const float sv[4] = { r, g, b, a };
		const float dv[4] = { dr * (1.0f / 255.0f), dg * (1.0f / 255.0f), db * (1.0f / 255.0f), da * (1.0f / 255.0f) };
		float sf[4], df[4];
		blend_factors(rs.blend_src, sv, dv, sf);
		blend_factors(rs.blend_dst, sv, dv, df);
		float out[4];
		for (int ch = 0; ch < 4; ch++)
		{
			const float s = sv[ch] * sf[ch], d = dv[ch] * df[ch];
			out[ch] = clamp01(rs.blend_subtract ? (s - d) : (s + d));
		}
		or_ = out[0] * 255.0f; og = out[1] * 255.0f; ob = out[2] * 255.0f; oa = out[3] * 255.0f;
	}

	const u32 res = pack_color(rs.dst_type, or_, og, ob, oa);
	if (probe)
		record('w', zval, zstored, read_pixel(rs.dst_base, rs.dst_pitch, rs.dst_bpp, x, y, rs.dst_tiled), res);
	write_pixel(rs.dst_base, rs.dst_pitch, rs.dst_bpp, x, y, (res & rs.write_mask) | (old & ~rs.write_mask), rs.dst_tiled);

	if (!rs.stencil && rs.z_write)
	{
		if (rs.z_bytes == 2)
			put_u16le(&m_vram[zaddr & ~1], zval);
		else if (rs.z_width == 1)
			put_u32le(&m_vram[zaddr & ~3], (zword & 0xff00'0000) | (zval & 0xff'ffff));
		else
			put_u32le(&m_vram[zaddr & ~3], (zword & 0x0000'00ff) | (zval << 8));
	}
}

/***************************************************************************
    PUML PIXEL PIPELINE

    Triangles are shaded by PUML blocks, one per combination of the draw
    state's modes (see puml_key), compiled on the emulation thread when a
    state is queued and run by the render threads over each covered run of
    a row.  The block does what shade_pixel does, in the same order and
    with the same arithmetic, so the result is identical byte for byte;
    the texture sampler is called through a helper.  Lines, points, the
    probe build and states the generator refuses stay on the C++ path.

    Regions: VRAM (masked by m_vram_mask), STATE (the raster_state, constant
    offsets only), TRI (the triangle's planes) and STIPPLE (the stipple rows).
***************************************************************************/

namespace {

// Debugging switches, also settable from the build (-DRAGE128_PUML=0 and so on):
// PUML_ENABLE off draws everything with the C++ path; PUML_VERIFY runs every
// pixel through both, keeps the C++ result and logs the first mismatches.
#ifndef RAGE128_PUML
#define RAGE128_PUML 1
#endif
#ifndef RAGE128_PUML_VERIFY
#define RAGE128_PUML_VERIFY 0
#endif
constexpr bool PUML_ENABLE = RAGE128_PUML;
constexpr bool PUML_VERIFY = RAGE128_PUML_VERIFY;
constexpr u32 PUML_VERIFY_LOG_LIMIT = 32;

enum : int { RGN_VRAM = 0, RGN_STATE, RGN_TRI, RGN_STIPPLE };

// plane order, as in draw_triangle
enum : int { PL_Z, PL_R, PL_G, PL_B, PL_A, PL_SR, PL_SG, PL_SB, PL_SF, PL_Q, PL_S0, PL_T0, PL_S1, PL_T1, PL_COUNT };

// CALL parameter for the texture helper: register numbers ride along
constexpr u64 tex_param(int unit, bool lod, bool key, int sreg, int treg, int rreg, int out, int nearest)
{
	return u64(unit) | (u64(lod) << 1) | (u64(key) << 2) | (u64(sreg) << 8) | (u64(treg) << 13) | (u64(rreg) << 18) | (u64(out) << 23) | (u64(nearest) << 28);
}

} // anonymous namespace


// The modes the generated code depends on.  Everything else (bases, pitches,
// colours, references, masks, the palette, texture addresses) is read from
// the state at run time, so states that differ only in data share a block.
std::string ati_rage128_device::puml_key(const raster_state &rs) const
{
	std::string key;
	auto put = [&key] (u32 v) { key.append(reinterpret_cast<const char *>(&v), sizeof(v)); };
	put(u32(rs.textured) | (u32(rs.perspective) << 1) | (u32(rs.stipple) << 2) | (u32(rs.z_test) << 3) | (u32(rs.z_write) << 4)
			| (u32(rs.stencil) << 5) | (u32(rs.z_tiled && tile_pitch_ok(rs.z_pitch)) << 6) | (u32(rs.specular) << 7) | (u32(rs.fog) << 8)
			| (u32(rs.alpha_test) << 9) | (u32(rs.chroma_key) << 10) | (u32(rs.alpha_mask) << 11) | (u32(rs.blend) << 12)
			| (u32(rs.blend_subtract) << 13) | (u32(rs.dst_tiled && tile_pitch_ok(rs.dst_pitch)) << 14) | (u32(rs.write_mask != ~0U) << 15));
	put(u32(rs.aux_cntl & 0x3f) | (u32(rs.z_func & 7) << 8) | (u32(rs.z_width & 3) << 11) | (u32(rs.sten_func & 7) << 13)
			| (u32(rs.sten_fail_op & 7) << 16) | (u32(rs.sten_zpass_op & 7) << 19) | (u32(rs.sten_zfail_op & 7) << 22) | (u32(rs.alpha_func & 7) << 25)
			| (u32(rs.clr_cmp_fn & 3) << 28));
	put(u32(rs.dst_type & 15) | (u32(rs.dst_bpp & 7) << 4) | (u32(rs.blend_src & 15) << 8) | (u32(rs.blend_dst & 15) << 12));
	if (rs.textured)
	{
		for (const texunit &tu : rs.tex)
		{
			put(u32(tu.enabled) | (u32(tu.sec_st) << 1) | (u32(tu.need_lod) << 2));
			put(tu.combine);
		}
	}
	return key;
}

// texture helper: param from tex_param; reads s, t and rho^2, writes the texel and the nearest texel
void ati_rage128_device::puml_texture(const puml::ctx &c, puml::frame &f, u64 param)
{
	ati_rage128_device &dev = *reinterpret_cast<ati_rage128_device *>(c.user);
	const raster_state &rs = *reinterpret_cast<const raster_state *>(c.base[RGN_STATE]);
	float lod = 0.0f;
	if (BIT(param, 1))
	{
		const float rho2 = f.fs(BIT(param, 18, 5));
		lod = ((rho2 > 0.0f) && std::isfinite(rho2)) ? (0.5f * std::log2(rho2)) : 0.0f;
	}
	u32 nearest = 0;
	const u32 texel = dev.sample_texture(rs.tex[BIT(param, 0)], rs.palette, f.fs(BIT(param, 8, 5)), f.fs(BIT(param, 13, 5)), lod, BIT(param, 2) ? &nearest : nullptr);
	f.i[BIT(param, 23, 5)] = texel;
	f.i[BIT(param, 28, 5)] = nearest;
}

// Writes the block for a state.  Only fields that puml_key covers may steer
// the generator; anything else must be loaded from the STATE region.
bool ati_rage128_device::puml_generate(const raster_state &rs, puml::block &b) const
{
	using puml::opnd;
	using puml::op;
	puml::builder g(b);
	using regmark = puml::builder::regmark;

	g.region(RGN_VRAM, "VRAM", m_vram_mask + 1, true);
	g.region(RGN_STATE, "STATE", sizeof(raster_state), false);
	g.region(RGN_TRI, "TRI", sizeof(puml_tri), false);
	g.region(RGN_STIPPLE, "STIPPLE", sizeof(rs.stipple_rows), true);
#define PUML_SYM(f) g.symbol(RGN_STATE, offsetof(raster_state, f), sizeof(rs.f), #f)
	PUML_SYM(tex_amask); PUML_SYM(dst_base); PUML_SYM(dst_pitch); PUML_SYM(write_mask); PUML_SYM(aux); PUML_SYM(stipple_x);
	PUML_SYM(stipple_y); PUML_SYM(z_base); PUML_SYM(z_pitch); PUML_SYM(sten_ref); PUML_SYM(sten_mask); PUML_SYM(sten_wmask);
	PUML_SYM(const_r); PUML_SYM(const_g); PUML_SYM(const_b); PUML_SYM(const_a); PUML_SYM(fog_r); PUML_SYM(fog_g); PUML_SYM(fog_b);
	PUML_SYM(alpha_ref); PUML_SYM(clr_cmp_clr); PUML_SYM(clr_cmp_msk); PUML_SYM(chroma_clr); PUML_SYM(chroma_msk);
#undef PUML_SYM
	g.symbol(RGN_TRI, offsetof(puml_tri, row), sizeof(puml_tri::row), "row");
	g.symbol(RGN_TRI, offsetof(puml_tri, dx), sizeof(puml_tri::dx), "dx");
	g.symbol(RGN_TRI, offsetof(puml_tri, dy), sizeof(puml_tri::dy), "dy");
	g.symbol(RGN_TRI, offsetof(puml_tri, tex_w), sizeof(puml_tri::tex_w), "tex_w");
	g.symbol(RGN_TRI, offsetof(puml_tri, tex_h), sizeof(puml_tri::tex_h), "tex_h");
	const int tex_helper = g.add_helper(&ati_rage128_device::puml_texture, "sample_texture");

	// a predicate that may be known at generation time
	struct pred { int reg; bool k; };
	const pred always{ -1, true }, never{ -1, false };

	auto alloc_f = [&g] { return g.alloc_f(); };
	auto alloc_i = [&g] { return g.alloc_i(); };
	auto fconst = [&g] (float v) { const int r = g.alloc_f(); g.fldi(r, v); return r; };
	auto fstate = [&g] (size_t off) { const int r = g.alloc_f(); g.fld(r, RGN_STATE, -1, u32(off)); return r; };
	auto istate = [&g] (size_t off, puml::msize sz) { const int r = g.alloc_i(); g.ld(r, RGN_STATE, -1, u32(off), sz); return r; };
	auto ftri = [&g] (size_t off) { const int r = g.alloc_f(); g.fld(r, RGN_TRI, -1, u32(off)); return r; };
	int fzero = -1, fone = -1;      // loaded in the span prologue
	auto clamp01 = [&g, &fzero, &fone] (int dst, int src) { g.clamp01(dst, src, fzero, fone); };
	auto one_minus = [&g, &fone] (int dst, int src) { g.fop(op::FSUB, dst, fone, src); };
	auto kill_if = [&g] (pred p, int any_i) {
		if (p.reg >= 0)
			g.kill(p.reg);
		else if (p.k)
		{
			const regmark m = g.mark();
			const int t = g.alloc_p();
			g.cmp(puml::C_EQ, t, any_i, opnd::i(any_i));
			g.kill(t);
			g.release(m);
		}
	};
	auto kill_unless = [&g, &kill_if] (pred p, int any_i) {
		if (p.reg < 0)
			kill_if(pred{ -1, !p.k }, any_i);
		else
		{
			const regmark m = g.mark();
			const int t = g.alloc_p();
			g.pnot(t, p.reg);
			g.kill(t);
			g.release(m);
		}
	};
	// depth_test(fn, a, b) with unsigned compares
	auto dtest = [&g] (int fn, int a, opnd bv) -> pred {
		static const puml::cond conds[8] = { puml::C_EQ, puml::C_LTU, puml::C_LEU, puml::C_EQ, puml::C_GEU, puml::C_GTU, puml::C_NE, puml::C_EQ };
		fn &= 7;
		if (fn == 0)
			return pred{ -1, false };
		if (fn == 7)
			return pred{ -1, true };
		const int p = g.alloc_p();
		g.cmp(conds[fn], p, a, bv);
		return pred{ p, false };
	};
	// byte offset of byte xb of line y in a surface, as surface_offset
	auto surface = [&g] (int dst, int xb, int y, int pitch, bool tiled) {
		if (tiled)
			g.tile_address(dst, xb, y, pitch, 6, 4);
		else
			g.linear_address(dst, xb, y, pitch);
	};

	g.comment(util::string_format("Rage 128: %s%s%s%s%s dst type %d", rs.textured ? "textured " : "", rs.z_test ? "Z " : "",
			rs.stencil ? "stencil " : "", rs.blend ? "blend " : "", rs.fog ? "fog " : "", rs.dst_type));

	// span prologue: the row and anything that depends only on it
	const int iy = alloc_i();
	g.input(iy, puml::IN_Y);
	fzero = fconst(0.0f);
	fone = fconst(1.0f);
	int stip_row = -1;
	if (rs.stipple)
	{
		stip_row = alloc_i();
		const regmark m = g.mark();
		const int t = istate(offsetof(raster_state, stipple_y), puml::SZ_U32);
		g.alu(op::ADD, t, iy, opnd::i(t));
		g.alui(op::AND, t, t, 31);
		g.alui(op::SHL, t, t, 2);
		g.ld(stip_row, RGN_STIPPLE, t, 0, puml::SZ_U32);
		g.release(m);
	}
	g.pixel();

	const int ix = alloc_i();
	g.input(ix, puml::IN_X);
	const int px = alloc_f();
	{
		const regmark m = g.mark();
		g.itof(px, ix, puml::CV_S32);
		const int h = fconst(0.5f);
		g.fop(op::FADD, px, px, h);
		g.release(m);
	}
	// a plane's value at this pixel: row + dx * px
	auto plane = [&] (int dst, int i) {
		const regmark m = g.mark();
		g.fld(dst, RGN_TRI, -1, u32(offsetof(puml_tri, row) + 4 * i));
		const int d = ftri(offsetof(puml_tri, dx) + 4 * i);
		g.fop(op::FMUL, d, d, px);
		g.fop(op::FADD, dst, dst, d);
		g.release(m);
	};

	// stipple
	if (rs.stipple)
	{
		g.comment("stipple");
		const regmark m = g.mark();
		const int t = istate(offsetof(raster_state, stipple_x), puml::SZ_U32);
		g.alu(op::ADD, t, ix, opnd::i(t));
		g.alui(op::AND, t, t, 31);
		g.alui(op::XOR, t, t, 7);       // (sx & ~7) | (~sx & 7)
		const int bit = alloc_i();
		g.alu(op::SHR, bit, stip_row, opnd::i(t));
		g.alui(op::AND, bit, bit, 1);
		const int p = g.alloc_p();
		g.cmp(puml::C_EQ, p, bit, opnd::imm(0));
		g.kill(p);
		g.release(m);
	}

	// auxiliary scissors
	if (rs.aux_cntl & 0x15)
	{
		g.comment("aux scissors");
		const regmark m = g.mark();
		const int or_hit = g.alloc_p();
		bool any_or = false;
		for (int i = 0; i < 3; i++)
		{
			if (!BIT(rs.aux_cntl, i * 2))
				continue;
			const regmark m2 = g.mark();
			const int inside = g.alloc_p(), t = g.alloc_p();
			const size_t base = offsetof(raster_state, aux) + 16 * i;
			const int l = istate(base + 0, puml::SZ_U32), r = istate(base + 4, puml::SZ_U32);
			const int tp = istate(base + 8, puml::SZ_U32), bt = istate(base + 12, puml::SZ_U32);
			g.cmp(puml::C_GE, inside, ix, opnd::i(l));
			g.cmp(puml::C_LE, t, ix, opnd::i(r));
			g.pop(op::PAND, inside, inside, t);
			g.cmp(puml::C_GE, t, iy, opnd::i(tp));
			g.pop(op::PAND, inside, inside, t);
			g.cmp(puml::C_LE, t, iy, opnd::i(bt));
			g.pop(op::PAND, inside, inside, t);
			if (BIT(rs.aux_cntl, i * 2 + 1))
				g.kill(inside);
			else
			{
				g.pop(op::POR, or_hit, any_or ? or_hit : inside, inside);
				any_or = true;
			}
			g.release(m2);
		}
		if (any_or)
		{
			const int t = g.alloc_p();
			g.pnot(t, or_hit);
			g.kill(t);
		}
		g.release(m);
	}

	// depth and stencil buffer
	int zaddr = -1, zval = -1, zword = -1, zstored = -1;
	pred zpass = always;
	if (rs.z_test || rs.stencil)
	{
		g.comment("depth");
		zaddr = alloc_i(); zval = alloc_i(); zword = alloc_i(); zstored = alloc_i();
		const regmark m = g.mark();
		const int xb = alloc_i();
		g.alui(op::SHL, xb, ix, (rs.z_bytes == 2) ? 1 : 2);
		const int pitch = istate(offsetof(raster_state, z_pitch), puml::SZ_U32);
		surface(zaddr, xb, iy, pitch, rs.z_tiled && tile_pitch_ok(rs.z_pitch));
		const int zb = istate(offsetof(raster_state, z_base), puml::SZ_U32);
		g.alu(op::ADD, zaddr, zb, opnd::i(zaddr));

		// zval = u32(double(clamp01(z)) * double(z_max))
		const int z = alloc_f();
		plane(z, PL_Z);
		clamp01(z, z);
		const int zd = alloc_f(), zm = alloc_f();
		g.fcvt(zd, z, puml::type::F64);
		g.fldi64(zm, double(rs.z_max));
		g.fop(op::FMUL, zd, zd, zm, puml::type::F64);
		g.ftoi(zval, zd, puml::CV_U32, puml::type::F64);

		const int a = alloc_i();
		g.alui(op::AND, a, zaddr, (rs.z_bytes == 2) ? ~1U : ~3U);
		g.ld(zword, RGN_VRAM, a, 0, (rs.z_bytes == 2) ? puml::SZ_U16 : puml::SZ_U32);
		g.alui(op::SHR, zstored, zword, rs.z_shift);
		g.alui(op::AND, zstored, zstored, rs.z_max);
		g.release(m);

		if (rs.z_test)
			zpass = dtest(rs.z_func, zval, opnd::i(zstored));      // lives to the end
		if (!rs.stencil)
			kill_unless(zpass, ix);
	}

	// colour
	g.comment("colour");
	const int cr = alloc_f(), cg = alloc_f(), cb = alloc_f(), ca = alloc_f();
	const int col[4] = { cr, cg, cb, ca };
	{
		const regmark m = g.mark();
		const int inv = fconst(1.0f / 255.0f);
		for (int c = 0; c < 4; c++)
		{
			plane(col[c], PL_R + c);
			g.fop(op::FMUL, col[c], col[c], inv);
		}
		g.release(m);
	}

	if (rs.textured)
	{
		const bool persp = rs.perspective;
		const regmark mt = g.mark();
		for (int unit = 0; unit < 2; unit++)
		{
			const texunit &tu = rs.tex[unit];
			if (!tu.enabled)
				continue;
			g.comment(util::string_format("texture unit %d, combine %08x", unit, tu.combine));
			const regmark mu = g.mark();
			const int texel = alloc_i(), nearest = alloc_i();
			const bool key = !unit && (rs.chroma_key || rs.clr_cmp_fn || rs.alpha_mask);
			const int sp = tu.sec_st ? PL_S1 : PL_S0, tp = tu.sec_st ? PL_T1 : PL_T0;
			const bool lod = tu.need_lod;
			{
				const regmark m = g.mark();
				const int iq = alloc_f();
				if (persp)
				{
					// iq = (Q != 0) ? 1 / Q : 0
					const regmark mq = g.mark();
					const int q = alloc_f(), r = alloc_f();
					plane(q, PL_Q);
					const int p = g.alloc_p();
					g.fcmp(puml::C_NE, p, q, fzero);
					g.fop(op::FDIV, r, fone, q);
					g.fsel(iq, p, r, fzero);
					g.release(mq);
				}
				else
					g.fldi(iq, 1.0f);
				const int rho = alloc_f();
				if (lod)
				{
					// rho^2 = max(dudx^2 + dvdx^2, dudy^2 + dvdy^2); unit 0 uses the primary coordinates
					const int lsp = unit ? sp : PL_S0, ltp = unit ? tp : PL_T0;
					const regmark m2 = g.mark();
					const int ls = alloc_f(), lt = alloc_f();
					plane(ls, lsp);
					g.fop(op::FMUL, ls, ls, iq);
					plane(lt, ltp);
					g.fop(op::FMUL, lt, lt, iq);
					// d = ((d[pl] - coord * d[Q]) * iq) * size, for dx and dy
					auto deriv = [&] (int dst, size_t table, int pl, int coord, size_t size) {
						const regmark m3 = g.mark();
						g.fld(dst, RGN_TRI, -1, u32(table + 4 * pl));
						const int dq = ftri(table + 4 * PL_Q);
						g.fop(op::FMUL, dq, coord, dq);
						g.fop(op::FSUB, dst, dst, dq);
						g.fop(op::FMUL, dst, dst, iq);
						g.fld(dq, RGN_TRI, -1, u32(size));
						g.fop(op::FMUL, dst, dst, dq);
						g.release(m3);
					};
					const size_t w = offsetof(puml_tri, tex_w) + 4 * unit, h = offsetof(puml_tri, tex_h) + 4 * unit;
					const int sum = alloc_f(), u = alloc_f(), v = alloc_f();
					deriv(u, offsetof(puml_tri, dx), lsp, ls, w);
					deriv(v, offsetof(puml_tri, dx), ltp, lt, h);
					g.fop(op::FMUL, u, u, u);
					g.fop(op::FMUL, v, v, v);
					g.fop(op::FADD, sum, u, v);
					deriv(u, offsetof(puml_tri, dy), lsp, ls, w);
					deriv(v, offsetof(puml_tri, dy), ltp, lt, h);
					g.fop(op::FMUL, u, u, u);
					g.fop(op::FMUL, v, v, v);
					g.fop(op::FADD, u, u, v);
					g.fop(op::FMAX, rho, sum, u);     // std::max(dx term, dy term)
					g.release(m2);
				}
				const int s = alloc_f(), t = alloc_f();
				plane(s, sp);
				g.fop(op::FMUL, s, s, iq);
				plane(t, tp);
				g.fop(op::FMUL, t, t, iq);
				puml::call_info regs;
				regs.read_i = 0;
				regs.read_f = (1U << s) | (1U << t) | (lod ? (1U << rho) : 0);
				regs.write_i = (1U << texel) | (1U << nearest);
				regs.write_f = 0;
				g.call(tex_helper, tex_param(unit, lod, key, s, t, rho, texel, nearest), regs);
				g.release(m);
			}
			const int tr = alloc_f(), tg = alloc_f(), tb = alloc_f(), ta = alloc_f();
			{
				if (key)
				{
					g.comment("colour keys");
					const regmark mk = g.mark();
					const int k = alloc_i(), p = g.alloc_p();
					if (rs.alpha_mask)
					{
						g.alui(op::SHR, k, nearest, 24);
						g.cmp(puml::C_EQ, p, k, opnd::imm(0));
						g.kill(p);
					}
					if (rs.chroma_key)
					{
						const regmark m2 = g.mark();
						const int msk = istate(offsetof(raster_state, chroma_msk), puml::SZ_U32), clr = istate(offsetof(raster_state, chroma_clr), puml::SZ_U32);
						g.alu(op::AND, k, nearest, opnd::i(msk));
						g.cmp(puml::C_EQ, p, k, opnd::i(clr));
						g.kill(p);
						g.release(m2);
					}
					if (rs.clr_cmp_fn == 1)
						kill_if(always, ix);
					else if (rs.clr_cmp_fn)
					{
						const regmark m2 = g.mark();
						const int msk = istate(offsetof(raster_state, clr_cmp_msk), puml::SZ_U32), clr = istate(offsetof(raster_state, clr_cmp_clr), puml::SZ_U32);
						g.alu(op::AND, k, nearest, opnd::i(msk));
						g.cmp((rs.clr_cmp_fn == 3) ? puml::C_EQ : puml::C_NE, p, k, opnd::i(clr));
						g.kill(p);
						g.release(m2);
					}
					g.release(mk);
				}

				// texel |= tex_amask, then unpack_argb
				const regmark m2 = g.mark();
				const int am = istate(offsetof(raster_state, tex_amask), puml::SZ_U32);
				g.alu(op::OR, texel, texel, opnd::i(am));
				const int k = alloc_i(), inv = fconst(1.0f / 255.0f);
				const int dsts[4] = { ta, tr, tg, tb };
				for (int c = 0; c < 4; c++)
				{
					g.alui(op::SHR, k, texel, 24 - 8 * c);
					if (c)
						g.alui(op::AND, k, k, 0xff);
					g.itof(dsts[c], k, puml::CV_U32);
					g.fop(op::FMUL, dsts[c], dsts[c], inv);
				}
				g.release(m2);
			}

			// combine(tu.combine, !unit, ...), with i = the current colour
			const u32 cntl = tu.combine;
			const bool first = !unit;
			const int comb = cntl & 0xf, cfac = BIT(cntl, 4, 4), ifac = BIT(cntl, 10, 4);
			const int acomb = BIT(cntl, 14, 4), afac = BIT(cntl, 18, 4), iafac = BIT(cntl, 25, 3);
			const int texc[3] = { tr, tg, tb };
			const size_t constoff[4] = { offsetof(raster_state, const_r), offsetof(raster_state, const_g), offsetof(raster_state, const_b), offsetof(raster_state, const_a) };

			const regmark mc = g.mark();
			int blend_a = ca;
			if (comb == 9)
				blend_a = ta;
			else if (comb == 10)
				blend_a = fstate(constoff[3]);

			// op(mode, f, x, blend_a, tex_a, c), writing dst last so that it may alias an input
			auto combine_op = [&] (int dst, int mode, int f, int x, int c) {
				const regmark m = g.mark();
				switch (mode)
				{
				case 1: g.fmov(dst, f); break;
				case 3: g.fop(op::FMUL, dst, f, x); break;
				case 4: case 5:
				{
					const int t = alloc_f(), k = fconst((mode == 4) ? 2.0f : 4.0f);
					g.fop(op::FMUL, t, k, f);
					g.fop(op::FMUL, t, t, x);
					clamp01(dst, t);
					break;
				}
				case 6:
				{
					const int t = alloc_f();
					g.fop(op::FADD, t, f, x);
					clamp01(dst, t);
					break;
				}
				case 7: case 14:
				{
					const int t = alloc_f(), k = fconst(0.5f);
					g.fop(op::FADD, t, f, x);
					g.fop(op::FSUB, t, t, k);
					if (mode == 14)
					{
						g.fldi(k, 2.0f);
						g.fop(op::FMUL, t, k, t);
					}
					clamp01(dst, t);
					break;
				}
				case 8: case 9: case 10: case 12: case 15:
				{
					const int w = (mode == 15) ? c : blend_a;
					const int t = alloc_f(), u = alloc_f();
					g.fop(op::FMUL, t, f, w);
					one_minus(u, w);
					g.fop(op::FMUL, u, x, u);
					g.fop(op::FADD, dst, t, u);
					break;
				}
				case 11: case 13:
				{
					const int t = alloc_f(), u = alloc_f();
					one_minus(u, ta);
					if (mode == 11)
					{
						g.fop(op::FMUL, u, x, u);
						g.fop(op::FADD, t, f, u);
					}
					else
					{
						g.fop(op::FMUL, u, f, u);
						g.fop(op::FADD, t, u, x);
					}
					clamp01(dst, t);
					break;
				}
				default:        // 0, 2
					g.fmov(dst, x);
					break;
				}
				g.release(m);
			};

			const bool needs_c = (cfac == 0) || (cfac == 1) || (ifac == 2) || (comb == 15);
			for (int ch = 0; ch < 3; ch++)
			{
				const regmark m = g.mark();
				const int c = needs_c ? fstate(constoff[ch]) : -1;
				int f;
				switch (cfac)
				{
				case 0: f = c; break;
				case 1: f = alloc_f(); one_minus(f, c); break;
				case 5: f = alloc_f(); one_minus(f, texc[ch]); break;
				case 6: f = ta; break;
				case 7: f = alloc_f(); one_minus(f, ta); break;
				case 8: f = col[ch]; break;
				default: f = texc[ch]; break;
				}
				int x;
				switch (ifac)
				{
				case 2: x = c; break;
				case 3: x = fstate(constoff[3]); break;
				case 5: case 9: x = ca; break;
				default: x = col[ch]; break;
				}
				// the result replaces this channel of the colour; alpha is done last
				if (comb == 0)
				{
					if (BIT(cntl, 8))
					{
						g.fop(op::FSUB, col[ch], f, x);
						g.fop(op::FMAX, col[ch], col[ch], fzero);     // std::max(f - x, 0.0f)
					}
					else
						g.fmov(col[ch], texc[ch]);
				}
				else
					combine_op(col[ch], comb, f, x, c);
				g.release(m);
			}
			{
				const regmark m = g.mark();
				const int c = fstate(constoff[3]);
				int fa = ta;
				if (afac == 7)
				{
					fa = alloc_f();
					one_minus(fa, ta);
				}
				const int xa = (iafac == 1) ? c : ca;
				if (acomb == 0)
					g.fmov(ca, first ? ta : ca);
				else
					combine_op(ca, acomb, fa, xa, c);
				g.release(m);
			}
			g.release(mc);
			g.release(mu);
		}
		g.release(mt);
	}

	if (rs.specular)
	{
		g.comment("specular");
		const regmark m = g.mark();
		const int inv = fconst(1.0f / 255.0f), s = alloc_f();
		for (int c = 0; c < 3; c++)
		{
			plane(s, PL_SR + c);
			g.fop(op::FMUL, s, s, inv);
			g.fop(op::FADD, s, col[c], s);
			clamp01(col[c], s);
		}
		g.release(m);
	}

	if (rs.fog)
	{
		g.comment("fog");
		const regmark m = g.mark();
		const int inv = fconst(1.0f / 255.0f), f = alloc_f(), omf = alloc_f(), t = alloc_f(), u = alloc_f();
		plane(f, PL_SF);
		g.fop(op::FMUL, f, f, inv);
		clamp01(f, f);
		one_minus(omf, f);
		const size_t fogoff[3] = { offsetof(raster_state, fog_r), offsetof(raster_state, fog_g), offsetof(raster_state, fog_b) };
		for (int c = 0; c < 3; c++)
		{
			g.fop(op::FMUL, t, col[c], f);
			g.fld(u, RGN_STATE, -1, u32(fogoff[c]));
			g.fop(op::FMUL, u, u, omf);
			g.fop(op::FADD, col[c], t, u);
		}
		g.release(m);
	}

	if (rs.alpha_test)
	{
		g.comment("alpha test");
		const regmark m = g.mark();
		const int t = alloc_f(), k = fconst(255.0f), h = fconst(0.5f), v = alloc_i();
		g.fop(op::FMUL, t, ca, k);
		g.fop(op::FADD, t, t, h);
		g.ftoi(v, t, puml::CV_U32);
		const int ref = istate(offsetof(raster_state, alpha_ref), puml::SZ_U32);
		kill_unless(dtest(rs.alpha_func, v, opnd::i(ref)), ix);
		g.release(m);
	}

	g.order();

	if (rs.stencil)
	{
		g.comment("stencil");
		const regmark m = g.mark();
		const int value = alloc_i();
		g.alui(op::SHR, value, zword, 24);
		const int ref = istate(offsetof(raster_state, sten_ref), puml::SZ_U8);
		const int msk = istate(offsetof(raster_state, sten_mask), puml::SZ_U8);
		const int a = alloc_i(), bb = alloc_i();
		g.alu(op::AND, a, ref, opnd::i(msk));
		g.alu(op::AND, bb, value, opnd::i(msk));
		const pred spass = dtest(rs.sten_func, a, opnd::i(bb));

		// stencil_op for each of the three operations
		auto sop = [&] (int dst, int opn) {
			const regmark m2 = g.mark();
			switch (opn & 7)
			{
			case 1: g.ldi(dst, 0); break;
			case 2: g.mov(dst, ref); break;
			case 3: g.alui(op::ADD, dst, value, 1); g.alui(op::MINU, dst, dst, 0xff); break;
			case 4: g.alui(op::MAXU, dst, value, 1); g.alui(op::SUB, dst, dst, 1); break;
			case 5: g.alui(op::XOR, dst, value, 0xff); break;
			default: g.mov(dst, value); break;
			}
			g.release(m2);
		};
		const int rfail = alloc_i(), rpass = alloc_i(), rzfail = alloc_i(), res = alloc_i();
		sop(rfail, rs.sten_fail_op);
		sop(rpass, rs.sten_zpass_op);
		sop(rzfail, rs.sten_zfail_op);
		if (zpass.reg >= 0)
			g.sel(res, zpass.reg, opnd::i(rpass), opnd::i(rzfail));
		else
			g.mov(res, zpass.k ? rpass : rzfail);
		if (spass.reg >= 0)
			g.sel(res, spass.reg, opnd::i(res), opnd::i(rfail));
		else if (!spass.k)
			g.mov(res, rfail);

		// (result & wmask) | (value & ~wmask)
		const int wm = istate(offsetof(raster_state, sten_wmask), puml::SZ_U8);
		g.alu(op::AND, res, res, opnd::i(wm));
		g.alui(op::XOR, wm, wm, 0xff);
		g.alu(op::AND, a, value, opnd::i(wm));
		g.alu(op::OR, res, res, opnd::i(a));

		// zwrite = spass && zpass && z_write
		pred both = never;
		const int pb = g.alloc_p();
		if (spass.reg >= 0 && zpass.reg >= 0)
		{
			g.pop(op::PAND, pb, spass.reg, zpass.reg);
			both = pred{ pb, false };
		}
		else if (spass.reg >= 0)
			both = zpass.k ? spass : never;
		else if (zpass.reg >= 0)
			both = spass.k ? zpass : never;
		else
			both = pred{ -1, spass.k && zpass.k };

		const int low = alloc_i();
		if (rs.z_write && (both.reg >= 0))
		{
			g.alui(op::AND, a, zval, 0xff'ffff);
			g.sel(low, both.reg, opnd::i(a), opnd::i(zstored));
		}
		else if (rs.z_write && both.k)
			g.alui(op::AND, low, zval, 0xff'ffff);
		else
			g.mov(low, zstored);
		g.alui(op::SHL, res, res, 24);
		g.alu(op::OR, res, res, opnd::i(low));
		g.alui(op::AND, a, zaddr, ~3U);
		g.st(RGN_VRAM, a, 0, res, puml::SZ_U32);
		kill_unless(both, ix);
		g.release(m);
	}

	if (rs.dst_bpp)
	{
		g.comment("destination");
		const int bpp = rs.dst_bpp;
		const bool partial = rs.write_mask != ~0U;
		const bool tiled = rs.dst_tiled && tile_pitch_ok(rs.dst_pitch);
		const regmark m = g.mark();

		// byte addresses: one, or three for 24-bit pixels, which can straddle a tile column
		int caddr[3] = { -1, -1, -1 };
		for (int k = 0; k < ((bpp == 3) ? 3 : 1); k++)
			caddr[k] = alloc_i();
		{
			const regmark m2 = g.mark();
			const int pitch = istate(offsetof(raster_state, dst_pitch), puml::SZ_U32);
			const int base = istate(offsetof(raster_state, dst_base), puml::SZ_U32);
			const int xb = alloc_i(), xk = alloc_i();
			if (bpp == 3)
				g.alui(op::MUL, xb, ix, 3);
			else
				g.alui(op::SHL, xb, ix, (bpp == 4) ? 2 : (bpp == 2) ? 1 : 0);
			for (int k = 0; k < ((bpp == 3) ? 3 : 1); k++)
			{
				if (k)
					g.alui(op::ADD, xk, xb, k);
				surface(caddr[k], k ? xk : xb, iy, pitch, tiled);
				g.alu(op::ADD, caddr[k], base, opnd::i(caddr[k]));
			}
			g.release(m2);
		}
		auto aligned = [&g, &alloc_i] (int a, int bytes) {
			const int r = alloc_i();
			g.alui(op::AND, r, a, (bytes == 2) ? ~1U : ~3U);
			return r;
		};

		int old = -1;
		if (rs.blend || partial)
		{
			old = alloc_i();
			switch (bpp)
			{
			case 1: g.ld(old, RGN_VRAM, caddr[0], 0, puml::SZ_U8); break;
			case 2: g.ld(old, RGN_VRAM, aligned(caddr[0], 2), 0, puml::SZ_U16); break;
			case 3:
			{
				const regmark m2 = g.mark();
				const int t = alloc_i();
				g.ld(old, RGN_VRAM, caddr[0], 0, puml::SZ_U8);
				for (int k = 1; k < 3; k++)
				{
					g.ld(t, RGN_VRAM, caddr[k], 0, puml::SZ_U8);
					g.alui(op::SHL, t, t, 8 * k);
					g.alu(op::OR, old, old, opnd::i(t));
				}
				g.release(m2);
				break;
			}
			default: g.ld(old, RGN_VRAM, aligned(caddr[0], 4), 0, puml::SZ_U32); break;
			}
		}

		// output colour, 0..255
		const int o[4] = { alloc_f(), alloc_f(), alloc_f(), alloc_f() };
		{
			const regmark m2 = g.mark();
			const int k255 = fconst(255.0f);
			if (rs.blend)
			{
				g.comment("blend");
				// unpack_color: dv = channel (0..255) / 255
				const int d[4] = { alloc_f(), alloc_f(), alloc_f(), alloc_f() };
				const int t = alloc_i();
				auto expand = [&] (int dst, int shift, int bits) {
					const regmark m3 = g.mark();
					g.alui(op::SHR, t, old, shift);
					g.alui(op::AND, t, t, (1U << bits) - 1);
					const int u = alloc_i();
					switch (bits)
					{
					case 2: g.alui(op::MUL, t, t, 0x55); break;
					case 3:
					{
						const int w = alloc_i();
						g.alui(op::SHL, u, t, 5);
						g.alui(op::SHL, w, t, 2);
						g.alu(op::OR, u, u, opnd::i(w));
						g.alui(op::SHR, w, t, 1);
						g.alu(op::OR, t, u, opnd::i(w));
						break;
					}
					case 4: g.alui(op::MUL, t, t, 0x11); break;
					case 5: g.alui(op::SHL, u, t, 3); g.alui(op::SHR, t, t, 2); g.alu(op::OR, t, u, opnd::i(t)); break;
					case 6: g.alui(op::SHL, u, t, 2); g.alui(op::SHR, t, t, 4); g.alu(op::OR, t, u, opnd::i(t)); break;
					default: break;     // 8 bits
					}
					g.itof(d[dst], t, puml::CV_U32);
					g.release(m3);
				};
				auto opaque = [&] (int dst) { g.fmov(d[dst], k255); };
				switch (rs.dst_type & 15)
				{
				case 2: case 9: expand(0, 0, 8); g.fmov(d[1], d[0]); g.fmov(d[2], d[0]); opaque(3); break;
				case 3:
				{
					const regmark m3 = g.mark();
					const int p = g.alloc_p(), zero = fconst(0.0f);
					g.tst(p, old, opnd::imm(0x8000));
					g.fsel(d[3], p, k255, zero);
					g.release(m3);
					expand(0, 10, 5); expand(1, 5, 5); expand(2, 0, 5);
					break;
				}
				case 4: opaque(3); expand(0, 11, 5); expand(1, 5, 6); expand(2, 0, 5); break;
				case 5: opaque(3); expand(0, 16, 8); expand(1, 8, 8); expand(2, 0, 8); break;
				case 7: opaque(3); expand(0, 5, 3); expand(1, 2, 3); expand(2, 0, 2); break;
				case 15: expand(3, 12, 4); expand(0, 8, 4); expand(1, 4, 4); expand(2, 0, 4); break;
				default: expand(3, 24, 8); expand(0, 16, 8); expand(1, 8, 8); expand(2, 0, 8); break;
				}
				{
					const int inv = fconst(1.0f / 255.0f);
					for (int c = 0; c < 4; c++)
						g.fop(op::FMUL, d[c], d[c], inv);
				}

				// blend_factors for source and destination, channel by channel
				auto factor = [&] (int dst, int mode, int ch) {
					switch (mode)
					{
					case 0: g.fldi(dst, 0.0f); break;
					case 2: g.fmov(dst, col[ch]); break;
					case 3: one_minus(dst, col[ch]); break;
					case 4: case 11: g.fmov(dst, ca); break;
					case 5: case 12: one_minus(dst, ca); break;
					case 6: g.fmov(dst, d[3]); break;
					case 7: one_minus(dst, d[3]); break;
					case 8: g.fmov(dst, d[ch]); break;
					case 9: one_minus(dst, d[ch]); break;
					case 10:
						if (ch == 3)
							g.fldi(dst, 1.0f);
						else
						{
							const regmark m3 = g.mark();
							const int t2 = alloc_f();
							one_minus(t2, d[3]);
							g.fop(op::FMIN, dst, ca, t2);     // std::min(s[3], 1 - d[3])
							g.release(m3);
						}
						break;
					default: g.fldi(dst, 1.0f); break;
					}
				};
				const int sf = alloc_f(), df = alloc_f();
				for (int c = 0; c < 4; c++)
				{
					factor(sf, rs.blend_src & 15, c);
					factor(df, rs.blend_dst & 15, c);
					g.fop(op::FMUL, sf, col[c], sf);
					g.fop(op::FMUL, df, d[c], df);
					g.fop(rs.blend_subtract ? op::FSUB : op::FADD, sf, sf, df);
					clamp01(sf, sf);
					g.fop(op::FMUL, o[c], sf, k255);
				}
			}
			else
			{
				for (int c = 0; c < 4; c++)
					g.fop(op::FMUL, o[c], col[c], k255);
			}
			g.release(m2);
		}

		// pack_color: clamp8 the channels it needs, then pack
		g.comment("pack");
		const int dtype = rs.dst_type & 15;
		const bool need_a = (dtype == 3) || (dtype == 6) || (dtype == 15) || ((dtype != 2) && (dtype != 9) && (dtype != 4) && (dtype != 5) && (dtype != 7));
		int c8[4] = { -1, -1, -1, -1 };
		{
			for (int c = 0; c < 4; c++)
			{
				if ((c == 3) && !need_a)
					continue;
				if ((c > 0) && (c < 3) && ((dtype == 2) || (dtype == 9)))
					continue;
				c8[c] = alloc_i();
			}
			const regmark m2 = g.mark();
			const int zero = fconst(0.0f), k255 = fconst(255.0f), h = fconst(0.5f), t = alloc_f();
			const int p = g.alloc_p();
			for (int c = 0; c < 4; c++)
			{
				if (c8[c] < 0)
					continue;
				// u8(std::clamp(v, 0.0f, 255.0f) + 0.5f)
				g.fcmp(puml::C_GT, p, o[c], k255);
				g.fsel(t, p, k255, o[c]);
				g.fcmp(puml::C_LT, p, o[c], zero);
				g.fsel(t, p, zero, t);
				g.fop(op::FADD, t, t, h);
				g.ftoi(c8[c], t, puml::CV_U32);
				g.alui(op::AND, c8[c], c8[c], 0xff);
			}
			g.release(m2);
		}
		const int res = alloc_i(), t = alloc_i();
		auto field = [&] (int c, int shr, int shl, u32 mask = ~0U) {
			g.alui(op::SHR, t, c8[c], shr);
			if (mask != ~0U)
				g.alui(op::AND, t, t, mask);
			g.alui(op::SHL, t, t, shl);
			g.alu(op::OR, res, res, opnd::i(t));
		};
		g.ldi(res, 0);
		switch (dtype)
		{
		case 2: case 9: g.mov(res, c8[0]); break;
		case 3:
		{
			const int p = g.alloc_p();
			g.cmp(puml::C_GEU, p, c8[3], opnd::imm(128));
			g.sel(res, p, opnd::imm(0x8000), opnd::imm(0));
			field(0, 3, 10); field(1, 3, 5); field(2, 3, 0);
			break;
		}
		case 4: field(0, 3, 11); field(1, 2, 5); field(2, 3, 0); break;
		case 5: field(0, 0, 16); field(1, 0, 8); field(2, 0, 0); break;
		case 7:
			g.alui(op::AND, res, c8[0], 0xe0);
			field(1, 3, 0, 0x1c);
			field(2, 6, 0);
			break;
		case 15: field(3, 4, 12); field(0, 4, 8); field(1, 4, 4); field(2, 4, 0); break;
		default: field(3, 0, 24); field(0, 0, 16); field(1, 0, 8); field(2, 0, 0); break;
		}

		if (partial)
		{
			const int wm = istate(offsetof(raster_state, write_mask), puml::SZ_U32);
			g.alu(op::AND, res, res, opnd::i(wm));
			g.alui(op::XOR, wm, wm, ~0U);
			g.alu(op::AND, t, old, opnd::i(wm));
			g.alu(op::OR, res, res, opnd::i(t));
		}

		switch (bpp)
		{
		case 1: g.st(RGN_VRAM, caddr[0], 0, res, puml::SZ_U8); break;
		case 2: g.st(RGN_VRAM, aligned(caddr[0], 2), 0, res, puml::SZ_U16); break;
		case 3:
			g.st(RGN_VRAM, caddr[0], 0, res, puml::SZ_U8);
			for (int k = 1; k < 3; k++)
			{
				g.alui(op::SHR, t, res, 8 * k);
				g.st(RGN_VRAM, caddr[k], 0, t, puml::SZ_U8);
			}
			break;
		default: g.st(RGN_VRAM, aligned(caddr[0], 4), 0, res, puml::SZ_U32); break;
		}
		g.release(m);

		// depth write without stencil
		if (!rs.stencil && rs.z_write)
		{
			g.comment("depth write");
			const regmark m2 = g.mark();
			const int a = alloc_i(), v = alloc_i(), u = alloc_i();
			if (rs.z_bytes == 2)
			{
				g.alui(op::AND, a, zaddr, ~1U);
				g.st(RGN_VRAM, a, 0, zval, puml::SZ_U16);
			}
			else
			{
				g.alui(op::AND, a, zaddr, ~3U);
				if (rs.z_width == 1)
				{
					g.alui(op::AND, v, zword, 0xff00'0000);
					g.alui(op::AND, u, zval, 0xff'ffff);
				}
				else
				{
					g.alui(op::AND, v, zword, 0xff);
					g.alui(op::SHL, u, zval, 8);
				}
				g.alu(op::OR, v, v, opnd::i(u));
				g.st(RGN_VRAM, a, 0, v, puml::SZ_U32);
			}
			g.release(m2);
		}
	}

	if (g.overflow())
	{
		const puml::builder::regmark peak = g.peak();
		m_puml->log(util::string_format("PUML: block %s needs more registers than PUML has (I%d F%d P%d), drawing with C++\n", b.name, peak.i, peak.f, peak.p));
		return false;
	}
	return true;
}

// The compiled block for a state, compiling it if need be; nullptr draws with
// the C++ path.  Called on the emulation thread only.
const puml::compiled *ati_rage128_device::puml_block(const raster_state &rs)
{
	if (!PUML_ENABLE || !m_puml || (VERBOSE & LOG_PROBE))
		return nullptr;

	const std::string key = puml_key(rs);
	if (const puml::compiled *code = m_puml->find(key))
		return code;
	if (m_puml_refused.count(key))
		return nullptr;

	// a full cache is flushed once nothing that might use it is queued or rendering
	if (m_puml->full())
	{
		wait_for_render();
		m_puml->flush();
		m_puml_refused.clear();
		LOG("PUML: block cache flushed\n");
	}

	puml::block b;
	b.name = util::string_format("rage128_%u", u32(m_puml->statistics().compiled + m_puml->statistics().refused));
	const puml::compiled *code = puml_generate(rs, b) ? m_puml->add(key, std::move(b)) : nullptr;
	if (!code)
		m_puml_refused.insert(key);
	return code;
}

// Runs one pixel through the block and through the C++ path, keeps the C++
// result and records any difference in the bytes either may write.
void ati_rage128_device::puml_verify_pixel(const puml::compiled &code, puml::ctx &pc, puml::frame &pf, const raster_state &rs,
		int x, int y, const std::function<void ()> &cpp)
{
	// what the pixel may write: its colour bytes and the depth/stencil word
	u32 addr[8];
	int count = 0;
	if (rs.dst_bpp)
	{
		const u32 xb = u32(x) * rs.dst_bpp;
		for (int k = 0; k < rs.dst_bpp; k++)
			addr[count++] = (rs.dst_base + surface_offset(xb + k, u32(y), rs.dst_pitch, rs.dst_tiled)) & m_vram_mask;
	}
	if (rs.z_test || rs.stencil)
	{
		const u32 z = ((rs.z_base + surface_offset(u32(x) * rs.z_bytes, u32(y), rs.z_pitch, rs.z_tiled)) & m_vram_mask) & ~3U;
		for (int k = 0; k < 4; k++)
			addr[count++] = (z + k) & m_vram_mask;
	}
	u8 before[8], jit[8], ref[8];
	for (int n = 0; n < count; n++)
		before[n] = m_vram[addr[n]];

	pc.x0 = x;
	pc.x1 = x + 1;
	code.run(pc, pf);
	// (the colour and depth bytes may overlap, so read everything before restoring)
	for (int n = 0; n < count; n++)
		jit[n] = m_vram[addr[n]];
	for (int n = count - 1; n >= 0; n--)
		m_vram[addr[n]] = before[n];
	cpp();
	for (int n = 0; n < count; n++)
		ref[n] = m_vram[addr[n]];

	if (std::memcmp(jit, ref, count))
	{
		const u32 seen = m_puml_mismatches.fetch_add(1, std::memory_order_relaxed);
		if (seen < PUML_VERIFY_LOG_LIMIT)
		{
			std::string text = util::string_format("PUML verify: %s mismatch at %d,%d:", code.backend_name, x, y);
			for (int n = 0; n < count; n++)
				text += util::string_format(" %06x:%02x/%02x", addr[n], jit[n], ref[n]);
			std::lock_guard<std::mutex> lock(m_probe_lock);
			m_puml_verify_log.push_back(std::move(text));
		}
	}
}

// Only this band's rows are drawn: rows go to the bands in 16-line stripes.
void ati_rage128_device::draw_triangle(const raster_state &rs, const vertex3d &v0, const vertex3d &v1, const vertex3d &v2, int band, int bands, const puml::compiled *block)
{
	const float ex1 = v1.x - v0.x, ey1 = v1.y - v0.y;
	const float ex2 = v2.x - v0.x, ey2 = v2.y - v0.y;
	const float area = ex1 * ey2 - ex2 * ey1;
	if (!(area != 0.0f) || !std::isfinite(area))
		return;

	// face culling and fill mode (PM4_VC_FPU_SETUP: front is CCW with bit 0 set,
	// back and front face functions in bits 2:1 and 4:3, bit 18 draws every face solid)
	const bool ccw_front = BIT(rs.fpu_setup, 0);
	const bool front = ccw_front ? (area < 0.0f) : (area > 0.0f);
	const int face_fn = BIT(rs.fpu_setup, 18) ? 3 : (front ? BIT(rs.fpu_setup, 3, 2) : BIT(rs.fpu_setup, 1, 2));
	switch (face_fn)
	{
	case 0:
		return;
	case 1:
		draw_point(rs, v0, band, bands);
		draw_point(rs, v1, band, bands);
		draw_point(rs, v2, band, bands);
		return;
	case 2:
		draw_line3d(rs, v0, v1, band, bands);
		draw_line3d(rs, v1, v2, band, bands);
		draw_line3d(rs, v2, v0, band, bands);
		return;
	}

	// flat colour source; the fog factor is still interpolated
	vertex3d flat = (rs.flat_vertex == 1) ? v1 : (rs.flat_vertex == 2) ? v2 : v0;
	if (rs.shade == SHADE_SOLID)
		apply_solid_color(rs, flat);
	const bool gouraud = rs.shade == SHADE_GOURAUD;

	// Every parameter is a plane a(x, y) = base + dx * x + dy * y.  With
	// perspective correction the texture coordinates are interpolated
	// multiplied by RHW (Q), by the setup engine or already by the driver,
	// and divided again per pixel.
	enum { P_Z, P_R, P_G, P_B, P_A, P_SR, P_SG, P_SB, P_SF, P_Q, P_S0, P_T0, P_S1, P_T1, P_COUNT };
	const bool persp = rs.textured && rs.perspective;
	auto load = [&] (const vertex3d &v, float *a) {
		const vertex3d &c = gouraud ? v : flat;
		const float q = persp ? v.w : 1.0f;
		const float m = (persp && rs.premultiply) ? q : 1.0f;
		a[P_Z] = v.z;
		a[P_R] = c.r; a[P_G] = c.g; a[P_B] = c.b; a[P_A] = c.a;
		a[P_SR] = c.sr; a[P_SG] = c.sg; a[P_SB] = c.sb; a[P_SF] = v.sf;
		a[P_Q] = q;
		a[P_S0] = v.s0 * m; a[P_T0] = v.t0 * m;
		a[P_S1] = v.s1 * m; a[P_T1] = v.t1 * m;
	};
	float a0[P_COUNT], a1[P_COUNT], a2[P_COUNT];
	load(v0, a0);
	load(v1, a1);
	load(v2, a2);

	const float inv_area = 1.0f / area;
	float base[P_COUNT], dx[P_COUNT], dy[P_COUNT];
	for (int i = 0; i < P_COUNT; i++)
	{
		const float d1 = a1[i] - a0[i], d2 = a2[i] - a0[i];
		dx[i] = (d1 * ey2 - d2 * ey1) * inv_area;
		dy[i] = (d2 * ex1 - d1 * ex2) * inv_area;
		base[i] = a0[i] - dx[i] * v0.x - dy[i] * v0.y;
	}

	// Edge functions, positive inside.  Each edge is evaluated with its end points
	// in a fixed order and the sign flipped as needed, so that triangles sharing
	// an edge get bit-identical values and the top-left rule (an edge owns pixel
	// centres exactly on it if it is a top or left edge) gives each pixel to
	// exactly one of them.  a, b and c are the same function scaled by 1/area,
	// only used to find each row's span.
	struct edge_t
	{
		float a, b, c;
		float x0, y0, dxe, dye;
		bool negate, owns;

		float eval(float px, float py) const
		{
			const float v = dxe * (py - y0) - dye * (px - x0);
			return negate ? -v : v;
		}
	};
	auto make_edge = [area, inv_area] (const vertex3d &p, const vertex3d &q) {
		const float dxe = q.x - p.x, dye = q.y - p.y;
		edge_t e;
		e.a = -dye * inv_area;
		e.b = dxe * inv_area;
		e.c = (dye * p.x - dxe * p.y) * inv_area;
		e.owns = (area > 0) ? ((dye < 0) || ((dye == 0) && (dxe > 0))) : ((dye > 0) || ((dye == 0) && (dxe < 0)));
		const bool swap = (q.y < p.y) || ((q.y == p.y) && (q.x < p.x));
		const vertex3d &s = swap ? q : p, &t = swap ? p : q;
		e.x0 = s.x;
		e.y0 = s.y;
		e.dxe = t.x - s.x;
		e.dye = t.y - s.y;
		e.negate = swap != (area < 0);
		return e;
	};
	const edge_t edges[3] = { make_edge(v1, v2), make_edge(v2, v0), make_edge(v0, v1) };

	const int miny = std::max(rs.sc_top, int(std::floor(std::min({ v0.y, v1.y, v2.y }))));
	const int maxy = std::min(rs.sc_bottom, int(std::ceil(std::max({ v0.y, v1.y, v2.y }))));
	const int bminx = std::max(rs.sc_left, int(std::floor(std::min({ v0.x, v1.x, v2.x }))));
	const int bmaxx = std::min(rs.sc_right, int(std::ceil(std::max({ v0.x, v1.x, v2.x }))));
	if ((miny > maxy) || (bminx > bmaxx))
		return;

	// level of detail, for units whose filters depend on it
	const bool lod_unit[2] = { rs.tex[0].enabled && rs.tex[0].need_lod, rs.tex[1].enabled && rs.tex[1].need_lod };
	const int s_param[2] = { P_S0, rs.tex[1].sec_st ? P_S1 : P_S0 };
	const int t_param[2] = { P_T0, rs.tex[1].sec_st ? P_T1 : P_T0 };

	float row[P_COUNT];
	float val[P_COUNT];
	vertex3d p;

	// one pixel through the C++ pipeline
	auto shade_cpp = [&] (int x, int y, float px, float py) {
		for (int i = 0; i < P_COUNT; i++)
			val[i] = row[i] + dx[i] * px;

		p.x = px;
		p.y = py;
		p.z = val[P_Z];
		p.w = val[P_Q];
		p.r = val[P_R]; p.g = val[P_G]; p.b = val[P_B]; p.a = val[P_A];
		p.sr = val[P_SR]; p.sg = val[P_SG]; p.sb = val[P_SB]; p.sf = val[P_SF];

		float lod[2] = { 0.0f, 0.0f };
		if (rs.textured)
		{
			const float iq = persp ? ((val[P_Q] != 0.0f) ? (1.0f / val[P_Q]) : 0.0f) : 1.0f;
			p.s0 = val[P_S0] * iq; p.t0 = val[P_T0] * iq;
			p.s1 = val[P_S1] * iq; p.t1 = val[P_T1] * iq;

			for (int unit = 0; unit < 2; unit++)
			{
				if (!lod_unit[unit])
					continue;
				const int sp = s_param[unit], tp = t_param[unit];

				// d(S/Q) = (dS - (S/Q) dQ) / Q; without perspective Q is 1 and dQ 0
				const float s = val[sp] * iq, t = val[tp] * iq;
				const float w = float(1 << rs.tex[unit].width_l2), h = float(1 << rs.tex[unit].height_l2);
				const float dudx = (dx[sp] - s * dx[P_Q]) * iq * w, dvdx = (dx[tp] - t * dx[P_Q]) * iq * h;
				const float dudy = (dy[sp] - s * dy[P_Q]) * iq * w, dvdy = (dy[tp] - t * dy[P_Q]) * iq * h;
				const float rho2 = std::max(dudx * dudx + dvdx * dvdx, dudy * dudy + dvdy * dvdy);
				lod[unit] = ((rho2 > 0.0f) && std::isfinite(rho2)) ? (0.5f * std::log2(rho2)) : 0.0f;
			}
		}

		shade_pixel(rs, x, y, p, lod, true);
	};

	// With a PUML block the covered pixels of each row are handed over a run at
	// a time; the block repeats the per-pixel work above.
	static_assert((puml_tri::PLANES == int(P_COUNT)) && (int(PL_COUNT) == int(P_COUNT)) && (int(PL_Q) == int(P_Q)) && (int(PL_T1) == int(P_T1)));
	puml_tri tri;
	puml::ctx pc{};
	puml::frame pf{};
	if (block)
	{
		std::copy_n(dx, P_COUNT, tri.dx);
		std::copy_n(dy, P_COUNT, tri.dy);
		for (int unit = 0; unit < 2; unit++)
		{
			tri.tex_w[unit] = rs.tex[unit].enabled ? float(1 << rs.tex[unit].width_l2) : 1.0f;
			tri.tex_h[unit] = rs.tex[unit].enabled ? float(1 << rs.tex[unit].height_l2) : 1.0f;
		}
		pc.base[RGN_VRAM] = m_vram;
		pc.mask[RGN_VRAM] = m_vram_mask;
		pc.base[RGN_STATE] = const_cast<u8 *>(reinterpret_cast<const u8 *>(&rs));
		pc.base[RGN_TRI] = reinterpret_cast<u8 *>(&tri);
		pc.base[RGN_STIPPLE] = const_cast<u8 *>(reinterpret_cast<const u8 *>(rs.stipple_rows));
		pc.mask[RGN_STIPPLE] = sizeof(rs.stipple_rows) - 1;
		pc.user = this;
		pc.thread = u32(band);
	}

	for (int y = miny; y <= maxy; y++)
	{
		// skip to this band's next stripe
		if (((y >> 4) % bands) != band)
		{
			y |= 15;
			continue;
		}
		const float py = float(y) + 0.5f;

		// the span where every edge function is non-negative, widened by a pixel
		// each side so that the exact test below decides the boundary pixels
		float lo = float(bminx), hi = float(bmaxx) + 1.0f;
		for (const edge_t &e : edges)
		{
			const float k = e.b * py + e.c;
			if (e.a > 0.0f)
				lo = std::max(lo, -k / e.a);
			else if (e.a < 0.0f)
				hi = std::min(hi, -k / e.a);
		}
		if (!(lo <= hi + 2.0f))
			continue;
		const int minx = std::max(bminx, int(std::floor(lo - 0.5f)) - 1);
		const int maxx = std::min(bmaxx, int(std::ceil(hi - 0.5f)) + 1);
		if (minx > maxx)
			continue;

		for (int i = 0; i < P_COUNT; i++)
			row[i] = base[i] + dy[i] * py;
		if (block)
		{
			std::copy_n(row, P_COUNT, tri.row);
			pc.y = y;
		}

		int run = 0;
		bool in_run = false;
		for (int x = minx; x <= maxx + (block ? 1 : 0); x++)
		{
			const float px = float(x) + 0.5f;
			bool inside = x <= maxx;
			for (const edge_t &e : edges)
			{
				if (!inside)
					break;
				const float w = e.eval(px, py);
				if ((w < 0.0f) || ((w == 0.0f) && !e.owns))
					inside = false;
			}

			if (block)
			{
				if (inside && !in_run)
				{
					run = x;
					in_run = true;
				}
				else if (!inside && in_run)
				{
					in_run = false;
					if (PUML_VERIFY)
					{
						for (int vx = run; vx < x; vx++)
							puml_verify_pixel(*block, pc, pf, rs, vx, y, [&] { shade_cpp(vx, y, float(vx) + 0.5f, py); });
					}
					else
					{
						pc.x0 = run;
						pc.x1 = x;
						block->run(pc, pf);
					}
				}
			}
			else if (inside)
				shade_cpp(x, y, px, py);
		}
	}
}

// solid shading: CONSTANT_COLOR_C, no specular
void ati_rage128_device::apply_solid_color(const raster_state &rs, vertex3d &v)
{
	const u32 c = rs.solid_color;
	v.a = float(c >> 24);
	v.r = float((c >> 16) & 0xff);
	v.g = float((c >> 8) & 0xff);
	v.b = float(c & 0xff);
	v.sr = v.sg = v.sb = 0.0f;
}

// points and lines have no perspective to correct, but coordinates the driver
// multiplied by RHW still need dividing
void ati_rage128_device::direct_st(const raster_state &rs, vertex3d &v)
{
	if (rs.textured && rs.perspective && !rs.premultiply && (v.w != 0.0f))
	{
		const float iw = 1.0f / v.w;
		v.s0 *= iw; v.t0 *= iw;
		v.s1 *= iw; v.t1 *= iw;
	}
}

void ati_rage128_device::draw_point(const raster_state &rs, const vertex3d &v, int band, int bands)
{
	const int y = int(std::floor(v.y));
	if ((y < 0) || (((y >> 4) % bands) != band))
		return;
	const float lod[2] = { 0.0f, 0.0f };
	vertex3d p = v;
	direct_st(rs, p);
	if (rs.shade == SHADE_SOLID)
		apply_solid_color(rs, p);
	shade_pixel(rs, int(std::floor(v.x)), y, p, lod, false);
}

void ati_rage128_device::draw_line3d(const raster_state &rs, const vertex3d &v0, const vertex3d &v1, int band, int bands)
{
	const float lod[2] = { 0.0f, 0.0f };
	const float dx = v1.x - v0.x, dy = v1.y - v0.y;
	const float len = std::max(std::fabs(dx), std::fabs(dy));
	if (!std::isfinite(len))
		return;
	const int steps = std::clamp(int(std::ceil(len)), 1, 8192);
	for (int i = 0; i < steps; i++)
	{
		const float f = float(i) / float(steps);
		vertex3d p;
		p.x = v0.x + dx * f;
		p.y = v0.y + dy * f;
		p.z = v0.z + (v1.z - v0.z) * f;
		p.w = v0.w + (v1.w - v0.w) * f;
		p.r = v0.r + (v1.r - v0.r) * f;
		p.g = v0.g + (v1.g - v0.g) * f;
		p.b = v0.b + (v1.b - v0.b) * f;
		p.a = v0.a + (v1.a - v0.a) * f;
		p.sr = v0.sr + (v1.sr - v0.sr) * f;
		p.sg = v0.sg + (v1.sg - v0.sg) * f;
		p.sb = v0.sb + (v1.sb - v0.sb) * f;
		p.sf = v0.sf + (v1.sf - v0.sf) * f;
		p.s0 = v0.s0 + (v1.s0 - v0.s0) * f;
		p.t0 = v0.t0 + (v1.t0 - v0.t0) * f;
		p.s1 = v0.s1 + (v1.s1 - v0.s1) * f;
		p.t1 = v0.t1 + (v1.t1 - v0.t1) * f;
		const int y = int(std::floor(p.y));
		if ((y < 0) || (((y >> 4) % bands) != band))
			continue;
		direct_st(rs, p);
		if (rs.shade == SHADE_SOLID)
			apply_solid_color(rs, p);
		shade_pixel(rs, int(std::floor(p.x)), y, p, lod, false);
	}
}
