//sm4.cpp - simple 4-axis stepper control
#include "sm4.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <math.h>
#include "Arduino.h"

#include "boards.h"
#define false 0
#define true 1
#include "Configuration_var.h"
#if (MOTHERBOARD == BOARD_SKR_MINI_E3_V3 || MOTHERBOARD == BOARD_FYSETC_CHEETAH_V3)
#include "pins.h"
#include "fastio.h"
#endif


#ifdef NEW_XYZCAL


// Signal pinouts

// direction signal - MiniRambo
//#define X_DIR_PIN    48 //PL1 (-)
//#define Y_DIR_PIN    49 //PL0 (-)
//#define Z_DIR_PIN    47 //PL2 (-)
//#define E0_DIR_PIN   43 //PL6 (+)

//direction signal - EinsyRambo
//#define X_DIR_PIN    49 //PL0 (+)
//#define Y_DIR_PIN    48 //PL1 (-)
//#define Z_DIR_PIN    47 //PL2 (+)
//#define E0_DIR_PIN   43 //PL6 (-)

//step signal pinout - common for all rambo boards
//#define X_STEP_PIN   37 //PC0 (+)
//#define Y_STEP_PIN   36 //PC1 (+)
//#define Z_STEP_PIN   35 //PC2 (+)
//#define E0_STEP_PIN  34 //PC3 (+)


#define XDIR INVERT_X_DIR:!INVERT_X_DIR
#define YDIR INVERT_Y_DIR:!INVERT_Y_DIR
#define ZDIR INVERT_Z_DIR:!INVERT_Z_DIR
#define EDIR INVERT_E0_DIR:!INVERT_E0_DIR

uint8_t dir_mask = 0x0F^(INVERT_X_DIR | (INVERT_Y_DIR << 1) | (INVERT_Z_DIR << 2) | (INVERT_E0_DIR << 3));

sm4_stop_cb_t sm4_stop_cb = 0;

sm4_update_pos_cb_t sm4_update_pos_cb = 0;

sm4_calc_delay_cb_t sm4_calc_delay_cb = 0;

void sm4_set_dir(uint8_t axis, uint8_t dir)
{
	switch (axis)
	{
#if ((MOTHERBOARD == BOARD_RAMBO_MINI_1_0) || (MOTHERBOARD == BOARD_RAMBO_MINI_1_3))
	case 0: if (dir == INVERT_X_DIR) PORTL |= 2; else PORTL &= ~2; break;
	case 1: if (dir == INVERT_Y_DIR) PORTL |= 1; else PORTL &= ~1; break;
	case 2: if (dir == INVERT_Z_DIR) PORTL |= 4; else PORTL &= ~4; break;
	case 3: if (dir == INVERT_E0_DIR) PORTL |= 64; else PORTL &= ~64; break;
#elif ((MOTHERBOARD == BOARD_EINSY_1_0a))
	case 0: if (dir == INVERT_X_DIR) PORTL |= 1; else PORTL &= ~1; break;
	case 1: if (dir == INVERT_Y_DIR) PORTL |= 2; else PORTL &= ~2; break;
	case 2: if (dir == INVERT_Z_DIR) PORTL |= 4; else PORTL &= ~4; break;
	case 3: if (dir == INVERT_E0_DIR) PORTL |= 64; else PORTL &= ~64; break;
#elif (MOTHERBOARD == BOARD_SKR_MINI_E3_V3 || MOTHERBOARD == BOARD_FYSETC_CHEETAH_V3)
	// same pin levels as the EINSy PORTL bits
	case 0: WRITE(X_DIR_PIN, dir == INVERT_X_DIR); break;
	case 1: WRITE(Y_DIR_PIN, dir == INVERT_Y_DIR); break;
	case 2: WRITE(Z_DIR_PIN, dir == INVERT_Z_DIR); break;
	case 3: WRITE(E0_DIR_PIN, dir == INVERT_E0_DIR); break;
#endif
	}
	asm("nop");
}

void sm4_set_dir_bits(uint8_t dir_bits)
{
#if (MOTHERBOARD == BOARD_SKR_MINI_E3_V3 || MOTHERBOARD == BOARD_FYSETC_CHEETAH_V3)
	dir_bits ^= dir_mask;
	WRITE(X_DIR_PIN, dir_bits & 1);
	WRITE(Y_DIR_PIN, dir_bits & 2);
	WRITE(Z_DIR_PIN, dir_bits & 4);
	WRITE(E0_DIR_PIN, dir_bits & 8);
#else
	uint8_t portL = PORTL;
	portL &= 0xb8; //set direction bits to zero
	//TODO -optimize in asm
#if ((MOTHERBOARD == BOARD_RAMBO_MINI_1_0) || (MOTHERBOARD == BOARD_RAMBO_MINI_1_3))
	dir_bits ^= dir_mask;
	if (dir_bits & 1) portL |= 2;  //set X direction bit
	if (dir_bits & 2) portL |= 1;  //set Y direction bit
	if (dir_bits & 4) portL |= 4;  //set Z direction bit
	if (dir_bits & 8) portL |= 64; //set E direction bit
#elif ((MOTHERBOARD == BOARD_EINSY_1_0a))
	dir_bits ^= dir_mask;
	if (dir_bits & 1) portL |= 1;  //set X direction bit
	if (dir_bits & 2) portL |= 2;  //set Y direction bit
	if (dir_bits & 4) portL |= 4;  //set Z direction bit
	if (dir_bits & 8) portL |= 64; //set E direction bit
#endif
	PORTL = portL;
#endif
	asm("nop");
}

void sm4_do_step(uint8_t axes_mask)
{
#if (MOTHERBOARD == BOARD_SKR_MINI_E3_V3 || MOTHERBOARD == BOARD_FYSETC_CHEETAH_V3)
#ifdef TMC2130_DEDGE_STEPPING
	// toggle step signals by mask
	if (axes_mask & 1) TOGGLE(X_STEP_PIN);
	if (axes_mask & 2) TOGGLE(Y_STEP_PIN);
	if (axes_mask & 4) TOGGLE(Z_STEP_PIN);
	if (axes_mask & 8) TOGGLE(E0_STEP_PIN);
#else
	if (axes_mask & 1) WRITE(X_STEP_PIN, 1);
	if (axes_mask & 2) WRITE(Y_STEP_PIN, 1);
	if (axes_mask & 4) WRITE(Z_STEP_PIN, 1);
	if (axes_mask & 8) WRITE(E0_STEP_PIN, 1);
	delayMicroseconds(1);
	WRITE(X_STEP_PIN, 0);
	WRITE(Y_STEP_PIN, 0);
	WRITE(Z_STEP_PIN, 0);
	WRITE(E0_STEP_PIN, 0);
#endif
#endif
#if ((MOTHERBOARD == BOARD_RAMBO_MINI_1_0) || (MOTHERBOARD == BOARD_RAMBO_MINI_1_3) || (MOTHERBOARD == BOARD_EINSY_1_0a))
#ifdef TMC2130_DEDGE_STEPPING
	PINC = (axes_mask & 0x0f); // toggle step signals by mask
#else
	uint8_t portC = PORTC & 0xf0;
	PORTC = portC | (axes_mask & 0x0f); //set step signals by mask
	asm("nop");
	PORTC = portC; //set step signals to zero
	asm("nop");
#endif
#endif //((MOTHERBOARD == BOARD_RAMBO_MINI_1_0) || (MOTHERBOARD == BOARD_RAMBO_MINI_1_3) || (MOTHERBOARD == BOARD_EINSY_1_0a))
}

uint16_t sm4_line_xyz_ui(uint16_t dx, uint16_t dy, uint16_t dz){
	uint16_t dd = (uint16_t)(sqrt((float)(((uint32_t)dx)*dx + ((uint32_t)dy*dy) + ((uint32_t)dz*dz))) + 0.5);
	uint16_t nd = dd;
	uint16_t cx = dd;
	uint16_t cy = dd;
	uint16_t cz = dd;
	uint16_t x = 0;
	uint16_t y = 0;
	uint16_t z = 0;
	while (nd){
		if (sm4_stop_cb && (*sm4_stop_cb)()) break;
		uint8_t sm = 0; //step mask
		if (cx <= dx){
			sm |= 1;
			cx += dd;
			x++;
		}
		if (cy <= dy){
			sm |= 2;
			cy += dd;
			y++;
		}
		if (cz <= dz){
			sm |= 4;
			cz += dd;
			z++;
		}
		cx -= dx;
		cy -= dy;
		cz -= dz;
		sm4_do_step(sm);
		uint16_t delay = SM4_DEFDELAY;
		if (sm4_calc_delay_cb) delay = (*sm4_calc_delay_cb)(nd, dd);
		if (delay) delayMicroseconds(delay);
		nd--;
	}
	if (sm4_update_pos_cb)
		(*sm4_update_pos_cb)(x, y, z, 0);
	return nd;
}

#endif //NEW_XYZCAL
