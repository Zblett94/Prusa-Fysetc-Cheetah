//! @file
//! @brief Support for the Renode emulator test build (env:renode_test, -DEMULATION).
//! Not compiled into the printer firmware.

#ifdef EMULATION
#include "../Marlin.h"
#include "../eeprom.h"
#include "../util.h"

//! Language preset for the EEPROM (0 = English, 1.. = language tables, 255 = ask at
//! start-up). In the flash, the test script (run.py --lang) overwrites it.
extern "C" const volatile uint8_t emu_lang = 0;

//! The emulator has no I2C EEPROM: preset the RAM mirror like a calibrated printer,
//! so the first run wizard does not block the G-code tests.
void hal_emulation_eeprom_preset()
{
	eeprom_write_byte((uint8_t*)EEPROM_WIZARD_ACTIVE, 0);
	eeprom_write_byte((uint8_t*)EEPROM_CALIBRATION_STATUS_V2, CALIBRATION_WIZARD_STEPS);
	eeprom_write_byte((uint8_t*)EEPROM_LANG, emu_lang);
}

//! HD44780 DDRAM shadow, read by the emulator test scripts (symbol emu_lcd_ddram)
extern "C" {
volatile char emu_lcd_ddram[128];
volatile uint8_t emu_lcd_addr;
}

void emulation_lcd_send(uint8_t data, uint8_t flags)
{
	if (flags & 0x02) return; // LCD_HALF_FLAG: initialization nibbles
	if (flags & 0x01) { // LCD_RS_FLAG: data
		emu_lcd_ddram[emu_lcd_addr & 0x7F] = data;
		emu_lcd_addr++;
	} else if (data & 0x80) { // set DDRAM address
		emu_lcd_addr = data & 0x7F;
	} else if (data == 0x01) { // clear
		for (uint8_t i = 0; i < sizeof(emu_lcd_ddram); i++) emu_lcd_ddram[i] = ' ';
		emu_lcd_addr = 0;
	} else if ((data & 0xFE) == 0x02) { // home
		emu_lcd_addr = 0;
	}
}
#endif //EMULATION
