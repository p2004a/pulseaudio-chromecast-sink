#!/usr/bin/env python3

# Copyright (C) 2016  Marek Rusinowski
# MIT License
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import subprocess
import sys


if sys.version_info[0] != 3 or sys.version_info[1] < 4:
    print("This script requires Python >= 3.4")
    sys.exit(2)


def call(command, input=None):
    return subprocess.check_output(
            command, shell=True, universal_newlines=True, input=input)


def get_staged_files():
    for line in call('git diff --cached --name-only --diff-filter=ACMRT').split('\n'):
        line = line.strip()
        if line != '':
            yield line


def filter_interesting_files(iterable):
    for file in iterable:
        if any([file.endswith(suff) for suff in ['.cpp', '.c', '.hpp', '.h']]):
            yield file


def main():
    for file in filter_interesting_files(get_staged_files()):
        file_content = call('git show :"{}"'.format(file))
        replacements = call('clang-format -style=file -output-replacements-xml -', file_content)
        if '<replacement ' in replacements:
            print("Didn't commit. Changes were not properly formatted. Run format-all.sh.")
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
