#!/usr/bin/env python3
"""
Architecture guard for IndusScope — single PreToolUse entry point.

Reads a JSON hook payload from stdin, extracts file_path and content,
and runs three sequential checks.  The first failing check exits 2
immediately with a clear guard label and reason — later checks do not run.

Guards (in order):
  1. Qt-in-core   — core/ must not #include Qt headers
  2. New/delete    — core/ and protocol/ must not use raw new/delete
  3. push_back     — core/ must not use vector::push_back (sampling hot path)

Exit codes:
  0 — all checks passed (or file not in scope)
  2 — guard violation (must block the tool call)

Usage (manual test):
  echo '{"tool_input":{"file_path":"core/src/foo.cpp","content":"#include <QObject>"}}' | python .claude/hooks/guard_main.py
"""

import json
import os
import re
import sys


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def is_under(path: str, dirs: tuple[str, ...]) -> bool:
    """True if *path* lives under one of the given top-level directories.

    Handles both absolute paths (C:\\...\\core\\src\\foo.cpp) and relative
    paths (core/src/foo.cpp) by checking every path component pair: if the
    directory name appears and is followed by another component (meaning the
    file is INSIDE, not just AT the directory), return True.
    """
    norm = path.replace("\\", "/")
    parts = [p for p in norm.split("/") if p]  # strip empty segments
    for d in dirs:
        d_clean = d.rstrip("/")
        for i, part in enumerate(parts):
            if part == d_clean and i < len(parts) - 1:
                return True
    return False


def in_block_comment(line: str, in_block: bool) -> tuple[bool, bool]:
    """
    Ultra-simple block-comment tracker.

    Returns (line_contains_block_comment_part, new_in_block).

    We don't need a perfect parser — we only use this to skip /* */ regions
    for the conservative new/delete check.

    NOTE: this does NOT handle the / and * characters ending up on the same
    line as code — it only tracks the *state* across lines.  That's enough
    for "宁可漏报不可误杀": if a new appears on a line that has both /* and
    */ we just process naive line-level checks below.
    """
    # If we enter a block comment this line
    starts = "/*" in line
    ends = "*/" in line

    if in_block:
        if ends:
            return True, False  # comment ends this line
        return True, True       # still inside

    if starts and ends:
        # Block comment opens and closes on same line — treat as commented
        if line.find("/*") < line.find("*/"):
            return True, False
        return False, False
    if starts:
        return True, True
    return False, False


# ---------------------------------------------------------------------------
# Guard 1 — Qt in core
# ---------------------------------------------------------------------------

def guard_qt_in_core(file_path: str, content: str) -> str | None:
    """
    Returns an error string if core/ tries to include a Qt header.

    Only checks files under core/ — ui/ is EXPECTED to use Qt.
    """
    if not is_under(file_path, ("core",)):
        return None

    for lineno, line in enumerate(content.splitlines(), 1):
        # Ignore comment lines (//)
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        # Check for #include <Q...> or #include <qt...> (case-insensitive on qt)
        if re.search(r'#include\s*<Q\w+', line):
            return (
                f"[Qt-in-core 守卫] {file_path}:{lineno} 禁止 include Qt 头文件: "
                f"{line.strip()}\n"
                f"  SPEC §4 铁律: core/ 层零 Qt 依赖,必须能脱离 Qt 独立单测。\n"
                f"  如需 Qt 类型,在 ui/ 层封装后通过信号/slot 与 core 交互。"
            )
        if re.search(r'#include\s*<[Qq][Tt]', line):
            return (
                f"[Qt-in-core 守卫] {file_path}:{lineno} 禁止 include Qt 头文件: "
                f"{line.strip()}\n"
                f"  SPEC §4 铁律: core/ 层零 Qt 依赖,必须能脱离 Qt 独立单测。"
            )

    return None


# ---------------------------------------------------------------------------
# Guard 2 — Raw new / delete
# ---------------------------------------------------------------------------

# Patterns that indicate a raw new or delete we should flag.
# We look for these AFTER stripping comments and whitelisted forms.
_RAW_NEW_RE = re.compile(r'\bnew\s+(?!\()')    # "new " but NOT "new (" (placement)
_RAW_DELETE_RE = re.compile(r'\bdelete\b')
_RAW_DELETE_ARRAY_RE = re.compile(r'\bdelete\[\]')

# Whitelist patterns — if a line matches these, skip it entirely.
_WHITELIST_NEW_DELETE = [
    re.compile(r'\boperator\s+new\b'),      # operator new declaration
    re.compile(r'\boperator\s+delete\b'),   # operator delete declaration
    re.compile(r'=\s*delete\b'),            # deleted function (= delete) is NOT manual deallocation / 删除函数,非手动释放
]


def guard_raw_newdelete(file_path: str, content: str) -> str | None:
    """
    Returns an error string if core/ or protocol/ uses raw new/delete.

    Conservative ("宁可漏报不可误杀"):
      - Skips lines starting with // (comment)
      - Skips lines inside /* */ blocks (best-effort)
      - Skips operator new / operator delete declarations
      - Skips placement new (new (ptr) Type)
      - Does NOT try to detect new/delete inside string literals
    """
    if not is_under(file_path, ("core", "protocol")):
        return None

    in_block = False

    for lineno, line in enumerate(content.splitlines(), 1):
        stripped = line.lstrip()

        # Track /* */ state
        line_in_block, in_block = in_block_comment(line, in_block)
        if line_in_block:
            continue  # entire line considered commented for our purposes

        # Skip // comment lines
        if stripped.startswith("//"):
            continue

        # Skip whitelisted patterns (operator new/delete)
        if any(p.search(line) for p in _WHITELIST_NEW_DELETE):
            continue

        # Check raw new (excluding placement new)
        m_new = _RAW_NEW_RE.search(line)
        if m_new:
            return (
                f"[裸 new/delete 守卫] {file_path}:{lineno} 禁止裸 new: "
                f"{line.strip()}\n"
                f"  CLAUDE.md 代码规范: 一律 RAII + 智能指针,出现裸 new/delete 视为 bug。\n"
                f"  改用 std::make_unique / std::make_shared / 对象池复用。"
            )

        # Check raw delete / delete[]
        m_del = _RAW_DELETE_RE.search(line)
        if m_del:
            return (
                f"[裸 new/delete 守卫] {file_path}:{lineno} 禁止裸 delete: "
                f"{line.strip()}\n"
                f"  CLAUDE.md 代码规范: 一律 RAII + 智能指针。\n"
                f"  改用智能指针自动释放,或对象池管理生命周期。"
            )

    return None


# ---------------------------------------------------------------------------
# Guard 3 — vector::push_back in core
# ---------------------------------------------------------------------------

def guard_pushback_in_core(file_path: str, content: str) -> str | None:
    """
    Returns an error string if core/ uses vector::push_back.

    This is strict: ANY .push_back( in core/ triggers it.
    Known legitimate uses (one-time init, test setup) will be whitelisted
    later once we have real sampling paths.  For now, block everything.
    """
    if not is_under(file_path, ("core",)):
        return None

    for lineno, line in enumerate(content.splitlines(), 1):
        stripped = line.lstrip()
        # Skip comment lines
        if stripped.startswith("//"):
            continue

        if ".push_back(" in line:
            return (
                f"[push_back 守卫] {file_path}:{lineno} 禁止 vector::push_back: "
                f"{line.strip()}\n"
                f"  SPEC §5.2: 1kHz+ 采样下 push_back 反复重分配造成抖动 → 禁用。\n"
                f"  改用固定容量环形缓冲 / SPSC 无锁队列。\n"
                f"  ⚠ 如果此处确实是一次性初始化(非采样热路径),"
                f" 请暂时用注释 // push_back-guard-allow 标记,后续 slice 会细化为白名单。"
            )

    return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError as e:
        print(f"[guard_main] 无法解析 stdin JSON: {e}", file=sys.stderr)
        sys.exit(1)  # exit 1 = hook script error (not a guard violation)

    tool_input = payload.get("tool_input", {})
    file_path = tool_input.get("file_path", "")
    content = tool_input.get("content", "")

    if not file_path:
        # No file to check — pass
        sys.exit(0)

    if not content:
        # Empty content, nothing to check — pass
        sys.exit(0)

    # Run guards sequentially — first failure wins
    guards = [
        guard_qt_in_core,
        guard_raw_newdelete,
        guard_pushback_in_core,
    ]

    for guard_fn in guards:
        error = guard_fn(file_path, content)
        if error is not None:
            print(error, file=sys.stderr)
            sys.exit(2)  # exit 2 = BLOCK the tool call

    sys.exit(0)


if __name__ == "__main__":
    main()
