#!/usr/bin/env python3
"""aarch64 反汇编辅助工具
用法:
  python3 sky_disasm.py <func_start_va> <func_end_va> [--full]
  python3 sky_disasm.py --all           # 反汇编全部 .text (非常慢, 仅用于导出)
"""
import sys, subprocess, re
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

import os
ELF = os.environ.get("SKY_ELF", "/tmp/libBootloader.so")   # 反汇编对象路径(命令行 --elf 或环境变量 SKY_ELF 指定)

# ---- 解析符号表 ----
def readelf_ws():
    out = subprocess.run(["readelf", "-Ws", ELF], capture_output=True, text=True).stdout
    syms = {}          # va -> name
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 8 or not parts[0].rstrip(":").isdigit():
            continue
        # Num: Value Size Type Bind Vis Ndx Name
        if parts[1] == "UND" or parts[1] == "0":
            continue
        try:
            va = int(parts[1], 16)
        except ValueError:
            continue
        if parts[6] == "UND":
            continue
        syms[va] = parts[7]
    return syms

def plt_map():
    """解析 .rela.plt -> PLT stub VA -> 导入符号名"""
    out = subprocess.run(["readelf", "-r", ELF], capture_output=True, text=True).stdout
    plt_base = 0x2b5c2a0
    got_plt_base = 0x2cfab40
    entry_size = 16
    mapping = {}
    in_plt = False
    for line in out.splitlines():
        if ".rela.plt" in line:
            in_plt = True
            continue
        if in_plt and line.strip() == "":
            break
        if not in_plt:
            continue
        m = re.match(r"\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+R_AARCH64_JUMP_SLOT\s+([0-9a-fA-F]+)\s+(\S+)", line)
        if m:
            got_va = int(m.group(1), 16)
            idx = (got_va - got_plt_base) // 8
            stub_va = plt_base + idx * entry_size
            mapping[stub_va] = m.group(3)
    return mapping

syms = readelf_ws()
plt = plt_map()

# ---- 字符串表 (.rodata: 0x433d40 .. 0x607500, 文件偏移 0x433d40) ----
def load_rodata():
    with open(ELF, "rb") as f:
        f.seek(0x433d40)
        data = f.read(0x1d37c0)
    return data

rodata = load_rodata()
RO_START = 0x433d40

def read_cstr(va):
    off = va - RO_START
    if not (0 <= off < len(rodata)):
        return None
    end = rodata.find(b"\x00", off)
    if end < 0 or end - off > 200:
        return None
    try:
        s = rodata[off:end].decode("utf-8", "replace")
    except Exception:
        return None
    return s if s and all(32 <= ord(c) < 127 or c in "中文" for c in s) else None

def fmt_addr(va):
    if va in syms:
        return f"0x{va:x} <{syms[va]}>"
    if va in plt:
        return f"0x{va:x} <PLT:{plt[va]}>"
    return f"0x{va:x}"

def disasm(start, end, full=False):
    with open(ELF, "rb") as f:
        f.seek(start)
        code = f.read(end - start)
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = False
    pending_str = {}
    for insn in md.disasm(code, start):
        line = f"0x{insn.address:x}: {insn.mnemonic:<8} {insn.op_str}"
        # 分支目标
        if insn.mnemonic in ("bl", "b"):
            try:
                tgt = int(insn.op_str, 16)
                line += f"   ; -> {fmt_addr(tgt)}"
            except ValueError:
                pass
        # 字符串引用: adrp + add 组合
        if insn.mnemonic == "adrp":
            try:
                m = re.match(r"x(\d+), #0x([0-9a-fA-F]+)", insn.op_str)
                if m:
                    reg, page = m.group(1), int(m.group(2), 16)
                    pending_str[reg] = page
            except Exception:
                pass
        elif insn.mnemonic == "add" and full:
            m = re.match(r"x(\d+), x(\d+), #0x([0-9a-fA-F]+)", insn.op_str)
            if m and m.group(2) in pending_str:
                reg, base, imm = m.group(1), pending_str[m.group(2)], int(m.group(3), 16)
                va = base + imm
                s = read_cstr(va)
                if s:
                    line += f"   ; str[{va:#x}] = \"{s[:90]}\""
        print(line)

if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--all":
        pass
    if len(sys.argv) == 3:
        disasm(int(sys.argv[1], 16), int(sys.argv[2], 16), full=True)
    elif len(sys.argv) == 4 and sys.argv[3] == "--nofull":
        disasm(int(sys.argv[1], 16), int(sys.argv[2], 16), full=False)
    else:
        print("用法: sky_disasm.py <start_va> <end_va>")
