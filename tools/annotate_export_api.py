#!/usr/bin/env python3
"""Annotate the ksengine export surface with KSENGINE_API.

Input : ksengine_export_surface.csv  (Scope, Name, Kind, Template, Symbols)
        produced by tools/ksengine_export_surface.ps1 from the last build.

Every row is resolved to its *declaring header* inside src/engine:

  * a row whose Scope is a class (a `class Foo` / `struct Foo` *definition*
    exists) annotates the class itself -- a class-level annotation exports
    all of its members, so the member rows collapse into one edit;
  * a row whose Scope is a namespace annotates the free function declaration
    at that namespace scope.

Each touched header then gets `#include "KsExport.h"`.

Dry-run by default; pass --apply to write. Every unresolved or ambiguous
entity is reported instead of guessed at.
"""
from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CSV_PATH = ROOT / "ksengine_export_surface.csv"
SCAN_ROOT = ROOT / "src" / "engine"
MACRO = "KSENGINE_API"
INCLUDE_LINE = '#include "KsExport.h"'
SKIP_DIRS = {"external"}  # vendored third-party code (lua, ...)

KEYWORDS = {
    "if", "for", "while", "switch", "sizeof", "return", "alignas",
    "decltype", "noexcept", "static_assert", "defined", "typeid", "throw",
    "new", "delete", "catch",
}


# --------------------------------------------------------------------------
# line surgery
# --------------------------------------------------------------------------
def strip_line(line: str, in_block: bool) -> tuple[str, bool]:
    """Remove // and /* */ comments and string/char literals."""
    out: list[str] = []
    i, n = 0, len(line)
    while i < n:
        if in_block:
            j = line.find("*/", i)
            if j < 0:
                return "".join(out), True
            in_block = False
            i = j + 2
            continue
        c = line[i]
        if c == "/" and i + 1 < n and line[i + 1] == "*":
            in_block, i = True, i + 2
            continue
        if c == "/" and i + 1 < n and line[i + 1] == "/":
            break
        if c in "\"'":
            q, i = c, i + 1
            while i < n:
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == q:
                    i += 1
                    break
                i += 1
            out.append('""')
            continue
        out.append(c)
        i += 1
    return "".join(out), in_block


def balanced(text: str) -> bool:
    return text.count("(") == text.count(")") and text.count("[") == text.count("]")


def terminates(text: str) -> bool:
    return text.rstrip().endswith((";", "{", "}"))


# --------------------------------------------------------------------------
# header scanning
# --------------------------------------------------------------------------
class Record:
    __slots__ = ("start", "count", "code", "ns", "in_class")

    def __init__(self, start: int, count: int, code: str, ns: str, in_class: bool):
        self.start = start      # index of first physical line
        self.count = count      # number of physical lines
        self.code = code        # joined (comment-stripped) statement text
        self.ns = ns            # enclosing namespace chain, "global" if none
        self.in_class = in_class

    @property
    def end(self) -> int:
        return self.start + self.count - 1


# A class head tolerates export annotations between the keyword and the name
# (`class KSENGINE_API Foo {`), so re-running the tool on an already annotated
# tree is a no-op instead of a mis-classification.
_MACRO = r"(?:(?:[A-Z][A-Z0-9_]*|__declspec\s*\([^)]*\))\s+)*"
CLASS_HEAD = re.compile(
    r"^\s*(?:template\s*<.*?>\s*)?(class|struct)\s+" + _MACRO +
    r"([A-Za-z_]\w*)\b", re.S)
OPEN_HEAD = re.compile(
    r"\b(namespace|class|struct)\s+" + _MACRO +
    r"([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)"
    r"\s*(?:final\b)?(?:\s*:\s*[^{}]*)?$")


def scan_file(path: Path) -> tuple[str, list[str], list[Record]]:
    raw = path.read_text(encoding="utf-8", errors="surrogateescape").splitlines(keepends=True)
    records: list[Record] = []
    ns_stack: list[str] = []
    class_stack: list[str] = []
    brace_stack: list[tuple[str, int]] = []   # ("ns", n) | ("cls", 1) | ("other", 1)
    pend_idx: list[int] = []
    pend_txt: list[str] = []
    pend_ns = "global"
    pend_class = False
    in_block = False
    if_stack: list[bool] = []                 # True while inside `#if 0`

    def flush(flat: str) -> None:
        nonlocal pend_idx, pend_txt
        records.append(Record(pend_idx[0], len(pend_idx),
                              "\n".join(t.strip() for t in pend_txt),
                              pend_ns, pend_class))
        pend_idx, pend_txt = [], []

    for i, rl in enumerate(raw):
        code, in_block = strip_line(rl.rstrip("\r\n"), in_block)
        st = code.strip()

        # --- preprocessor: track `#if 0` regions --------------------------
        if st.startswith("#"):
            if re.match(r"#\s*if\b", st):
                body = re.sub(r"#\s*if\b", "", st, count=1)
                if_stack.append(bool(re.match(r"\s*0\b", body)))
            elif re.match(r"#\s*endif\b", st):
                if if_stack:
                    if_stack.pop()
            if any(if_stack) or st.startswith("#"):
                if pend_idx:
                    pend_idx.append(i)
                    pend_txt.append(code)
                continue

        ns_here = "::".join(ns_stack) or "global"
        in_class_here = bool(class_stack)

        # --- statement accumulation --------------------------------------
        record_code: str | None = None
        if pend_idx:
            pend_idx.append(i)
            pend_txt.append(code)
            flat = " ".join(t.strip() for t in pend_txt)
            if balanced(flat) and terminates(flat):
                record_code = flat
                flush(flat)
        elif st and not st.startswith("#"):
            pend_idx, pend_txt = [i], [code]
            pend_ns, pend_class = ns_here, in_class_here
            if balanced(st) and terminates(st):
                record_code = st
                flush(st)

        # --- brace tracking (drives namespace / class nesting) ------------
        last = 0
        first_brace = True
        for m in re.finditer(r"[{}]", code):
            ch = m.group(0)
            if ch == "}":
                if brace_stack:
                    kind, cnt = brace_stack.pop()
                    if kind == "ns":
                        del ns_stack[len(ns_stack) - cnt:]
                    elif kind == "cls":
                        class_stack.pop()
                last = m.end()
                continue
            if first_brace and record_code is not None and "{" in record_code:
                seg = record_code[:record_code.find("{")]
            else:
                seg = code[last:m.start()]
            first_brace = False
            m2 = OPEN_HEAD.search(seg)
            if m2:
                kind, name = m2.group(1), m2.group(2)
                if kind == "namespace":
                    parts = name.split("::")
                    brace_stack.append(("ns", len(parts)))
                    ns_stack.extend(parts)
                else:
                    brace_stack.append(("cls", 1))
                    class_stack.append(name)
            else:
                brace_stack.append(("other", 1))
            last = m.end()

    return path.read_text(encoding="utf-8", errors="surrogateescape"), [ln.rstrip("\r\n") for ln in raw], records


# --------------------------------------------------------------------------
# entity extraction from a statement
# --------------------------------------------------------------------------
def func_name(record: Record) -> str | None:
    """Name of the declared function, or None if this is no declaration."""
    if record.in_class:
        return None
    code = re.sub(r"\[\[.*?\]\]", " ", record.code, flags=re.S)
    code = code.strip()
    if not code.endswith(";"):
        return None                              # definition, not declaration
    m = re.search(r"\b([A-Za-z_]\w*)\s*\(", code)
    if not m:
        return None
    name = m.group(1)
    if name in KEYWORDS:
        return None
    before = code[: m.start()]
    if before.rstrip().endswith((".", "->", "::")):
        return None
    if re.search(r"\(\s*\*", code):              # function pointer variable
        return None
    return name


def class_head(record: Record) -> tuple[str, str] | None:
    """(keyword, class name) when the statement defines a class/struct."""
    m = CLASS_HEAD.match(record.code)
    if not m:
        return None
    return m.group(1), m.group(2)


def is_definition(record: Record, name: str) -> bool:
    """True when the class statement has a body (not a forward declaration)."""
    m = CLASS_HEAD.search(record.code)
    if not m:
        return False
    tail = record.code[m.end():]
    return "{" in tail


# --------------------------------------------------------------------------
def main() -> int:
    apply = "--apply" in sys.argv

    # ---- 1. scan every engine header ------------------------------------
    files: dict[Path, tuple[str, list[str], list[Record]]] = {}
    for ext in ("*.h", "*.hpp"):
        for p in sorted(SCAN_ROOT.rglob(ext)):
            sub = p.relative_to(SCAN_ROOT)
            if SKIP_DIRS & set(sub.parts):
                continue
            files[p] = scan_file(p)

    class_defs: dict[str, list[tuple[Path, Record]]] = {}
    decls: dict[str, list[tuple[Path, Record]]] = {}   # function name -> records
    for path, (_t, _l, recs) in files.items():
        for r in recs:
            ch = class_head(r)
            if ch and is_definition(r, ch[1]):
                class_defs.setdefault(ch[1], []).append((path, r))
            fn = func_name(r)
            if fn:
                decls.setdefault(fn, []).append((path, r))
    print(f"scanned {len(files)} headers: "
          f"{len(class_defs)} class definitions, {len(decls)} free-function declarations")

    # ---- 2. classify the surface rows -----------------------------------
    rows = list(csv.DictReader(CSV_PATH.open(newline="", encoding="utf-8-sig")))
    want_classes: dict[str, str] = {}   # class FQN -> scope namespace
    want_funcs: list[tuple[str, str]] = []   # (namespace, name)
    skipped_ksnet: list[str] = []        # supplied by the ksnet static lib

    for row in rows:
        scope, name = row["Scope"], row["Name"]
        parts = scope.split("::")
        last = parts[-1]
        # ksnet::Server / ksnet::Client are deliberately NOT exported: ksnet is
        # its own static library (src/engine/network/ksnet) that every consumer
        # of those classes links directly, and ksnet.h must keep working in
        # ksnet's standalone build, where KsExport.h is not on the include
        # path. ksengine also compiles ksnet.cpp (the source glob covers
        # network/), but inside ksengine.dll that copy is dead code.
        if scope == "ksnet" or scope.startswith("ksnet::"):
            skipped_ksnet.append(f"{scope}::{name}" if name else scope)
            continue
        if row["Kind"] == "special":
            # special rows describe ctor/dtor/vtable: Scope is the *enclosing*
            # scope, Name is the class itself.
            fqn = f"{scope}::{name}" if name else scope
            want_classes[fqn] = scope
            continue
        # class scope? a class definition with that name in that namespace
        ns_of_last = "::".join(parts[:-1]) or "global"
        hit = any(class_defs.get(last) and r.ns == ns_of_last and not r.in_class
                  for _p, r in class_defs.get(last, []))
        if hit:
            want_classes[scope] = ns_of_last
        else:
            want_funcs.append((scope, name))

    print(f"surface: {len(want_classes)} classes, {len(want_funcs)} free functions")
    if skipped_ksnet:
        print(f"skipped {len(skipped_ksnet)} ksnet entities (resolved from the "
              f"ksnet static lib, not from ksengine.dll): "
              + ", ".join(sorted(skipped_ksnet)))

    # ---- 3. resolve and annotate ----------------------------------------
    plan: dict[Path, dict] = {}   # path -> {classes: [...], funcs: [...], line edits}
    problems: list[str] = []
    warnings: list[str] = []

    def slot(path: Path) -> dict:
        return plan.setdefault(path, {"class_edits": [], "func_edits": [],
                                      "classes": [], "funcs": []})

    for fqn, ns in want_classes.items():
        name = fqn.split("::")[-1]
        cands = [(p, r) for p, r in class_defs.get(name, [])
                 if r.ns == ns and not r.in_class]
        if not cands:
            cands = [(p, r) for p, r in class_defs.get(name, []) if not r.in_class]
            if len(cands) == 1:
                warnings.append(f"{fqn}: found in namespace '{cands[0][1].ns}' "
                                f"(expected '{ns}') -> {rel(cands[0][0])}")
            elif not cands:
                problems.append(f"CLASS NOT FOUND: {fqn}")
                continue
            else:
                problems.append(f"CLASS AMBIGUOUS: {fqn} in "
                                + ", ".join(rel(p) for p, _ in cands))
                continue
        if len(cands) > 1:
            problems.append(f"CLASS DEFINED TWICE: {fqn} in "
                            + ", ".join(f"{rel(p)}:{r.start + 1}" for p, r in cands))
            continue
        path, rec = cands[0]
        slot(path)["class_edits"].append((rec, fqn))
        slot(path)["classes"].append(fqn)

    for ns, name in want_funcs:
        cands = [(p, r) for p, r in decls.get(name, []) if r.ns == ns]
        if not cands:
            all_c = decls.get(name, [])
            if len(all_c) == 1:
                warnings.append(f"{ns}::{name}: found in namespace "
                                f"'{all_c[0][1].ns}' (expected '{ns}') "
                                f"-> {rel(all_c[0][0])}")
                cands = all_c
            elif not all_c:
                problems.append(f"FUNCTION NOT FOUND: {ns}::{name}")
                continue
            else:
                problems.append(f"FUNCTION AMBIGUOUS: {ns}::{name} in "
                                + ", ".join(f"{rel(p)}:{r.start + 1}" for p, r in all_c))
                continue
        for path, rec in cands:
            slot(path)["func_edits"].append((rec, name))
            slot(path)["funcs"].append(f"{ns}::{name}")

    # ---- 4. apply --------------------------------------------------------
    if "--verbose" in sys.argv:
        print("\nresolved targets:")
        for path, plan_ in sorted(plan.items()):
            for rec, name in plan_["class_edits"]:
                print(f"  class {name:<24} {rel(path)}:{rec.start + 1}")
            for rec, name in plan_["func_edits"]:
                print(f"  func  {name:<24} {rel(path)}:{rec.start + 1}")
        print()

    changed = 0
    for path, plan_ in sorted(plan.items()):
        text, lines, _recs = files[path]
        edits = []
        for rec, fqn in plan_["class_edits"]:
            name = fqn.split("::")[-1]
            line = lines[rec.start]
            if MACRO in line:
                continue
            new, n = re.subn(
                r"\b(class|struct)(\s+)" + re.escape(name) + r"\b",
                r"\1" + r"\2" + MACRO + r"\2" + name,
                line, count=1)
            if n != 1:
                problems.append(f"{rel(path)}:{rec.start + 1}: cannot insert macro "
                                f"into class {name}: {line.strip()}")
                continue
            edits.append((rec.start, new))
        for rec, name in plan_["func_edits"]:
            line = lines[rec.start]
            if MACRO in line:
                continue
            m = re.match(r"^(\s*)((?:\[\[.*?\]\]\s*)*)", line)
            indent, attrs = m.group(1), m.group(2)
            rest = line[len(indent) + len(attrs):]
            edits.append((rec.start, f"{indent}{attrs}{MACRO} {rest}"))
        for idx, new in edits:
            lines[idx] = new

        # make sure the include is present
        out = "\n".join(lines)
        if not text.endswith("\n"):
            out += ""
        if INCLUDE_LINE not in out:
            eol = "\r\n" if "\r\n" in text else "\n"
            body = out.splitlines()
            insert_at = 0
            for i, ln in enumerate(body):
                if ln.strip() == "#pragma once":
                    insert_at = i + 1
                    break
            body.insert(insert_at, INCLUDE_LINE)
            out = eol.join(body) + eol
        else:
            eol = "\r\n" if "\r\n" in text else "\n"
            out = eol.join(out.splitlines()) + (eol if text.endswith("\n") else "")

        if out != text:
            changed += 1
            print(f"{'APPLIED' if apply else 'would change'}: {rel(path)}"
                  f"  [{len(plan_['classes'])} classes, {len(plan_['funcs'])} functions]")
            if apply:
                path.write_text(out, encoding="utf-8", errors="surrogateescape")

    print()
    for w in warnings:
        print("WARNING:", w)
    if problems:
        print("\nPROBLEMS:")
        for p in problems:
            print("  -", p)
    print(f"\n{changed} headers {'updated' if apply else 'would change'}, "
          f"{len(problems)} problems, {len(warnings)} warnings")
    return 1 if problems else 0


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace("\\", "/")


if __name__ == "__main__":
    sys.exit(main())
