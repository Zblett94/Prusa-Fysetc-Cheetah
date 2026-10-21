// Host unit test of the motion engine (Firmware/motion/motion_core.cpp).
//
// Feeds planner-like blocks, executes the produced events like the step interrupt and
// compares the motor positions with an exact (double precision) model of the shaped
// motion. Build and run: test/motion/run.sh
#include "../../Firmware/motion/motion_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace motion;

static int failures = 0;
void motion::test_fail(const char *msg) { static int n; if (n++ < 3) { ++failures; printf("FAIL: %s\n", msg); } }
static std::vector<std::pair<uint32_t,int64_t>> g_pa_in;
void motion::test_pa_input_hook(uint32_t t, int64_t x) { g_pa_in.push_back({t, x}); }
struct SampleRec { uint32_t t; int64_t p[AXES]; };
static std::vector<SampleRec> g_samples;
void motion::test_sample_hook(uint32_t t, const int64_t *p) {
    SampleRec r; r.t = t;
    for (int a = 0; a < AXES; ++a) r.p[a] = p[a];
    g_samples.push_back(r);
}
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const double STEPS_PER_MM[AXES] = { 100, 100, 400, 280 };

struct Move {
    double d[AXES];     // mm
    double feed;        // mm/s
    double v_entry, v_exit, accel;
};

// Reference model: exact trapezoids, shaped with the same pulses in double precision
struct RefSeg { double t0, dur, s0, v0, a; int blk; };
struct RefBlk { double start[AXES], r[AXES], mm; bool pa; };

struct Reference {
    std::vector<RefSeg> segs;
    std::vector<RefBlk> blks;
    double pos0[AXES] = {};

    double path(double tau, double &v, int &blk) const {
        v = 0;
        if (segs.empty() || tau <= segs[0].t0) { blk = segs.empty() ? -1 : segs[0].blk; return 0; }
        for (size_t i = 0; i < segs.size(); ++i) {
            const RefSeg &s = segs[i];
            if (tau < s.t0 + s.dur || i + 1 == segs.size()) {
                blk = s.blk;
                double t = tau - s.t0;
                if (t <= 0) return s.s0;
                if (t >= s.dur) return blks[s.blk].mm;
                v = s.v0 + s.a * t;
                return s.s0 + s.v0 * t + .5 * s.a * t * t;
            }
        }
        return 0;
    }
    double pos(int axis, double tau, double pa_k = 0) const {
        double v; int b = -1;
        double s = path(tau, v, b);
        if (b < 0) return pos0[axis];
        double p = blks[b].start[axis] + blks[b].r[axis] * s;
        if (pa_k > 0 && axis == E && blks[b].pa) p += pa_k * blks[b].r[axis] * v;
        return p;
    }
};

struct Sim {
    long count[AXES] = {};
    long e_count = 0;
    double time = 0; // s
    std::vector<int> done_ids;
    int keepalives = 0;
    double max_err[AXES] = {};
};

static void build(const std::vector<Move> &moves, std::vector<BlockIn> &blocks, Reference &ref, double t_start) {
    double pos[AXES] = {};
    long steps_pos[AXES] = {};
    double t = t_start;
    for (size_t i = 0; i < moves.size(); ++i) {
        const Move &m = moves[i];
        BlockIn b = {};
        long target[AXES];
        double mm2 = 0;
        for (int a = 0; a < AXES; ++a) {
            pos[a] += m.d[a];
            target[a] = lround(pos[a] * STEPS_PER_MM[a]);
            b.steps[a] = int32_t(target[a] - steps_pos[a]);
            if (a < 3) mm2 += pow(b.steps[a] / STEPS_PER_MM[a], 2);
        }
        double mm = sqrt(mm2);
        if (mm == 0) mm = fabs(b.steps[E] / STEPS_PER_MM[E]);
        b.millimeters = float(mm);
        b.nominal_speed = float(m.feed);
        b.entry_speed = float(m.v_entry);
        b.exit_speed = float(m.v_exit);
        b.acceleration = float(m.accel);
        b.id = uint8_t(i & (BLK_N - 1));
        b.e_reset = false;
        blocks.push_back(b);

        RefBlk rb;
        for (int a = 0; a < AXES; ++a) {
            rb.start[a] = steps_pos[a];
            rb.r[a] = b.steps[a] / mm;
            steps_pos[a] = target[a];
        }
        rb.mm = mm;
        rb.pa = b.steps[E] > 0 && (b.steps[0] || b.steps[1]);
        int bi = int(ref.blks.size());
        ref.blks.push_back(rb);
        // exact trapezoid
        double ve = m.v_entry, vn = m.feed, vx = m.v_exit, a = m.accel;
        double da = (vn * vn - ve * ve) / (2 * a), dd = (vn * vn - vx * vx) / (2 * a);
        if (da + dd > mm) { vn = sqrt((2 * a * mm + ve * ve + vx * vx) / 2); da = (vn * vn - ve * ve) / (2 * a); dd = mm - da; }
        double dc = mm - da - dd;
        if ((vn - ve) / a > 0) { ref.segs.push_back({ t, (vn - ve) / a, 0, ve, a, bi }); t += (vn - ve) / a; }
        if (dc / vn > 0) { ref.segs.push_back({ t, dc / vn, da, vn, 0, bi }); t += dc / vn; }
        if ((vn - vx) / a > 0) { ref.segs.push_back({ t, (vn - vx) / a, da + dc, vn, -a, bi }); t += (vn - vx) / a; }
    }
}

// shaped/smoothed reference position of an axis
static double ref_pos(const Reference &ref, int axis, double t, const input_shaper::Pulses *pulses, double pa_k, int pa_box) {
    if (axis < 3) {
        double p = 0;
        for (int i = 0; i < pulses[axis].num_pulses; ++i)
            p += pulses[axis].a[i] * ref.pos(axis, t - pulses[axis].t[i] / double(TICK_HZ));
        return p;
    }
    if (pa_box <= 0) return ref.pos(E, t);
    // triangle = box * box of the 1 ms samples (sample grid of the engine), linear in between
    const double ts = SAMPLE_TICKS / double(TICK_HZ);
    auto filtered = [&](double tg) {
        double sum = 0, wsum = 0;
        for (int k = -(pa_box - 1); k <= pa_box - 1; ++k) {
            double w = pa_box - abs(k);
            sum += w * ref.pos(E, tg + k * ts, pa_k);
            wsum += w;
        }
        return sum / wsum;
    };
    const double k0 = floor(t / ts);
    const double f = t / ts - k0;
    return (1 - f) * filtered(k0 * ts) + f * filtered((k0 + 1) * ts);
}

static void run(const char *name, const std::vector<Move> &moves, bool shaping, double pa_k, int feed_every = 0, int e_reset_every = 0) {
    init();
    input_shaper::Pulses pulses[3];
    for (int a = 0; a < 3; ++a) {
        input_shaper::AxisConfig c = input_shaper::axis_defaults[a];
        if (!shaping) c.enabled = false;
        input_shaper::make_pulses(pulses[a], c, TICK_HZ);
        set_shaper(uint8_t(a), pulses[a]);
    }
    set_pressure_advance(float(pa_k), 0.04f);
    const int pa_box = pa_k > 0 ? 20 : 0;
    const int32_t zero[AXES] = {};
    reset(zero, 0);

    std::vector<BlockIn> blocks;
    Reference ref;
    build(moves, blocks, ref, double(SAMPLE_TICKS + lead_ticks()) / TICK_HZ);
    if (e_reset_every)
        for (size_t i = 0; i < blocks.size(); ++i)
            blocks[i].e_reset = (i % e_reset_every) == 0 && i;
    long e_count = 0, e_since_reset = 0;

    Sim sim;
    size_t next_block = 0;
    long long ticks = 0;
    int guard = 0;
    double sum_sq[AXES] = {};
    long n_cmp = 0;
    for (;;) {
        while (wants_block() && next_block < blocks.size() && (!feed_every || guard % feed_every == 0)) {
            add_block(blocks[next_block++]);
            if (feed_every) break;
        }
        if (!produce_sample()) {
            if (next_block >= blocks.size() && events_queued() == 0) break;
        }
        uint32_t e;
        while (event_pop(e)) {
            ticks += ev_delta(e);
            const double t = double(ticks) / TICK_HZ;
            switch (ev_type(e)) {
            case EV_STEP:
                if (!ev_mask(e)) { ++sim.keepalives; break; }
                for (int a = 0; a < AXES; ++a)
                    if (ev_mask(e) & (1 << a)) {
                        sim.count[a] += (ev_dirs(e) & (1 << a)) ? -1 : 1;
                        if (a == E) e_count += (ev_dirs(e) & (1 << a)) ? -1 : 1;
                        const double r = ref_pos(ref, a, t, pulses, pa_k, pa_box);
                        // a step is due when the reference crosses the half step: right after
                        // the step the count is the rounded position
                        const double err = fabs(sim.count[a] - r);
                        if (getenv("DBG2") && a == E && t > 1.235 && t < 1.25) printf("  t %.5f count %ld ref %.3f\n", t, sim.count[a], r);
                        if (err > sim.max_err[a]) { sim.max_err[a] = err; if (getenv("DBG") && a == E) printf("  E err %.3f at %.4f s count %ld ref %.3f\n", err, t, sim.count[a], r); }
                        sum_sq[a] += (sim.count[a] - r) * (sim.count[a] - r);
                    }
                ++n_cmp;
                break;
            case EV_E_RESET:
                e_count -= e_shift[ev_arg(e)];
                break;
            case EV_BLOCK_DONE:
                sim.done_ids.push_back(ev_arg(e));
                break;
            }
        }
        if (++guard > 1000000) { CHECK(false, "%s: no end", name); break; }
    }
    if (getenv("DBG3") && pa_k > 0) {
        for (const SampleRec &r : g_samples) {
            double t = r.t / double(TICK_HZ);
            if (t < 1.23 || t > 1.25) continue;
            printf("  sample %.4f engine %.3f ref %.3f raw %.3f\n", t, r.p[E] / 256.0, ref_pos(ref, E, t, pulses, pa_k, pa_box), ref.pos(E, t, pa_k));
        }
    }
    if (getenv("DBG4") && pa_k > 0) {
        double maxd = 0, tmax = 0;
        for (auto &r : g_pa_in) {
            double t = r.first / double(TICK_HZ);
            double d = fabs(r.second / 256.0 - ref.pos(E, t, pa_k));
            if (d > maxd) { maxd = d; tmax = t; }
            if (getenv("DBG5") && t > 1.240 && t < 1.252) { double v; int b = -1; ref.path(t, v, b); printf("    tau %.4f engine %.3f ref %.3f blk %d v %.2f\n", t, r.second / 256.0, ref.pos(E, t, pa_k), b, v); }
        }
        printf("  PA input max diff %.3f steps at %.4f\n", maxd, tmax);
    }
    g_pa_in.clear();
    g_samples.clear();
    printf("%-28s %6.1f ms, %ld events, max err steps X %.2f Y %.2f Z %.2f E %.2f, gaps %u\n",
        name, ticks / 2000.0, n_cmp, sim.max_err[0], sim.max_err[1], sim.max_err[2], sim.max_err[3], stats.gaps);
    for (int a = 0; a < AXES; ++a) {
        long expect = 0;
        for (const BlockIn &b : blocks) expect += b.steps[a];
        CHECK(sim.count[a] == expect, "%s: axis %d ends at %ld, expected %ld", name, a, sim.count[a], expect);
        // with planner underruns the blocks are delayed, the reference timing does not apply
        if (!feed_every) CHECK(sim.max_err[a] < 0.75, "%s: axis %d max error %.3f steps", name, a, sim.max_err[a]);
    }
    CHECK(sim.done_ids.size() == blocks.size(), "%s: %zu block done events for %zu blocks", name, sim.done_ids.size(), blocks.size());
    for (size_t i = 0; i < sim.done_ids.size() && i < blocks.size(); ++i)
        CHECK(sim.done_ids[i] == blocks[i].id, "%s: done event %zu for block %d", name, i, sim.done_ids[i]);
    if (!feed_every) CHECK(stats.gaps == 0, "%s: %u gaps", name, stats.gaps);
    else CHECK(stats.gaps > 0, "%s: expected gaps", name);
    // E count as seen by the firmware: steps since the last E reset
    for (const BlockIn &b : blocks) {
        if (b.e_reset) e_since_reset = 0;
        e_since_reset += b.steps[E];
    }
    CHECK(e_count == e_since_reset, "%s: E count %ld, expected %ld", name, e_count, e_since_reset);
}

int main() {
    // shaper sanity: MK3.5 defaults
    input_shaper::Pulses px, py;
    input_shaper::make_pulses(px, input_shaper::axis_defaults[0], TICK_HZ);
    input_shaper::make_pulses(py, input_shaper::axis_defaults[1], TICK_HZ);
    printf("X MZV 50.7 Hz:");
    for (int i = 0; i < px.num_pulses; ++i) printf(" %.4f@%.2fms", px.a[i], px.t[i] / 2000.0);
    printf("\nY MZV 40.6 Hz:");
    for (int i = 0; i < py.num_pulses; ++i) printf(" %.4f@%.2fms", py.a[i], py.t[i] / 2000.0);
    printf("\n");
    double sa = 0, st = 0;
    for (int i = 0; i < px.num_pulses; ++i) { sa += px.a[i]; st += px.a[i] * px.t[i]; }
    CHECK(fabs(sa - 1) < 1e-5 && fabs(st) < 2 * px.num_pulses, "X pulses not normalized/centered");

    const double A = 4000; // mm/s^2
    // single moves from rest to rest
    run("X 100mm 200mm/s", { { { 100, 0, 0, 0 }, 200, 0, 0, A } }, true, 0);
    run("Y 50mm 300mm/s unshaped", { { { 0, 50, 0, 0 }, 300, 0, 0, A } }, false, 0);
    run("diagonal back and forth", {
        { { 30, 20, 0, 0 }, 250, 0, 0, A },
        { { -30, -20, 0, 0 }, 250, 0, 0, A } }, true, 0);
    // a square printed with extrusion, joined corners at 8 mm/s (classic jerk)
    std::vector<Move> square;
    const double side = 20, f = 150, vj = 8;
    double dirs[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
    for (int loop = 0; loop < 3; ++loop)
        for (int k = 0; k < 4; ++k) {
            bool first = loop == 0 && k == 0, last = loop == 2 && k == 3;
            square.push_back({ { side * dirs[k][0], side * dirs[k][1], 0, side * 0.0333 }, f, first ? 0 : vj, last ? 0 : vj, A });
        }
    run("square, shaped", square, true, 0);
    run("square, shaped + PA 0.04", square, true, 0.04);
    run("square, unshaped + PA 0.05", square, false, 0.05);
    // many short segments (a circle of 0.5 mm chords) at 200 mm/s
    std::vector<Move> circle;
    const int n = 180;
    const double rad = 15, chord = 2 * M_PI * rad / n;
    const double vjunction = 60;
    for (int i = 0; i < n; ++i) {
        double a0 = 2 * M_PI * i / n, a1 = 2 * M_PI * (i + 1) / n;
        double dx = rad * (cos(a1) - cos(a0)), dy = rad * (sin(a1) - sin(a0));
        double ve = i == 0 ? 0 : vjunction, vx = i == n - 1 ? 0 : vjunction;
        circle.push_back({ { dx, dy, 0, chord * 0.04 }, 200, ve, vx, A });
    }
    run("circle 0.5mm chords + PA", circle, true, 0.04);
    run("square, planner underrun", square, true, 0.04, 9);
    run("square, E resets", square, true, 0.04, 0, 3);
    run("circle, E resets, underrun", circle, true, 0.04, 12, 7);
    run("Z 10mm 12mm/s", { { { 0, 0, 10, 0 }, 12, 0, 0, 400 } }, true, 0);
    run("retract E only", { { { 0, 0, 0, -0.8 }, 35, 0, 0, 1250 }, { { 0, 0, 0, 0.8 }, 35, 0, 0, 1250 } }, true, 0.04);

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("OK\n");
    return 0;
}
