/*
 *  SID_rp2350.cpp - 6581 emulation for RP2350 (I2S audio output)
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Based on Frodo SID.cpp by Christian Bauer.
 *  Modified for RP2350 with I2S audio output instead of SDL.
 */

#include "../sysdeps.h"
#include "../SID.h"
#include "../VIC.h"  // For TOTAL_RASTERS
#include "../Prefs.h"
#include "../board_config.h"
#include "debug_log.h"

// Map board_config.h names to Frodo names
#ifndef SCREEN_FREQ
#define SCREEN_FREQ C64_SCREEN_FREQ
#endif

// RP2350 headers
extern "C" {
#include "pico/stdlib.h"
}

#include <cmath>
#include <cstring>

// Note: We don't use <complex> or <numbers> to reduce code size
// Simple inline replacements for filter calculations

// Types for filter calculations (use float for RP2350 FPU)
using filter_t = float;

// SID waveforms
enum {
    WAVE_NONE,
    WAVE_TRI,
    WAVE_SAW,
    WAVE_TRISAW,
    WAVE_RECT,
    WAVE_TRIRECT,
    WAVE_SAWRECT,
    WAVE_TRISAWRECT,
    WAVE_NOISE
};

// Combined waveform tables
#include "../SID_wave_tables.h"

// RP2350 Audio configuration
constexpr int SAMPLE_FREQ = 44100;          // Target sample frequency
#ifdef NTSC
constexpr uint32_t SID_FREQ = 1022727;      // SID frequency (NTSC)
#else
constexpr uint32_t SID_FREQ = 985248;       // SID frequency (PAL)
#endif
constexpr size_t SAMPLE_BUF_SIZE = TOTAL_RASTERS * 2;  // Double buffered

// External I2S interface (from sid_i2s.cpp)
extern "C" {
    void sid_add_sample(int16_t left, int16_t right);
    int sid_get_buffer_fill(void);
}

// Structure for one voice
struct DRVoice {
    int wave;           // Selected waveform
    int eg_state;       // Current state of EG
    DRVoice *mod_by;    // Voice that modulates this one
    DRVoice *mod_to;    // Voice that is modulated by this one

    uint32_t count;     // Phase accumulator, 8.16 fixed
    uint32_t add;       // Added to accumulator each sample

    uint16_t freq;      // SID frequency value
    uint16_t pw;        // SID pulse-width value

    int32_t a_add;      // EG parameters
    int32_t d_sub;
    int32_t s_level;
    int32_t r_sub;
    int32_t eg_level;   // Current EG level, 8.16 fixed

    uint32_t noise;     // Last noise output

    bool gate;          // EG gate bit
    bool ring;          // Ring modulation
    bool test;          // Test bit
    bool sync;          // Sync modulation
};

// Renderer class for RP2350
class DigitalRenderer : public SIDRenderer {
public:
    DigitalRenderer(MOS6581 *sid);
    virtual ~DigitalRenderer();

    void Reset() override;
    void EmulateLine() override;
    void WriteRegister(uint16_t adr, uint8_t byte) override;
    void NewPrefs(const Prefs *prefs) override;
    void Pause() override;
    void Resume() override;

private:
    void calc_filter();
    void calc_samples(int count);
    int16_t calc_single_sample();

    bool ready;
    MOS6581 *the_sid;

    uint8_t mode_vol;       // MODE/VOL register
    uint8_t res_filt;       // RES/FILT register

    uint32_t sid_cycles_frac;   // SID cycles per sample (16.16)

    DRVoice voice[3];

    uint16_t f_fc;          // Filter cutoff (11 bits)
    uint8_t f_res;          // Filter resonance (4 bits)

    // Simplified IIR filter state (single-pole for RP2350)
    filter_t lp_state;
    filter_t hp_state;
    filter_t bp_state;
    filter_t filter_cutoff;
    filter_t filter_resonance;

    // Sample buffer for raster-synced playback
    uint8_t sample_mode_vol[SAMPLE_BUF_SIZE];
    uint8_t sample_res_filt[SAMPLE_BUF_SIZE];
    unsigned sample_in_ptr;

    // Fractional sample counting (16.16 fixed point)
    uint32_t samples_per_line_frac;  // Samples per line in 16.16 fixed point
    uint32_t sample_accum;           // Fractional sample accumulator

    bool is6581;
};

// EG tables
const int16_t MOS6581::EGDivTable[16] = {
    9, 32, 63, 95, 149, 220, 267, 313,
    392, 977, 1954, 3126, 3906, 11720, 19531, 31251
};

const uint8_t MOS6581::EGDRShift[256] = {
    5,5,5,5,5,5,5,5,4,4,4,4,4,4,4,4,
    3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// SID leakage tables
uint16_t MOS6581::sid_leakage_cycles[9] = {
    0, 0xa300, 0x3b00, 0x2280, 0x0400, 0x1280, 0x1a80, 0x3a00, 0x0080
};

uint8_t MOS6581::sid_leakage_mask[9] = {
    0, 0x7f, 0xfb, 0xf7, 0xfd, 0xbf, 0xdf, 0xef, 0xfe
};

/*
 *  MOS6581 Constructor
 */
MOS6581::MOS6581()
{
    the_renderer = nullptr;
    for (unsigned i = 0; i < 32; ++i) {
        regs[i] = 0;
    }

    // Open renderer
    open_close_renderer(SIDTYPE_NONE, ThePrefs.SIDType);
}

MOS6581::~MOS6581()
{
    open_close_renderer(ThePrefs.SIDType, SIDTYPE_NONE);
}

void MOS6581::Reset()
{
    for (unsigned i = 0; i < 32; ++i) {
        regs[i] = 0;
    }

    last_sid_byte = 0;
    last_sid_seq = 0;

    set_wave_tables(ThePrefs.SIDType);

    fake_v3_update_cycle = 0;
    fake_v3_count = 0x555555;
    fake_v3_eg_level = 0;
    fake_v3_eg_state = EG_RELEASE;

    if (the_renderer != nullptr) {
        the_renderer->Reset();
    }
}

void MOS6581::NewPrefs(const Prefs *prefs)
{
    set_wave_tables(prefs->SIDType);
    open_close_renderer(ThePrefs.SIDType, prefs->SIDType);
    if (the_renderer != nullptr) {
        the_renderer->NewPrefs(prefs);
    }
}

void MOS6581::PauseSound()
{
    if (the_renderer != nullptr) {
        the_renderer->Pause();
    }
}

void MOS6581::ResumeSound()
{
    if (the_renderer != nullptr) {
        the_renderer->Resume();
    }
}

void MOS6581::set_wave_tables(int sid_type)
{
    if (sid_type == SIDTYPE_DIGITAL_8580) {
        TriSawTable = TriSawTable_8580;
        TriRectTable = TriRectTable_8580;
        SawRectTable = SawRectTable_8580;
        TriSawRectTable = TriSawRectTable_8580;
    } else {
        TriSawTable = TriSawTable_6581;
        TriRectTable = TriRectTable_6581;
        SawRectTable = SawRectTable_6581;
        TriSawRectTable = TriSawRectTable_6581;
    }
}

uint8_t MOS6581::v3_random()
{
    v3_random_seed = v3_random_seed * 1103515245 + 12345;
    return v3_random_seed >> 16;
}

void MOS6581::update_osc3()
{
    // Simplified for RP2350 - just update counter
    uint8_t v3_ctrl = regs[0x12];
    if (v3_ctrl & 8) {
        fake_v3_count = 0;
    } else {
        uint32_t add = (regs[0x0f] << 8) | regs[0x0e];
        fake_v3_count = (fake_v3_count + add) & 0xffffff;
    }
}

uint8_t MOS6581::read_osc3()
{
    update_osc3();

    uint32_t count = fake_v3_count;
    uint8_t v3_ctrl = regs[0x12];
    uint8_t wave = v3_ctrl >> 4;

    switch (wave) {
        case WAVE_TRI:
            if (count & 0x800000) {
                return (count >> 15) ^ 0xff;
            } else {
                return count >> 15;
            }
        case WAVE_SAW:
            return count >> 16;
        case WAVE_NOISE:
            return v3_random();
        default:
            return 0;
    }
}

uint8_t MOS6581::read_env3() const
{
    return (uint8_t)(fake_v3_eg_level >> 16);
}

void MOS6581::GetState(MOS6581State *s) const
{
    s->freq_lo_1 = regs[0]; s->freq_hi_1 = regs[1];
    s->pw_lo_1 = regs[2]; s->pw_hi_1 = regs[3];
    s->ctrl_1 = regs[4]; s->AD_1 = regs[5]; s->SR_1 = regs[6];

    s->freq_lo_2 = regs[7]; s->freq_hi_2 = regs[8];
    s->pw_lo_2 = regs[9]; s->pw_hi_2 = regs[10];
    s->ctrl_2 = regs[11]; s->AD_2 = regs[12]; s->SR_2 = regs[13];

    s->freq_lo_3 = regs[14]; s->freq_hi_3 = regs[15];
    s->pw_lo_3 = regs[16]; s->pw_hi_3 = regs[17];
    s->ctrl_3 = regs[18]; s->AD_3 = regs[19]; s->SR_3 = regs[20];

    s->fc_lo = regs[21]; s->fc_hi = regs[22];
    s->res_filt = regs[23]; s->mode_vol = regs[24];

    s->pot_x = 0xff; s->pot_y = 0xff;
    s->v3_update_cycle = fake_v3_update_cycle;
    s->v3_count = fake_v3_count;
    s->v3_eg_level = fake_v3_eg_level;
    s->v3_eg_state = fake_v3_eg_state;
    s->v3_random_seed = v3_random_seed;
    s->last_sid_cycles = last_sid_cycles;
    s->last_sid_seq = last_sid_seq;
    s->last_sid_byte = last_sid_byte;
}

void MOS6581::SetState(const MOS6581State *s)
{
    regs[0] = s->freq_lo_1; regs[1] = s->freq_hi_1;
    regs[2] = s->pw_lo_1; regs[3] = s->pw_hi_1;
    regs[4] = s->ctrl_1; regs[5] = s->AD_1; regs[6] = s->SR_1;

    regs[7] = s->freq_lo_2; regs[8] = s->freq_hi_2;
    regs[9] = s->pw_lo_2; regs[10] = s->pw_hi_2;
    regs[11] = s->ctrl_2; regs[12] = s->AD_2; regs[13] = s->SR_2;

    regs[14] = s->freq_lo_3; regs[15] = s->freq_hi_3;
    regs[16] = s->pw_lo_3; regs[17] = s->pw_hi_3;
    regs[18] = s->ctrl_3; regs[19] = s->AD_3; regs[20] = s->SR_3;

    regs[21] = s->fc_lo; regs[22] = s->fc_hi;
    regs[23] = s->res_filt; regs[24] = s->mode_vol;

    fake_v3_update_cycle = s->v3_update_cycle;
    fake_v3_count = s->v3_count;
    fake_v3_eg_level = s->v3_eg_level;
    fake_v3_eg_state = s->v3_eg_state;
    v3_random_seed = s->v3_random_seed;
    last_sid_cycles = s->last_sid_cycles;
    last_sid_seq = s->last_sid_seq;
    last_sid_byte = s->last_sid_byte;

    if (the_renderer != nullptr) {
        for (unsigned i = 0; i < 25; ++i) {
            the_renderer->WriteRegister(i, regs[i]);
        }
    }
}

static bool is_digital(int sid_type)
{
    return sid_type == SIDTYPE_DIGITAL_6581 || sid_type == SIDTYPE_DIGITAL_8580;
}

void MOS6581::open_close_renderer(int old_type, int new_type)
{
    if (old_type == new_type || is_digital(old_type) == is_digital(new_type))
        return;

    delete the_renderer;

    if (new_type == SIDTYPE_DIGITAL_6581 || new_type == SIDTYPE_DIGITAL_8580) {
        the_renderer = new DigitalRenderer(this);
    } else {
        the_renderer = nullptr;
    }

    if (the_renderer != nullptr) {
        for (unsigned i = 0; i < 25; ++i) {
            the_renderer->WriteRegister(i, regs[i]);
        }
    }
}

/*
 *  DigitalRenderer Constructor
 */
DigitalRenderer::DigitalRenderer(MOS6581 *sid) : the_sid(sid)
{
    // Link voices
    voice[0].mod_by = &voice[2];
    voice[1].mod_by = &voice[0];
    voice[2].mod_by = &voice[1];
    voice[0].mod_to = &voice[1];
    voice[1].mod_to = &voice[2];
    voice[2].mod_to = &voice[0];

    // Calculate cycles per sample (16.16 fixed point)
    sid_cycles_frac = (uint32_t)((float)SID_FREQ / SAMPLE_FREQ * 65536.0f);

    // Samples per raster line (16.16 fixed point for accurate timing)
    // PAL: 44100 / (50 * 312) = 2.827 samples/line
    // NTSC: 44100 / (60 * 263) = 2.795 samples/line
    samples_per_line_frac = (uint32_t)((float)SAMPLE_FREQ / (SCREEN_FREQ * TOTAL_RASTERS) * 65536.0f);
    sample_accum = 0;

    Reset();

    MII_DEBUG_PRINTF("SID: SCREEN_FREQ=%d, TOTAL_RASTERS=%d, samples_per_line=%.3f\n",
           SCREEN_FREQ, TOTAL_RASTERS, (float)samples_per_line_frac / 65536.0f);

    is6581 = (ThePrefs.SIDType == SIDTYPE_DIGITAL_6581);

    ready = true;
}

DigitalRenderer::~DigitalRenderer()
{
}

void DigitalRenderer::Reset()
{
    mode_vol = 0;
    res_filt = 0;

    for (unsigned v = 0; v < 3; ++v) {
        voice[v].wave = WAVE_NONE;
        voice[v].eg_state = EG_RELEASE;
        voice[v].count = 0x555555;
        voice[v].add = 0;
        voice[v].freq = voice[v].pw = 0;
        voice[v].eg_level = voice[v].s_level = 0;
        voice[v].a_add = voice[v].d_sub = voice[v].r_sub = sid_cycles_frac / MOS6581::EGDivTable[0];
        voice[v].gate = voice[v].ring = voice[v].test = voice[v].sync = false;
        voice[v].noise = 0x7ffff8;
    }

    f_fc = f_res = 0;
    lp_state = hp_state = bp_state = 0.0f;
    filter_cutoff = 0.0f;
    filter_resonance = 0.0f;

    sample_in_ptr = 0;
    memset(sample_mode_vol, 0, SAMPLE_BUF_SIZE);
    memset(sample_res_filt, 0, SAMPLE_BUF_SIZE);

    sample_accum = 0;
}

void DigitalRenderer::Pause()
{
    // Nothing to do on RP2350 (I2S continues running)
}

void DigitalRenderer::Resume()
{
    // Nothing to do
}

void DigitalRenderer::EmulateLine()
{
    // Record registers for sample playback
    sample_mode_vol[sample_in_ptr] = mode_vol;
    sample_res_filt[sample_in_ptr] = res_filt;
    sample_in_ptr = (sample_in_ptr + 1) % SAMPLE_BUF_SIZE;

    // Fractional sample counting: accumulate samples per line
    // and generate the integer part, keeping fractional remainder
    sample_accum += samples_per_line_frac;
    unsigned samples_to_generate = sample_accum >> 16;  // Integer part
    sample_accum &= 0xFFFF;  // Keep fractional part

    if (samples_to_generate > 0) {
        calc_samples(samples_to_generate);
    }
}

void DigitalRenderer::WriteRegister(uint16_t adr, uint8_t byte)
{
    if (!ready)
        return;

    unsigned v = adr / 7;

    switch (adr) {
        case 0: case 7: case 14:
            voice[v].freq = (voice[v].freq & 0xff00) | byte;
            voice[v].add = (uint32_t)((float)voice[v].freq / SAMPLE_FREQ * SID_FREQ);
            break;

        case 1: case 8: case 15:
            voice[v].freq = (voice[v].freq & 0xff) | (byte << 8);
            voice[v].add = (uint32_t)((float)voice[v].freq / SAMPLE_FREQ * SID_FREQ);
            break;

        case 2: case 9: case 16:
            voice[v].pw = (voice[v].pw & 0x0f00) | byte;
            break;

        case 3: case 10: case 17:
            voice[v].pw = (voice[v].pw & 0xff) | ((byte & 0xf) << 8);
            break;

        case 4: case 11: case 18:
            voice[v].wave = (byte >> 4) & 0xf;
            if ((byte & 1) != voice[v].gate) {
                if (byte & 1) {
                    voice[v].eg_state = EG_ATTACK;
                } else {
                    voice[v].eg_state = EG_RELEASE;
                }
            }
            voice[v].gate = byte & 1;
            voice[v].mod_by->sync = byte & 2;
            voice[v].ring = byte & 4;
            if ((voice[v].test = byte & 8) != 0) {
                voice[v].count = 0;
            }
            break;

        case 5: case 12: case 19:
            voice[v].a_add = sid_cycles_frac / MOS6581::EGDivTable[byte >> 4];
            voice[v].d_sub = sid_cycles_frac / MOS6581::EGDivTable[byte & 0xf];
            break;

        case 6: case 13: case 20:
            voice[v].s_level = (byte >> 4) * 0x111111;
            voice[v].r_sub = sid_cycles_frac / MOS6581::EGDivTable[byte & 0xf];
            break;

        case 21:
            f_fc = (f_fc & 0x7f8) | (byte & 7);
            break;

        case 22:
            f_fc = (f_fc & 7) | (byte << 3);
            break;

        case 23:
            res_filt = byte;
            f_res = byte >> 4;
            break;

        case 24:
            mode_vol = byte;
            break;
    }
}

void DigitalRenderer::NewPrefs(const Prefs *prefs)
{
    is6581 = (prefs->SIDType == SIDTYPE_DIGITAL_6581);
}

void DigitalRenderer::calc_filter()
{
    // Simplified filter calculation for RP2350
    // Convert 11-bit fc to normalized cutoff frequency
    filter_cutoff = (float)f_fc / 2048.0f;
    filter_resonance = 1.0f + (float)f_res / 15.0f;
}

// Random number generator for noise waveform
static uint8_t sid_random()
{
    static uint32_t seed = 1;
    seed = seed * 1103515245 + 12345;
    return seed >> 16;
}

int16_t DigitalRenderer::calc_single_sample()
{
    // Get current settings
    uint8_t master_volume = mode_vol & 0xf;

    int32_t sum_output = 0;
    int32_t sum_input_filter = 0;
    int32_t sum_output_filter = 0;

    // Loop for all three voices
    for (unsigned j = 0; j < 3; ++j) {
        DRVoice *v = &voice[j];

        // Envelope generator
        uint16_t envelope;

        switch (v->eg_state) {
            case EG_ATTACK:
                v->eg_level += v->a_add;
                if (v->eg_level > 0xffffff) {
                    v->eg_level = 0xffffff;
                    v->eg_state = EG_DECAY_SUSTAIN;
                }
                break;
            case EG_DECAY_SUSTAIN:
                v->eg_level -= v->d_sub >> MOS6581::EGDRShift[v->eg_level >> 16];
                if (v->eg_level < v->s_level) {
                    v->eg_level = v->s_level;
                }
                break;
            case EG_RELEASE:
                v->eg_level -= v->r_sub >> MOS6581::EGDRShift[v->eg_level >> 16];
                if (v->eg_level < 0) {
                    v->eg_level = 0;
                }
                break;
        }
        envelope = v->eg_level >> 16;

        // Waveform generator
        uint16_t output;

        if (!v->test) {
            v->count += v->add;
        }

        if (v->sync && (v->count > 0x1000000)) {
            v->mod_to->count = 0;
        }

        v->count &= 0xffffff;

        switch (v->wave) {
            case WAVE_TRI: {
                uint32_t ctrl = v->count;
                if (v->ring) {
                    ctrl ^= v->mod_by->count;
                }
                if (ctrl & 0x800000) {
                    output = (v->count >> 7) ^ 0xffff;
                } else {
                    output = v->count >> 7;
                }
                break;
            }
            case WAVE_SAW:
                output = v->count >> 8;
                break;
            case WAVE_RECT:
                if (v->test || (v->count >> 12) >= v->pw) {
                    output = 0xffff;
                } else {
                    output = 0;
                }
                break;
            case WAVE_TRISAW:
                output = the_sid->TriSawTable[v->count >> 12];
                if (is6581) {
                    v->count &= 0x7fffff | (output << 8);
                }
                break;
            case WAVE_TRIRECT:
                if (v->test || (v->count >> 12) >= v->pw) {
                    uint32_t ctrl = v->count;
                    if (v->ring) {
                        ctrl ^= ~(v->mod_by->count) & 0x800000;
                    }
                    output = the_sid->TriRectTable[ctrl >> 12];
                } else {
                    output = 0;
                }
                break;
            case WAVE_SAWRECT:
                if (v->test || (v->count >> 12) >= v->pw) {
                    output = the_sid->SawRectTable[v->count >> 12];
                } else {
                    output = 0;
                }
                if (is6581) {
                    v->count &= 0x7fffff | (output << 8);
                }
                break;
            case WAVE_TRISAWRECT:
                if (v->test || (v->count >> 12) >= v->pw) {
                    output = the_sid->TriSawRectTable[v->count >> 12];
                } else {
                    output = 0;
                }
                if (is6581) {
                    v->count &= 0x7fffff | (output << 8);
                }
                break;
            case WAVE_NOISE:
                if (v->count > 0x100000) {
                    output = v->noise = sid_random() << 8;
                    v->count &= 0xfffff;
                } else {
                    output = v->noise;
                }
                break;
            default:
                output = 0x8000;
                break;
        }

        // Route voice through filter if selected
        if (res_filt & (1 << j)) {
            sum_input_filter += (int16_t)(output ^ 0x8000) * envelope;
        } else if (j != 2 || (mode_vol & 0x80) == 0) {
            sum_output += (int16_t)(output ^ 0x8000) * envelope;
        }
    }

    // Simplified filter (single-pole IIR for RP2350)
    float cutoff = 0.1f + filter_cutoff * 0.8f;  // Map to useful range

    // Low-pass
    if (mode_vol & 0x10) {
        lp_state = lp_state + cutoff * ((float)sum_input_filter - lp_state);
        sum_output_filter += (int32_t)lp_state;
    }

    // Band-pass (difference of two low-passes)
    if (mode_vol & 0x20) {
        float bp_input = (float)sum_input_filter;
        float bp_lp = bp_state + cutoff * (bp_input - bp_state);
        bp_state = bp_lp;
        sum_output_filter += (int32_t)(bp_input - bp_lp) * (int32_t)(filter_resonance);
    }

    // High-pass
    if (mode_vol & 0x40) {
        hp_state = hp_state + cutoff * ((float)sum_input_filter - hp_state);
        sum_output_filter += (int32_t)((float)sum_input_filter - hp_state);
    }

    // Mix and apply master volume
    // Scale down to prevent clipping (>> 16 instead of >> 14)
    int32_t output = (sum_output + sum_output_filter) * master_volume;
    output = output >> 16;

    // Add small DC offset for 6581 (after volume scaling)
    if (is6581) {
        output += 128;
    }

    // Clamp to 16-bit range
    if (output > 32767) {
        output = 32767;
    } else if (output < -32768) {
        output = -32768;
    }

    return (int16_t)output;
}

void DigitalRenderer::calc_samples(int count)
{
    // Debug: track sample generation rate
    calc_filter();

    for (int i = 0; i < count; ++i) {
        int16_t sample = calc_single_sample();

        // Output to I2S (stereo, same on both channels)
        sid_add_sample(sample, sample);
    }
}
