#!/bin/bash

set -x
set -e

# This script helps you set up an M1 Mac to build the x86_64 version of KiCad.
# This is not intended to be the complete answer to "M1 support".

# Notes

# If you are on an Mx Mac, /usr/local/bin/brew is for x86_64 things, and the default and M1-y Homebrew is in /opt/homebrew
# Many folks would only need the M1-y homebrew, but if you building x86_64 KiCad on an M1 Mac you are not most folks.

# One way of handling this multiverse of madness is to have the M1-y Homebrew in your path first.  When you type `brew`, it means the M1 homebrew.
# If you need to use the x86_64 Homebrew, you would run `arch -x86_64 /usr/local/bin/brew`.

# After running this script, you could set up CLion, for instance, with
# arch -x86_64 ./build.py --target setup-kicad-dependencies
# checking out KiCad, opening it in Intel CLion (I am not sure if the Apple Silicon CLion will work), copying the CMake arguments in, and then doing Build > Install in CLion.
# To do a regular package build using kicad-mac-builder, you'll need to install dyldstyle to get wrangle-bundle, which you can do with `ci/src/get-wrangle-bundle.sh`.
# Add it to your PATH, so `wrangle-bundle` works at the CLI.
# Then you can use build.py like:
# `arch -x86_64 ./build.py --target kicad`

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/../src/brew_deps.sh"

echo "Checking for Rosetta 2..."
if pgrep -q oahd; then
  echo "Rosetta 2 is installed."
else
  echo "Rosetta 2 is not installed."
  echo "You'll need to install it.  One way is with: "
  echo "/usr/sbin/softwareupdate --install-rosetta"
  exit 1
fi

if [ ! -e /usr/local/bin/brew ]; then
  echo "Installing x86_64 Homebrew..."
  arch -x86_64 /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)" < /dev/null
fi

export HOMEBREW_NO_ANALYTICS=1

echo "Installing some dependencies"

arch -x86_64 /usr/local/bin/brew install "${BREW_DEPS[@]}"
echo "Done!"
