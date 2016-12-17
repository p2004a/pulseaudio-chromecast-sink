#!/usr/bin/env bash

GIT_ROOT="`git rev-parse --git-dir`/.."

find "${GIT_ROOT}/src" \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name -'*.c' \) -print0 | xargs -n 1 -P 0 -0 clang-format -style=file -i
