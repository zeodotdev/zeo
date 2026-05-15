#!/bin/bash
set -x
set -e

# Easy hack to get a timeout command
function timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "${SCRIPT_DIR}/../src/brew_deps.sh"

for _ in 1 2 3; do
  if ! command -v brew >/dev/null; then
    echo "Installing Homebrew ..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)" < /dev/null
  else
    echo "Homebrew installed."
    break
  fi
done

PATH=$PATH:/usr/local/bin
export HOMEBREW_NO_ANALYTICS=1

echo "Updating SSH"
brew install openssh

echo "Installing some dependencies"
brew install "${BREW_DEPS[@]}"
echo "Done."