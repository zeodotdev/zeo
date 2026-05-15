#!/bin/bash

set -euo pipefail

# Print out details of the environment

# If you have installed some of the dependencies outside of Homebrew or in a weird way,
# this script might not work right :)

BOTH_BREWS=0

if [ $# -gt 0 ]; then
  if [ "$1" == "--both" ]; then
    BOTH_BREWS=1
  else
    BOTH_BREWS=0
  fi
fi

echo "PATH: ${PATH}"
echo "MacOS version: $(sw_vers -productVersion | cut -d. -f1-2)"
echo "Host architecture (cpu.brand_string): $(sysctl -n machdep.cpu.brand_string)"
echo "'arch': $(arch)"
echo "which brew: $(which brew)"
echo "which python3: $(which python3)"
echo "python3 --version: $(python3 --version)"
echo ""
echo "Dependencies:"
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/../src/brew_deps.sh"

ISSUES=""
for dep in "${BREW_DEPS[@]}"; do
  echo ""
  if [ "$BOTH_BREWS" -eq 1 ]; then

    set +e
    arm64_version=$(/opt/homebrew/bin/brew list --version "$dep") # does this die if error?
    x86_64_version=$(arch -x86_64 /usr/local/bin/brew list --version "$dep")
    set -e


    if [ -z "$arm64_version" ]; then
      ISSUES="${ISSUES}arm64 version of $dep not installed\n"
    fi

    if [ -z "$x86_64_version" ]; then
      ISSUES="${ISSUES}x86_64 version of $dep not installed\n"
    fi

    if [ "$arm64_version" != "$x86_64_version" ]; then
      ISSUES="${ISSUES}Version mismatch for $dep between arm64 and x86_64\n"
    fi

    echo "arm64: $arm64_version"
    echo "x86_64: $x86_64_version"
  else
    set +e
    version=$(brew list --version "$dep")
    set -e
    echo "$version"
    if [ -z "$version" ]; then
      ISSUES="${ISSUES}Homebrew at $(which brew) says $dep not installed\n"
    fi
  fi
done

echo ""

if [ -n "$ISSUES" ]; then
  echo "Dependency issues detected:"
  echo -e "$ISSUES"
  echo "Exiting."
  exit 1
else
  echo "Done."
  exit 0
fi