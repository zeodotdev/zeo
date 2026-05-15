#!/bin/bash

shopt -s nullglob # needed to error when we have a glob expansion
set -euo pipefail

if [ ! -e "$1" ]; then
    echo "Cannot test $1. Make sure it exists."
    exit 1
fi

echo -n "Checking that python3 doesn't pull in a system Python... "
if DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 "$1" -B -c 'print("Hello world.")' 2>&1 | grep /System/Library/Frameworks/Python.framework ; then
    echo "Error: $1 appears to call the System Python framework.  DYLD_PRINT_LIBRARIES=1 \"$1\" may help you debug the issue."
    exit 1
fi
echo "OK"

echo -n "Checking that python3 executes Python 3 code... "
if ! "$1" -B -c 'print("Hello" + " " + "World")' 2>&1 | grep 'Hello World' > /dev/null; then
    echo "Error: $1 did not appear to actually execute Python code."
    exit 1
fi
echo "OK"
