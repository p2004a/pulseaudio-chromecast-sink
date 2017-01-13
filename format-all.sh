#!/bin/bash

cd $(dirname $(realpath "$0"))

num_cores=$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)

find src \
     -regex '.*\.\(cpp\|hpp\|c\|h\)' \
     -print0 | \
     xargs -n 1 -P $num_cores -0 clang-format -style=file -i
