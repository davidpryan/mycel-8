/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* MYCEL-8: eight generative parts. No notes are written anywhere: each part is described by a few numbers and grows
 * its music from them, the way the Torso T-1, the Music Thing Turing Machine and Euclidean sequencers do.
 *   WHEN   a Euclidean rhythm: PULSES spread as evenly as they go over STEPS, turned by ROTATE, at a DIVISION
 *   WHAT   a shift register (the Turing Machine): LENGTH bits loop, each bit leaving may flip by MUTATE percent.
 *          0 % repeats for ever (locked), 100 % is new every time, in between it drifts. The register's value picks a
 *          note from the part's POOL: the scale degrees lit on the 16 white keys, so it is always in key.
 *   FEEL   chance a hit plays, accents (a second Euclid), gate length, ratchets
 * Everything is a seed: the same numbers play the same music, a DICE is a new register, and the last 8 registers are
 * kept so PRESETS turned back returns to one you liked. Eight snapshots keep the whole field. */

#define NTRK 8u
#define NSNAP 8u
#define NHIST 8u
static const uint16_t DIV_T[] = {12, 16, 24, 32, 48, 64, 96, 192, 384};
static const char *const DIV_NM[] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "BAR"};
#define NDIV (sizeof DIV_T / sizeof DIV_T[0])
enum { MD_TURING, MD_CYCLE, MD_BOUNCE, MD_RANDOM, MD_DRONE, NMODE };
static const char *const MODE_NM[NMODE] = {"TURING", "CYCLE", "BOUNCE", "RANDOM", "DRONE"};

typedef struct {
    uint8_t steps, pulses, rotate, div;                 /* WHEN */
    uint8_t len, mutate, range, mode;                   /* WHAT: register length, flip %, notes of the pool used */
    uint8_t chance, accent, gate, ratchet;              /* FEEL: %, accent pulses, gate %, ratchet % */
    uint16_t pool;                                      /* the scale degrees it may play (bit k: degree k, 0..15) */
    uint16_t reg;                                       /* the register */
    int8_t octave;
    uint8_t drum;                                       /* a drum part's note (36 kick ...) */
    uint8_t muted, rsv;
} part_t;

#define PROJ_MAGIC 0x384C4359u                          /* "YCL8" */
#define PROJ_VER 1u
typedef struct {
    uint16_t bpm10;
    uint8_t root, scale, swing, tide, clk_mode, clk_send;
    uint8_t echo_time, echo_fb, density, rsv0;
    part_t part[NTRK];
    patch_t ins[NTRK];
    part_t snap[NSNAP][NTRK];
    uint8_t snap_used;                                  /* bit per snapshot */
    uint8_t rsv[31];
} proj_t;
static proj_t P;

static struct {
    uint32_t seed;
    uint8_t lock;                                       /* FX: no register changes */
    uint8_t boost;                                      /* LFO held: more pulses on every part */
    volatile uint8_t resync;                            /* ENV: every part from its first step */
    volatile uint8_t snap_cue;                          /* a snapshot waiting for the bar (0xFF none) */
    struct {
        uint32_t t;                                     /* ticks into the step */
        int16_t step;                                   /* the step playing (-1 before the first) */
        uint8_t idx;                                    /* CYCLE / BOUNCE position */
        int8_t dir;
        int16_t note;                                   /* the note playing (-1 none) */
        uint8_t hit;                                    /* this step was a hit (the screen) */
        uint8_t rat_left, rat_gap, rat_t, rat_vel;      /* ratchets still to play */
        uint8_t swing_wait;                             /* ticks a swung hit waits */
        uint8_t pending_vel;
        uint16_t hist[NHIST];                           /* the last registers (PRESETS back) */
        uint8_t hist_n, hist_at;
    } t[NTRK];
} G;

/* ---- the Euclid ---- */
static int euclid(uint32_t i, uint32_t pulses, uint32_t steps, uint32_t rot)
{
    if (!steps || !pulses)
        return 0;
    if (pulses >= steps)
        return 1;
    i = (i + steps - rot % steps) % steps;
    return (i * pulses) % steps < pulses;
}
static uint32_t pulses_now(const part_t *p)
{
    int32_t n = (int32_t)p->pulses + ((int32_t)P.density - 8) * (int32_t)p->steps / 16 + (G.boost ? (int32_t)p->steps / 4 : 0);
    return (uint32_t)clamp(n, 0, p->steps);
}
static int part_hit(const part_t *p, uint32_t i) { return euclid(i, pulses_now(p), p->steps, p->rotate); }

/* the pool as a list of degrees */
static uint32_t pool_list(const part_t *p, int8_t *deg)
{
    uint32_t k, n = 0;
    for (k = 0; k < 16u; k++)
        if (p->pool >> k & 1u)
            deg[n++] = (int8_t)k;
    return n;
}

/* the register moves one place; the bit that leaves comes back in, flipped by MUTATE (plus the tide) */
static void reg_step(uint32_t t)
{
    part_t *p = &P.part[t];
    uint32_t len = p->len < 2u ? 2u : p->len > 16u ? 16u : p->len, top = p->reg >> (len - 1u) & 1u;
    uint32_t m = (uint32_t)clamp((int32_t)p->mutate + (int32_t)P.tide, 0, 100);
    if (!G.lock && m && xs32(&G.seed) % 100u < m)
        top ^= 1u;
    p->reg = (uint16_t)(((p->reg << 1) | top) & ((1u << len) - 1u));
}

static int32_t part_note(uint32_t t)
{
    part_t *p = &P.part[t];
    int8_t deg[16];
    uint32_t n = pool_list(p, deg), use, i = 0;
    if (P.ins[t].type == PT_DRUM)
        return p->drum;
    if (!n)
        return -1;
    use = p->range ? (p->range < n ? p->range : n) : n;
    switch (p->mode) {
    case MD_TURING: i = (p->reg & 0xFFu) * use / 256u; break;
    case MD_CYCLE: i = G.t[t].idx++ % use; break;
    case MD_BOUNCE:
        if (use > 1u) {
            int32_t x = (int32_t)G.t[t].idx + G.t[t].dir;
            if (x < 0 || x >= (int32_t)use) {
                G.t[t].dir = (int8_t)-G.t[t].dir;
                x = (int32_t)G.t[t].idx + G.t[t].dir;
            }
            G.t[t].idx = (uint8_t)clamp(x, 0, (int32_t)use - 1);
        }
        i = G.t[t].idx;
        break;
    case MD_RANDOM: i = xs32(&G.seed) % use; break;
    default: i = 0; break;
    }
    return scale_deg(deg[i % n], P.root, P.scale, 48 + 12 * p->octave);
}

static uint32_t step_ticks(const part_t *p) { return DIV_T[p->div % NDIV]; }

static void part_play(uint32_t t, uint32_t vel)
{
    part_t *p = &P.part[t];
    int32_t n;
    if (p->muted)
        return;
    n = part_note(t);
    if (n < 0)
        return;
    if (G.t[t].note >= 0 && P.ins[t].type != PT_DRUM)
        offs_owner(t);
    play_note(t, &P.ins[t], n, vel,
              ticks_to_samples((step_ticks(p) * (uint32_t)clamp(p->gate, 5, 100)) / 100u + 1u), 1);
    G.t[t].note = (int16_t)n;
}

static void step_start(uint32_t t)
{
    part_t *p = &P.part[t];
    uint32_t i, vel, st = step_ticks(p);
    G.t[t].step = (int16_t)((G.t[t].step + 1) % (p->steps ? p->steps : 1));
    i = (uint32_t)G.t[t].step;
    reg_step(t);
    G.t[t].hit = 0;
    if (!part_hit(p, i))
        return;
    if (p->chance < 100u && xs32(&G.seed) % 100u >= p->chance)
        return;
    G.t[t].hit = 1;
    vel = euclid(i, p->accent, p->steps, p->rotate) ? 120u : 82u;
    if (P.ins[t].type == PT_DRUM && vel < 120u)         /* a drum part: the register shades its quiet hits */
        vel = (uint32_t)clamp((int32_t)vel - 24 + (int32_t)(p->reg & 31u), 30, 127);
    if (i & 1u && P.swing > 50u && st <= 48u) {         /* swing: odd steps late */
        G.t[t].swing_wait = (uint8_t)((uint32_t)(P.swing - 50u) * st / 50u);
        G.t[t].pending_vel = (uint8_t)vel;
    } else {
        G.t[t].swing_wait = 0;
        part_play(t, vel);
    }
    if (p->ratchet && xs32(&G.seed) % 100u < p->ratchet && st >= 12u) {
        uint32_t r = 2u + xs32(&G.seed) % 3u;           /* 2..4 hits in the step */
        G.t[t].rat_left = (uint8_t)(r - 1u);
        G.t[t].rat_gap = (uint8_t)(st / r);
        G.t[t].rat_t = 0;
        G.t[t].rat_vel = (uint8_t)(vel * 3u / 4u);
    }
}

static void history_push(uint32_t t)
{
    G.t[t].hist[G.t[t].hist_n % NHIST] = P.part[t].reg;
    G.t[t].hist_n++;
    G.t[t].hist_at = 0;
}
static void dice(uint32_t t)                            /* a new register (the old one kept) */
{
    history_push(t);
    P.part[t].reg = (uint16_t)(xs32(&G.seed) & 0xFFFFu);
}
static void history_back(uint32_t t, int32_t d)         /* PRESETS: back through the kept registers, or a new dice */
{
    uint32_t n = G.t[t].hist_n < NHIST ? G.t[t].hist_n : NHIST;
    if (d > 0 && G.t[t].hist_at == 0u) {
        dice(t);
        return;
    }
    G.t[t].hist_at = (uint8_t)clamp((int32_t)G.t[t].hist_at - d, 0, (int32_t)n);
    if (G.t[t].hist_at)
        P.part[t].reg = G.t[t].hist[(G.t[t].hist_n - G.t[t].hist_at) % NHIST];
}

static void snap_store(uint32_t s)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        P.snap[s][t] = P.part[t];
    P.snap_used |= (uint8_t)(1u << s);
}
static void snap_recall_now(uint32_t s)
{
    uint32_t t;
    if (!(P.snap_used >> s & 1u))
        return;
    for (t = 0; t < NTRK; t++) {
        uint8_t muted = P.part[t].muted;
        P.part[t] = P.snap[s][t];
        P.part[t].muted = muted;
    }
}

static void seq_started(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        G.t[t].t = 0;
        G.t[t].step = -1;
        G.t[t].note = -1;
        G.t[t].rat_left = 0;
        G.t[t].swing_wait = 0;
        G.t[t].dir = 1;
        G.t[t].idx = 0;
    }
}
static void seq_stopped(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        G.t[t].note = -1;
}

static void seq_tick(void)
{
    uint32_t t;
    if (T.tick % 384u == 0u && G.snap_cue != 0xFFu) {   /* the bar: a snapshot waiting */
        snap_recall_now(G.snap_cue);
        G.snap_cue = 0xFFu;
    }
    if (G.resync) {
        G.resync = 0;
        seq_started();
    }
    for (t = 0; t < NTRK; t++) {
        part_t *p = &P.part[t];
        uint32_t st = step_ticks(p);
        if (G.t[t].step < 0 || G.t[t].t >= st) {
            G.t[t].t = 0;
            step_start(t);
        } else {
            if (G.t[t].swing_wait && --G.t[t].swing_wait == 0u)
                part_play(t, G.t[t].pending_vel);
            if (G.t[t].rat_left && ++G.t[t].rat_t >= G.t[t].rat_gap) {
                G.t[t].rat_t = 0;
                G.t[t].rat_left--;
                part_play(t, G.t[t].rat_vel);
            }
        }
        G.t[t].t++;
    }
}

/* MIDI in and the keys in PLAY mode: the selected part's sound, snapped to the key */
static volatile uint8_t sel_part;
static void seq_midi_note(int32_t note, uint32_t vel, uint32_t chan)
{
    (void)chan;
    if (vel)
        play_note(9, &P.ins[sel_part % NTRK], scale_snap(note, P.root, P.scale), vel, 0, 0);
    else
        note_end(9, &P.ins[sel_part % NTRK], scale_snap(note, P.root, P.scale));
}

/* ---- a new project: a field that grows at once ---- */
static void part_set(uint32_t t, uint32_t steps, uint32_t pulses, uint32_t rot, uint32_t div, uint32_t pool,
                     uint32_t mutate, int32_t oct, uint32_t drum)
{
    part_t *p = &P.part[t];
    p->steps = (uint8_t)steps;
    p->pulses = (uint8_t)pulses;
    p->rotate = (uint8_t)rot;
    p->div = (uint8_t)div;
    p->len = 8;
    p->mutate = (uint8_t)mutate;
    p->range = 0;
    p->mode = MD_TURING;
    p->chance = 100;
    p->accent = (uint8_t)(pulses / 2u);
    p->gate = 50;
    p->ratchet = 0;
    p->pool = (uint16_t)pool;
    p->reg = (uint16_t)(0xA5C3u >> t);
    p->octave = (int8_t)oct;
    p->drum = (uint8_t)drum;
    p->muted = 0;
}

static void project_new(void)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)&P;
    for (i = 0; i < sizeof P; i++)
        b[i] = 0;
    P.bpm10 = 1150;
    P.root = 9;                                         /* A minor */
    P.scale = SC_MINOR;
    P.swing = 56;
    P.tide = 0;
    P.density = 8;
    P.clk_mode = CK_INTERNAL;
    P.clk_send = 1;
    P.echo_time = 90;
    P.echo_fb = 70;
    for (i = 0; i < NTRK; i++)
        patch_default(&P.ins[i], PT_SYNTH, i + 1u);
    patch_default(&P.ins[0], PT_DRUM, 10);
    patch_default(&P.ins[1], PT_DRUM, 10);
    patch_default(&P.ins[2], PT_DRUM, 10);
    part_set(0, 16, 4, 0, 2, 0, 0, 0, 36);              /* kick */
    part_set(1, 16, 2, 4, 2, 0, 20, 0, 38);             /* snare */
    part_set(2, 16, 11, 1, 2, 0, 35, 0, 42);            /* hats */
    P.part[2].ratchet = 12;
    P.part[2].accent = 3;
    part_set(3, 16, 7, 0, 2, 0x0015u, 10, -1, 0);       /* bass: degrees 1 3 5 */
    P.ins[3].octave = -1;
    P.ins[3].cutoff = 45;
    P.ins[3].reso = 85;
    P.ins[3].envmod = 70;
    P.ins[3].decay = 40;
    P.ins[3].sustain = 10;
    part_set(4, 13, 5, 2, 2, 0x00B5u, 30, 0, 0);       /* a 13-step lead */
    P.ins[4].wave = WV_SQR;
    P.ins[4].echo = 70;
    P.ins[4].decay = 45;
    P.ins[4].sustain = 30;
    part_set(5, 7, 3, 0, 4, 0x0295u, 15, 1, 0);         /* a 7-step bell at eighths */
    P.ins[5].wave = WV_TRI;
    P.ins[5].echo = 90;
    P.ins[5].decay = 50;
    P.ins[5].sustain = 0;
    P.part[5].mode = MD_CYCLE;
    part_set(6, 4, 1, 0, 8, 0x0095u, 50, 0, 0);         /* pads: a note a bar */
    P.ins[6].wave = WV_SUPER;
    P.ins[6].attack = 80;
    P.ins[6].release = 90;
    P.ins[6].sustain = 110;
    P.ins[6].level = 60;
    P.ins[6].echo = 60;
    P.part[6].gate = 100;
    part_set(7, 32, 9, 0, 1, 0x0F00u, 60, 1, 0);        /* sparkles, muted */
    P.ins[7].wave = WV_SINE;
    P.ins[7].decay = 20;
    P.ins[7].sustain = 0;
    P.ins[7].echo = 110;
    P.part[7].chance = 60;
    P.part[7].muted = 1;
    P.ins[7].level = 70;
    snap_store(0);
}

static void project_apply(void)
{
    T.bpm10 = P.bpm10;
    T.clk_mode = P.clk_mode;
    T.clk_send = P.clk_send;
    echo_time = 0.05f + (float)P.echo_time * (0.32f / 127.0f);
    echo_fb = (float)P.echo_fb * (0.85f / 127.0f);
}
