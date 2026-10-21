//! @file
//! @brief Motion engine of the STM32 port: input shaping and pressure advance
//!
//! The planner of Prusa-Firmware 3.14 is kept. Its blocks (trapezoidal velocity profiles)
//! are turned into constant acceleration segments along the path. The position of every
//! motor is sampled every SAMPLE_TICKS (1 ms):
//!  - X, Y (and optionally Z) through the input shaper: x_s(t) = sum a[i] * x(t - t[i]),
//!    filters and defaults of Prusa-Firmware-Buddy (MK3.5/MK4),
//!  - E with pressure advance e(t) + K * v_e(t), smoothed by a triangular window
//!    (Buddy/Klipper style, 40 ms by default).
//! Between two samples a motor moves linearly, so its steps are evenly spaced and are
//! generated with integer arithmetic only. The steps of all motors are merged into one
//! time ordered queue of events, which the step timer interrupt executes.
//!
//! This file has no hardware dependency: it is also compiled into the host unit test.
#pragma once
#include <stdint.h>
#include "input_shaper.h"

namespace motion {

constexpr uint32_t TICK_HZ = 2000000;      //!< step timer frequency
constexpr uint32_t SAMPLE_TICKS = 2000;    //!< sampling period (1 ms)
constexpr uint8_t AXES = 4;                //!< X Y Z E
constexpr uint8_t E = 3;
constexpr uint16_t SEG_N = 256;            //!< segment queue size (power of 2)
constexpr uint16_t BLK_N = 64;             //!< >= planner BLOCK_BUFFER_SIZE (power of 2)
constexpr uint16_t EVQ_N = 1024;           //!< event queue size (power of 2)
constexpr uint16_t PA_MAX_BOX = 100;       //!< max. smoothing box length (samples)
constexpr uint32_t MAX_EVENT_DELTA = 30000; //!< ticks, longer gaps get keep-alive events

//! Event, 32 bits:
//!  0-15  ticks since the previous event
//! 16-19  step mask (bit = axis)
//! 20-23  direction of the stepping axes (1 = negative)
//! 24-25  type
//! 26-31  argument (planner block index)
enum EventType : uint8_t {
    EV_STEP = 0,       //!< steps (mask 0: keep-alive)
    EV_BLOCK_DONE = 1, //!< the oldest planner block is no longer needed
    EV_E_RESET = 2,    //!< E count reset of block <arg> (BLOCK_FLAG_E_RESET)
};
inline uint32_t ev_delta(uint32_t e) { return e & 0xFFFFu; }
inline uint8_t ev_mask(uint32_t e) { return (e >> 16) & 0x0Fu; }
inline uint8_t ev_dirs(uint32_t e) { return (e >> 20) & 0x0Fu; }
inline uint8_t ev_type(uint32_t e) { return (e >> 24) & 0x03u; }
inline uint8_t ev_arg(uint32_t e) { return (e >> 26) & 0x3Fu; }

//! What the engine needs to know about a planner block
struct BlockIn {
    int32_t steps[AXES];   //!< signed step counts
    float millimeters;     //!< path length (XYZ, or E for E only moves)
    float entry_speed;     //!< mm/s along the path
    float nominal_speed;
    float exit_speed;
    float acceleration;    //!< mm/s^2
    uint8_t id;            //!< planner block index
    bool e_reset;          //!< E count starts at zero with this block
};

struct Stats {
    uint32_t samples;
    uint32_t events;
    uint32_t blocks;
    uint32_t gaps;         //!< blocks that did not join the previous one (planner underrun)
    uint16_t max_steps[AXES]; //!< most steps of an axis in one sample period
};

// --- configuration (only while the engine is idle) ---
void init();
//! Discard everything, motors at rest at engine position pos. e_ref: engine E position
//! that corresponds to the E count 0.
void reset(const int32_t pos[AXES], int32_t e_ref);
//! Start a new time base (t = 0) when the engine was idle, keeping the positions.
void new_epoch();
void set_shaper(uint8_t axis, const input_shaper::Pulses &pulses);
void set_pressure_advance(float k, float smooth_time);
float pressure_advance();

// --- producer ---
//! The trajectory ends too early, a further block should be added now.
bool wants_block();
void add_block(const BlockIn &b);
//! True while samples remain to be generated (moving, settling, markers pending).
bool busy();
//! Generate the events of the next sample period. False when not busy or the queue is full.
bool produce_sample();
uint32_t generated_time();   //!< ticks
float last_exit_speed();     //!< mm/s, actual exit speed of the last added block
uint32_t lead_ticks();
uint32_t lag_ticks();

// --- event queue (single producer, single consumer) ---
uint16_t events_queued();
uint16_t events_free();
bool event_pop(uint32_t &e);
void events_clear();

//! E count shift of an EV_E_RESET event (indexed by its argument)
extern int32_t e_shift[BLK_N];
extern Stats stats;

#ifdef MOTION_CORE_TEST
//! Host unit test: called with every sample (positions in steps, Q8)
void test_sample_hook(uint32_t t, const int64_t *p);
void test_pa_input_hook(uint32_t t, int64_t x);
void test_fail(const char *msg);
#endif

} // namespace motion
