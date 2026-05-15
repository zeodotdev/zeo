#!/bin/bash

shopt -s nullglob # needed to error when we have a glob expansion
set -euo pipefail

if [ ! -e "$1" ]; then
    echo "Cannot test $1 as it does not appear to exist."
    exit 1
fi

echo -n "Checking that importing wx doesn't pull in a system Python... "
if DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 "$1" -B -c 'import wx ; print("Imported" + "Module" + "Successfully")' 2>&1 | grep /System/Library/Frameworks/Python.framework > /dev/null; then
    echo "Error: $1 appears to call the System Python framework.  DYLD_PRINT_LIBRARIES=1 \"$1\" may help you debug the issue."
    exit 1
fi
echo "OK"

echo -n "Checking that importing wx succeeds... "
if ! DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 "$1" -B -c 'import wx ; print("Imported" + "Module" + "Successfully")' 2>&1 | grep ImportedModuleSuccessfully > /dev/null; then
    echo "Error importing pcbnew. DYLD_PRINT_LIBRARIES=1 \"$1\" may help you debug the issue."
    exit 1
fi
echo "OK"
