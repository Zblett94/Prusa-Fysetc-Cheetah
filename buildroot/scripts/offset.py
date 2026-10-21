#
# offset.py - relocate the firmware behind the 8KB BigTreeTech SD-card bootloader.
#
# The STM32duino builder always links with LD_FLASH_OFFSET=0 unless it is
# uploading over DFU/HID, so take 'board_build.offset' and apply it to both the
# linker script (LD_FLASH_OFFSET) and the vector table (VECT_TAB_OFFSET).
#
Import("env")

board = env.BoardConfig()
offset = board.get("build.offset", "0x0")

env.Append(CPPDEFINES=[("VECT_TAB_OFFSET", offset)])

flags = env["LINKFLAGS"]
for i, flag in enumerate(flags):
    if "-Wl,--defsym=LD_FLASH_OFFSET" in flag:
        flags[i] = "-Wl,--defsym=LD_FLASH_OFFSET=" + offset
