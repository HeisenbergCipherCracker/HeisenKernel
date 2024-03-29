#include <fudge.h>
#include <abi.h>
#include "audio.h"
#include "video.h"
#include "cpu.h"

/* Interrupt masks */
#define VBLANK_INTR                     0x01
#define LCDC_INTR                       0x02
#define TIMER_INTR                      0x04
#define SERIAL_INTR                     0x08
#define CONTROL_INTR                    0x10
#define ANY_INTR                        0x1F

/* Memory addresses */
#define ROM_0_ADDR                      0x0000
#define ROM_N_ADDR                      0x4000
#define VRAM_ADDR                       0x8000
#define CART_RAM_ADDR                   0xA000
#define WRAM_0_ADDR                     0xC000
#define WRAM_1_ADDR                     0xD000
#define ECHO_ADDR                       0xE000
#define OAM_ADDR                        0xFE00
#define UNUSED_ADDR                     0xFEA0
#define IO_ADDR                         0xFF00
#define HRAM_ADDR                       0xFF80
#define INTR_EN_ADDR                    0xFFFF

/* Cart section sizes */
#define ROM_BANK_SIZE                   0x4000
#define WRAM_BANK_SIZE                  0x1000
#define CRAM_BANK_SIZE                  0x2000
#define VRAM_BANK_SIZE                  0x2000

/* DIV Register is incremented at rate of 16384Hz.
 * 4194304 / 16384 = 256 clock cycles for one increment. */
#define DIV_CYCLES                      256

/* Serial clock locked to 8192Hz on DMG.
 * 4194304 / (8192 / 8) = 4096 clock cycles for sending 1 byte. */
#define SERIAL_CYCLES                   4096

/* Calculating VSYNC. */
#define DMG_CLOCK_FREQ                  4194304.0
#define SCREEN_REFRESH_CYCLES           70224.0
#define VERTICAL_SYNC                   (DMG_CLOCK_FREQ/SCREEN_REFRESH_CYCLES)

/* STAT register masks */
#define STAT_LYC_INTR                   0x40
#define STAT_MODE_2_INTR                0x20
#define STAT_MODE_1_INTR                0x10
#define STAT_MODE_0_INTR                0x08
#define STAT_LYC_COINC                  0x04
#define STAT_MODE                       0x03
#define STAT_USER_BITS                  0xF8

/* LCD characteristics */
#define LCD_LINE_CYCLES                 456
#define LCD_MODE_0_CYCLES               0
#define LCD_MODE_2_CYCLES               204
#define LCD_MODE_3_CYCLES               284
#define LCD_VERT_LINES                  154

/* LCDC control masks */
#define LCDC_ENABLE                     0x80
#define LCDC_WINDOW_MAP                 0x40
#define LCDC_WINDOW_ENABLE              0x20
#define LCDC_TILE_SELECT                0x10
#define LCDC_BG_MAP                     0x08
#define LCDC_OBJ_SIZE                   0x04
#define LCDC_OBJ_ENABLE                 0x02
#define LCDC_BG_ENABLE                  0x01

/* VRAM Locations */
#define VRAM_TILES_1                    (0x8000 - VRAM_ADDR)
#define VRAM_TILES_2                    (0x8800 - VRAM_ADDR)
#define VRAM_BMAP_1                     (0x9800 - VRAM_ADDR)
#define VRAM_BMAP_2                     (0x9C00 - VRAM_ADDR)
#define VRAM_TILES_3                    (0x8000 - VRAM_ADDR + VRAM_BANK_SIZE)
#define VRAM_TILES_4                    (0x8800 - VRAM_ADDR + VRAM_BANK_SIZE)

/* Interrupt jump addresses */
#define VBLANK_INTR_ADDR                0x0040
#define LCDC_INTR_ADDR                  0x0048
#define TIMER_INTR_ADDR                 0x0050
#define SERIAL_INTR_ADDR                0x0058
#define CONTROL_INTR_ADDR               0x0060

/* SPRITE controls */
#define NUM_SPRITES                     0x28
#define MAX_SPRITES_LINE                0x0A
#define OBJ_PRIORITY                    0x80
#define OBJ_FLIP_Y                      0x40
#define OBJ_FLIP_X                      0x20
#define OBJ_PALETTE                     0x10

#define ROM_HEADER_CHECKSUM_LOC         0x014D

#ifndef MIN
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#endif

void gb_tick_rtc(struct gb_s *gb)
{

    if ((gb->cart_rtc[4] & 0x40) == 0)
    {

        if (++gb->rtc_bits.sec == 60)
        {

            gb->rtc_bits.sec = 0;

            if (++gb->rtc_bits.min == 60)
            {

                gb->rtc_bits.min = 0;

                if (++gb->rtc_bits.hour == 24)
                {

                    gb->rtc_bits.hour = 0;

                    if (++gb->rtc_bits.yday == 0)
                    {

                        if (gb->rtc_bits.high & 1)
                            gb->rtc_bits.high |= 0x80;

                        gb->rtc_bits.high ^= 1;

                    }

                }

            }

        }

    }

}

void gb_set_rtc(struct gb_s *gb, const struct tm * const time)
{

    gb->cart_rtc[0] = time->tm_sec;
    gb->cart_rtc[1] = time->tm_min;
    gb->cart_rtc[2] = time->tm_hour;
    gb->cart_rtc[3] = time->tm_yday & 0xFF;
    gb->cart_rtc[4] = time->tm_yday >> 8;

}

static unsigned char __gb_read(struct gb_s *gb, const unsigned int addr)
{

    switch (addr >> 12)
    {

    case 0x0:
    case 0x1:
    case 0x2:
    case 0x3:
        return gb->gb_rom_read(gb, addr);

    case 0x4:
    case 0x5:
    case 0x6:
    case 0x7:
        if (gb->mbc == 1 && gb->cart_mode_select)
            return gb->gb_rom_read(gb, addr + ((gb->selected_rom_bank & 0x1F) - 1) * ROM_BANK_SIZE);
        else
            return gb->gb_rom_read(gb, addr + (gb->selected_rom_bank - 1) * ROM_BANK_SIZE);

    case 0x8:
    case 0x9:
        return gb->vram[addr - VRAM_ADDR];

    case 0xA:
    case 0xB:
        if (gb->cart_ram && gb->enable_cart_ram)
        {

            if (gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
                return gb->cart_rtc[gb->cart_ram_bank - 0x08];
            else if ((gb->cart_mode_select || gb->mbc != 1) && gb->cart_ram_bank < gb->num_ram_banks)
                return gb->gb_cart_ram_read(gb, addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE));
            else
                return gb->gb_cart_ram_read(gb, addr - CART_RAM_ADDR);

        }

        return 0;

    case 0xC:
        return gb->wram[addr - WRAM_0_ADDR];

    case 0xD:
        return gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE];

    case 0xE:
        return gb->wram[addr - ECHO_ADDR];

    case 0xF:
        if (addr < OAM_ADDR)
            return gb->wram[addr - ECHO_ADDR];

        if (addr < UNUSED_ADDR)
            return gb->oam[addr - OAM_ADDR];

        if (addr < IO_ADDR)
            return 0xFF;

        if (HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
            return gb->hram[addr - HRAM_ADDR];

        if ((addr >= 0xFF10) && (addr <= 0xFF3F))
#if ENABLE_AUDIO
            return audio_read(addr);
#else
            return 1;
#endif

        switch (addr & 0xFF)
        {

        case 0x00:
            return 0xC0 | gb->gb_reg.P1;

        case 0x01:
            return gb->gb_reg.SB;

        case 0x02:
            return gb->gb_reg.SC;

        case 0x04:
            return gb->gb_reg.DIV;

        case 0x05:
            return gb->gb_reg.TIMA;

        case 0x06:
            return gb->gb_reg.TMA;

        case 0x07:
            return gb->gb_reg.TAC;

        case 0x0F:
            return gb->gb_reg.IF;

        case 0x40:
            return gb->gb_reg.LCDC;

        case 0x41:
            return (gb->gb_reg.STAT & STAT_USER_BITS) | (gb->gb_reg.LCDC & LCDC_ENABLE ? gb->lcd_mode : LCD_VBLANK);

        case 0x42:
            return gb->gb_reg.SCY;

        case 0x43:
            return gb->gb_reg.SCX;

        case 0x44:
            return gb->gb_reg.LY;

        case 0x45:
            return gb->gb_reg.LYC;

        case 0x46:
            return gb->gb_reg.DMA;

        case 0x47:
            return gb->gb_reg.BGP;

        case 0x48:
            return gb->gb_reg.OBP0;

        case 0x49:
            return gb->gb_reg.OBP1;

        case 0x4A:
            return gb->gb_reg.WY;

        case 0x4B:
            return gb->gb_reg.WX;

        case 0xFF:
            return gb->gb_reg.IE;

        default:
            return 0xFF;

        }

    }

    gb->gb_error(gb, GB_INVALID_READ, addr);

    return 0xFF;

}

static void __gb_write(struct gb_s *gb, const unsigned int addr, const unsigned char val)
{

    unsigned char i;

    switch (addr >> 12)
    {

    case 0x0:
    case 0x1:
        if (gb->mbc == 2 && addr & 0x10)
            return;
        else if (gb->mbc > 0 && gb->cart_ram)
            gb->enable_cart_ram = ((val & 0x0F) == 0x0A);

        return;

    case 0x2:
        if (gb->mbc == 5)
        {

            gb->selected_rom_bank = (gb->selected_rom_bank & 0x100) | val;
            gb->selected_rom_bank = gb->selected_rom_bank % gb->num_rom_banks;

            return;

        }

    case 0x3:
        if (gb->mbc == 1)
        {

            gb->selected_rom_bank = (val & 0x1F) | (gb->selected_rom_bank & 0x60);

            if ((gb->selected_rom_bank & 0x1F) == 0x00)
                gb->selected_rom_bank++;

        }

        else if (gb->mbc == 2 && addr & 0x10)
        {

            gb->selected_rom_bank = val & 0x0F;

            if (!gb->selected_rom_bank)
                gb->selected_rom_bank++;

        }

        else if (gb->mbc == 3)
        {

            gb->selected_rom_bank = val & 0x7F;

            if (!gb->selected_rom_bank)
                gb->selected_rom_bank++;

        }

        else if (gb->mbc == 5)
        {

            gb->selected_rom_bank = (val & 0x01) << 8 | (gb->selected_rom_bank & 0xFF);

        }

        gb->selected_rom_bank = gb->selected_rom_bank % gb->num_rom_banks;

        return;

    case 0x4:
    case 0x5:
        if (gb->mbc == 1)
        {

            gb->cart_ram_bank = (val & 3);
            gb->selected_rom_bank = ((val & 3) << 5) | (gb->selected_rom_bank & 0x1F);
            gb->selected_rom_bank = gb->selected_rom_bank % gb->num_rom_banks;

        }

        else if (gb->mbc == 3)
        {

            gb->cart_ram_bank = val;

        }

        else if (gb->mbc == 5)
        {

            gb->cart_ram_bank = (val & 0x0F);

        }

        return;

    case 0x6:
    case 0x7:
        gb->cart_mode_select = (val & 1);

        return;

    case 0x8:
    case 0x9:
        gb->vram[addr - VRAM_ADDR] = val;

        return;

    case 0xA:
    case 0xB:
        if (gb->cart_ram && gb->enable_cart_ram)
        {

            if (gb->mbc == 3 && gb->cart_ram_bank >= 0x08)
                gb->cart_rtc[gb->cart_ram_bank - 0x08] = val;
            else if (gb->cart_mode_select && gb->cart_ram_bank < gb->num_ram_banks)
                gb->gb_cart_ram_write(gb, addr - CART_RAM_ADDR + (gb->cart_ram_bank * CRAM_BANK_SIZE), val);
            else if (gb->num_ram_banks)
                gb->gb_cart_ram_write(gb, addr - CART_RAM_ADDR, val);

        }

        return;

    case 0xC:
        gb->wram[addr - WRAM_0_ADDR] = val;

        return;

    case 0xD:
        gb->wram[addr - WRAM_1_ADDR + WRAM_BANK_SIZE] = val;

        return;

    case 0xE:
        gb->wram[addr - ECHO_ADDR] = val;

        return;

    case 0xF:
        if (addr < OAM_ADDR)
        {

            gb->wram[addr - ECHO_ADDR] = val;

            return;

        }

        if (addr < UNUSED_ADDR)
        {

            gb->oam[addr - OAM_ADDR] = val;

            return;

        }

        if (addr < IO_ADDR)
            return;

        if (HRAM_ADDR <= addr && addr < INTR_EN_ADDR)
        {

            gb->hram[addr - HRAM_ADDR] = val;

            return;

        }

        if ((addr >= 0xFF10) && (addr <= 0xFF3F))
        {

#if ENABLE_AUDIO
            audio_write(addr, val);
#endif

            return;

        }

        switch (addr & 0xFF)
        {

        case 0x00:
            gb->gb_reg.P1 = val;

            if ((gb->gb_reg.P1 & 0b010000) == 0)
                gb->gb_reg.P1 |= (gb->direct.joypad >> 4);
            else
                gb->gb_reg.P1 |= (gb->direct.joypad & 0x0F);

            return;

        case 0x01:
            gb->gb_reg.SB = val;

            return;

        case 0x02:
            gb->gb_reg.SC = val;

            return;

        case 0x04:
            gb->gb_reg.DIV = 0x00;

            return;

        case 0x05:
            gb->gb_reg.TIMA = val;

            return;

        case 0x06:
            gb->gb_reg.TMA = val;

            return;

        case 0x07:
            gb->gb_reg.TAC = val;

            return;

        case 0x0F:
            gb->gb_reg.IF = (val | 0b11100000);

            return;

        case 0x40:
            gb->gb_reg.LCDC = val;

            if ((gb->gb_reg.LCDC & LCDC_ENABLE) == 0)
            {

                if (gb->lcd_mode != LCD_VBLANK)
                {

                    gb->gb_reg.LCDC |= LCDC_ENABLE;

                    return;

                }

                gb->gb_reg.STAT = (gb->gb_reg.STAT & ~0x03) | LCD_VBLANK;
                gb->gb_reg.LY = 0;
                gb->counter.lcd_count = 0;

            }

            return;

        case 0x41:
            gb->gb_reg.STAT = (val & 0b01111000);

            return;

        case 0x42:
            gb->gb_reg.SCY = val;

            return;

        case 0x43:
            gb->gb_reg.SCX = val;

            return;

        case 0x45:
            gb->gb_reg.LYC = val;

            return;

        case 0x46:
            gb->gb_reg.DMA = (val % 0xF1);

            for (i = 0; i < OAM_SIZE; i++)
                gb->oam[i] = __gb_read(gb, (gb->gb_reg.DMA << 8) + i);

            return;

        case 0x47:
            gb->gb_reg.BGP = val;
            gb->display.bg_palette[0] = (gb->gb_reg.BGP & 0x03);
            gb->display.bg_palette[1] = (gb->gb_reg.BGP >> 2) & 0x03;
            gb->display.bg_palette[2] = (gb->gb_reg.BGP >> 4) & 0x03;
            gb->display.bg_palette[3] = (gb->gb_reg.BGP >> 6) & 0x03;

            return;

        case 0x48:
            gb->gb_reg.OBP0 = val;
            gb->display.sp_palette[0] = (gb->gb_reg.OBP0 & 0x03);
            gb->display.sp_palette[1] = (gb->gb_reg.OBP0 >> 2) & 0x03;
            gb->display.sp_palette[2] = (gb->gb_reg.OBP0 >> 4) & 0x03;
            gb->display.sp_palette[3] = (gb->gb_reg.OBP0 >> 6) & 0x03;

            return;

        case 0x49:
            gb->gb_reg.OBP1 = val;
            gb->display.sp_palette[4] = (gb->gb_reg.OBP1 & 0x03);
            gb->display.sp_palette[5] = (gb->gb_reg.OBP1 >> 2) & 0x03;
            gb->display.sp_palette[6] = (gb->gb_reg.OBP1 >> 4) & 0x03;
            gb->display.sp_palette[7] = (gb->gb_reg.OBP1 >> 6) & 0x03;

            return;

        case 0x4A:
            gb->gb_reg.WY = val;

            return;

        case 0x4B:
            gb->gb_reg.WX = val;

            return;

        case 0x50:
            gb->gb_bios_enable = 0;

            return;

        case 0xFF:
            gb->gb_reg.IE = val;

            return;

        }

    }

    gb->gb_error(gb, GB_INVALID_WRITE, addr);

}

static void __gb_execute_cb(struct gb_s *gb)
{

    unsigned char cbop = __gb_read(gb, gb->cpu_reg.pc++);
    unsigned char r = (cbop & 0x7);
    unsigned char b = (cbop >> 3) & 0x7;
    unsigned char d = (cbop >> 3) & 0x1;
    unsigned char val;
    unsigned char writeback = 1;

    switch (cbop & 0x0F)
    {

    case 0x06:
    case 0x0E:
        gb->counter.div_count += 8;

        break;

    }

    switch (r)
    {

    case 0:
        val = gb->cpu_reg.b;

        break;

    case 1:
        val = gb->cpu_reg.c;

        break;

    case 2:
        val = gb->cpu_reg.d;

        break;

    case 3:
        val = gb->cpu_reg.e;

        break;

    case 4:
        val = gb->cpu_reg.h;

        break;

    case 5:
        val = gb->cpu_reg.l;

        break;

    case 6:
        val = __gb_read(gb, gb->cpu_reg.hl);

        break;

    default:
        val = gb->cpu_reg.a;

        break;

    }

    switch (cbop >> 6)
    {

    case 0x0:
        cbop = (cbop >> 4) & 0x3;

        switch (cbop)
        {

        case 0x0: /* RdC R */
        case 0x1: /* Rd R */
            if (d) /* RRC R / RR R */
            {

                unsigned char temp = val;

                val = (val >> 1);
                val |= cbop ? (gb->cpu_reg.f_bits.c << 7) : (temp << 7);
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
                gb->cpu_reg.f_bits.c = (temp & 0x01);

            }

            else /* RLC R / RL R */
            {

                unsigned char temp = val;

                val = (val << 1);
                val |= cbop ? gb->cpu_reg.f_bits.c : (temp >> 7);
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
                gb->cpu_reg.f_bits.c = (temp >> 7);

            }

            break;

        case 0x2:
            if (d) /* SRA R */
            {

                gb->cpu_reg.f_bits.c = val & 0x01;
                val = (val >> 1) | (val & 0x80);
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;

            }

            else /* SLA R */
            {

                gb->cpu_reg.f_bits.c = (val >> 7);
                val = val << 1;
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;

            }

            break;

        case 0x3:
            if (d) /* SRL R */
            {

                gb->cpu_reg.f_bits.c = val & 0x01;
                val = val >> 1;
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;

            }

            else /* SWAP R */
            {

                unsigned char temp = (val >> 4) & 0x0F;

                temp |= (val << 4) & 0xF0;
                val = temp;
                gb->cpu_reg.f_bits.z = (val == 0x00);
                gb->cpu_reg.f_bits.n = 0;
                gb->cpu_reg.f_bits.h = 0;
                gb->cpu_reg.f_bits.c = 0;

            }

            break;

        }

        break;

    case 0x1: /* BIT B, R */
        gb->cpu_reg.f_bits.z = !((val >> b) & 0x1);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        writeback = 0;

        break;

    case 0x2: /* RES B, R */
        val &= (0xFE << b) | (0xFF >> (8 - b));

        break;

    case 0x3: /* SET B, R */
        val |= (0x1 << b);

        break;

    }

    if (writeback)
    {

        switch (r)
        {

        case 0:
            gb->cpu_reg.b = val;

            break;

        case 1:
            gb->cpu_reg.c = val;

            break;

        case 2:
            gb->cpu_reg.d = val;

            break;

        case 3:
            gb->cpu_reg.e = val;

            break;

        case 4:
            gb->cpu_reg.h = val;

            break;

        case 5:
            gb->cpu_reg.l = val;

            break;

        case 6:
            __gb_write(gb, gb->cpu_reg.hl, val);

            break;

        case 7:
            gb->cpu_reg.a = val;

            break;

        }

    }

}

#if ENABLE_VIDEO
static void __gb_draw_line(struct gb_s *gb)
{

    unsigned char pixels[160] = {0};

    if (gb->direct.frame_skip && !gb->display.frame_skip_count)
        return;

    if (gb->direct.interlace)
    {

        if ((gb->display.interlace_count == 0 && (gb->gb_reg.LY & 1) == 0) || (gb->display.interlace_count == 1 && (gb->gb_reg.LY & 1) == 1))
        {

            if (gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE && gb->gb_reg.LY >= gb->display.WY && gb->gb_reg.WX <= 166)
                gb->display.window_clear++;

            return;

        }

    }

    if (gb->gb_reg.LCDC & LCDC_BG_ENABLE)
    {

        const unsigned char bg_y = gb->gb_reg.LY + gb->gb_reg.SCY;
        const unsigned short bg_map = ((gb->gb_reg.LCDC & LCDC_BG_MAP) ? VRAM_BMAP_2 : VRAM_BMAP_1) + (bg_y >> 3) * 0x20;
        unsigned char disp_x = LCD_WIDTH - 1;
        unsigned char bg_x = disp_x + gb->gb_reg.SCX;
        unsigned char idx = gb->vram[bg_map + (bg_x >> 3)];
        const unsigned char py = (bg_y & 0x07);
        unsigned char px = 7 - (bg_x & 0x07);
        unsigned char t1;
        unsigned char t2;
        unsigned short tile;

        if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
            tile = VRAM_TILES_1 + idx * 0x10;
        else
            tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

        tile += 2 * py;
        t1 = gb->vram[tile] >> px;
        t2 = gb->vram[tile + 1] >> px;

        for (; disp_x != 0xFF; disp_x--)
        {

            unsigned char c;

            if (px == 8)
            {

                px = 0;
                bg_x = disp_x + gb->gb_reg.SCX;
                idx = gb->vram[bg_map + (bg_x >> 3)];

                if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
                    tile = VRAM_TILES_1 + idx * 0x10;
                else
                    tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

                tile += 2 * py;
                t1 = gb->vram[tile];
                t2 = gb->vram[tile + 1];

            }

            c = (t1 & 0x1) | ((t2 & 0x1) << 1);
            pixels[disp_x] = gb->display.bg_palette[c];
            pixels[disp_x] |= LCD_PALETTE_BG;
            t1 = t1 >> 1;
            t2 = t2 >> 1;
            px++;

        }

    }

    if (gb->gb_reg.LCDC & LCDC_WINDOW_ENABLE && gb->gb_reg.LY >= gb->display.WY && gb->gb_reg.WX <= 166)
    {

        unsigned short win_line = (gb->gb_reg.LCDC & LCDC_WINDOW_MAP) ? VRAM_BMAP_2 : VRAM_BMAP_1;
        unsigned char disp_x = LCD_WIDTH - 1;
        unsigned char win_x = disp_x - gb->gb_reg.WX + 7;
        unsigned char py = gb->display.window_clear & 0x07;
        unsigned char px = 7 - (win_x & 0x07);
        unsigned char idx;
        unsigned short tile;
        unsigned char t1;
        unsigned char t2;
        unsigned char end;

        idx = gb->vram[win_line + (win_x >> 3)];
        win_line += (gb->display.window_clear >> 3) * 0x20;

        if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
            tile = VRAM_TILES_1 + idx * 0x10;
        else
            tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

        tile += 2 * py;
        t1 = gb->vram[tile] >> px;
        t2 = gb->vram[tile + 1] >> px;
        end = (gb->gb_reg.WX < 7 ? 0 : gb->gb_reg.WX - 7) - 1;

        for (; disp_x != end; disp_x--)
        {

            unsigned char c;

            if (px == 8)
            {

                px = 0;
                win_x = disp_x - gb->gb_reg.WX + 7;
                idx = gb->vram[win_line + (win_x >> 3)];

                if (gb->gb_reg.LCDC & LCDC_TILE_SELECT)
                    tile = VRAM_TILES_1 + idx * 0x10;
                else
                    tile = VRAM_TILES_2 + ((idx + 0x80) % 0x100) * 0x10;

                tile += 2 * py;
                t1 = gb->vram[tile];
                t2 = gb->vram[tile + 1];
            }

            c = (t1 & 0x1) | ((t2 & 0x1) << 1);
            pixels[disp_x] = gb->display.bg_palette[c];
            pixels[disp_x] |= LCD_PALETTE_BG;
            t1 = t1 >> 1;
            t2 = t2 >> 1;
            px++;

        }

        gb->display.window_clear++;

    }

    if (gb->gb_reg.LCDC & LCDC_OBJ_ENABLE)
    {

        unsigned char count = 0;
        unsigned char s;

        for (s = NUM_SPRITES - 1; s != 0xFF; s--)
        {

            unsigned char OY = gb->oam[4 * s + 0];
            unsigned char OX = gb->oam[4 * s + 1];
            unsigned char OT = gb->oam[4 * s + 2] & (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0xFE : 0xFF);
            unsigned char OF = gb->oam[4 * s + 3];
            unsigned char py;
            unsigned char t1;
            unsigned char t2;
            unsigned char dir, start, end, shift;
            unsigned char disp_x;

            if (gb->gb_reg.LY + (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 0 : 8) >= OY || gb->gb_reg.LY + 16 < OY)
                continue;

            count++;

            if (OX == 0 || OX >= 168)
                continue;

            py = gb->gb_reg.LY - OY + 16;

            if (OF & OBJ_FLIP_Y)
                py = (gb->gb_reg.LCDC & LCDC_OBJ_SIZE ? 15 : 7) - py;

            t1 = gb->vram[VRAM_TILES_1 + OT * 0x10 + 2 * py];
            t2 = gb->vram[VRAM_TILES_1 + OT * 0x10 + 2 * py + 1];

            if (OF & OBJ_FLIP_X)
            {

                dir = 1;
                start = (OX < 8 ? 0 : OX - 8);
                end = MIN(OX, LCD_WIDTH);
                shift = 8 - OX + start;

            }

            else
            {

                dir = -1;
                start = MIN(OX, LCD_WIDTH) - 1;
                end = (OX < 8 ? 0 : OX - 8) - 1;
                shift = OX - (start + 1);

            }

            t1 >>= shift;
            t2 >>= shift;

            for (disp_x = start; disp_x != end; disp_x += dir)
            {

                unsigned char c = (t1 & 0x1) | ((t2 & 0x1) << 1);

                if (c && !(OF & OBJ_PRIORITY && pixels[disp_x] & 0x3))
                {

                    pixels[disp_x] = (OF & OBJ_PALETTE) ? gb->display.sp_palette[c + 4] : gb->display.sp_palette[c];
                    pixels[disp_x] |= (OF & OBJ_PALETTE);
                    pixels[disp_x] &= ~LCD_PALETTE_BG;

                }

                t1 = t1 >> 1;
                t2 = t2 >> 1;

            }

        }

    }

    video_drawline(pixels, gb->gb_reg.LY);

}
#endif

static void __gb_step_cpu(struct gb_s *gb)
{

    unsigned char opcode, inst_cycles;
    static const unsigned char op_cycles[0x100] =
    {
        4, 12, 8, 8, 4, 4, 8, 4, 20, 8, 8, 8, 4, 4, 8, 4,
        4, 12, 8, 8, 4, 4, 8, 4, 12, 8, 8, 8, 4, 4, 8, 4,
        8, 12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4,
        8, 12, 8, 8, 12, 12, 12, 4, 8, 8, 8, 8, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4,
        8, 12, 12, 16, 12, 16, 8, 16, 8, 16, 12, 8, 12, 24, 8, 16,
        8, 12, 12, 0, 12, 16, 8, 16, 8, 16, 12, 0, 12, 0, 8, 16,
        12, 12, 8, 0, 0, 16, 8, 16, 16, 4, 16, 0, 0, 0, 8, 16,
        12, 12, 8, 4, 0, 16, 8, 16, 12, 8, 16, 4, 0, 0, 8, 16
    };

    if ((gb->gb_ime || gb->gb_halt) && (gb->gb_reg.IF & gb->gb_reg.IE & ANY_INTR))
    {

        gb->gb_halt = 0;

        if (gb->gb_ime)
        {

            gb->gb_ime = 0;

            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

            if (gb->gb_reg.IF & gb->gb_reg.IE & VBLANK_INTR)
            {

                gb->cpu_reg.pc = VBLANK_INTR_ADDR;
                gb->gb_reg.IF ^= VBLANK_INTR;

            }

            else if (gb->gb_reg.IF & gb->gb_reg.IE & LCDC_INTR)
            {

                gb->cpu_reg.pc = LCDC_INTR_ADDR;
                gb->gb_reg.IF ^= LCDC_INTR;

            }

            else if (gb->gb_reg.IF & gb->gb_reg.IE & TIMER_INTR)
            {

                gb->cpu_reg.pc = TIMER_INTR_ADDR;
                gb->gb_reg.IF ^= TIMER_INTR;

            }

            else if (gb->gb_reg.IF & gb->gb_reg.IE & SERIAL_INTR)
            {

                gb->cpu_reg.pc = SERIAL_INTR_ADDR;
                gb->gb_reg.IF ^= SERIAL_INTR;

            }

            else if (gb->gb_reg.IF & gb->gb_reg.IE & CONTROL_INTR)
            {

                gb->cpu_reg.pc = CONTROL_INTR_ADDR;
                gb->gb_reg.IF ^= CONTROL_INTR;

            }

        }

    }

    opcode = (gb->gb_halt ? 0x00 : __gb_read(gb, gb->cpu_reg.pc++));
    inst_cycles = op_cycles[opcode];

    switch (opcode)
    {

    case 0x00: /* NOP */
        break;

    case 0x01: /* LD BC, imm */
        gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x02: /* LD (BC), A */
        __gb_write(gb, gb->cpu_reg.bc, gb->cpu_reg.a);

        break;

    case 0x03: /* INC BC */
        gb->cpu_reg.bc++;

        break;

    case 0x04: /* INC B */
        gb->cpu_reg.b++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.b == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.b & 0x0F) == 0x00);

        break;

    case 0x05: /* DEC B */
        gb->cpu_reg.b--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.b == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.b & 0x0F) == 0x0F);

        break;

    case 0x06: /* LD B, imm */
        gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x07: /* RLCA */
        gb->cpu_reg.a = (gb->cpu_reg.a << 1) | (gb->cpu_reg.a >> 7);
        gb->cpu_reg.f_bits.z = 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = (gb->cpu_reg.a & 0x01);

        break;

    case 0x08: /* LD (imm), SP */
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
            __gb_write(gb, temp++, gb->cpu_reg.sp & 0xFF);
            __gb_write(gb, temp, gb->cpu_reg.sp >> 8);

        }

        break;

    case 0x09: /* ADD HL, BC */
        {

            unsigned int temp = gb->cpu_reg.hl + gb->cpu_reg.bc;

            gb->cpu_reg.f_bits.n = 0;
            gb->cpu_reg.f_bits.h = (temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.bc) & 0x1000 ? 1 : 0;
            gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
            gb->cpu_reg.hl = (temp & 0x0000FFFF);

        }

        break;

    case 0x0A: /* LD A, (BC) */
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.bc);

        break;

    case 0x0B: /* DEC BC */
        gb->cpu_reg.bc--;

        break;

    case 0x0C: /* INC C */
        gb->cpu_reg.c++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.c == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.c & 0x0F) == 0x00);

        break;

    case 0x0D: /* DEC C */
        gb->cpu_reg.c--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.c == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.c & 0x0F) == 0x0F);

        break;

    case 0x0E: /* LD C, imm */
        gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x0F: /* RRCA */
        gb->cpu_reg.f_bits.c = gb->cpu_reg.a & 0x01;
        gb->cpu_reg.a = (gb->cpu_reg.a >> 1) | (gb->cpu_reg.a << 7);
        gb->cpu_reg.f_bits.z = 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;

        break;

    case 0x10: /* STOP */
        break;

    case 0x11: /* LD DE, imm */
        gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x12: /* LD (DE), A */
        __gb_write(gb, gb->cpu_reg.de, gb->cpu_reg.a);

        break;

    case 0x13: /* INC DE */
        gb->cpu_reg.de++;

        break;

    case 0x14: /* INC D */
        gb->cpu_reg.d++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.d == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.d & 0x0F) == 0x00);

        break;

    case 0x15: /* DEC D */
        gb->cpu_reg.d--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.d == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.d & 0x0F) == 0x0F);

        break;

    case 0x16: /* LD D, imm */
        gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x17: /* RLA */
        {

            unsigned char temp = gb->cpu_reg.a;

            gb->cpu_reg.a = (gb->cpu_reg.a << 1) | gb->cpu_reg.f_bits.c;
            gb->cpu_reg.f_bits.z = 0;
            gb->cpu_reg.f_bits.n = 0;
            gb->cpu_reg.f_bits.h = 0;
            gb->cpu_reg.f_bits.c = (temp >> 7) & 0x01;

        }

        break;

    case 0x18: /* JR imm */
        {

            char temp = __gb_read(gb, gb->cpu_reg.pc++);

            gb->cpu_reg.pc += temp;

        }

        break;

    case 0x19: /* ADD HL, DE */
        {

            unsigned int temp = gb->cpu_reg.hl + gb->cpu_reg.de;

            gb->cpu_reg.f_bits.n = 0;
            gb->cpu_reg.f_bits.h = (temp ^ gb->cpu_reg.hl ^ gb->cpu_reg.de) & 0x1000 ? 1 : 0;
            gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
            gb->cpu_reg.hl = (temp & 0x0000FFFF);

        }

        break;

    case 0x1A: /* LD A, (DE) */
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.de);

        break;

    case 0x1B: /* DEC DE */
        gb->cpu_reg.de--;

        break;

    case 0x1C: /* INC E */
        gb->cpu_reg.e++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.e == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.e & 0x0F) == 0x00);

        break;

    case 0x1D: /* DEC E */
        gb->cpu_reg.e--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.e == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.e & 0x0F) == 0x0F);

        break;

    case 0x1E: /* LD E, imm */
        gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x1F: /* RRA */
        {

            unsigned char temp = gb->cpu_reg.a;

            gb->cpu_reg.a = gb->cpu_reg.a >> 1 | (gb->cpu_reg.f_bits.c << 7);
            gb->cpu_reg.f_bits.z = 0;
            gb->cpu_reg.f_bits.n = 0;
            gb->cpu_reg.f_bits.h = 0;
            gb->cpu_reg.f_bits.c = temp & 0x1;

        }

        break;

    case 0x20: /* JP NZ, imm */
        if (!gb->cpu_reg.f_bits.z)
        {

            char temp = __gb_read(gb, gb->cpu_reg.pc++);

            gb->cpu_reg.pc += temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc++;

        }

        break;

    case 0x21: /* LD HL, imm */
        gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x22: /* LDI (HL), A */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);

        gb->cpu_reg.hl++;

        break;

    case 0x23: /* INC HL */
        gb->cpu_reg.hl++;

        break;

    case 0x24: /* INC H */
        gb->cpu_reg.h++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.h == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.h & 0x0F) == 0x00);

        break;

    case 0x25: /* DEC H */
        gb->cpu_reg.h--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.h == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.h & 0x0F) == 0x0F);

        break;

    case 0x26: /* LD H, imm */
        gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x27: /* DAA */
    {

        unsigned short a = gb->cpu_reg.a;

        if (gb->cpu_reg.f_bits.n)
        {

            if (gb->cpu_reg.f_bits.h)
                a = (a - 0x06) & 0xFF;

            if (gb->cpu_reg.f_bits.c)
                a -= 0x60;

        }

        else
        {

            if (gb->cpu_reg.f_bits.h || (a & 0x0F) > 9)
                a += 0x06;

            if (gb->cpu_reg.f_bits.c || a > 0x9F)
                a += 0x60;

        }

        if ((a & 0x100) == 0x100)
            gb->cpu_reg.f_bits.c = 1;

        gb->cpu_reg.a = a;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0);
        gb->cpu_reg.f_bits.h = 0;

        break;

    }

    case 0x28: /* JP Z, imm */
        if (gb->cpu_reg.f_bits.z)
        {

            char temp = __gb_read(gb, gb->cpu_reg.pc++);

            gb->cpu_reg.pc += temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc++;

        }

        break;

    case 0x29: /* ADD HL, HL */
    {

        unsigned int temp = gb->cpu_reg.hl + gb->cpu_reg.hl;

        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (temp & 0x1000) ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFFFF0000) ? 1 : 0;
        gb->cpu_reg.hl = (temp & 0x0000FFFF);

        break;

    }

    case 0x2A: /* LD A, (HL+) */
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl++);

        break;

    case 0x2B: /* DEC HL */
        gb->cpu_reg.hl--;

        break;

    case 0x2C: /* INC L */
        gb->cpu_reg.l++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.l == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.l & 0x0F) == 0x00);

        break;

    case 0x2D: /* DEC L */
        gb->cpu_reg.l--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.l == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.l & 0x0F) == 0x0F);

        break;

    case 0x2E: /* LD L, imm */
        gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x2F: /* CPL */
        gb->cpu_reg.a = ~gb->cpu_reg.a;
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = 1;

        break;

    case 0x30: /* JP NC, imm */
        if (!gb->cpu_reg.f_bits.c)
        {

            char temp = __gb_read(gb, gb->cpu_reg.pc++);

            gb->cpu_reg.pc += temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc++;

        }

        break;

    case 0x31: /* LD SP, imm */
        gb->cpu_reg.sp = __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.sp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

        break;

    case 0x32: /* LD (HL), A */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);

        gb->cpu_reg.hl--;

        break;

    case 0x33: /* INC SP */
        gb->cpu_reg.sp++;

        break;

    case 0x34: /* INC (HL) */
    {

        unsigned char temp = __gb_read(gb, gb->cpu_reg.hl) + 1;

        gb->cpu_reg.f_bits.z = (temp == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((temp & 0x0F) == 0x00);

        __gb_write(gb, gb->cpu_reg.hl, temp);

        break;

    }

    case 0x35: /* DEC (HL) */
    {

        unsigned char temp = __gb_read(gb, gb->cpu_reg.hl) - 1;

        gb->cpu_reg.f_bits.z = (temp == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((temp & 0x0F) == 0x0F);

        __gb_write(gb, gb->cpu_reg.hl, temp);

        break;

    }

    case 0x36: /* LD (HL), imm */
        __gb_write(gb, gb->cpu_reg.hl, __gb_read(gb, gb->cpu_reg.pc++));

        break;

    case 0x37: /* SCF */
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 1;

        break;

    case 0x38: /* JP C, imm */
        if (gb->cpu_reg.f_bits.c)
        {
            char temp = __gb_read(gb, gb->cpu_reg.pc++);

            gb->cpu_reg.pc += temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc++;

        }

        break;

    case 0x39: /* ADD HL, SP */
    {

        unsigned int temp = gb->cpu_reg.hl + gb->cpu_reg.sp;

        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.hl & 0xFFF) + (gb->cpu_reg.sp & 0xFFF)) & 0x1000 ? 1 : 0;
        gb->cpu_reg.f_bits.c = temp & 0x10000 ? 1 : 0;
        gb->cpu_reg.hl = (unsigned short)temp;

        break;

    }

    case 0x3A: /* LD A, (HL) */
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl--);

        break;

    case 0x3B: /* DEC SP */
        gb->cpu_reg.sp--;

        break;

    case 0x3C: /* INC A */
        gb->cpu_reg.a++;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0x0F) == 0x00);

        break;

    case 0x3D: /* DEC A */
        gb->cpu_reg.a--;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0x0F) == 0x0F);

        break;

    case 0x3E: /* LD A, imm */
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.pc++);

        break;

    case 0x3F: /* CCF */
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = ~gb->cpu_reg.f_bits.c;

        break;

    case 0x40: /* LD B, B */
        break;

    case 0x41: /* LD B, C */
        gb->cpu_reg.b = gb->cpu_reg.c;

        break;

    case 0x42: /* LD B, D */
        gb->cpu_reg.b = gb->cpu_reg.d;

        break;

    case 0x43: /* LD B, E */
        gb->cpu_reg.b = gb->cpu_reg.e;

        break;

    case 0x44: /* LD B, H */
        gb->cpu_reg.b = gb->cpu_reg.h;

        break;

    case 0x45: /* LD B, L */
        gb->cpu_reg.b = gb->cpu_reg.l;

        break;

    case 0x46: /* LD B, (HL) */
        gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x47: /* LD B, A */
        gb->cpu_reg.b = gb->cpu_reg.a;

        break;

    case 0x48: /* LD C, B */
        gb->cpu_reg.c = gb->cpu_reg.b;

        break;

    case 0x49: /* LD C, C */
        break;

    case 0x4A: /* LD C, D */
        gb->cpu_reg.c = gb->cpu_reg.d;

        break;

    case 0x4B: /* LD C, E */
        gb->cpu_reg.c = gb->cpu_reg.e;

        break;

    case 0x4C: /* LD C, H */
        gb->cpu_reg.c = gb->cpu_reg.h;

        break;

    case 0x4D: /* LD C, L */
        gb->cpu_reg.c = gb->cpu_reg.l;

        break;

    case 0x4E: /* LD C, (HL) */
        gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x4F: /* LD C, A */
        gb->cpu_reg.c = gb->cpu_reg.a;

        break;

    case 0x50: /* LD D, B */
        gb->cpu_reg.d = gb->cpu_reg.b;

        break;

    case 0x51: /* LD D, C */
        gb->cpu_reg.d = gb->cpu_reg.c;

        break;

    case 0x52: /* LD D, D */
        break;

    case 0x53: /* LD D, E */
        gb->cpu_reg.d = gb->cpu_reg.e;

        break;

    case 0x54: /* LD D, H */
        gb->cpu_reg.d = gb->cpu_reg.h;

        break;

    case 0x55: /* LD D, L */
        gb->cpu_reg.d = gb->cpu_reg.l;

        break;

    case 0x56: /* LD D, (HL) */
        gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x57: /* LD D, A */
        gb->cpu_reg.d = gb->cpu_reg.a;

        break;

    case 0x58: /* LD E, B */
        gb->cpu_reg.e = gb->cpu_reg.b;

        break;

    case 0x59: /* LD E, C */
        gb->cpu_reg.e = gb->cpu_reg.c;

        break;

    case 0x5A: /* LD E, D */
        gb->cpu_reg.e = gb->cpu_reg.d;

        break;

    case 0x5B: /* LD E, E */
        break;

    case 0x5C: /* LD E, H */
        gb->cpu_reg.e = gb->cpu_reg.h;

        break;

    case 0x5D: /* LD E, L */
        gb->cpu_reg.e = gb->cpu_reg.l;

        break;

    case 0x5E: /* LD E, (HL) */
        gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x5F: /* LD E, A */
        gb->cpu_reg.e = gb->cpu_reg.a;

        break;

    case 0x60: /* LD H, B */
        gb->cpu_reg.h = gb->cpu_reg.b;

        break;

    case 0x61: /* LD H, C */
        gb->cpu_reg.h = gb->cpu_reg.c;

        break;

    case 0x62: /* LD H, D */
        gb->cpu_reg.h = gb->cpu_reg.d;

        break;

    case 0x63: /* LD H, E */
        gb->cpu_reg.h = gb->cpu_reg.e;

        break;

    case 0x64: /* LD H, H */
        break;

    case 0x65: /* LD H, L */
        gb->cpu_reg.h = gb->cpu_reg.l;

        break;

    case 0x66: /* LD H, (HL) */
        gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x67: /* LD H, A */
        gb->cpu_reg.h = gb->cpu_reg.a;

        break;

    case 0x68: /* LD L, B */
        gb->cpu_reg.l = gb->cpu_reg.b;

        break;

    case 0x69: /* LD L, C */
        gb->cpu_reg.l = gb->cpu_reg.c;

        break;

    case 0x6A: /* LD L, D */
        gb->cpu_reg.l = gb->cpu_reg.d;

        break;

    case 0x6B: /* LD L, E */
        gb->cpu_reg.l = gb->cpu_reg.e;

        break;

    case 0x6C: /* LD L, H */
        gb->cpu_reg.l = gb->cpu_reg.h;

        break;

    case 0x6D: /* LD L, L */
        break;

    case 0x6E: /* LD L, (HL) */
        gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x6F: /* LD L, A */
        gb->cpu_reg.l = gb->cpu_reg.a;

        break;

    case 0x70: /* LD (HL), B */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.b);

        break;

    case 0x71: /* LD (HL), C */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.c);

        break;

    case 0x72: /* LD (HL), D */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.d);

        break;

    case 0x73: /* LD (HL), E */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.e);

        break;

    case 0x74: /* LD (HL), H */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.h);

        break;

    case 0x75: /* LD (HL), L */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.l);

        break;

    case 0x76: /* HALT */
        /* TODO: Emulate HALT bug? */
        gb->gb_halt = 1;
        break;

    case 0x77: /* LD (HL), A */
        __gb_write(gb, gb->cpu_reg.hl, gb->cpu_reg.a);

        break;

    case 0x78: /* LD A, B */
        gb->cpu_reg.a = gb->cpu_reg.b;

        break;

    case 0x79: /* LD A, C */
        gb->cpu_reg.a = gb->cpu_reg.c;

        break;

    case 0x7A: /* LD A, D */
        gb->cpu_reg.a = gb->cpu_reg.d;

        break;

    case 0x7B: /* LD A, E */
        gb->cpu_reg.a = gb->cpu_reg.e;

        break;

    case 0x7C: /* LD A, H */
        gb->cpu_reg.a = gb->cpu_reg.h;

        break;

    case 0x7D: /* LD A, L */
        gb->cpu_reg.a = gb->cpu_reg.l;

        break;

    case 0x7E: /* LD A, (HL) */
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.hl);

        break;

    case 0x7F: /* LD A, A */
        break;

    case 0x80: /* ADD A, B */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.b;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x81: /* ADD A, C */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x82: /* ADD A, D */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.d;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x83: /* ADD A, E */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.e;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x84: /* ADD A, H */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.h;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x85: /* ADD A, L */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.l;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x86: /* ADD A, (HL) */
    {

        unsigned char hl = __gb_read(gb, gb->cpu_reg.hl);
        unsigned short temp = gb->cpu_reg.a + hl;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ hl ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x87: /* ADD A, A */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.a;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = temp & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x88: /* ADC A, B */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.b + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x89: /* ADC A, C */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.c + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x8A: /* ADC A, D */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.d + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x8B: /* ADC A, E */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.e + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x8C: /* ADC A, H */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.h + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x8D: /* ADC A, L */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.l + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x8E: /* ADC A, (HL) */
    {

        unsigned char val = __gb_read(gb, gb->cpu_reg.hl);
        unsigned short temp = gb->cpu_reg.a + val + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x8F: /* ADC A, A */
    {

        unsigned short temp = gb->cpu_reg.a + gb->cpu_reg.a + gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        /* TODO: Optimisation here? */
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.a ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x90: /* SUB B */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.b;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x91: /* SUB C */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x92: /* SUB D */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.d;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x93: /* SUB E */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.e;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x94: /* SUB H */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.h;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x95: /* SUB L */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.l;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x96: /* SUB (HL) */
    {

        unsigned char val = __gb_read(gb, gb->cpu_reg.hl);
        unsigned short temp = gb->cpu_reg.a - val;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x97: /* SUB A */
        gb->cpu_reg.a = 0;
        gb->cpu_reg.f_bits.z = 1;
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0x98: /* SBC A, B */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.b - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x99: /* SBC A, C */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.c - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x9A: /* SBC A, D */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.d - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x9B: /* SBC A, E */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.e - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x9C: /* SBC A, H */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.h - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x9D: /* SBC A, L */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.l - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x9E: /* SBC A, (HL) */
    {

        unsigned char val = __gb_read(gb, gb->cpu_reg.hl);
        unsigned short temp = gb->cpu_reg.a - val - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0x9F: /* SBC A, A */
        gb->cpu_reg.a = gb->cpu_reg.f_bits.c ? 0xFF : 0x00;
        gb->cpu_reg.f_bits.z = gb->cpu_reg.f_bits.c ? 0x00 : 0x01;
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = gb->cpu_reg.f_bits.c;

        break;

    case 0xA0: /* AND B */
        gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.b;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA1: /* AND C */
        gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.c;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA2: /* AND D */
        gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.d;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA3: /* AND E */
        gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.e;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA4: /* AND H */
        gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.h;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA5: /* AND L */
        gb->cpu_reg.a = gb->cpu_reg.a & gb->cpu_reg.l;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA6: /* AND B */
        gb->cpu_reg.a = gb->cpu_reg.a & __gb_read(gb, gb->cpu_reg.hl);
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA7: /* AND A */
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA8: /* XOR B */
        gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.b;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xA9: /* XOR C */
        gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.c;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xAA: /* XOR D */
        gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.d;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xAB: /* XOR E */
        gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.e;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xAC: /* XOR H */
        gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.h;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xAD: /* XOR L */
        gb->cpu_reg.a = gb->cpu_reg.a ^ gb->cpu_reg.l;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xAE: /* XOR (HL) */
        gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read(gb, gb->cpu_reg.hl);
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xAF: /* XOR A */
        gb->cpu_reg.a = 0x00;
        gb->cpu_reg.f_bits.z = 1;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB0: /* OR B */
        gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.b;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB1: /* OR C */
        gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.c;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB2: /* OR D */
        gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.d;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB3: /* OR E */
        gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.e;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB4: /* OR H */
        gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.h;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB5: /* OR L */
        gb->cpu_reg.a = gb->cpu_reg.a | gb->cpu_reg.l;
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB6: /* OR (HL) */
        gb->cpu_reg.a = gb->cpu_reg.a | __gb_read(gb, gb->cpu_reg.hl);
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB7: /* OR A */
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xB8: /* CP B */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.b;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.b ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xB9: /* CP C */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.c;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.c ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xBA: /* CP D */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.d;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.d ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xBB: /* CP E */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.e;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.e ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xBC: /* CP H */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.h;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.h ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xBD: /* CP L */
    {

        unsigned short temp = gb->cpu_reg.a - gb->cpu_reg.l;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ gb->cpu_reg.l ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    /* TODO: Optimsation by combining similar opcode routines. */
    case 0xBE: /* CP B */
    {

        unsigned char val = __gb_read(gb, gb->cpu_reg.hl);
        unsigned short temp = gb->cpu_reg.a - val;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xBF: /* CP A */
        gb->cpu_reg.f_bits.z = 1;
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xC0: /* RET NZ */
        if (!gb->cpu_reg.f_bits.z)
        {

            gb->cpu_reg.pc = __gb_read(gb, gb->cpu_reg.sp++);
            gb->cpu_reg.pc |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
            inst_cycles += 12;

        }

        break;

    case 0xC1: /* POP BC */
        gb->cpu_reg.c = __gb_read(gb, gb->cpu_reg.sp++);
        gb->cpu_reg.b = __gb_read(gb, gb->cpu_reg.sp++);

        break;

    case 0xC2: /* JP NZ, imm */
        if (!gb->cpu_reg.f_bits.z)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
            gb->cpu_reg.pc = temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xC3: /* JP imm */
    {

        unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

        temp |= __gb_read(gb, gb->cpu_reg.pc) << 8;
        gb->cpu_reg.pc = temp;

        break;

    }

    case 0xC4: /* CALL NZ imm */
        if (!gb->cpu_reg.f_bits.z)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);
            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xC5: /* PUSH BC */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.b);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.c);

        break;

    case 0xC6: /* ADD A, imm */
    {

        unsigned char value = __gb_read(gb, gb->cpu_reg.pc++);
        unsigned short calc = gb->cpu_reg.a + value;

        gb->cpu_reg.f_bits.z = ((unsigned char)calc == 0) ? 1 : 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a & 0xF) + (value & 0xF) > 0x0F) ? 1 : 0;
        gb->cpu_reg.f_bits.c = calc > 0xFF ? 1 : 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.a = (unsigned char)calc;

        break;

    }

    case 0xC7: /* RST 0x0000 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0000;

        break;

    case 0xC8: /* RET Z */
        if (gb->cpu_reg.f_bits.z)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.sp++);

            temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        break;

    case 0xC9: /* RET */
    {

        unsigned short temp = __gb_read(gb, gb->cpu_reg.sp++);

        temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
        gb->cpu_reg.pc = temp;

        break;

    }

    case 0xCA: /* JP Z, imm */
        if (gb->cpu_reg.f_bits.z)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
            gb->cpu_reg.pc = temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xCB: /* CB INST */
        __gb_execute_cb(gb);

        break;

    case 0xCC: /* CALL Z, imm */
        if (gb->cpu_reg.f_bits.z)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xCD: /* CALL imm */
    {

        unsigned short addr = __gb_read(gb, gb->cpu_reg.pc++);

        addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = addr;

        break;

    }

    case 0xCE: /* ADC A, imm */
    {

        unsigned char value, a, carry;

        value = __gb_read(gb, gb->cpu_reg.pc++);
        a = gb->cpu_reg.a;
        carry = gb->cpu_reg.f_bits.c;
        gb->cpu_reg.a = a + value + carry;
        gb->cpu_reg.f_bits.z = gb->cpu_reg.a == 0 ? 1 : 0;
        gb->cpu_reg.f_bits.h = ((a & 0xF) + (value & 0xF) + carry > 0x0F) ? 1 : 0;
        gb->cpu_reg.f_bits.c = (((unsigned short) a) + ((unsigned short) value) + carry > 0xFF) ? 1 : 0;
        gb->cpu_reg.f_bits.n = 0;

        break;

    }

    case 0xCF: /* RST 0x0008 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0008;

        break;

    case 0xD0: /* RET NC */
        if (!gb->cpu_reg.f_bits.c)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.sp++);

            temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        break;

    case 0xD1: /* POP DE */
        gb->cpu_reg.e = __gb_read(gb, gb->cpu_reg.sp++);
        gb->cpu_reg.d = __gb_read(gb, gb->cpu_reg.sp++);

        break;

    case 0xD2: /* JP NC, imm */
        if (!gb->cpu_reg.f_bits.c)
        {

            unsigned short temp =  __gb_read(gb, gb->cpu_reg.pc++);

            temp |=  __gb_read(gb, gb->cpu_reg.pc++) << 8;
            gb->cpu_reg.pc = temp;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xD4: /* CALL NC, imm */
        if (!gb->cpu_reg.f_bits.c)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xD5: /* PUSH DE */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.d);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.e);

        break;

    case 0xD6: /* SUB imm */
    {

        unsigned char val = __gb_read(gb, gb->cpu_reg.pc++);
        unsigned short temp = gb->cpu_reg.a - val;

        gb->cpu_reg.f_bits.z = ((temp & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ val ^ temp) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp & 0xFF);

        break;

    }

    case 0xD7: /* RST 0x0010 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0010;

        break;

    case 0xD8: /* RET C */
        if (gb->cpu_reg.f_bits.c)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.sp++);

            temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        break;

    case 0xD9: /* RETI */
    {

        unsigned short temp = __gb_read(gb, gb->cpu_reg.sp++);

        temp |= __gb_read(gb, gb->cpu_reg.sp++) << 8;
        gb->cpu_reg.pc = temp;
        gb->gb_ime = 1;

        break;

    }

    case 0xDA: /* JP C, imm */
        if (gb->cpu_reg.f_bits.c)
        {

            unsigned short addr = __gb_read(gb, gb->cpu_reg.pc++);

            addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
            gb->cpu_reg.pc = addr;
            inst_cycles += 4;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xDC: /* CALL C, imm */
        if (gb->cpu_reg.f_bits.c)
        {

            unsigned short temp = __gb_read(gb, gb->cpu_reg.pc++);

            temp |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
            __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

            gb->cpu_reg.pc = temp;
            inst_cycles += 12;

        }

        else
        {

            gb->cpu_reg.pc += 2;

        }

        break;

    case 0xDE: /* SBC A, imm */
    {

        unsigned char temp_8 = __gb_read(gb, gb->cpu_reg.pc++);
        unsigned short temp_16 = gb->cpu_reg.a - temp_8 - gb->cpu_reg.f_bits.c;

        gb->cpu_reg.f_bits.z = ((temp_16 & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = (gb->cpu_reg.a ^ temp_8 ^ temp_16) & 0x10 ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp_16 & 0xFF00) ? 1 : 0;
        gb->cpu_reg.a = (temp_16 & 0xFF);

        break;

    }

    case 0xDF: /* RST 0x0018 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0018;

        break;

    case 0xE0: /* LD (0xFF00+imm), A */
        __gb_write(gb, 0xFF00 | __gb_read(gb, gb->cpu_reg.pc++), gb->cpu_reg.a);

        break;

    case 0xE1: /* POP HL */
        gb->cpu_reg.l = __gb_read(gb, gb->cpu_reg.sp++);
        gb->cpu_reg.h = __gb_read(gb, gb->cpu_reg.sp++);

        break;

    case 0xE2: /* LD (C), A */
        __gb_write(gb, 0xFF00 | gb->cpu_reg.c, gb->cpu_reg.a);

        break;

    case 0xE5: /* PUSH HL */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.h);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.l);

        break;

    case 0xE6: /* AND imm */
        /* TODO: Optimisation? */
        gb->cpu_reg.a = gb->cpu_reg.a & __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 1;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xE7: /* RST 0x0020 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0020;

        break;

    case 0xE8: /* ADD SP, imm */
    {

        char offset = __gb_read(gb, gb->cpu_reg.pc++);

        /* TODO: Move flag assignments for optimisation. */
        gb->cpu_reg.f_bits.z = 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0;
        gb->cpu_reg.f_bits.c = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF);
        gb->cpu_reg.sp += offset;

        break;

    }

    case 0xE9: /* JP (HL) */
        gb->cpu_reg.pc = gb->cpu_reg.hl;

        break;

    case 0xEA: /* LD (imm), A */
    {

        unsigned short addr = __gb_read(gb, gb->cpu_reg.pc++);

        addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;

        __gb_write(gb, addr, gb->cpu_reg.a);

        break;

    }

    case 0xEE: /* XOR imm */
        gb->cpu_reg.a = gb->cpu_reg.a ^ __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xEF: /* RST 0x0028 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0028;

        break;

    case 0xF0: /* LD A, (0xFF00+imm) */
        gb->cpu_reg.a = __gb_read(gb, 0xFF00 | __gb_read(gb, gb->cpu_reg.pc++));

        break;

    case 0xF1: /* POP AF */
    {

        unsigned char temp_8 = __gb_read(gb, gb->cpu_reg.sp++);

        gb->cpu_reg.f_bits.z = (temp_8 >> 7) & 1;
        gb->cpu_reg.f_bits.n = (temp_8 >> 6) & 1;
        gb->cpu_reg.f_bits.h = (temp_8 >> 5) & 1;
        gb->cpu_reg.f_bits.c = (temp_8 >> 4) & 1;
        gb->cpu_reg.a = __gb_read(gb, gb->cpu_reg.sp++);

        break;

    }

    case 0xF2: /* LD A, (C) */
        gb->cpu_reg.a = __gb_read(gb, 0xFF00 | gb->cpu_reg.c);

        break;

    case 0xF3: /* DI */
        gb->gb_ime = 0;

        break;

    case 0xF5: /* PUSH AF */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.a);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.f_bits.z << 7 | gb->cpu_reg.f_bits.n << 6 | gb->cpu_reg.f_bits.h << 5 | gb->cpu_reg.f_bits.c << 4);

        break;

    case 0xF6: /* OR imm */
        gb->cpu_reg.a = gb->cpu_reg.a | __gb_read(gb, gb->cpu_reg.pc++);
        gb->cpu_reg.f_bits.z = (gb->cpu_reg.a == 0x00);
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = 0;
        gb->cpu_reg.f_bits.c = 0;

        break;

    case 0xF7: /* PUSH AF */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0030;

        break;

    case 0xF8: /* LD HL, SP+/-imm */
    {

        char offset = __gb_read(gb, gb->cpu_reg.pc++);

        gb->cpu_reg.hl = gb->cpu_reg.sp + offset;
        gb->cpu_reg.f_bits.z = 0;
        gb->cpu_reg.f_bits.n = 0;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.sp & 0xF) + (offset & 0xF) > 0xF) ? 1 : 0;
        gb->cpu_reg.f_bits.c = ((gb->cpu_reg.sp & 0xFF) + (offset & 0xFF) > 0xFF) ? 1 : 0;

        break;

    }

    case 0xF9: /* LD SP, HL */
        gb->cpu_reg.sp = gb->cpu_reg.hl;

        break;

    case 0xFA: /* LD A, (imm) */
    {

        unsigned short addr = __gb_read(gb, gb->cpu_reg.pc++);

        addr |= __gb_read(gb, gb->cpu_reg.pc++) << 8;
        gb->cpu_reg.a = __gb_read(gb, addr);

        break;

    }

    case 0xFB: /* EI */
        gb->gb_ime = 1;

        break;

    case 0xFE: /* CP imm */
    {

        unsigned char temp_8 = __gb_read(gb, gb->cpu_reg.pc++);
        unsigned short temp_16 = gb->cpu_reg.a - temp_8;

        gb->cpu_reg.f_bits.z = ((temp_16 & 0xFF) == 0x00);
        gb->cpu_reg.f_bits.n = 1;
        gb->cpu_reg.f_bits.h = ((gb->cpu_reg.a ^ temp_8 ^ temp_16) & 0x10) ? 1 : 0;
        gb->cpu_reg.f_bits.c = (temp_16 & 0xFF00) ? 1 : 0;

        break;

    }

    case 0xFF: /* RST 0x0038 */
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc >> 8);
        __gb_write(gb, --gb->cpu_reg.sp, gb->cpu_reg.pc & 0xFF);

        gb->cpu_reg.pc = 0x0038;

        break;

    default:
        gb->gb_error(gb, GB_INVALID_OPCODE, opcode);

        break;

    }

    gb->counter.div_count += inst_cycles;

    if (gb->counter.div_count >= DIV_CYCLES)
    {

        gb->gb_reg.DIV++;
        gb->counter.div_count -= DIV_CYCLES;

    }

    if ((gb->gb_reg.SC & 0x81) == 0x81)
    {

        gb->counter.serial_count += inst_cycles;

        if (gb->counter.serial_count >= SERIAL_CYCLES)
        {

            if (gb->gb_serial_transfer == 0)
                gb->gb_reg.SB = 0xFF;
            else
                gb->gb_reg.SB = gb->gb_serial_transfer(gb, gb->gb_reg.SB);

            gb->gb_reg.SC &= 0x01;
            gb->gb_reg.IF |= SERIAL_INTR;
            gb->counter.serial_count -= SERIAL_CYCLES;

        }

    }

    if (gb->gb_reg.tac_enable)
    {

        static const unsigned int TAC_CYCLES[4] = {1024, 16, 64, 256};

        gb->counter.tima_count += inst_cycles;

        if (gb->counter.tima_count >= TAC_CYCLES[gb->gb_reg.tac_rate])
        {

            gb->counter.tima_count -= TAC_CYCLES[gb->gb_reg.tac_rate];

            if (++gb->gb_reg.TIMA == 0)
            {

                gb->gb_reg.IF |= TIMER_INTR;
                gb->gb_reg.TIMA = gb->gb_reg.TMA;

            }

        }

    }

    if ((gb->gb_reg.LCDC & LCDC_ENABLE) == 0)
        return;

    gb->counter.lcd_count += inst_cycles;

    if (gb->counter.lcd_count > LCD_LINE_CYCLES)
    {

        gb->counter.lcd_count -= LCD_LINE_CYCLES;

        if (gb->gb_reg.LY == gb->gb_reg.LYC)
        {

            gb->gb_reg.STAT |= STAT_LYC_COINC;

            if (gb->gb_reg.STAT & STAT_LYC_INTR)
                gb->gb_reg.IF |= LCDC_INTR;

        }

        else
        {

            gb->gb_reg.STAT &= 0xFB;

        }

        gb->gb_reg.LY = (gb->gb_reg.LY + 1) % LCD_VERT_LINES;

        if (gb->gb_reg.LY == LCD_HEIGHT)
        {

            gb->lcd_mode = LCD_VBLANK;
            gb->gb_frame = 1;
            gb->gb_reg.IF |= VBLANK_INTR;

            if (gb->gb_reg.STAT & STAT_MODE_1_INTR)
                gb->gb_reg.IF |= LCDC_INTR;

#if ENABLE_VIDEO
            if (gb->direct.frame_skip)
                gb->display.frame_skip_count = !gb->display.frame_skip_count;

            if (gb->direct.interlace && (!gb->direct.frame_skip || gb->display.frame_skip_count))
                gb->display.interlace_count = !gb->display.interlace_count;
#endif

        }

        else if (gb->gb_reg.LY < LCD_HEIGHT)
        {

            if (gb->gb_reg.LY == 0)
            {

                gb->display.WY = gb->gb_reg.WY;
                gb->display.window_clear = 0;

            }

            gb->lcd_mode = LCD_HBLANK;

            if (gb->gb_reg.STAT & STAT_MODE_0_INTR)
                gb->gb_reg.IF |= LCDC_INTR;

        }

    }

    else if (gb->lcd_mode == LCD_HBLANK && gb->counter.lcd_count >= LCD_MODE_2_CYCLES)
    {

        gb->lcd_mode = LCD_SEARCH_OAM;

        if (gb->gb_reg.STAT & STAT_MODE_2_INTR)
            gb->gb_reg.IF |= LCDC_INTR;

    }

    else if (gb->lcd_mode == LCD_SEARCH_OAM && gb->counter.lcd_count >= LCD_MODE_3_CYCLES)
    {

        gb->lcd_mode = LCD_TRANSFER;
#if ENABLE_VIDEO
        __gb_draw_line(gb);
#endif

    }

}

void gb_run_frame(struct gb_s *gb)
{

    gb->gb_frame = 0;

    while (!gb->gb_frame)
        __gb_step_cpu(gb);

}

void gb_init_serial(struct gb_s *gb, unsigned char (*gb_serial_transfer)(struct gb_s*, const unsigned char))
{

    gb->gb_serial_transfer = gb_serial_transfer;

}

void gb_reset(struct gb_s *gb)
{

    gb->gb_halt = 0;
    gb->gb_ime = 1;
    gb->gb_bios_enable = 0;
    gb->lcd_mode = LCD_HBLANK;
    gb->selected_rom_bank = 1;
    gb->cart_ram_bank = 0;
    gb->enable_cart_ram = 0;
    gb->cart_mode_select = 0;
    gb->cpu_reg.af = 0x01B0;
    gb->cpu_reg.bc = 0x0013;
    gb->cpu_reg.de = 0x00D8;
    gb->cpu_reg.hl = 0x014D;
    gb->cpu_reg.sp = 0xFFFE;
    gb->cpu_reg.pc = 0x0100;
    gb->counter.lcd_count = 0;
    gb->counter.div_count = 0;
    gb->counter.tima_count = 0;
    gb->counter.serial_count = 0;
    gb->gb_reg.TIMA = 0x00;
    gb->gb_reg.TMA = 0x00;
    gb->gb_reg.TAC = 0xF8;
    gb->gb_reg.DIV = 0xAC;
    gb->gb_reg.IF = 0xE1;
    gb->gb_reg.LCDC = 0x91;
    gb->gb_reg.SCY = 0x00;
    gb->gb_reg.SCX = 0x00;
    gb->gb_reg.LYC = 0x00;
    gb->gb_reg.SC = 0x7E;
    gb->gb_reg.STAT = 0;
    gb->gb_reg.LY = 0;

    __gb_write(gb, 0xFF47, 0xFC);
    __gb_write(gb, 0xFF48, 0xFF);
    __gb_write(gb, 0xFF49, 0x0F);

    gb->gb_reg.WY = 0x00;
    gb->gb_reg.WX = 0x00;
    gb->gb_reg.IE = 0x00;
    gb->direct.joypad = 0xFF;
    gb->gb_reg.P1 = 0xCF;

}

enum gb_init_error_e gb_init(struct gb_s *gb, unsigned char (*gb_rom_read)(struct gb_s*, const unsigned int), unsigned char (*gb_cart_ram_read)(struct gb_s*, const unsigned int), void (*gb_cart_ram_write)(struct gb_s*, const unsigned int, const unsigned char), void (*gb_error)(struct gb_s*, const enum gb_error_e, const unsigned short))
{

    const unsigned short mbc_location = 0x0147;
    const unsigned short bank_count_location = 0x0148;
    const unsigned short ram_size_location = 0x0149;

    /**
     * Table for cartridge type (MBC). -1 if invalid.
     * TODO: MMM01 is untested.
     * TODO: MBC6 is untested.
     * TODO: MBC7 is unsupported.
     * TODO: POCKET CAMERA is unsupported.
     * TODO: BANDAI TAMA5 is unsupported.
     * TODO: HuC3 is unsupported.
     * TODO: HuC1 is unsupported.
     **/

    const unsigned char cart_mbc[] = {
        0, 1, 1, 1, -1, 2, 2, -1, 0, 0, -1, 0, 0, 0, -1, 3,
        3, 3, 3, 3, -1, -1, -1, -1, -1, 5, 5, 5, 5, 5, 5, -1
    };
    const unsigned char cart_ram[] = {
        0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0,
        1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0
    };
    const unsigned short num_rom_banks[] = {
        2, 4, 8, 16, 32, 64, 128, 256, 512, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 72, 80, 96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    const unsigned char num_ram_banks[] = { 0, 1, 1, 4, 16, 8 };
    unsigned char mbc_value;
    unsigned char x = 0;
    unsigned short i;

    gb->gb_rom_read = gb_rom_read;
    gb->gb_cart_ram_read = gb_cart_ram_read;
    gb->gb_cart_ram_write = gb_cart_ram_write;
    gb->gb_error = gb_error;
    gb->gb_serial_transfer = 0;
#if ENABLE_VIDEO
    gb->direct.interlace = 0;
    gb->display.interlace_count = 0;
    gb->direct.frame_skip = 0;
    gb->display.frame_skip_count = 0;
    gb->display.window_clear = 0;
    gb->display.WY = 0;
#endif

    for (i = 0x0134; i <= 0x014C; i++)
        x = x - gb->gb_rom_read(gb, i) - 1;

    if (x != gb->gb_rom_read(gb, ROM_HEADER_CHECKSUM_LOC))
        return GB_INIT_INVALID_CHECKSUM;

    mbc_value = gb->gb_rom_read(gb, mbc_location);

    if (mbc_value > sizeof(cart_mbc) - 1 || (gb->mbc = cart_mbc[gb->gb_rom_read(gb, mbc_location)]) == 255u)
        return GB_INIT_CARTRIDGE_UNSUPPORTED;

    gb->cart_ram = cart_ram[gb->gb_rom_read(gb, mbc_location)];
    gb->num_rom_banks = num_rom_banks[gb->gb_rom_read(gb, bank_count_location)];
    gb->num_ram_banks = num_ram_banks[gb->gb_rom_read(gb, ram_size_location)];

    gb_reset(gb);

    return GB_INIT_NO_ERROR;

}

