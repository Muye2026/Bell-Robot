#!/usr/bin/env bash
# Bell Robot 固件构建脚本（macOS / Linux）。
#
# 用法（在 firmware-idf 目录或任意位置执行均可）：
#   ./tools/build.sh                     # 构建
#   ./tools/build.sh set-target          # 强制重新指定 esp32s3 目标
#   ./tools/build.sh --flash /dev/cu.usbmodem01   # 构建并烧录
#   ./tools/build.sh --monitor /dev/cu.usbmodem01 # 构建并打开串口监视
#
# ESP-IDF 路径：优先 IDF_PATH 环境变量，否则默认 ~/esp/esp-idf-v6.0.2。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

if [ -z "${IDF_PATH:-}" ]; then
  IDF_PATH="$HOME/esp/esp-idf-v6.0.2"
fi
if [ ! -f "$IDF_PATH/export.sh" ]; then
  echo "ESP-IDF 未找到: $IDF_PATH" >&2
  echo "请先安装 ESP-IDF v6.0，或用 IDF_PATH 环境变量指定路径。" >&2
  exit 1
fi

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null

cd "$PROJECT_DIR"

BUILD_DIR="${BUILD_DIR:-build}"
FORCE_SET_TARGET=false
FLASH_PORT=""
MONITOR_PORT=""

while [ $# -gt 0 ]; do
  case "$1" in
    set-target)
      FORCE_SET_TARGET=true
      ;;
    --flash)
      FLASH_PORT="${2:-}"
      shift
      ;;
    --monitor)
      MONITOR_PORT="${2:-}"
      shift
      ;;
    *)
      echo "未知参数: $1" >&2
      exit 1
      ;;
  esac
  shift
done

if [ "$FORCE_SET_TARGET" = true ] || [ ! -f sdkconfig ]; then
  echo "==> idf.py set-target esp32s3"
  idf.py -B "$BUILD_DIR" set-target esp32s3
fi

echo "==> idf.py build"
idf.py -B "$BUILD_DIR" build

if [ -n "$FLASH_PORT" ]; then
  echo "==> idf.py flash ($FLASH_PORT)"
  idf.py -B "$BUILD_DIR" -p "$FLASH_PORT" flash
fi

if [ -n "$MONITOR_PORT" ]; then
  echo "==> idf.py monitor ($MONITOR_PORT)"
  idf.py -B "$BUILD_DIR" -p "$MONITOR_PORT" monitor
fi
