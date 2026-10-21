#
# lang.py - add the translations to firmware.bin (LANG_MODE=1).
#
# The Prusa build does this with lang/lang-map.py, lang/lang-build.py and
# lang/lang-patchsec.py for the AVR. On the STM32 all languages are stored in the
# internal flash, so after linking this script:
#  1. collects the translatable strings (section loc_pri of the ELF file) and gives
#     each one an ID, in address order, like lang-map.py,
#  2. writes the symbol map and builds one language table per lang/po/*.po file
#     with lang/lang-build.py (unchanged Prusa tool, same table format),
#  3. patches the string IDs, the signature (_PRI_LANG_SIGNATURE) and all language
#     tables (one after the other, into _SEC_LANG) into firmware.bin.
#
Import("env")

import os
import struct
import subprocess
import sys
import zlib

# Official languages first, then the community languages (the order of the menu)
LANGUAGES = ["cs", "de", "es", "fr", "it", "pl",
             "nl", "ro", "hu", "hr", "sk", "sv", "no"]

PROJECT_DIR = env.subst("$PROJECT_DIR")
LANG_DIR = os.path.join(PROJECT_DIR, "lang")


def python_deps():
    python = env.subst("$PYTHONEXE")
    try:
        subprocess.check_call([python, "-c", "import polib, elftools"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        print("lang.py: installing polib/pyelftools")
        subprocess.check_call([python, "-m", "pip", "install", "-q", "polib==1.1.1", "pyelftools"])
    return python


def lang_symbols(elf):
    symtab = elf.get_section_by_name(".symtab")
    sec = elf.get_section_by_name("loc_pri")
    if sec is None:
        raise RuntimeError("section loc_pri not found, is LANG_MODE=1 set?")
    sec_start = sec["sh_addr"]
    sec_end = sec_start + sec["sh_size"]
    sec_data = sec.data()

    syms = []
    for sym in sorted(symtab.iter_symbols(), key=lambda x: x.entry.st_value):
        start = sym.entry.st_value
        size = sym.entry.st_size
        if size == 0 or sym["st_info"]["type"] != "STT_OBJECT":
            continue
        if start < sec_start or start + size > sec_end:
            continue
        data = sec_data[start - sec_start:start - sec_start + size]
        if data[0] != 255 or data[1] != 255:
            raise RuntimeError(f"{sym.name}: invalid location offset")
        if data[-1] != 0:
            raise RuntimeError(f"{sym.name}: unterminated string")
        syms.append({"start": start, "size": size, "name": sym.name,
                     "id": len(syms), "data": data[2:-1]})
    return syms


def fw_signature(syms):
    # same as lang-map.py, but with 32 bit addresses
    data = b""
    for sym in syms:
        data += struct.pack("<IHH", sym["start"], sym["size"], sym["id"])
        data += sym["name"].encode("ascii") + b"\0"
        data += sym["data"] + b"\0"
    return zlib.crc32(data)


def symbol(elf, name):
    sym = elf.get_section_by_name(".symtab").get_symbol_by_name(name)
    if not sym:
        raise RuntimeError(f"symbol {name} not found")
    return sym[0].entry.st_value, sym[0].entry.st_size


def bin_base(elf):
    # objcopy -O binary starts at the lowest load address
    return min(seg["p_paddr"] for seg in elf.iter_segments()
               if seg["p_type"] == "PT_LOAD" and seg["p_filesz"] > 0)


def add_languages(target, source, env):
    import elftools.elf.elffile

    python = python_deps()
    build_dir = env.subst("$BUILD_DIR")
    elf_path = os.path.join(build_dir, env.subst("${PROGNAME}.elf"))
    bin_path = str(target[0])
    out_dir = os.path.join(build_dir, "lang")
    os.makedirs(out_dir, exist_ok=True)

    with open(elf_path, "rb") as f:
        elf = elftools.elf.elffile.ELFFile(f)
        syms = lang_symbols(elf)
        signature = fw_signature(syms)
        sig_addr, _ = symbol(elf, "_PRI_LANG_SIGNATURE")
        sec_addr, sec_size = symbol(elf, "_SEC_LANG")
        base = bin_base(elf)

    # symbol map in the format of lang-map.py
    map_path = os.path.join(out_dir, "firmware.map")
    with open(map_path, "w") as f:
        f.write("OFFSET\tSIZE\tNAME\tID\tSTRING\n")
        for sym in syms:
            f.write("{:04x}\t{:04x}\t{}\t{}\t{}\n".format(
                sym["start"], sym["size"], sym["name"], sym["id"], sym["data"]))
        f.write("{:04x}\t{:04x}\t{}\t{}\t{}\n".format(
            sig_addr, 4, "_PRI_LANG_SIGNATURE", "", signature))

    # language tables
    tables = b""
    for code in LANGUAGES:
        po = os.path.join(LANG_DIR, "po", f"Firmware_{code}.po")
        out = os.path.join(out_dir, f"lang_{code}.bin")
        res = subprocess.run([python, os.path.join(LANG_DIR, "lang-build.py"), map_path, po, out],
                             cwd=LANG_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        with open(os.path.join(out_dir, f"lang_{code}.log"), "w") as f:
            f.write(res.stdout)
        if res.returncode != 0:
            print(res.stdout)
            raise RuntimeError(f"lang-build.py failed for {code}")
        with open(out, "rb") as f:
            tables += f.read()
    if len(tables) > sec_size:
        raise RuntimeError(f"language tables need {len(tables)} bytes, LANG_SIZE_RESERVED is {sec_size}")

    with open(bin_path, "r+b") as fw:
        fw.seek(0, os.SEEK_END)
        bin_size = fw.tell()
        for addr, size in ((sec_addr, sec_size), (sig_addr, 4)):
            if addr < base or addr + size - base > bin_size:
                raise RuntimeError(f"address {addr:#x} is not in {bin_path}")
        for sym in syms:
            fw.seek(sym["start"] - base)
            fw.write(struct.pack("<H", sym["id"]))
        fw.seek(sig_addr - base)
        fw.write(struct.pack("<I", signature))
        fw.seek(sec_addr - base)
        fw.write(tables)

    print("Languages: en %s, %d strings, %d of %d bytes used" % (
        " ".join(LANGUAGES), len(syms), len(tables), sec_size))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", add_languages)
