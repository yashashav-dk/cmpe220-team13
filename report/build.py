#!/usr/bin/env python3
"""build.py -- build the two report PDFs from the toolchain's own output.

    python3 report/build.py [--build build] [--out build/report] [--html-only]

Run `make run` first (or just `make report`, which does both). Every memory
table in the PDFs is printed by t13view from a real t13sim trace, every
listing comes from t13as, and the instruction-encoding table is assembled
by t13as and checked against include/isa_ops.def, so the report cannot
drift from the code.

Output: <out>/part1.html, <out>/part2.html and, when a Chrome/Chromium
browser is found (or WeasyPrint is installed), the PDFs
<out>/Team13_Part1_CPU_Design.pdf and <out>/Team13_Part2_Program_Layout.pdf.
Set CHROME=/path/to/chrome to pick the browser. Python 3.8+, standard
library only.
"""
import argparse
import datetime
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = "https://github.com/yashashav-dk/cmpe220-team13"
TEAM = [
    ("Manoj Konda", "kmanoj03"),
    ("Sai Karan Akula", "karanAkula"),
    ("Yashashav Devalapalli Kamalraj", "yashashav-dk"),
]
esc = html.escape
SAI, MANOJ = "Sai Karan", "Manoj"
# A teammate who has reviewed their areas adds their name here, e.g. {SAI}.
# Every "[to be confirmed by ...]" marker with that name then drops out of both PDFs.
CONFIRMED = {SAI, MANOJ}


def tbc(*who):
    """The one marker for a part a teammate still has to confirm ('' once confirmed)."""
    left = [w for w in who if w not in CONFIRMED]
    return f"[to be confirmed by {' and '.join(left)}]" if left else ""


# ---- running the tools ---------------------------------------------------------

def run(cmd, **kw):
    """Run cmd from the repo root and return stdout; fail loudly."""
    p = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True, **kw)
    if p.returncode != 0:
        sys.exit(f"build.py: {' '.join(map(str, cmd))} failed ({p.returncode}):\n{p.stdout}{p.stderr}")
    return p.stdout


class Tools:
    def __init__(self, build):
        self.build = build
        self.asm = build / "t13as"
        self.sim = build / "t13sim"
        self.view = build / "t13view"
        for t in (self.asm, self.sim, self.view):
            if not t.exists():
                sys.exit(f"build.py: {t} not found; run `make run` first")

    def trace(self, name):
        p = self.build / "programs" / f"{name}.trc"
        if not p.exists():
            sys.exit(f"build.py: {p} not found; run `make run` first")
        return p

    def lst(self, name):
        return self.build / "programs" / f"{name}.lst"

    def html_view(self, name, mode, table=True, confirm=()):
        """t13view --html output; tables get the 'tbl' class so they are
        captioned 'Table n' instead of 'Figure n'. `confirm` names who still
        confirms this view; the marker goes on its first caption."""
        out = run([self.view, self.trace(name), f"--{mode}", "--html", "--name", f"{name}.s"])
        if table:
            out = out.replace('<figure class="t13fig">', '<figure class="t13fig tbl">')
        if tbc(*confirm):
            out = out.replace("</figcaption>", f" {tbc(*confirm)}</figcaption>", 1)
        return out


# ---- listings ------------------------------------------------------------------

LINE_RE = re.compile(r"^(?:(?P<label>[A-Za-z_]\w*):)?\s*(?P<ins>[^;]*?)\s*(?:;\s?(?P<cmt>.*))?$")


def parse_lst(path):
    """[(addr, words, source)] for every source line of a t13as listing."""
    rows = []
    for line in Path(path).read_text().splitlines():
        if line.startswith("SYMBOLS"):
            break
        if line.startswith("ADDR  WORDS"):
            continue
        rows.append((line[0:4].strip(), line[6:22].strip(), line[22:]))
    return rows


def split_source(src):
    m = LINE_RE.match(src.strip())
    return (m.group("label") or "", m.group("ins") or "", (m.group("cmt") or "").rstrip()) if m else ("", src, "")


def listing_table(lst, caption):
    """Program written directly in assembly: one row per statement."""
    rows = []
    started = False
    for addr, words, src in parse_lst(lst):
        s = src.strip()
        if not started:
            started = s.startswith(".equ") or s.startswith(".text")
            if not started:
                continue
        if not s or s.startswith(";"):
            continue
        label, ins, cmt = split_source(s)
        rows.append(
            f"<tr><td class='addr'>{esc(addr)}</td><td class='words'>{esc(words)}</td>"
            f"<td>{esc(label + ':' if label else '')}</td><td class='asm'>{esc(ins)}</td>"
            f"<td class='cmt'>{esc(cmt)}</td></tr>"
        )
    return (
        "<figure class='tbl'><table class='src'><thead><tr><th>Addr</th><th>Words</th><th>Label</th>"
        "<th>Assembly</th><th>Comment</th></tr></thead><tbody>"
        + "".join(rows)
        + f"</tbody></table><figcaption>{caption}</figcaption></figure>"
    )


def side_by_side(lst, caption):
    """Translated program: each C line next to the instructions under it in
    the .s file (the C line is the comment above them), with addresses and
    machine words from the listing."""
    groups = []  # [c_text, [(addr, words, label, ins, cmt)]]
    data = []
    seg = None
    for addr, words, src in parse_lst(lst):
        s = src.strip()
        if s.startswith(".text"):
            seg = "text"
            continue
        if s.startswith(".data"):
            seg = "data"
            continue
        if seg is None or not s:
            continue
        if seg == "data":
            if addr:
                label, ins, cmt = split_source(s)
                data.append((addr, words, label, ins, cmt))
            continue
        if s.startswith(";") and src.startswith(";"):  # a C line kept as a comment
            groups.append([s[1:].strip(), []])
            continue
        if s.startswith(";") or s.startswith("."):
            continue
        label, ins, cmt = split_source(s)
        if not groups:
            groups.append(["", []])
        groups[-1][1].append((addr, words, label, ins, cmt))

    rows = []
    for c, asm in groups:
        n = max(1, len(asm))
        cells = asm or [("", "", "", "", "")]
        for i, (addr, words, label, ins, cmt) in enumerate(cells):
            ccell = f"<td class='c' rowspan='{n}'>{esc(c)}</td>" if i == 0 else ""
            asm_text = (label + ":").ljust(9) + ins if label else " " * 9 + ins if ins else ""
            rows.append(
                f"<tr class='{'grp' if i == 0 else ''}'>{ccell}<td class='addr'>{esc(addr)}</td>"
                f"<td class='words'>{esc(words)}</td><td class='asm'>{esc(asm_text)}</td>"
                f"<td class='cmt'>{esc(cmt)}</td></tr>"
            )
    for addr, words, label, ins, cmt in data:
        rows.append(
            f"<tr class='grp'><td class='c'>(Data segment)</td><td class='addr'>{esc(addr)}</td>"
            f"<td class='words'>{esc(words)}</td><td class='asm'>{esc((label + ':').ljust(9) + ins)}</td>"
            f"<td class='cmt'>{esc(cmt)}</td></tr>"
        )
    return (
        "<figure class='tbl'><table class='src'><thead><tr><th>C (programs/*.c)</th><th>Addr</th>"
        "<th>Words</th><th>T13 assembly (programs/*.s)</th><th>Comment</th></tr></thead><tbody>"
        + "".join(rows)
        + f"</tbody></table><figcaption>{caption}</figcaption></figure>"
    )


# ---- instruction encoding table --------------------------------------------------

ALU = [(2, "ADD", "+"), (3, "SUB", "−"), (4, "MUL", "×"), (5, "DIV", "÷"),
       (6, "AND", "AND"), (7, "OR", "OR"), (8, "XOR", "XOR")]


def isa_rows():
    """(op, mode, form, words, operation, example) for every legal pair."""
    r = [
        (0, 0, "HALT", "0000", "stop the machine", "HALT"),
        (0, 1, "NOP", "0100", "no operation", "NOP"),
        (1, 0, "MOV Ra, Rb", "10ab", "Ra ← Rb", "MOV R1, R2"),
        (1, 1, "MOV Ra, #k", "11a0 k", "Ra ← k", "MOV R1, #5"),
        (1, 2, "MOV Ra, [Rb]", "12ab", "Ra ← M[Rb]", "MOV R0, [R2]"),
        (1, 3, "MOV Ra, [Rb+k]", "13ab k", "Ra ← M[Rb+k]", "MOV R3, [FP+4]"),
        (1, 4, "MOV [Ra], Rb", "14ab", "M[Ra] ← Rb", "MOV [SP], R1"),
        (1, 5, "MOV [Ra+k], Rb", "15ab k", "M[Ra+k] ← Rb", "MOV [FP+6], R0"),
        (1, 6, "MOV Ra, [addr]", "16a0 addr", "Ra ← M[addr]", "MOV R0, [0x0100]"),
        (1, 7, "MOV [addr], Ra", "17a0 addr", "M[addr] ← Ra", "MOV [0x0102], R0"),
    ]
    for op, name, sym in ALU:
        r += [
            (op, 0, f"{name} Ra, Rb", f"{op:X}0ab", f"Ra ← Ra {sym} Rb", f"{name} R1, R2"),
            (op, 1, f"{name} Ra, #k", f"{op:X}1a0 k", f"Ra ← Ra {sym} k", f"{name} R1, #3"),
            (op, 2, f"{name} Ra, [Rb]", f"{op:X}2ab", f"Ra ← Ra {sym} M[Rb]", f"{name} R0, [R2]"),
            (op, 3, f"{name} Ra, [Rb+k]", f"{op:X}3ab k", f"Ra ← Ra {sym} M[Rb+k]", f"{name} R0, [FP+4]"),
        ]
    r += [
        (9, 0, "SHL Ra, Rb", "90ab", "Ra ← Ra << (Rb & 15)", "SHL R1, R2"),
        (9, 1, "SHL Ra, #n", "91a0 n", "Ra ← Ra << n", "SHL R1, #1"),
        (9, 2, "SHR Ra, Rb", "92ab", "Ra ← Ra >> (Rb & 15)", "SHR R1, R2"),
        (9, 3, "SHR Ra, #n", "93a0 n", "Ra ← Ra >> n (logical)", "SHR R1, #1"),
        (0xA, 0, "CMP Ra, Rb", "A0ab", "flags of Ra − Rb", "CMP R1, R2"),
        (0xA, 1, "CMP Ra, #k", "A1a0 k", "flags of Ra − k", "CMP R1, #0"),
        (0xB, 0, "JMP addr", "B000 addr", "PC ← addr", "JMP 0x0010"),
        (0xB, 1, "JEQ addr", "B100 addr", "if Z: PC ← addr", "JEQ 0x0010"),
        (0xB, 2, "JNE addr", "B200 addr", "if not Z", "JNE 0x0010"),
        (0xB, 3, "JLT addr", "B300 addr", "if C (unsigned <)", "JLT 0x0010"),
        (0xB, 4, "JGE addr", "B400 addr", "if not C", "JGE 0x0010"),
        (0xB, 5, "JGT addr", "B500 addr", "if not C and not Z", "JGT 0x0010"),
        (0xB, 6, "JLE addr", "B600 addr", "if C or Z", "JLE 0x0010"),
        (0xC, 0, "CALL addr", "C000 addr", "SP ← SP−2; M[SP] ← PC; PC ← addr", "CALL 0x0040"),
        (0xD, 0, "RET", "D000", "PC ← M[SP]; SP ← SP+2", "RET"),
    ]
    return r


def isa_def_pairs():
    """{(op, mode)} from include/isa_ops.def, the source of truth."""
    pairs = set()
    for m in re.finditer(r"^OP\(\s*0x([0-9A-F]),\s*(\w+),\s*(\d+),", (ROOT / "include/isa_ops.def").read_text(), re.M):
        op, nmodes = int(m.group(1), 16), int(m.group(3))
        pairs |= {(op, md) for md in range(nmodes)}
    return pairs


def encoding_table(tools, work):
    rows = isa_rows()
    if {(op, md) for op, md, *_ in rows} != isa_def_pairs() or len(rows) != len(isa_def_pairs()):
        sys.exit("build.py: the encoding table does not cover exactly the (OP, MODE) pairs of isa_ops.def")
    src = work / "isa_examples.s"
    src.write_text("; every legal (OP, MODE) pair once, assembled for the encoding table\n        .text\n"
                   + "".join(f"        {ex}\n" for *_, ex in rows))
    lst = work / "isa_examples.lst"
    run([tools.asm, src, "-o", work / "isa_examples.img", "-l", lst])
    words = [w for a, w, s in parse_lst(lst) if a]
    if len(words) != len(rows):
        sys.exit("build.py: isa_examples listing does not have one line per example")
    out, prev = [], None
    for (op, md, form, tmpl, oper, ex), hexw in zip(rows, words):
        first = int(hexw.split()[0], 16)
        if (first >> 12, (first >> 8) & 15) != (op, md):
            sys.exit(f"build.py: '{ex}' assembled to {hexw}, not OP {op:X} MODE {md:X}")
        grp = "g0" if op % 2 == 0 else "g1"
        flags = "Z N C" if 2 <= op <= 0xA else "—"
        out.append(
            f"<tr class='{grp}'><td class='op'>{op:X}</td><td>{md:X}</td><td>{op:04b} {md:04b}</td>"
            f"<td class='ins'>{esc(form)}</td><td>{esc(tmpl)}</td><td class='ins'>{esc(oper)}</td>"
            f"<td>{flags}</td><td class='ins'>{esc(ex)}</td><td><b>{esc(hexw)}</b></td></tr>"
        )
    return (
        "<figure class='tbl'><table class='t13 enc'><thead><tr><th>OP</th><th>MODE</th><th>Opcode bits [15:8]</th>"
        "<th class='ins'>Assembly</th><th>Word(s)</th><th class='ins'>Operation</th><th>Flags</th>"
        "<th class='ins'>Example</th><th>Hex (from t13as)</th></tr></thead><tbody>"
        + "".join(out)
        + "</tbody></table><figcaption>instruction encoding, every legal OP/MODE pair (53). "
        "a = Ra field, b = Rb field, k / n / addr = the EXT word that follows; M[x] = 16-bit word at x. "
        "Hex column assembled by <code>t13as</code> and checked against <code>include/isa_ops.def</code>. "
        "Codes E and F, and any MODE not listed, are illegal instructions.</figcaption></figure>"
    )


# ---- traces (for the layout figure) -------------------------------------------------

def parse_trc(path):
    """Symbols, FP of every complete frame, lowest SP, and the functions
    (the entry point and every CALL target) of a trace."""
    syms, fps, min_sp, ir, funcs = {}, [], 0x0400, None, set()
    for line in Path(path).read_text().splitlines():
        if line.startswith("SYM "):
            _, name, addr = line.split()
            syms[name] = int(addr, 16)
        elif line.startswith("ENTRY "):
            funcs.add(int(line.split()[1], 16))
        elif line.startswith("STEP "):
            ir = re.search(r"IR=([0-9A-F]{4})", line).group(1)
            if ir == "C000":
                funcs.add(int(re.search(r"EXT=([0-9A-F]{4})", line).group(1), 16))
        elif line.startswith("REGS") and ir is not None:
            regs = dict(kv.split("=") for kv in line.split()[1:])
            sp, fp = int(regs["SP"], 16), int(regs["FP"], 16)
            min_sp = min(min_sp, sp)
            if ir == "10DE":  # MOV FP, SP: a frame is complete
                fps.append(fp)
    return syms, fps, min_sp, funcs


def code_end(lst):
    end = 0
    for addr, words, src in parse_lst(lst):
        if addr and int(addr, 16) < 0x0100:
            end = max(end, int(addr, 16) + 2 * len(words.split()))
    return end


# ---- diagrams -----------------------------------------------------------------------

SVG_DEFS = """<defs>
<marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
<path d="M0,0 L10,5 L0,10 z" fill="#1b1f24"/></marker>
<marker id="ab" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
<path d="M0,0 L10,5 L0,10 z" fill="#0b5cad"/></marker>
<marker id="ag" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
<path d="M0,0 L10,5 L0,10 z" fill="#8a6d00"/></marker>
</defs>
<style>
.box{fill:#fff;stroke:#1b1f24;stroke-width:1.4}
.cpu{fill:#f7f9fb;stroke:#1b1f24;stroke-width:1.6}
.t{font-size:12px;fill:#1b1f24}
.tb{font-size:13px;font-weight:bold;fill:#1b1f24}
.ts{font-size:10.5px;fill:#57606a}
.mono{font-family:Menlo,monospace}
.w{stroke:#1b1f24;stroke-width:1.5;fill:none}
.d{stroke:#0b5cad;stroke-width:1.5;fill:none}
.c{stroke:#8a6d00;stroke-width:1.2;fill:none;stroke-dasharray:5 3}
.lab{font-size:10.5px;fill:#0b5cad}
.labc{font-size:10.5px;fill:#8a6d00}
</style>"""


def cpu_svg():
    return f"""<svg viewBox="0 0 1000 610" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="T13 CPU diagram">
{SVG_DEFS}
<!-- RAM -->
<text x="115" y="40" text-anchor="middle" class="tb">RAM · 1 KiB</text>
<rect x="20" y="50" width="190" height="470" class="box"/>
<rect x="20" y="50" width="190" height="165" fill="#fbe4e1" stroke="#1b1f24"/>
<rect x="20" y="215" width="190" height="110" fill="#fdf2d6" stroke="#1b1f24"/>
<rect x="20" y="325" width="190" height="55" fill="#e2f3e6" stroke="#1b1f24"/>
<rect x="20" y="380" width="190" height="140" fill="#e3ecfd" stroke="#1b1f24"/>
<text x="115" y="80" text-anchor="middle" class="tb">Stack</text>
<text x="115" y="97" text-anchor="middle" class="ts mono">0280–03FF</text>
<text x="115" y="114" text-anchor="middle" class="ts">frames, grows down from SP=0400</text>
<text x="115" y="250" text-anchor="middle" class="tb">Heap</text>
<text x="115" y="267" text-anchor="middle" class="ts mono">0180–027F</text>
<text x="115" y="284" text-anchor="middle" class="ts">malloc blocks, grows up</text>
<text x="115" y="350" text-anchor="middle" class="tb">Data</text>
<text x="115" y="367" text-anchor="middle" class="ts mono">0100–017F · globals</text>
<text x="115" y="425" text-anchor="middle" class="tb">Text</text>
<text x="115" y="442" text-anchor="middle" class="ts mono">0000–00FF · code</text>
<text x="115" y="459" text-anchor="middle" class="ts">read-only: a write traps</text>
<text x="115" y="540" text-anchor="middle" class="ts">16-bit words, little-endian, even addresses</text>
<line x1="210" y1="300" x2="240" y2="300" class="w" marker-start="url(#ar)" marker-end="url(#ar)"/>

<!-- bus -->
<rect x="240" y="50" width="46" height="470" fill="#eef1f4" stroke="#1b1f24" stroke-width="1.4"/>
<text transform="translate(268,285) rotate(-90)" text-anchor="middle" class="tb">SYSTEM BUS · address 16 · data 16 · read / write</text>

<!-- CPU -->
<rect x="316" y="16" width="668" height="578" rx="8" class="cpu"/>
<text x="332" y="38" class="tb">T13 CPU</text>
<text x="975" y="38" text-anchor="end" class="ts">src/sim/cpu.c</text>

<rect x="340" y="66" width="120" height="50" class="box"/>
<text x="400" y="88" text-anchor="middle" class="tb">PC</text>
<text x="400" y="105" text-anchor="middle" class="ts">program counter</text>
<rect x="340" y="140" width="190" height="40" class="box"/>
<text x="435" y="157" text-anchor="middle" class="t">next PC</text>
<text x="435" y="172" text-anchor="middle" class="ts mono">PC+2/+4 | EXT | M[SP]</text>
<line x1="400" y1="140" x2="400" y2="118" class="w" marker-end="url(#ar)"/>
<path d="M340,90 H288" class="d" marker-end="url(#ab)"/>

<rect x="500" y="66" width="190" height="50" class="box"/>
<text x="595" y="86" text-anchor="middle" class="tb">IR</text>
<text x="595" y="104" text-anchor="middle" class="ts mono">OP | MODE | Ra | Rb (+EXT)</text>
<path d="M288,56 H595 V64" class="d" marker-end="url(#ab)"/>
<text x="440" y="51" text-anchor="middle" class="lab">① fetch: instruction word + EXT</text>

<rect x="735" y="60" width="230" height="156" class="box"/>
<text x="850" y="82" text-anchor="middle" class="tb">CONTROL UNIT</text>
<text x="850" y="99" text-anchor="middle" class="ts">sim_step(): ② decode</text>
<text x="750" y="120" class="t">OP → operation</text>
<text x="750" y="137" class="t">MODE → operand form / condition</text>
<text x="750" y="154" class="t">Ra, Rb → register select</text>
<text x="750" y="176" class="t">control bits:</text>
<text x="750" y="192" class="ts">RegWrite · MemRead · MemWrite</text>
<text x="750" y="207" class="ts">ALUfn · FlagsWrite · PCload</text>
<path d="M690,91 H733" class="w" marker-end="url(#ar)"/>

<!-- control bits (dashed) -->
<path d="M760,216 V236 H300" class="c"/>
<path d="M300,236 V236" class="c"/>
<path d="M430,236 V256" class="c" marker-end="url(#ag)"/>
<path d="M690,236 V256" class="c" marker-end="url(#ag)"/>
<path d="M470,236 V182" class="c" marker-end="url(#ag)"/>
<path d="M300,236 H290" class="c" marker-end="url(#ag)"/>
<text x="560" y="230" text-anchor="middle" class="labc">control bits</text>

<rect x="340" y="258" width="180" height="246" class="box"/>
<text x="430" y="280" text-anchor="middle" class="tb">REGISTERS</text>
<text x="352" y="304" class="t mono">R0</text><text x="400" y="304" class="ts">accumulator</text>
<text x="352" y="326" class="t mono">R1–R3</text><text x="400" y="326" class="ts">scratch</text>
<text x="352" y="348" class="t mono">R4–R12</text><text x="400" y="348" class="ts">general</text>
<text x="352" y="370" class="t mono">R13</text><text x="400" y="370" class="ts">FP frame pointer</text>
<text x="352" y="392" class="t mono">R14</text><text x="400" y="392" class="ts">SP stack pointer</text>
<text x="352" y="414" class="t mono">R15</text><text x="400" y="414" class="ts">reserved</text>
<text x="430" y="446" text-anchor="middle" class="ts">16 × 16 bits</text>
<path d="M340,470 H288" class="d" marker-end="url(#ab)"/>
<text x="346" y="490" class="lab">← address / store data</text>

<rect x="575" y="258" width="150" height="82" class="box"/>
<text x="650" y="280" text-anchor="middle" class="tb">SOURCE MUX</text>
<text x="650" y="298" text-anchor="middle" class="ts mono">Rb | #EXT</text>
<text x="650" y="314" text-anchor="middle" class="ts mono">M[Rb] | M[Rb+EXT]</text>
<text x="650" y="330" text-anchor="middle" class="ts">operand s (MODE)</text>
<path d="M520,300 H573" class="w" marker-end="url(#ar)"/>
<text x="546" y="294" text-anchor="middle" class="ts">Rb</text>
<path d="M620,116 V256" class="w" marker-end="url(#ar)"/>
<text x="626" y="200" class="ts">EXT</text>
<path d="M620,160 H532" class="w" marker-end="url(#ar)"/>
<text x="575" y="154" text-anchor="middle" class="ts">jump target</text>
<path d="M288,560 H760 V320 H727" class="d" marker-end="url(#ab)"/>
<text x="768" y="500" class="lab">memory</text>
<text x="768" y="514" class="lab">operand</text>

<polygon points="560,372 740,372 705,470 595,470" class="box"/>
<text x="650" y="398" text-anchor="middle" class="tb">ALU  ③</text>
<text x="650" y="418" text-anchor="middle" class="ts mono">ADD SUB MUL DIV</text>
<text x="650" y="434" text-anchor="middle" class="ts mono">AND OR XOR SHL SHR</text>
<text x="650" y="452" text-anchor="middle" class="ts">CMP = SUB, flags only</text>
<path d="M650,340 V370" class="w" marker-end="url(#ar)"/>
<text x="656" y="360" class="ts">B = s</text>
<path d="M520,392 H566" class="w" marker-end="url(#ar)"/>
<text x="540" y="386" text-anchor="middle" class="ts">A = Ra</text>
<path d="M650,470 V530 H470 V506" class="w" marker-end="url(#ar)"/>
<text x="560" y="524" text-anchor="middle" class="ts">④ result → Ra</text>

<rect x="810" y="400" width="150" height="52" class="box"/>
<text x="885" y="422" text-anchor="middle" class="tb">FLAGS</text>
<text x="885" y="440" text-anchor="middle" class="t mono">Z  N  C</text>
<path d="M722,420 H808" class="w" marker-end="url(#ar)"/>
<path d="M885,400 V218" class="w" marker-end="url(#ar)"/>
<text x="891" y="320" class="ts">jump</text>
<text x="891" y="333" class="ts">condition</text>

<text x="332" y="582" class="ts">① fetch IR ← M[PC], PC ← PC+2/+4   ② decode   ③ execute (ALU or address)   ④ store (register, memory, PC); every check runs before ④</text>
</svg>"""


def insn_svg():
    cells = []
    fields = [("OP", "15–12", "#e3ecfd", "0010", "2 = ADD"), ("MODE", "11–8", "#fdf2d6", "0000", "0 = Ra, Rb"),
              ("Ra", "7–4", "#e2f3e6", "0001", "R1"), ("Rb", "3–0", "#fbe4e1", "0010", "R2")]
    x = 60
    for name, bits, fill, val, mean in fields:
        cells.append(f'<rect x="{x}" y="40" width="180" height="46" fill="{fill}" stroke="#1b1f24" stroke-width="1.4"/>'
                     f'<text x="{x + 90}" y="30" text-anchor="middle" class="ts mono">bits {bits}</text>'
                     f'<text x="{x + 90}" y="69" text-anchor="middle" class="tb">{name}</text>'
                     f'<text x="{x + 90}" y="108" text-anchor="middle" class="t mono">{val}</text>'
                     f'<text x="{x + 90}" y="126" text-anchor="middle" class="ts">{mean}</text>')
        x += 180
    return f"""<svg viewBox="0 0 860 215" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="instruction word">
{SVG_DEFS}
{''.join(cells)}
<rect x="60" y="150" width="720" height="34" fill="#f6f8fa" stroke="#1b1f24" stroke-dasharray="5 3"/>
<text x="420" y="172" text-anchor="middle" class="t">EXT word (16 bits) at address + 2, only in modes with a constant, offset or address</text>
<text x="800" y="69" class="tb mono">= 2012</text>
<text x="800" y="108" class="ts">ADD R1, R2</text>
<text x="420" y="206" text-anchor="middle" class="ts">OP + MODE = the 8-bit opcode of the class design; each field is one hex digit, so a dump decodes by eye</text>
</svg>"""


def layout_svg(syms, fps, min_sp, text_end, funcs):
    """Part 2 program layout: the four segments of memory, to scale."""
    top, bottom = 30, 590
    scale = (bottom - top) / 0x400

    def y(addr):
        return bottom - addr * scale

    segs = [("Text", 0x0000, 0x0100, "#e3ecfd"), ("Data", 0x0100, 0x0180, "#e2f3e6"),
            ("Heap", 0x0180, 0x0280, "#fdf2d6"), ("Stack", 0x0280, 0x0400, "#fbe4e1")]
    out = []
    for name, lo, hi, fill in segs:
        out.append(f'<rect x="230" y="{y(hi):.1f}" width="170" height="{(hi - lo) * scale:.1f}" fill="{fill}" stroke="#1b1f24" stroke-width="1.4"/>'
                   f'<text x="315" y="{(y(hi) + y(lo)) / 2 + 5:.1f}" text-anchor="middle" class="tb">{name}</text>'
                   f'<text x="220" y="{y(lo) + 4:.1f}" text-anchor="end" class="ts mono">{lo:04X}</text>')
    out.append(f'<text x="220" y="{y(0x400) + 4:.1f}" text-anchor="end" class="ts mono">0400</text>')
    # used parts
    out.append(f'<rect x="230" y="{y(text_end):.1f}" width="22" height="{text_end * scale:.1f}" fill="#0b5cad" opacity="0.55"/>')
    if fps:
        lo = min(fps)
        out.append(f'<rect x="230" y="{y(0x400):.1f}" width="22" height="{(0x400 - lo) * scale:.1f}" fill="#c5221f" opacity="0.55"/>')
    # annotations
    notes = []
    textsyms = sorted((a, n) for n, a in syms.items() if a in funcs)
    for i, (a, n) in enumerate(textsyms):
        nxt = textsyms[i + 1][0] if i + 1 < len(textsyms) else text_end
        notes.append((a, f"{n}(): {a:04X}–{nxt - 1:04X}", "#0b5cad"))
    for n, a in sorted(syms.items(), key=lambda kv: kv[1]):
        if 0x0100 <= a < 0x0180:
            notes.append((a, f"{n} @ {a:04X}", "#1e7a34"))
    notes.append((0x0180, "malloc block @ 0180 (bump pointer heap_top)", "#8a6d00"))
    if fps:
        notes.append((max(fps), f"first frame @ {max(fps):04X} (8 bytes)", "#c5221f"))
        notes.append((min(fps), f"deepest frame @ {min(fps):04X}; {len(fps)} frames", "#c5221f"))
    placed = []
    for a, text, color in sorted(notes, key=lambda n: n[0]):
        ty = y(a) - 2
        while any(abs(ty - p) < 15 for p in placed):
            ty -= 15
        placed.append(ty)
        out.append(f'<path d="M402,{y(a):.1f} L440,{ty:.1f} H452" stroke="{color}" fill="none" stroke-width="1"/>'
                   f'<text x="456" y="{ty + 4:.1f}" class="t" fill="{color}" style="fill:{color}">{esc(text)}</text>')
    out.append(f'<path d="M150,{y(0x0400) + 10:.1f} V{y(0x0300):.1f}" class="w" marker-end="url(#ar)"/>'
               f'<text x="140" y="{y(0x0390):.1f}" text-anchor="end" class="ts">stack grows down</text>'
               f'<text x="140" y="{y(0x0390) + 14:.1f}" text-anchor="end" class="ts">from SP = 0400</text>'
               f'<path d="M150,{y(0x0190):.1f} V{y(0x0230):.1f}" class="w" marker-end="url(#ar)"/>'
               f'<text x="140" y="{y(0x01E0):.1f}" text-anchor="end" class="ts">heap grows up</text>'
               f'<text x="140" y="{y(0x0080):.1f}" text-anchor="end" class="ts">entry: main @ 0000</text>')
    return f"""<svg viewBox="0 0 860 610" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="program layout">
{SVG_DEFS}
{''.join(out)}
<rect x="660" y="140" width="14" height="10" fill="#0b5cad" opacity="0.55"/><text x="680" y="149" class="ts">code in use</text>
<rect x="660" y="158" width="14" height="10" fill="#c5221f" opacity="0.55"/><text x="680" y="167" class="ts">stack in use, deepest call</text>
</svg>"""


def frame_svg():
    slots = [("FP+6", "RETV", "return value", "callee writes it"),
             ("FP+4", "ARG", "argument k", "caller: PUSH"),
             ("FP+2", "RA", "return address", "CALL pushes it"),
             ("FP+0", "OLDFP", "caller's FP", "callee: PUSH FP")]
    out = []
    yy = 60
    for off, name, what, who in slots:
        out.append(f'<rect x="200" y="{yy}" width="260" height="40" fill="#fbe4e1" stroke="#1b1f24" stroke-width="1.4"/>'
                   f'<text x="190" y="{yy + 25}" text-anchor="end" class="t mono">{off}</text>'
                   f'<text x="215" y="{yy + 25}" class="tb mono">{name}</text>'
                   f'<text x="300" y="{yy + 25}" class="t">{what}</text>'
                   f'<text x="475" y="{yy + 25}" class="ts">{who}</text>')
        yy += 40
    return f"""<svg viewBox="0 0 860 300" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="stack frame">
{SVG_DEFS}
<text x="330" y="40" text-anchor="middle" class="ts">higher addresses: the caller's frame</text>
{''.join(out)}
<path d="M120,240 H196" class="w" marker-end="url(#ar)"/><text x="40" y="244" class="tb">FP, SP</text>
<text x="330" y="270" text-anchor="middle" class="ts">lower addresses: the next call's frame (8 bytes lower)</text>
<path d="M640,60 V220" class="w" marker-start="url(#ar)" marker-end="url(#ar)"/>
<text x="652" y="135" class="t">4 words = 8 bytes</text>
<text x="652" y="152" class="ts">8-byte aligned (SP starts at 0400)</text>
<text x="652" y="169" class="ts">so a 4-word dump row = 1 frame</text>
</svg>"""


# ---- shared pages ---------------------------------------------------------------------

def title_page(part, title, today):
    names = "".join(f"<tr><td>{esc(n)}</td><td><code>{esc(g)}</code></td></tr>" for n, g in TEAM)
    return f"""<div class="title">
<div class="course">CMPE 220 · System Software · San José State University · Fall 2026</div>
<div class="name">{esc(title)}</div>
<div class="sub">Project Part {part} · Team 13 · the T13 16-bit CPU</div>
<table>{names}</table>
<div class="meta">Code: <code>{REPO}</code><br>Built {today:%b %d, %Y} from the code by <code>make report</code></div>
</div>"""


def run_table(part):
    if part == 1:
        rows = [
            ("1. Get the code", f"git clone {REPO}.git<br>cd cmpe220-team13", "needs a C99 compiler and make (macOS clang or Linux gcc)"),
            ("2. Build, test, run every program", "make check", "last test line: <code>programs: all passed</code>; views in <code>build/views/</code>"),
            ("3. Program A (8²) by hand", "build/t13as programs/power.s -o power.img -l power.lst<br>build/t13sim power.img -o power.trc<br>build/t13view power.trc --waterfall",
             "<code>power.img: HALT after 17 steps</code>; 18-row waterfall (§3)"),
            ("4. Program B (8!)", "build/t13as programs/fact.s -o fact.img<br>build/t13sim fact.img<br>build/t13view fact.trc --peek result",
             "<code>result=40320</code> (= <code>9D80</code>)"),
            ("5. Same C, native", "cc programs/fact.c -o fact && ./fact", "<code>result=40320</code>"),
        ]
    else:
        rows = [
            ("1. Get the code", f"git clone {REPO}.git<br>cd cmpe220-team13", "needs a C99 compiler and make (macOS clang or Linux gcc)"),
            ("2. Build, test, run every program", "make check", "last test line: <code>programs: all passed</code>"),
            ("3. Assemble and run", "build/t13as programs/recfact.s -o recfact.img<br>build/t13sim recfact.img",
             "<code>recfact.img: HALT after 112 steps</code>"),
            ("4. Loaded executable, decoded", "build/t13view recfact.trc --loaded", "§3"),
            ("5. Four segments per call", "build/t13view recfact.trc --segments", "12 snapshots (§4)"),
            ("6. Result", "build/t13view recfact.trc --peek '*result'<br>cc programs/recfact.c -o rf && ./rf", "both print <code>*result=120</code>"),
        ]
    body = "".join(f"<tr><td>{s}</td><td><code>{c}</code></td><td>{e}</td></tr>" for s, c, e in rows)
    return ("<figure class='tbl keep'><table class='facts'><thead><tr><th>Step</th><th>Command (repo root)</th><th>Expected</th>"
            f"</tr></thead><tbody>{body}</tbody></table><figcaption>how to download and run the code.</figcaption></figure>")


def tests_table(build):
    suites = [
        (build / "test_isa", "codec: encode / decode / disassemble every instruction"),
        (build / "test_asm", "assembler parser, symbols, instruction selection, disasm → asm round trip"),
        (build / "test_assemble", "two-pass assembler, directives, errors, image and listing writers"),
        (build / "test_sim", "simulator: loader errors, every ALU flag rule, jumps, CALL/RET, faults, trace"),
        (build / "test_view", "viewer: trace checks (rule 11), every view"),
    ]
    rows = []
    for exe, what in suites:
        last = run([exe]).strip().splitlines()[-1]
        rows.append((f"<code>{exe.name}</code>", what, last))
    last = run(["sh", "tests/golden.sh", build]).strip().splitlines()[-1]
    rows.append(("<code>tests/golden.sh</code>", "t13as images and listings, t13sim traces vs hand-written golden traces, CLI exit codes", last))
    out = run(["sh", "tests/programs.sh", build], env={**os.environ, "CC": os.environ.get("CC", "cc")}).strip().splitlines()
    rows.append(("<code>tests/programs.sh</code>", "every program end to end; each .c compiled natively prints what t13sim leaves in memory",
                 "<br>".join(esc(l) for l in out)))
    body = "".join(f"<tr><td>{a}</td><td>{b}</td><td><code>{c}</code></td></tr>" for a, b, c in rows)
    return ("<figure class='tbl keep'><table class='facts'><thead><tr><th>Suite</th><th>Checks</th><th>Result (this build)</th></tr></thead>"
            f"<tbody>{body}</tbody></table><figcaption>test results, captured while building this PDF (<code>make test</code>).</figcaption></figure>")


def contrib_table():
    """Same three areas for every member, as split in the team plan. A teammate's
    area ends in the tbc() marker until that teammate confirms it."""
    rows = [
        ("Manoj Konda",
         "Trace format <code>docs/trace.md</code> and the hand-written golden traces <code>halt.trc</code> and <code>power.trc</code> "
         "(the simulator's acceptance tests); CI on Linux gcc/clang and macOS clang (PR #3). Repository layout and <code>Makefile</code>, "
         f"viewer <code>t13view</code>, the recursive Part 2 program <code>recfact</code>. {tbc(MANOJ)}",
         f"§3 and §4 memory tables and their legend, the appendix; title page and page-number template. {tbc(MANOJ)}",
         f"§2 <code>recfact</code> C next to assembly; §4 four-segment view with stack frames, one snapshot per call and return. {tbc(MANOJ)}"),
        ("Sai Karan Akula",
         "Reviewed and approved ISA v1.0 (Nov 3). Image format <code>docs/image.md</code>, stack-frame layout <code>docs/abi.md</code>, "
         f"simulator <code>t13sim</code> (loader and CPU), Program B: 8! from C to T13 assembly. {tbc(SAI)}",
         f"§1 CPU diagram; §4 Program B; put the Part 1 PDF together. {tbc(SAI)}",
         f"§1 program-layout and stack-frame diagrams; §2 calling sequence; §4 CALL/RET snapshots and stack checks. {tbc(SAI)}"),
        ("Yashashav Devalapalli Kamalraj",
         "Repository and MIT license. ISA v1.0 <code>docs/isa.md</code>, opcode table <code>isa_ops.def</code>, codec "
         "<code>isa.c</code> with 561 checks; assembler <code>t13as</code> with listing and manual <code>docs/asm.md</code>, "
         "931 checks and golden files; Program A: 8² in T13 assembly.",
         "§2 instruction encoding table; §3 Program A.",
         "§3 executable loaded into memory; put the Part 2 PDF together."),
    ]
    body = "".join(f"<tr><td>{esc(n)}</td><td>{c}</td><td>{p1}</td><td>{p2}</td></tr>" for n, c, p1, p2 in rows)
    return ("<figure class='tbl keep'><table class='facts'><thead><tr><th style='width:16%'>Member</th><th style='width:40%'>Code and documents</th>"
            "<th>Part 1 report</th><th>Part 2 report</th></tr></thead>"
            f"<tbody>{body}</tbody></table><figcaption>contributions of each member.</figcaption></figure>")


def page(title, body, label):
    css = (ROOT / "report" / "report.css").read_text()
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>{esc(title)}</title>
<style>{css}
@page {{ @bottom-left {{ content: "{label}"; font: 7.5pt "Helvetica Neue", Helvetica, Arial, sans-serif; color: #777; }} }}
</style></head><body>
{body}
</body></html>"""


# ---- the two reports ----------------------------------------------------------------

def part1(tools, work, today):
    b = [title_page(1, "CPU Design", today)]
    b.append("<section><h1>How to run</h1>" + run_table(1) + "</section>")
    b.append("<section><h1>1 · CPU design</h1>"
             "<figure class='keep'>" + cpu_svg() + "<figcaption>T13 CPU: datapath and control. Each block is a function in "
             "<code>src/sim/cpu.c</code>; ①–④ is one instruction cycle. " + tbc(SAI) + "</figcaption></figure>"
             "<figure class='keep'>" + insn_svg() + "<figcaption>instruction word: 16 bits, four 4-bit fields, optional EXT word.</figcaption></figure>"
             "<div class='two'>"
             "<figure class='tbl keep'><table class='facts'><thead><tr><th>Register</th><th>Role</th></tr></thead><tbody>"
             "<tr><td><code>R0</code></td><td>accumulator, return value</td></tr><tr><td><code>R1–R3</code></td><td>scratch (caller-saved)</td></tr>"
             "<tr><td><code>R4–R12</code></td><td>general (callee-saved)</td></tr><tr><td><code>R13 = FP</code></td><td>frame pointer</td></tr>"
             "<tr><td><code>R14 = SP</code></td><td>stack pointer, starts at <code>0400</code></td></tr><tr><td><code>R15</code></td><td>reserved</td></tr>"
             "<tr><td><code>PC · IR · FLAGS</code></td><td>special: program counter, instruction register (word + EXT), Z N C</td></tr>"
             "</tbody></table><figcaption>registers.</figcaption></figure>"
             "<figure class='tbl keep'><table class='facts'><thead><tr><th>Flag</th><th>Set by OP 2–A when</th></tr></thead><tbody>"
             "<tr><td>Z</td><td>result = 0</td></tr><tr><td>N</td><td>bit 15 of the result</td></tr>"
             "<tr><td>C</td><td>ADD carry · SUB/CMP borrow · MUL product &gt; FFFF · SHL/SHR last bit out · else 0</td></tr>"
             "<tr><td>jumps</td><td>unsigned: LT = C, GE = ¬C, GT = ¬C∧¬Z, LE = C∨Z</td></tr>"
             "</tbody></table><figcaption>flags and jump conditions.</figcaption></figure></div></section>")
    b.append("<section><h1>2 · Instruction encoding</h1>" + encoding_table(tools, work) + "</section>")
    b.append("<section><h1>3 · Program A: 8² written in T13 assembly</h1>"
             + listing_table(tools.lst("power"), "<code>programs/power.s</code>: BASE<sup>EXP</sup> = 8² by repeated MUL; addresses and words from <code>t13as</code>.")
             + tools.html_view("power", "grid", confirm=(MANOJ,))
             + "</section><section><h2>Program A: memory after every instruction</h2>"
             + tools.html_view("power", "waterfall", confirm=(MANOJ,))
             + "</section>")
    b.append("<section><h1>4 · Program B: 8! written in C, translated to T13 assembly</h1>"
             + side_by_side(tools.lst("fact"), "<code>programs/fact.c</code> next to <code>programs/fact.s</code>: each C line and the instructions that implement it. " + tbc(SAI))
             + "</section><section><h2>Program B: memory after loading and at the end</h2>"
             + tools.html_view("fact", "grid", confirm=(MANOJ,))
             + "</section><section><h2>Program B: memory after every instruction</h2>"
             + tools.html_view("fact", "waterfall", confirm=(MANOJ,))
             + "</section>")
    b.append("<section><h1>5 · Tests and contributions</h1>" + tests_table(tools.build) + contrib_table() + "</section>")
    b.append("<section><h1>Appendix · Program A, full memory block after every instruction</h1>"
             + tools.html_view("power", "blocks", confirm=(MANOJ,)) + "</section>")
    return page("Team 13 · Part 1 · CPU Design", "\n".join(b), "CMPE 220 · Team 13 · Part 1: CPU Design")


def part2(tools, work, today):
    syms, fps, min_sp, funcs = parse_trc(tools.trace("recfact"))
    text_end = code_end(tools.lst("recfact"))
    b = [title_page(2, "Program Layout & Execution", today)]
    b.append("<section><h1>How to run</h1>" + run_table(2) + "</section>")
    b.append("<section><h1>1 · Program layout</h1>"
             "<figure class='keep'>" + layout_svg(syms, fps, min_sp, text_end, funcs)
             + "<figcaption>layout of <code>recfact</code> in the 1 KiB memory, to scale: Text, Data, Heap, Stack. " + tbc(SAI) + "</figcaption></figure>"
             "<figure class='keep'>" + frame_svg() + "<figcaption>one stack frame (<code>docs/abi.md</code>). " + tbc(SAI) + "</figcaption></figure></section>")
    b.append("<section><h1>2 · Calling sequence</h1>"
             "<figure class='tbl keep'><table class='facts'><thead><tr><th>#</th><th>Caller</th><th>Callee</th><th>SP after (first call)</th></tr></thead><tbody>"
             "<tr><td>1</td><td><code>SUB SP, #2</code> return-value slot</td><td></td><td><code>03FE</code></td></tr>"
             "<tr><td>2</td><td><code>PUSH R1</code> argument</td><td></td><td><code>03FC</code></td></tr>"
             "<tr><td>3</td><td><code>CALL fact</code> pushes return address</td><td></td><td><code>03FA</code></td></tr>"
             "<tr><td>4</td><td></td><td><code>PUSH FP · MOV FP, SP</code> prologue</td><td><code>03F8</code></td></tr>"
             "<tr><td>5</td><td></td><td>body; <code>MOV [FP+RETV], R0</code></td><td><code>03F8</code></td></tr>"
             "<tr><td>6</td><td></td><td><code>MOV SP, FP · POP FP · RET</code> epilogue</td><td><code>03FC</code></td></tr>"
             "<tr><td>7</td><td><code>ADD SP, #2</code> drop argument</td><td></td><td><code>03FE</code></td></tr>"
             "<tr><td>8</td><td><code>POP R0</code> the result</td><td></td><td><code>0400</code></td></tr>"
             "</tbody></table><figcaption>call sequence; <code>PUSH</code>/<code>POP</code> are <code>SUB</code>/<code>ADD SP, #2</code> plus a move. " + tbc(SAI) + "</figcaption></figure>"
             + side_by_side(tools.lst("recfact"), "<code>programs/recfact.c</code> next to <code>programs/recfact.s</code>: main and the recursive fact. " + tbc(MANOJ))
             + "</section>")
    b.append("<section><h1>3 · Executable loaded into memory</h1>" + tools.html_view("recfact", "loaded") + "</section>")
    snaps = tools.html_view("recfact", "segments", table=False, confirm=(SAI, MANOJ))
    b.append("<section><h1>4 · The four segments at every call, every return, and the end</h1>" + snaps + "</section>")
    b.append("<section><h1>5 · Tests and contributions</h1>" + tests_table(tools.build) + contrib_table() + "</section>")
    return page("Team 13 · Part 2 · Program Layout & Execution", "\n".join(b), "CMPE 220 · Team 13 · Part 2: Program Layout & Execution")


# ---- PDF ---------------------------------------------------------------------------

def find_chrome():
    cands = [os.environ.get("CHROME", "")]
    cands += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
              "/Applications/Chromium.app/Contents/MacOS/Chromium",
              "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"]
    cands += [shutil.which(n) or "" for n in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser")]
    return next((c for c in cands if c and Path(c).exists()), None)


def to_pdf(src, dst):
    chrome = find_chrome()
    if chrome:
        with tempfile.TemporaryDirectory() as prof:
            subprocess.run([chrome, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                            f"--user-data-dir={prof}", f"--print-to-pdf={dst}", src.resolve().as_uri()],
                           capture_output=True, timeout=120)
        if Path(dst).exists():
            return f"Chrome ({chrome})"
    try:
        import weasyprint  # noqa: F401 (optional)
        weasyprint.HTML(filename=str(src)).write_pdf(str(dst))
        return "WeasyPrint"
    except Exception as e:  # pragma: no cover
        sys.exit(f"build.py: no PDF renderer (install Chrome/Chromium or WeasyPrint): {e}\nHTML is at {src}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", default="build")
    ap.add_argument("--out", default=None)
    ap.add_argument("--html-only", action="store_true")
    a = ap.parse_args()
    build = (ROOT / a.build).resolve()
    out = Path(a.out).resolve() if a.out else build / "report"
    out.mkdir(parents=True, exist_ok=True)
    tools = Tools(build)
    today = datetime.date.today()

    docs = [("part1.html", "Team13_Part1_CPU_Design.pdf", part1), ("part2.html", "Team13_Part2_Program_Layout.pdf", part2)]
    for html_name, pdf_name, fn in docs:
        h = out / html_name
        h.write_text(fn(tools, out, today), encoding="utf-8")
        if a.html_only:
            print(f"wrote {h}")
            continue
        pdf = out / pdf_name
        if pdf.exists():
            pdf.unlink()
        how = to_pdf(h, pdf)
        print(f"wrote {pdf} ({how})")


if __name__ == "__main__":
    main()
