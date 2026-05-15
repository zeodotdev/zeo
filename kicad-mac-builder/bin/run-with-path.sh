#!/bin/bash

set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 COMMAND PATH" >&2
    exit 1
fi

# Set path to the second parameter given
PATH=$2

# Attempt to execute the command
$1
