#!/usr/bin/env bash
# 用桩头文件 (test_host/stubs) 在电脑上对全部固件代码做 C++ 语法与类型检查。
# 这不是 ESP32 编译: 桩头文件只声明了本工程用到的 Arduino/库 API。真正的编译请用 `pio run`。
# 用法: test_host/compile_check.sh [gnu++11|gnu++17]
set -u
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
STD=${1:-gnu++17}
INC="-Itest_host/stubs -Iinclude"
for d in lib/*/; do INC="$INC -I${d%/}"; done
FLAGS="-std=$STD -fsyntax-only -fno-rtti -Wall -Wextra -Wno-unused-parameter -Wformat=2 -Werror"
fail=0
mkdir -p .host_build
check() {  # $1 = label, rest = args
  local label=$1; shift
  if out=$($CXX $FLAGS $INC "$@" 2>&1); then
    printf '  OK    %s\n' "$label"
  else
    printf '  FAIL  %s\n%s\n' "$label" "$out"; fail=1
  fi
}
echo "[compile_check] std=$STD"
for f in lib/*/*.cpp; do check "$f" "$f"; done
check "lib/PC_Voice/Voice.cpp (NO_TF=1)" -DNO_TF=1 lib/PC_Voice/Voice.cpp
for f in src/tests/*.cpp; do check "$f" "$f"; done
check "src/app/main.cpp (env app)" src/app/main.cpp
check "src/app/main.cpp (env app_noTF)" -DNO_TF=1 src/app/main.cpp
for h in lib/PC_Button/Button.h lib/PC_Console/Console.h lib/PC_Util/CalMath.h lib/PC_Util/Tokens.h lib/PC_Util/LogStats.h include/sysutil.h include/root_ca.h; do
  printf '#include "%s"\n' "$PWD/$h" > .host_build/hdr_check.cpp
  check "$h (header alone)" .host_build/hdr_check.cpp
done
if [ $fail -eq 0 ]; then echo "[compile_check] all files passed"; else echo "[compile_check] FAILED"; fi
exit $fail
