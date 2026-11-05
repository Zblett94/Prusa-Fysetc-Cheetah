#
# cheetah_images.py - flash images of the Cheetah V3 build that leave the settings alone.
#
# The settings (emulated EEPROM) live in flash sectors 1 and 2 (0x08004000..0x0800BFFF),
# between the two parts of the firmware. firmware.bin covers that gap (filled with
# zeros): flashing it would erase the settings. Written next to it:
#   firmware.hex              Intel HEX of the two parts only (STM32CubeProgrammer)
#   firmware_0x08000000.bin   part 1: vector table + libraries (sector 0)
#   firmware_0x0800C000.bin   part 2: the rest (sectors 3..5)
# dfu-util erases only the pages it writes:
#   dfu-util -a 0 -s 0x08000000 -D firmware_0x08000000.bin
#   dfu-util -a 0 -s 0x0800C000:leave -D firmware_0x0800C000.bin
#
Import("env")
import os

def make_images(source, target, env):
    elf = str(target[0])
    out = os.path.dirname(elf)
    objcopy = env.subst("$OBJCOPY")
    part1 = "-j .isr_vector -j .text0"
    part2 = "-j .text -j .rodata -j .ARM.extab -j .ARM -j .preinit_array -j .init_array -j .fini_array -j .data"
    env.Execute(f'"{objcopy}" -O ihex {part1} {part2} "{elf}" "{out}/firmware.hex"')
    env.Execute(f'"{objcopy}" -O binary {part1} "{elf}" "{out}/firmware_0x08000000.bin"')
    env.Execute(f'"{objcopy}" -O binary {part2} "{elf}" "{out}/firmware_0x0800C000.bin"')
    print("Cheetah V3 images: firmware.hex, firmware_0x08000000.bin, firmware_0x0800C000.bin")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", make_images)
