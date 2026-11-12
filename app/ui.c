/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* MYCEL-8's controls and screen.
 *   ALGO or black keys 1-8   the part          SELECT   the page: RHYTHM, PITCH, FEEL, SOUND (GLO: the field)
 *   K1-K4                    the page's four   PRESETS  dice (right) / the kept registers (left)
 *   white keys               the pool: which scale degrees the part may play (a drum part: its sound)
 *   FX lock   ARP dice   LFO held: denser   ENV re-sync   EDIT mute the part   REC keys play   OCT the part's octave
 *   GLO held + black: mutes      HOME held + white 1-8: a snapshot at the bar      SAVE held + white 1-8: keep one
 *   SEL held + white 1-12: the root      SEL held + PRESETS: the scale */

#define ACCENT RGB(140, 230, 120)
enum { PG_RHYTHM, PG_PITCH, PG_FEEL, PG_SOUND, NPG_K, PG_FIELD = NPG_K };
static const char *const PG_NM[] = {"RHYTHM", "PITCH", "FEEL", "SOUND", "FIELD"};
static const uint16_t PART_C[NTRK] = {RGB(255, 110, 90), RGB(255, 170, 80), RGB(250, 225, 100), RGB(140, 230, 120),
                                      RGB(90, 210, 220), RGB(110, 150, 255), RGB(190, 130, 255), RGB(255, 130, 200)};
static const uint8_t DRUM_NOTES[16] = {36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 48, 49, 51, 56, 57};

#define SET_MAGIC 0x3153594Du
typedef struct {
    uint32_t magic;
    uint8_t slot, ble_off, rsv[2];
} settings_t;
static settings_t set;

static struct {
    uint8_t page, prev_page, part, grow;
    uint8_t keys_play;
    uint8_t dirty, save_wait;
    int16_t key_note[NKEYS];
} U;

static const char *const CARD_TEXT[NB] = {
    "FX: LOCK. While lit no register changes: the parts repeat what they play now. Again to let them drift.",
    ("SEL held: white keys 1-12 set the key's root, PRESETS the scale. Everything follows at once, "
     "because parts keep scale degrees, not notes."),
    "ENV: RE-SYNC. Every part starts again from its first step, together.",
    "LFO held: DENSER. Every part gets a quarter more pulses while it is held.",
    "EDIT mutes or unmutes the part shown.",
    ("GLO: the FIELD page (tempo, key, swing, density, tide, clock, slots). "
     "Held with black keys 1-8: mute those parts."),
    "HOME held + white keys 1-8: that snapshot comes back at the next bar. Lit keys hold one.",
    "SAVE: the project to its slot. Held + white keys 1-8: keep the whole field as that snapshot.",
    "ARP: DICE. A new register for the part: new notes, same rhythm. PRESETS turned left goes back.",
    "SEQ: the next page (RHYTHM, PITCH, FEEL, SOUND), as SELECT turns them.",
    "PLAY starts and stops.",
    "REC: the keys play the part's sound (in the key) instead of choosing its pool.",
    "OCT-: the part an octave down.",
    "OCT+: the part an octave up.",
#if PLAT_SMK37
    0, 0, 0, 0,
#endif
};

static void dirty(void) { U.dirty = 1; }
static part_t *pt(void) { return &P.part[U.part % NTRK]; }
static void settings_save(void) { plat_store_save(OBJ_SETTINGS, &set, sizeof set); }
int mycel_ble_off(void) { return set.ble_off; }

static void project_save_now(void)
{
    int r;
    P.bpm10 = T.bpm10;
    r = slot_save(set.slot, PROJ_MAGIC, PROJ_VER, &P, sizeof P);
    if (r == 0)
        U.dirty = 0;
    app_say(r == 0 ? "SAVED" : "SAVE", r == 0 ? "" : "FAILED");
}
static void transport_wait_stop(void)
{
    uint32_t t0 = plat_ms();
    T.want = 2;
    while (T.playing && plat_ms() - t0 < 200u)
        plat_yield();
}

/* ---- the field (GLO) page ---- */
static void v_u8(char *o, uint32_t v) { i_text((int32_t)v, o); }
static void fv_tempo(char *o) { o[0] = 0; i_cat(o, T.bpm10 / 10); s_cat(o, "."); i_cat(o, T.bpm10 % 10); }
static void fv_root(char *o) { s_cpy(o, ROOT_NM[P.root % 12u]); }
static void fv_scale(char *o) { s_cpy(o, SCALE_NM[P.scale % NSCALE]); }
static void fv_swing(char *o) { v_u8(o, P.swing); s_cat(o, "%"); }
static void fv_dens(char *o) { o[0] = 0; sgn_cat(o, (int32_t)P.density - 8); }
static void fv_tide(char *o) { o[0] = 0; sgn_cat(o, P.tide); s_cat(o, "%"); }
static void fv_clk(char *o) { s_cpy(o, CK_NM[T.clk_mode % NCK]); }
static void fv_clko(char *o) { s_cpy(o, T.clk_send ? "SEND" : "OFF"); }
static void fv_et(char *o) { v_u8(o, P.echo_time); }
static void fv_ef(char *o) { v_u8(o, P.echo_fb); }
static void fv_slot(char *o) { o[0] = 0; i_cat(o, set.slot + 1); s_cat(o, slot_used(set.slot, PROJ_MAGIC) ? "  (SAVED)" : "  (EMPTY)"); }
static void fv_ble(char *o) { s_cpy(o, set.ble_off ? "OFF" : "ON"); }
static void ic(uint8_t *f, int32_t d, int32_t lo, int32_t hi) { *f = (uint8_t)clamp((int32_t)*f + d, lo, hi); dirty(); }
static void fc_tempo(int32_t d) { T.bpm10 = (uint16_t)clamp((int32_t)T.bpm10 + d * 10, 300, 3000); P.bpm10 = T.bpm10; dirty(); }
static void fc_root(int32_t d) { P.root = (uint8_t)((P.root + 12 + d % 12) % 12); dirty(); }
static void fc_scale(int32_t d) { ic(&P.scale, d, 0, NSCALE - 1); }
static void fc_swing(int32_t d) { ic(&P.swing, d, 50, 75); }
static void fc_dens(int32_t d) { ic(&P.density, d, 0, 16); }
static void fc_tide(int32_t d) { ic(&P.tide, d, 0, 100); }
static void fc_clk(int32_t d) { T.clk_mode = (uint8_t)clamp(T.clk_mode + d, 0, NCK - 1); P.clk_mode = T.clk_mode; dirty(); }
static void fc_clko(int32_t d) { T.clk_send = d > 0; P.clk_send = T.clk_send; dirty(); }
static void fc_et(int32_t d) { ic(&P.echo_time, d, 0, 127); project_apply(); }
static void fc_ef(int32_t d) { ic(&P.echo_fb, d, 0, 127); project_apply(); }
static void fc_slot(int32_t d) { set.slot = (uint8_t)clamp(set.slot + d, 0, NSLOTS - 1); settings_save(); }
static void fc_ble(int32_t d) { set.ble_off = d < 0; plat_ble_enable(!set.ble_off); settings_save(); }
static void fa_save(void)
{
    if (T.playing) {
        U.save_wait = 1;
        app_say("SAVES", "AT THE STOP");
    } else {
        project_save_now();
    }
}
static void fa_load(void)
{
    if (T.playing)
        transport_wait_stop();
    if (slot_load(set.slot, PROJ_MAGIC, &P, sizeof P)) {
        project_apply();
        U.dirty = 0;
        app_say("LOADED", "");
    } else {
        app_say("SLOT", "EMPTY");
    }
}
static void fa_new(void)
{
    if (T.playing)
        transport_wait_stop();
    project_new();
    project_apply();
    dirty();
    app_say("NEW", "FIELD");
}
static const row_def_t FIELD_ROWS[] = {
    {"TEMPO", fv_tempo, fc_tempo, 0},   {"ROOT", fv_root, fc_root, 0},       {"SCALE", fv_scale, fc_scale, 0},
    {"SWING", fv_swing, fc_swing, 0},   {"DENSITY (ALL)", fv_dens, fc_dens, 0}, {"TIDE (+MUTATE)", fv_tide, fc_tide, 0},
    {"CLOCK", fv_clk, fc_clk, 0},       {"CLOCK OUT", fv_clko, fc_clko, 0},  {"ECHO TIME", fv_et, fc_et, 0},
    {"ECHO FEEDBACK", fv_ef, fc_ef, 0}, {"SLOT", fv_slot, fc_slot, 0},       {"SAVE TO SLOT", 0, 0, fa_save},
    {"LOAD SLOT", 0, 0, fa_load},       {"NEW FIELD", 0, 0, fa_new},         {"BLUETOOTH", fv_ble, fc_ble, 0},
};
#define NFIELD (sizeof FIELD_ROWS / sizeof FIELD_ROWS[0])

/* ---- the four knobs of each page ---- */
static void knob(uint32_t k, int32_t d)
{
    part_t *p = pt();
    patch_t *in = &P.ins[U.part % NTRK];
    switch (U.page * 4u + k) {
    case 0: p->steps = (uint8_t)clamp(p->steps + d, 1, 32); p->pulses = (uint8_t)clamp(p->pulses, 0, p->steps); break;
    case 1: p->pulses = (uint8_t)clamp(p->pulses + d, 0, p->steps); break;
    case 2: p->rotate = (uint8_t)((p->rotate + p->steps + d % p->steps) % p->steps); break;
    case 3: p->div = (uint8_t)clamp(p->div + d, 0, NDIV - 1); break;
    case 4: p->len = (uint8_t)clamp(p->len + d, 2, 16); break;
    case 5: p->mutate = (uint8_t)clamp(p->mutate + d * 2, 0, 100); break;
    case 6: p->range = (uint8_t)clamp(p->range + d, 0, 16); break;
    case 7: p->mode = (uint8_t)clamp(p->mode + d, 0, NMODE - 1); break;
    case 8: p->chance = (uint8_t)clamp(p->chance + d * 2, 0, 100); break;
    case 9: p->accent = (uint8_t)clamp(p->accent + d, 0, p->steps); break;
    case 10: p->gate = (uint8_t)clamp(p->gate + d * 2, 5, 100); break;
    case 11: p->ratchet = (uint8_t)clamp(p->ratchet + d * 2, 0, 100); break;
    case 12: in->cutoff = (uint8_t)clamp(in->cutoff + d * 2, 0, 127); break;
    case 13: in->decay = (uint8_t)clamp(in->decay + d * 2, 0, 127); break;
    case 14: in->echo = (uint8_t)clamp(in->echo + d * 2, 0, 127); break;
    default:
        if (in->type == PT_DRUM)
            in->level = (uint8_t)clamp(in->level + d * 2, 0, 127);
        else
            in->wave = (uint8_t)clamp(in->wave + d, 0, NWAVE - 1);
        break;
    }
    dirty();
}
static void knob_text(uint32_t k, char *lab, char *val)
{
    const part_t *p = pt();
    const patch_t *in = &P.ins[U.part % NTRK];
    static const char *const LAB[16] = {"STEPS", "PULSES", "ROTATE", "DIVISION", "LENGTH", "MUTATE", "RANGE", "MODE",
                                        "CHANCE", "ACCENTS", "GATE", "RATCHET", "CUTOFF", "DECAY", "ECHO", "WAVE"};
    uint32_t i = U.page * 4u + k;
    s_cpy(lab, LAB[i % 16u]);
    val[0] = 0;
    switch (i) {
    case 0: i_cat(val, p->steps); break;
    case 1: i_cat(val, (int32_t)pulses_now(p)); break;
    case 2: i_cat(val, p->rotate); break;
    case 3: s_cpy(val, DIV_NM[p->div % NDIV]); break;
    case 4: i_cat(val, p->len); break;
    case 5: if (p->mutate == 0u) s_cpy(val, "LOCKED"); else { i_cat(val, p->mutate); s_cat(val, "%"); } break;
    case 6: if (p->range) i_cat(val, p->range); else s_cpy(val, "ALL"); break;
    case 7: s_cpy(val, MODE_NM[p->mode % NMODE]); break;
    case 8: i_cat(val, p->chance); s_cat(val, "%"); break;
    case 9: i_cat(val, p->accent); break;
    case 10: i_cat(val, p->gate); s_cat(val, "%"); break;
    case 11: i_cat(val, p->ratchet); s_cat(val, "%"); break;
    case 12: i_cat(val, in->cutoff); break;
    case 13: i_cat(val, in->decay); break;
    case 14: i_cat(val, in->echo); break;
    default:
        if (in->type == PT_DRUM) {
            s_cpy(lab, "LEVEL");
            i_cat(val, in->level);
        } else {
            s_cpy(val, WAVE_NM[in->wave % NWAVE]);
        }
        break;
    }
}

static void key_down(uint32_t k)
{
    int w = white_of(k), b = black_of(k);
    if (b >= 0 && b < (int)NTRK) {
        if (HELD(B_GLO)) {
            kit_use(B_GLO);
            P.part[b].muted ^= 1u;
            dirty();
        } else {
            U.part = (uint8_t)b;
            sel_part = U.part;
        }
        return;
    }
    if (w < 0)
        return;
    if (HELD(B_HOME) && w < (int)NSNAP) {
        kit_use(B_HOME);
        if (P.snap_used >> w & 1u) {
            if (T.playing)
                G.snap_cue = (uint8_t)w;
            else
                snap_recall_now((uint32_t)w);
            app_say("SNAPSHOT", T.playing ? "AT THE BAR" : "BACK");
        } else {
            app_say("SNAPSHOT", "EMPTY");
        }
        return;
    }
    if (HELD(B_SAVE) && w < (int)NSNAP) {
        kit_use(B_SAVE);
        snap_store((uint32_t)w);
        dirty();
        app_say("SNAPSHOT", "KEPT");
        return;
    }
    if (HELD(B_SEL) && w < 12) {
        kit_use(B_SEL);
        P.root = (uint8_t)w;
        dirty();
        app_say("ROOT", ROOT_NM[w]);
        return;
    }
    if (P.ins[U.part].type == PT_DRUM) {                /* a drum part: the key is its sound */
        pt()->drum = DRUM_NOTES[w];
        live_note(9, &P.ins[U.part], pt()->drum, 100, 1);
        live_note(9, &P.ins[U.part], pt()->drum, 0, 0);
    } else {
        pt()->pool ^= (uint16_t)(1u << w);
    }
    dirty();
}

static void input(void)
{
    uint32_t k;
    kit_input();
    if (DOWN(B_PLAY))
        T.want = T.playing ? 2 : 1;
    if (DOWN(B_FX)) {
        G.lock = !G.lock;
        app_say("LOCK", G.lock ? "ON" : "OFF");
    }
    if (DOWN(B_ARP)) {
        dice(U.part);
        app_say("DICE", "NEW REGISTER");
    }
    if (DOWN(B_ENV))
        G.resync = 1;
    G.boost = (uint8_t)HELD(B_LFO);
    if (DOWN(B_EDIT)) {
        pt()->muted ^= 1u;
        dirty();
    }
    if (DOWN(B_REC)) {
        U.keys_play = !U.keys_play;
        app_say("KEYS", U.keys_play ? "PLAY THE PART" : "CHOOSE THE POOL");
    }
    if (DOWN(B_SEQ))
        U.page = (uint8_t)(U.page >= PG_FIELD ? PG_RHYTHM : (U.page + 1u) % NPG_K);
    if (TAPPED(B_GLO))
        U.page = U.page == PG_FIELD ? U.prev_page : PG_FIELD;
    if (U.page != PG_FIELD)
        U.prev_page = U.page;
    if (TAPPED(B_SAVE)) {
        if (T.playing) {
            U.save_wait = 1;
            app_say("SAVES", "AT THE STOP");
        } else {
            project_save_now();
        }
    }
    if (DOWN(B_OCTDN) || DOWN(B_OCTUP)) {
        pt()->octave = (int8_t)clamp(pt()->octave + (DOWN(B_OCTUP) ? 1 : -1), -3, 3);
        dirty();
    }
    if (K.enc[EN_ALGO]) {
        U.part = (uint8_t)clamp(U.part + K.enc[EN_ALGO], 0, NTRK - 1);
        sel_part = U.part;
    }
    if (U.page == PG_FIELD) {
        kit_list_input(FIELD_ROWS, NFIELD, &U.grow);
    } else {
        if (K.enc[EN_SELECT])
            U.page = (uint8_t)clamp(U.page + K.enc[EN_SELECT], 0, NPG_K - 1);
        for (k = 0; k < 4u; k++)
            if (K.enc[EN_K1 + k])
                knob(k, K.enc[EN_K1 + k]);
        if (K.enc[EN_PRESET]) {
            if (HELD(B_SEL)) {
                kit_use(B_SEL);
                fc_scale(K.enc[EN_PRESET]);
            } else {
                history_back(U.part, K.enc[EN_PRESET]);
                dirty();
            }
        }
    }
    for (k = 0; k < NKEYS; k++) {
        if (K.kdown >> k & 1u) {
            if (U.keys_play && black_of(k) < 0 && !HELD(B_HOME) && !HELD(B_SAVE) && !HELD(B_SEL)) {
                int32_t n = scale_snap(53 + (int32_t)k, P.root, P.scale);
                live_note(9, &P.ins[U.part], n, K.vel[k], 1);
                U.key_note[k] = (int16_t)n;
            } else {
                key_down(k);
            }
        }
        if ((K.kup >> k & 1u) && U.key_note[k] >= 0) {
            live_note(9, &P.ins[U.part], U.key_note[k], 0, 0);
            U.key_note[k] = -1;
        }
    }
    kit_cards(CARD_TEXT);
}

/* ---- the screen ---- */
static void draw_ring(int32_t cx, int32_t cy, int32_t r)
{
    const part_t *p = pt();
    uint32_t i, n = p->steps ? p->steps : 1u;
    int32_t cur = G.t[U.part].step;
    uint16_t c = PART_C[U.part % NTRK];
    for (i = 0; i < n; i++) {
        float a = (float)i / (float)n * 6.2831853f - 1.5707963f;
        int32_t x = cx + (int32_t)(fm_cosf(a) * (float)r), y = cy + (int32_t)(fm_sinf(a) * (float)r);
        int hit = part_hit(p, i), acc = hit && euclid(i, p->accent, p->steps, p->rotate);
        int32_t s = acc ? 5 : hit ? 4 : 1;
        if (T.playing && (int32_t)i == cur) {
            cv_rect(x - 6, y - 6, 12, 12, hit ? K_FG : K_DIM);
        }
        cv_rect(x - s, y - s, 2 * s + 1, 2 * s + 1, hit ? (acc ? K_FG : c) : K_FAINT);
    }
    /* inside: the note and the register */
    if (T.playing && G.t[U.part].note >= 0 && heard[U.part]) {
        char t[8];
        if (P.ins[U.part].type == PT_DRUM)
            s_cpy(t, DRUM_NM[drum_of(G.t[U.part].note) % NDRUM]);
        else
            note_name(G.t[U.part].note, t);
        text_c(cx, cy - 18, &FONT_B, t, c);
    }
    {
        uint32_t len = p->len < 2u ? 2u : p->len, b;
        int32_t w = (int32_t)len * 5, x0 = cx - w / 2;
        for (b = 0; b < len; b++)
            cv_rect(x0 + (int32_t)b * 5, cy + 6, 4, 8, (p->reg >> (len - 1u - b) & 1u) ? c : K_LINE);
        text_c(cx, cy + 16, &FONT_T, G.lock ? "LOCKED" : p->mutate + P.tide ? "DRIFTING" : "LOOPING", G.lock ? K_REC : K_DIM);
    }
}

static void draw_pool(int32_t y)                        /* the 16 white keys: the pool (or the drum sounds) */
{
    const part_t *p = pt();
    uint32_t k;
    int drum = P.ins[U.part].type == PT_DRUM;
    for (k = 0; k < 16u; k++) {
        int32_t x = 4 + (int32_t)k * 15;
        int on = drum ? DRUM_NOTES[k] == p->drum : (int)(p->pool >> k & 1u);
        char t[6];
        cv_rect(x, y, 13, 12, on ? PART_C[U.part % NTRK] : K_PANEL);
        if (!drum) {
            int32_t n = scale_deg((int32_t)k, P.root, P.scale, 0);
            s_cpy(t, ROOT_NM[n % 12]);
            t[1] = 0;
            cv_text(x + 3, y - 1, &FONT_T, t, on ? K_BG : K_DIM);
        }
    }
}

static void draw_lanes(int32_t y)                       /* the eight parts, a line each */
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++, y += 9) {
        const part_t *p = &P.part[t];
        uint32_t n = p->steps ? p->steps : 1u;
        int32_t w = n > 16u ? 3 : 6;
        char s[4] = {(char)('1' + t), 0};
        uint16_t c = p->muted ? K_FAINT : PART_C[t];
        if (t == U.part)
            cv_rect(0, y, 240, 9, K_PANEL);
        cv_text(4, y - 2, &FONT_T, s, c);
        for (i = 0; i < n; i++) {
            int hit = part_hit(p, i), now = T.playing && (int32_t)i == G.t[t].step;
            cv_rect(16 + (int32_t)i * (w + 1), y + 2, w, 5, now ? K_FG : hit ? c : K_LINE);
        }
        if (heard[t] && !p->muted)
            cv_rect(234, y + 1, 4, 6, c);
        if (p->muted)
            text_r(232, y - 2, &FONT_T, "MUTE", K_FAINT);
    }
}

static void draw(void)
{
    char h[24], lab[16], val[16];
    uint32_t k;
    s_cpy(h, PG_NM[U.page]);
    kit_header("MYCEL-8", h, ACCENT, T.playing, 0, T.bpm10);
    cv_begin(240, 200, K_BG);
    if (K.card < NB && CARD_TEXT[K.card]) {
        kit_card(K.card, CARD_TEXT[K.card], 200, ACCENT);
    } else if (U.page == PG_FIELD) {
        cv_text(6, 0, &FONT_T, "THE FIELD: EVERY PART", ACCENT);
        kit_list_draw(FIELD_ROWS, NFIELD, U.grow, 14, 184, ACCENT);
    } else {
        char t[24] = "PART ";
        i_cat(t, U.part + 1);
        cv_text(6, 0, &FONT_XS, t, PART_C[U.part % NTRK]);
        cv_text(6, 14, &FONT_T, P.ins[U.part].type == PT_DRUM ? "DRUMS" : WAVE_NM[P.ins[U.part].wave % NWAVE], K_DIM);
        if (pt()->muted)
            cv_text(6, 26, &FONT_T, "MUTED", K_REC);
        draw_ring(82, 52, 39);
        for (k = 0; k < 4u; k++) {                      /* the page's knobs, with their values */
            int32_t y = 2 + (int32_t)k * 22;
            knob_text(k, lab, val);
            cv_text(134, y, &FONT_T, lab, K_DIM);
            text_r(234, y + 8, &FONT_XS, val, K_FG);
            cv_rect(134, y + 21, 100, 1, K_LINE);
        }
        s_cpy(t, ROOT_NM[P.root % 12u]);
        s_cat(t, " ");
        s_cat(t, SCALE_NM[P.scale % NSCALE]);
        cv_text(6, 93, &FONT_T, t, K_DIM);
        text_r(234, 93, &FONT_T, U.keys_play ? "KEYS PLAY" : P.ins[U.part].type == PT_DRUM ? "KEYS: SOUND" : "KEYS: POOL",
               U.keys_play ? K_REC : K_DIM);
        draw_pool(107);
        draw_lanes(124);
        if (G.snap_cue != 0xFFu)
            text_r(234, 0, &FONT_T, "SNAPSHOT AT BAR", K_REC);
    }
    cv_blit(0, 22);
    if (U.page == PG_FIELD) {
        kit_knobs("VALUE", "", "", "", ACCENT);
    } else {
        static const char *const KN[NPG_K][4] = {{"STEPS", "PULSES", "ROTATE", "DIV"}, {"LENGTH", "MUTATE", "RANGE", "MODE"},
                                                 {"CHANCE", "ACCENT", "GATE", "RATCHET"}, {"CUTOFF", "DECAY", "ECHO", "WAVE"}};
        kit_knobs(KN[U.page][0], KN[U.page][1], KN[U.page][2], KN[U.page][3], PART_C[U.part % NTRK]);
    }
}

static void lights(void)
{
    uint32_t btn = 0, keys = 0, dkeys = 0, k;
    if (T.playing)
        btn |= 1u << B_PLAY | LED_PLAY_GREEN;
    if (G.lock)
        btn |= 1u << B_FX;
    if (U.keys_play)
        btn |= 1u << B_REC;
    if (U.dirty)
        btn |= 1u << B_SAVE;
    if (U.page == PG_FIELD)
        btn |= 1u << B_GLO;
    if (pt()->muted)
        btn |= 1u << B_EDIT;
    for (k = 0; k < NTRK; k++) {                        /* black keys: the parts (bright: shown; dim: playing) */
        if (k == U.part)
            keys |= 1u << BKEY[k];
        else if (!P.part[k].muted)
            dkeys |= 1u << BKEY[k];
    }
    if (HELD(B_HOME) || HELD(B_SAVE)) {
        for (k = 0; k < NSNAP; k++)
            if (P.snap_used >> k & 1u)
                dkeys |= 1u << WKEY[k];
    } else if (!U.keys_play) {
        for (k = 0; k < 16u; k++)
            if (P.ins[U.part].type == PT_DRUM ? DRUM_NOTES[k] == pt()->drum : (pt()->pool >> k & 1u))
                dkeys |= 1u << WKEY[k];
    }
    keys |= K.keys;
    plat_lights(btn, keys, 0, dkeys, 1);
}
