//! @file
//! @brief Motion engine core, see motion_core.h
#include "motion_core.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

namespace motion {

int32_t e_shift[BLK_N];
Stats stats;

namespace {

constexpr float TICK_S = 1.f / TICK_HZ;
constexpr uint32_t SAMPLE_Q8 = SAMPLE_TICKS << 8;
constexpr uint32_t SEG_MASK = SEG_N - 1;
constexpr uint32_t BLK_MASK = BLK_N - 1;
constexpr uint32_t EVQ_MASK = EVQ_N - 1;

//! Constant acceleration piece of the path of one block
struct Seg {
    uint32_t t0;      //!< start time (ticks)
    uint32_t dur;     //!< duration (ticks)
    //! motor position from the block start (steps) at dt ticks after t0:
    //! c0 + c1 dt + c2 dt^2 (for E including the pressure advance term)
    float c0[AXES], c1[AXES], c2[AXES];
    uint8_t blk;      //!< block index
    bool last;        //!< last segment of its block
};

//! Geometry of a block in engine coordinates
struct Blk {
    int32_t start[AXES]; //!< start position (steps)
    int32_t end[AXES];   //!< end position (steps)
    float r[AXES];       //!< steps per mm of path (signed)
    float mm;
    uint32_t t0, t_end;
    bool pa;             //!< pressure advance active
    bool e_reset;
};

Seg segs[SEG_N];
uint32_t seg_head, seg_tail;   // free running, index & SEG_MASK
Blk blks[BLK_N];

// blocks waiting for their markers, in order
uint8_t mark_fifo[BLK_N];
uint8_t mark_head, mark_tail;  // free running, index & BLK_MASK
uint8_t mark_ereset_done;      // number of fifo entries (from the tail) whose E reset was emitted

input_shaper::Pulses pulses[3];
int32_t pulse_w[3][input_shaper::MAX_PULSES]; // amplitudes (Q16), see sample_shaped()

//! Position of one motor at the sample times of one shaper pulse. Inside a segment the
//! position is a quadratic function of time, so it is advanced by forward differencing
//! (integer arithmetic only); it is recalculated in floating point when a segment starts.
struct Cursor {
    uint32_t seg;    //!< segment (free running index)
    uint32_t until;  //!< p, d, dd are valid while the sample time is before this
    bool valid;
    int64_t p;       //!< position (steps, Q32)
    int64_t d;       //!< position change to the next sample
    int64_t dd;      //!< change of d
};
Cursor cur[3][input_shaper::MAX_PULSES];
Cursor cur_e;

float pa_k;
uint16_t pa_box;               // box length (samples), 0 = pressure advance off
int64_t pa_b1[PA_MAX_BOX], pa_b2[PA_MAX_BOX];
int64_t pa_s1, pa_s2;
uint16_t pa_idx;
uint32_t pa_recip;             // 2^32 / box^2

uint32_t lead, lag;            // ticks
uint32_t gen;                  // end of the generated time (ticks)
uint32_t traj_end;             // end of the converted trajectory (ticks)
uint32_t last_ev;              // time of the last event (ticks)
float last_exit;               // actual exit speed of the last block
bool have_blocks;              // a block was added in this epoch

int32_t conv_pos[AXES];        // engine position at the end of the converted trajectory
int32_t rest_pos[AXES];        // position before the first segment of the epoch
int32_t e_ref;
int32_t cnt[AXES];             // steps generated (engine position)
int64_t p_prev[AXES];          // sampled position (steps, Q8)

uint32_t ev_q[EVQ_N];
volatile uint16_t ev_head;     // written by the producer
volatile uint16_t ev_tail;     // written by the consumer

inline bool time_before(uint32_t a, uint32_t b) { return int32_t(a - b) < 0; }

void ev_push(uint32_t e) {
    ev_q[ev_head & EVQ_MASK] = e;
    ev_head = uint16_t(ev_head + 1);
    ++stats.events;
}

void emit(uint32_t at, uint8_t mask, uint8_t dirs, uint8_t type, uint8_t arg) {
    uint32_t delta = at - last_ev;
    if (int32_t(delta) < 0) delta = 0;
    while (delta > MAX_EVENT_DELTA) {
        ev_push(MAX_EVENT_DELTA);
        delta -= MAX_EVENT_DELTA;
    }
    ev_push(delta | (uint32_t(mask) << 16) | (uint32_t(dirs) << 20) | (uint32_t(type) << 24) | (uint32_t(arg) << 26));
    last_ev = at;
}

void update_lead_lag() {
    int32_t ahead = 0, behind = 0;
    for (uint8_t a = 0; a < 3; ++a)
        for (uint8_t i = 0; i < pulses[a].num_pulses; ++i) {
            if (-pulses[a].t[i] > ahead) ahead = -pulses[a].t[i];
            if (pulses[a].t[i] > behind) behind = pulses[a].t[i];
        }
    const int32_t pa_span = pa_box ? int32_t(pa_box - 1) * int32_t(SAMPLE_TICKS) : 0;
    if (pa_span > ahead) ahead = pa_span;
    if (pa_span > behind) behind = pa_span;
    lead = uint32_t(ahead);
    lag = uint32_t(behind);
}

//! Segment at time tau, advancing the cursor. nullptr when there is no segment.
const Seg *locate(uint32_t &cursor, uint32_t tau) {
    if (time_before(cursor, seg_tail)) cursor = seg_tail;
    if (cursor == seg_head) return nullptr;
    while (cursor + 1 != seg_head) {
        const Seg &s = segs[cursor & SEG_MASK];
        if (time_before(tau, s.t0 + s.dur)) break;
        ++cursor;
    }
    return &segs[cursor & SEG_MASK];
}

constexpr float SAMPLE_S = float(SAMPLE_TICKS) / TICK_HZ;

inline int64_t q32(int32_t steps) { return int64_t(steps) << 32; }

//! x * 2^32 truncated, |x| < 2^31. The library conversion (__aeabi_f2lz) goes through
//! double precision and costs ~1000 instructions on the Cortex-M0+.
inline int64_t f_to_q32(float x) {
    union { float f; uint32_t u; } v = { x };
    const int32_t e = int32_t((v.u >> 23) & 0xFFu);
    if (!e) return 0;
    const uint32_t m = (v.u & 0x7FFFFFu) | 0x800000u; // x = m * 2^(e - 150)
    const int32_t sh = e - 150 + 32;
    int64_t r;
    if (sh >= 0) r = int64_t(m) << (sh > 39 ? 39 : sh);
    else r = sh > -24 ? int64_t(m >> -sh) : 0;
    return (v.u & 0x80000000u) ? -r : r;
}

//! Recalculate a cursor at time tau. pa: add the pressure advance term K * v.
void cursor_init(Cursor &c, uint8_t axis, uint32_t tau) {
    c.valid = true;
    c.d = c.dd = 0;
    const Seg *s = locate(c.seg, tau);
    if (!s) {
        c.p = q32(rest_pos[axis]);
        c.until = tau + 1;
        return;
    }
    const Blk &b = blks[s->blk];
    const int32_t dt = int32_t(tau - s->t0);
    if (dt < 0) {
        // at rest before a block that follows a gap
        c.p = q32(b.start[axis]);
        c.until = s->t0;
        return;
    }
    if (uint32_t(dt) >= s->dur) {
        // at rest after the end of the trajectory, until more blocks come
        c.p = q32(b.end[axis]);
        c.until = tau + 1;
        return;
    }
    c.until = s->t0 + s->dur;
    const float c1 = s->c1[axis], c2 = s->c2[axis];
    if (c1 == 0.f && c2 == 0.f) {
        c.p = q32(b.start[axis]) + f_to_q32(s->c0[axis]);
        return;
    }
    // forward differences of the quadratic over one sample period h
    const float t = float(dt);
    const float h = float(SAMPLE_TICKS);
    const float pos = s->c0[axis] + t * (c1 + c2 * t);
    const float d = h * (c1 + c2 * (h + 2.f * t));
    c.p = q32(b.start[axis]) + f_to_q32(pos);
    c.d = f_to_q32(d);
    c.dd = f_to_q32(2.f * c2 * h * h);
}

//! Position of a cursor at tau, the previous call was for tau - SAMPLE_TICKS
inline int64_t cursor_sample(Cursor &c, uint8_t axis, uint32_t tau) {
    if (c.valid && time_before(tau, c.until)) {
        c.p += c.d;
        c.d += c.dd;
    } else {
        cursor_init(c, axis, tau);
    }
    return c.p;
}

//! Shaped position (steps, Q8): sum a[i] * x(t - t[i]) = x0 + sum_{i>0} a[i] * (x[i] - x0)
int64_t sample_shaped(uint8_t axis, uint32_t t) {
    const input_shaper::Pulses &p = pulses[axis];
    const int64_t x0 = cursor_sample(cur[axis][0], axis, t - uint32_t(p.t[0]));
    int64_t sum = 0;
    for (uint8_t i = 1; i < p.num_pulses; ++i) {
        const int64_t xi = cursor_sample(cur[axis][i], axis, t - uint32_t(p.t[i]));
        sum += (xi - x0) * pulse_w[axis][i];
    }
    return (x0 + (sum >> 16)) >> 24;
}

int64_t sample_e_input(uint32_t tau) {
    return cursor_sample(cur_e, E, tau) >> 24;
}

void pa_fill(int64_t x) {
    for (uint16_t i = 0; i < PA_MAX_BOX; ++i) {
        pa_b1[i] = x;
        pa_b2[i] = x * pa_box;
    }
    pa_s1 = x * pa_box;
    pa_s2 = x * pa_box * pa_box;
    pa_idx = 0;
}

//! The PA input is sampled ahead by the half width of the smoothing window
inline uint32_t lead_pa() { return pa_box ? uint32_t(pa_box - 1) * SAMPLE_TICKS : 0; }

int64_t sample_e(uint32_t t) {
    if (!pa_box) return sample_e_input(t);
    const int64_t x = sample_e_input(t + lead_pa());
#ifdef MOTION_CORE_TEST
    test_pa_input_hook(t + lead_pa(), x);
#endif
    pa_s1 += x - pa_b1[pa_idx];
    pa_b1[pa_idx] = x;
    pa_s2 += pa_s1 - pa_b2[pa_idx];
    pa_b2[pa_idx] = pa_s1;
    if (++pa_idx == pa_box) pa_idx = 0;
    // average = s2 / box^2, relative to the current position (small) and with a reciprocal
    const int64_t ref = int64_t(cnt[E]) << 8;
    int64_t rel = pa_s2 - ref * (int32_t(pa_box) * pa_box);
    if (rel > INT32_MAX) rel = INT32_MAX;
    if (rel < -INT32_MAX) rel = -INT32_MAX;
    const int64_t q = (int64_t(int32_t(rel)) * int64_t(pa_recip) + (int64_t(1) << 31)) >> 32;
    return ref + q;
}

void prune_segments() {
    uint32_t oldest = seg_head;
    for (uint8_t a = 0; a < 3; ++a)
        for (uint8_t i = 0; i < pulses[a].num_pulses; ++i)
            if (time_before(cur[a][i].seg, oldest)) oldest = cur[a][i].seg;
    if (time_before(cur_e.seg, oldest)) oldest = cur_e.seg;
    if (time_before(seg_tail, oldest)) seg_tail = oldest;
}

void invalidate_cursors() {
    for (uint8_t a = 0; a < 3; ++a)
        for (uint8_t i = 0; i < input_shaper::MAX_PULSES; ++i)
            cur[a][i] = Cursor {};
    cur_e = Cursor {};
}

//! Add a segment of block b: path position s0 + v0 t + a/2 t^2 (mm, s)
void push_seg(uint32_t t0, uint32_t dur, float s0, float v0, float a, uint8_t blk, bool last) {
#ifdef MOTION_CORE_TEST
    if (seg_head - seg_tail >= SEG_N) test_fail("segment queue overflow");
#endif
    Seg &s = segs[seg_head & SEG_MASK];
    const Blk &b = blks[blk];
    s.t0 = t0;
    s.dur = dur;
    s.blk = blk;
    s.last = last;
    // per tick: v0 / f, a / 2 / f^2
    const float v1 = v0 * TICK_S;
    const float a2 = .5f * a * TICK_S * TICK_S;
    for (uint8_t i = 0; i < AXES; ++i) {
        const float r = b.r[i];
        s.c0[i] = r * s0;
        s.c1[i] = r * v1;
        s.c2[i] = r * a2;
    }
    if (pa_box && b.pa) {
        // e + K * de/dt
        s.c0[E] += pa_k * b.r[E] * v0;
        s.c1[E] += pa_k * b.r[E] * a * TICK_S;
    }
    ++seg_head;
}

uint32_t to_ticks(float seconds) {
    const float t = seconds * TICK_HZ + .5f;
    return t > 0.f ? uint32_t(t) : 0;
}

//! Generate the steps of one axis between two samples
struct AxisSteps {
    uint32_t t_q8, dt_q8;
    uint16_t n;
    bool neg;
};

void plan_axis_steps(uint8_t axis, int64_t p0, int64_t p1, AxisSteps &out) {
    out.n = 0;
    const int32_t target = int32_t((p1 + 128) >> 8);
    const int32_t diff = target - cnt[axis];
    if (!diff) return;
    out.neg = diff < 0;
    out.n = uint16_t(out.neg ? -diff : diff);
    const int64_t dp = p1 - p0;
    if (dp != 0 && ((dp < 0) == out.neg)) {
        const uint32_t adp = uint32_t(dp < 0 ? -dp : dp);
        out.dt_q8 = (SAMPLE_Q8 << 8) / adp; // fits: 2000 * 2^16
        const int64_t boundary = (int64_t(cnt[axis]) << 8) + (out.neg ? -128 : 128);
        int64_t f = out.neg ? p0 - boundary : boundary - p0;
        if (f < 0) f = 0;
        const uint64_t t = (uint64_t(f) * out.dt_q8) >> 8;
        out.t_q8 = t < SAMPLE_Q8 ? uint32_t(t) : SAMPLE_Q8 - 1;
    } else {
        // steps owed against the sampled motion (only after numeric noise): spread them
        out.t_q8 = 0;
        out.dt_q8 = SAMPLE_Q8 / (out.n + 1u);
    }
    cnt[axis] = target;
    if (out.n > stats.max_steps[axis]) stats.max_steps[axis] = out.n;
}

void emit_markers(uint32_t t) {
    // E resets at the start of their block
    for (uint8_t k = mark_ereset_done; uint8_t(mark_tail + k) != mark_head; ++k) {
        const uint8_t id = mark_fifo[uint8_t(mark_tail + k) & BLK_MASK];
        if (time_before(t, blks[id].t0)) break;
        if (blks[id].e_reset) emit(t, 0, 0, EV_E_RESET, id);
        mark_ereset_done = uint8_t(k + 1);
    }
    // blocks done when no sample depends on them anymore
    while (mark_tail != mark_head && mark_ereset_done) {
        const uint8_t id = mark_fifo[mark_tail & BLK_MASK];
        if (time_before(t, blks[id].t_end + lag)) break;
        emit(t, 0, 0, EV_BLOCK_DONE, id);
        ++mark_tail;
        --mark_ereset_done;
    }
}

} // namespace

void init() {
    for (uint8_t a = 0; a < 3; ++a) {
        pulses[a].num_pulses = 1;
        pulses[a].a[0] = 1.f;
        pulses[a].t[0] = 0;
    }
    pa_k = 0.f;
    pa_box = 0;
    memset(&stats, 0, sizeof(stats));
    const int32_t zero[AXES] = {};
    reset(zero, 0);
}

void reset(const int32_t pos[AXES], int32_t eref) {
    events_clear();
    for (uint8_t a = 0; a < AXES; ++a) {
        conv_pos[a] = pos[a];
        cnt[a] = pos[a];
    }
    e_ref = eref;
    update_lead_lag();
    new_epoch();
}

void new_epoch() {
    seg_head = seg_tail = 0;
    invalidate_cursors();
    mark_head = mark_tail = 0;
    mark_ereset_done = 0;
    for (uint8_t a = 0; a < AXES; ++a) {
        rest_pos[a] = conv_pos[a];
        cnt[a] = conv_pos[a];
        p_prev[a] = int64_t(conv_pos[a]) << 8;
    }
    pa_fill(p_prev[E]);
    gen = 0;
    traj_end = 0;
    last_ev = 0;
    last_exit = 0.f;
    have_blocks = false;
}

void set_shaper(uint8_t axis, const input_shaper::Pulses &p) {
    if (axis < 3) {
        pulses[axis] = p;
        for (uint8_t i = 0; i < p.num_pulses; ++i)
            pulse_w[axis][i] = int32_t(lrintf(p.a[i] * 65536.f));
        invalidate_cursors();
    }
    update_lead_lag();
}

void set_pressure_advance(float k, float smooth_time) {
    pa_k = k > 0.f ? k : 0.f;
    if (pa_k > 0.f) {
        long box = lroundf(smooth_time * 0.5f * TICK_HZ / SAMPLE_TICKS);
        if (box < 1) box = 1;
        if (box > PA_MAX_BOX) box = PA_MAX_BOX;
        pa_box = uint16_t(box);
        pa_recip = uint32_t(((uint64_t(1) << 32) + uint64_t(box * box) - 1) / uint64_t(box * box));
    } else {
        pa_box = 0;
    }
    pa_fill(p_prev[E]);
    invalidate_cursors();
    update_lead_lag();
}

float pressure_advance() { return pa_k; }

bool wants_block() {
    // room for the (up to 4) segments and the marker of one more block
    if (seg_head - seg_tail > SEG_N - 4u || uint8_t(mark_head - mark_tail) >= BLK_N - 1u)
        return false;
    return time_before(traj_end, gen + SAMPLE_TICKS * 3 + lead);
}

void add_block(const BlockIn &in) {
    const uint8_t id = in.id & BLK_MASK;
    Blk &b = blks[id];
    const float mm = in.millimeters > 0.f ? in.millimeters : 1e-6f;
    for (uint8_t a = 0; a < AXES; ++a) {
        b.start[a] = conv_pos[a];
        b.r[a] = float(in.steps[a]) / mm;
        conv_pos[a] += in.steps[a];
        b.end[a] = conv_pos[a];
    }
    b.mm = mm;
    b.pa = in.steps[E] > 0 && (in.steps[0] || in.steps[1]);
    b.e_reset = in.e_reset;
    if (in.e_reset) {
        e_shift[id] = b.start[E] - e_ref;
        e_ref = b.start[E];
    }

    // start time: right after the previous block, unless the trajectory ran dry
    const uint32_t earliest = gen + SAMPLE_TICKS + lead;
    bool joined = have_blocks && !time_before(traj_end, earliest);
    const uint32_t t0 = joined ? traj_end : earliest;
    if (have_blocks && !joined) ++stats.gaps;
    have_blocks = true;

    // trapezoid
    const float a = in.acceleration > 0.f ? in.acceleration : 1.f;
    float ve = in.entry_speed;
    if (joined && ve > last_exit) ve = last_exit;
    float vn = in.nominal_speed;
    float vx = in.exit_speed;
    if (vn < ve) vn = ve;
    if (vx > vn) vx = vn;
    float d_acc = (vn * vn - ve * ve) / (2.f * a);
    float d_dec = (vn * vn - vx * vx) / (2.f * a);
    if (d_acc + d_dec > mm) {
        if (ve * ve - 2.f * a * mm >= vx * vx) {
            // decelerate over the whole block, possibly not down to vx
            vx = sqrtf(ve * ve - 2.f * a * mm);
            vn = ve;
            d_acc = 0.f;
            d_dec = mm;
        } else if (vx * vx - 2.f * a * mm >= ve * ve) {
            // accelerate over the whole block, possibly not up to vx
            vx = sqrtf(ve * ve + 2.f * a * mm);
            vn = vx;
            d_acc = mm;
            d_dec = 0.f;
        } else {
            vn = sqrtf((2.f * a * mm + ve * ve + vx * vx) * .5f);
            d_acc = (vn * vn - ve * ve) / (2.f * a);
            if (d_acc < 0.f) d_acc = 0.f;
            if (d_acc > mm) d_acc = mm;
            d_dec = mm - d_acc;
        }
    }
    float d_cru = mm - d_acc - d_dec;
    if (d_cru < 0.f) d_cru = 0.f;

    uint32_t t = t0;
    const uint32_t dur_acc = to_ticks((vn - ve) / a);
    const uint32_t dur_cru = vn > 0.f ? to_ticks(d_cru / vn) : 0;
    const uint32_t dur_dec = to_ticks((vn - vx) / a);
    const bool has_cru = dur_cru > 0;
    const bool has_dec = dur_dec > 0;
    if (dur_acc) {
        push_seg(t, dur_acc, 0.f, ve, a, id, !has_cru && !has_dec);
        t += dur_acc;
    }
    if (has_cru) {
        push_seg(t, dur_cru, d_acc, vn, 0.f, id, !has_dec);
        t += dur_cru;
    }
    if (has_dec) {
        push_seg(t, dur_dec, d_acc + d_cru, vn, -a, id, true);
        t += dur_dec;
    }
    if (t == t0) {
        // shorter than a tick: a jump within one tick
        push_seg(t, 1, 0.f, mm * TICK_HZ, 0.f, id, true);
        t += 1;
    }
    b.t0 = t0;
    b.t_end = t;
    traj_end = t;
    last_exit = vx;

    mark_fifo[mark_head & BLK_MASK] = id;
    ++mark_head;
    ++stats.blocks;
}

bool busy() {
    return mark_tail != mark_head || time_before(gen, traj_end + lag + SAMPLE_TICKS);
}

bool produce_sample() {
    if (!busy()) return false;
    if (events_free() < 4u * 64u) return false;
    const uint32_t t = gen + SAMPLE_TICKS;
    int64_t p[AXES];
    for (uint8_t a = 0; a < 3; ++a)
        p[a] = sample_shaped(a, t);
    p[E] = sample_e(t);
#ifdef MOTION_CORE_TEST
    test_sample_hook(t, p);
#endif

    AxisSteps st[AXES];
    uint8_t active = 0;
    uint16_t tick[AXES];
    for (uint8_t a = 0; a < AXES; ++a) {
        plan_axis_steps(a, p_prev[a], p[a], st[a]);
        if (st[a].n) {
            active |= uint8_t(1u << a);
            tick[a] = uint16_t(st[a].t_q8 >> 8);
        }
    }
    while (active) {
        uint16_t first = SAMPLE_TICKS;
        for (uint8_t a = 0; a < AXES; ++a)
            if ((active & (1u << a)) && tick[a] < first) first = tick[a];
        uint8_t mask = 0, dirs = 0;
        for (uint8_t a = 0; a < AXES; ++a) {
            if (!(active & (1u << a)) || tick[a] != first) continue;
            mask |= uint8_t(1u << a);
            if (st[a].neg) dirs |= uint8_t(1u << a);
            if (--st[a].n) {
                st[a].t_q8 += st[a].dt_q8;
                const uint32_t tk = st[a].t_q8 >> 8;
                tick[a] = uint16_t(tk < SAMPLE_TICKS ? tk : SAMPLE_TICKS - 1);
            } else {
                active &= uint8_t(~(1u << a));
            }
        }
        emit(gen + first, mask, dirs, EV_STEP, 0);
    }
    for (uint8_t a = 0; a < AXES; ++a)
        p_prev[a] = p[a];
    gen = t;
    emit_markers(t);
    // the step interrupt must not run out of events while the motors rest
    if (time_before(last_ev, gen - SAMPLE_TICKS + 1)) emit(t, 0, 0, EV_STEP, 0);
    prune_segments();
    ++stats.samples;
    return true;
}

uint32_t generated_time() { return gen; }
float last_exit_speed() { return last_exit; }
uint32_t lead_ticks() { return lead; }
uint32_t lag_ticks() { return lag; }

uint16_t events_queued() { return uint16_t(ev_head - ev_tail); }
uint16_t events_free() { return uint16_t(EVQ_N - events_queued()); }

bool event_pop(uint32_t &e) {
    const uint16_t tail = ev_tail;
    if (tail == ev_head) return false;
    e = ev_q[tail & EVQ_MASK];
    ev_tail = uint16_t(tail + 1);
    return true;
}

void events_clear() {
    ev_tail = ev_head;
}

} // namespace motion
