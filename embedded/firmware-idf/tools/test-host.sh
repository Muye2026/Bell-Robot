#!/usr/bin/env bash
# 主机端单元测试：sedentary_timer 状态机（纯逻辑，不依赖 ESP-IDF）。
#
# 用法：./tools/test-host.sh
# 产物：build-host/test_sedentary_timer（build-host/ 已被 .gitignore 覆盖）。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUT_DIR="$PROJECT_DIR/build-host"

mkdir -p "$OUT_DIR"

echo "==> c++ build host test"
c++ -std=c++17 -Wall -Wextra -I"$PROJECT_DIR/main" \
  "$PROJECT_DIR/test/test_sedentary_timer.cpp" \
  "$PROJECT_DIR/main/sedentary_timer.cpp" \
  -o "$OUT_DIR/test_sedentary_timer"

echo "==> run host test"
"$OUT_DIR/test_sedentary_timer"
