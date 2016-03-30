#!/bin/bash

GIT_ROOT="`git rev-parse --git-dir`/.."

echo "Reformat C++ files."
find "${GIT_ROOT}/src" \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -n 1 -P 0 -0 clang-format -style=file -i

