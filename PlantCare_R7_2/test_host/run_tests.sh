#!/usr/bin/env bash
# 在电脑上运行纯逻辑单元测试 (需要 g++ 或 clang++)
set -e
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}
mkdir -p .host_build
$CXX -std=c++17 -O1 -Wall -Wextra -Werror \
  -Ilib/PC_Care -Ilib/PC_Explain -Ilib/PC_Json -Ilib/PC_Util \
  test_host/test_pure.cpp lib/PC_Care/CareState.cpp lib/PC_Explain/Explain.cpp lib/PC_Json/MiniJson.cpp \
  -o .host_build/test_pure
./.host_build/test_pure
