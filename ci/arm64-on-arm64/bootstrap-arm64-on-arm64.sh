#!/bin/bash

# Bootstrap an Apple Silicon build environment on an Apple Silicon system

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/../src/brew_deps.sh"

ARCH=$(arch)
MACHINE=$(machine)

if [ "$MACHINE" != "arm64e" ] && [ "$ARCH" != "arm64" ]; then
  echo "unexpected machine or arch"
  exit 1
fi

if [ ! -e /opt/homebrew/bin/brew ]; then
  echo "Installing native Homebrew..."
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)" # by not redirecting /dev/null into stdin here, it means it's easier to use when running by hand.
fi

echo "Installing some dependencies"
/opt/homebrew/bin/brew install  "${BREW_DEPS[@]}"

echo "Done!"
