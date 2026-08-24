#!/usr/bin/env bash
# 主机端单元测试（纯逻辑模块，不依赖 ESP-IDF）：
#   - sedentary_timer  久坐计时状态机
#   - display_layout   显示布局 + 后端文字渲染
#
# 用法：./tools/test-host.sh
# 产物：build-host/（已被 .gitignore 覆盖）。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUT_DIR="$PROJECT_DIR/build-host"

mkdir -p "$OUT_DIR"

CXXFLAGS=(-std=c++17 -Wall -Wextra -I"$PROJECT_DIR/main")

echo "==> c++ build test_sedentary_timer"
c++ "${CXXFLAGS[@]}" \
  "$PROJECT_DIR/test/test_sedentary_timer.cpp" \
  "$PROJECT_DIR/main/sedentary_timer.cpp" \
  -o "$OUT_DIR/test_sedentary_timer"

echo "==> c++ build test_display_layout"
c++ "${CXXFLAGS[@]}" \
  "$PROJECT_DIR/test/test_display_layout.cpp" \
  "$PROJECT_DIR/main/display_layout.cpp" \
  "$PROJECT_DIR/main/display_backend.cpp" \
  "$PROJECT_DIR/main/sedentary_timer.cpp" \
  -o "$OUT_DIR/test_display_layout"

echo "==> run test_sedentary_timer"
"$OUT_DIR/test_sedentary_timer"

echo "==> run test_display_layout"
"$OUT_DIR/test_display_layout"
