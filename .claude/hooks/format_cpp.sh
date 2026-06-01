#!/usr/bin/env bash
# PostToolUse hook — auto-format .cpp/.h/.hpp files with clang-format.
#
# Reads the file path from $CLAUDE_TOOL_INPUT_FILE_PATH (Claude Code >= 1.0.30)
# or falls back to parsing stdin JSON for tool_input.file_path.
#
# Gracefully skips if clang-format is not installed.
# Does NOT block the tool call — formatting happens after the write.
set -euo pipefail

# ---------------------------------------------------------------------------
# 1. Resolve the file path
# ---------------------------------------------------------------------------
FILE_PATH=""

if [[ -n "${CLAUDE_TOOL_INPUT_FILE_PATH:-}" ]]; then
    FILE_PATH="$CLAUDE_TOOL_INPUT_FILE_PATH"
else
    # Fallback: parse stdin JSON (needs python or jq — we use python since it's already verified)
    FILE_PATH=$(python -c "
import json, sys
try:
    payload = json.load(sys.stdin)
    print(payload.get('tool_input', {}).get('file_path', ''))
except Exception:
    print('')
" 2>/dev/null || true)
fi

if [[ -z "$FILE_PATH" ]]; then
    exit 0
fi

# ---------------------------------------------------------------------------
# 2. Check file extension — only format C++ files
# ---------------------------------------------------------------------------
case "$FILE_PATH" in
    *.cpp|*.h|*.hpp|*.cxx|*.hxx|*.cc|*.hh) ;;
    *) exit 0 ;;
esac

# ---------------------------------------------------------------------------
# 3. Check if clang-format is available
# ---------------------------------------------------------------------------
if ! command -v clang-format &>/dev/null; then
    echo "[format_cpp] clang-format 未安装,跳过格式化 $FILE_PATH" >&2
    echo "[format_cpp] 提示: 安装 LLVM 或运行 'pip install clang-format' 获取 clang-format" >&2
    exit 0
fi

# ---------------------------------------------------------------------------
# 4. Run clang-format
# ---------------------------------------------------------------------------
if [[ -f "$FILE_PATH" ]]; then
    if clang-format -i "$FILE_PATH" 2>/dev/null; then
        echo "[format_cpp] 已格式化: $FILE_PATH" >&2
    else
        echo "[format_cpp] 格式化失败: $FILE_PATH (文件格式可能有误,已跳过)" >&2
        exit 0  # don't block on formatting failure
    fi
else
    echo "[format_cpp] 文件不存在,跳过: $FILE_PATH" >&2
fi

exit 0
