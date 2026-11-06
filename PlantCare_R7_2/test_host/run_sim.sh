#!/usr/bin/env bash
# 主机联调: 在电脑上运行整机主程序 src/app/main.cpp (模拟传感器、Wi-Fi、Gemini 返回), 检查台架流程与 AI 解释
# 加参数 long 再模拟 24 h 连续运行, 检查 logstat:  bash test_host/run_sim.sh long
set -e
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
mkdir -p .host_build
INC="-DHOST_SIM -Itest_host/stubs -Itest_host/sim -Iinclude"
for d in lib/*/; do INC="$INC -I${d%/}"; done
FLAGS="-std=gnu++11 -O1 -g -pthread -Wall -Wextra -Wno-unused-parameter -Werror"
SRCS="test_host/sim/sim_stubs.cpp test_host/sim/sim_app.cpp src/app/main.cpp lib/*/*.cpp"
$CXX $FLAGS $INC $SRCS -o .host_build/sim_app
$CXX $FLAGS $INC -DNO_TF=1 $SRCS -o .host_build/sim_app_noTF
./.host_build/sim_app
SIM_DF_NO_REPLY=1 ./.host_build/sim_app
./.host_build/sim_app_noTF
for m in blank same larger crash; do ./.host_build/sim_app fs $m; done   # 日志分区里的旧文件系统
./.host_build/sim_app hist            # 补水历史: sim 不写入、改标定和 clear 时清掉
if [ "${1:-}" = "long" ]; then ./.host_build/sim_app long; fi   # 24 h 连续运行 (约 1-2 min)
