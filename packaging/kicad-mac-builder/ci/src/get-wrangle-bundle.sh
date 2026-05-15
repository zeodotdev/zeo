#!/bin/bash

set -e
set -x


echo "Creating python virtual environment..."
python3 -m venv virtualenv
echo "Entering it..."
source virtualenv/bin/activate
echo "Installing dyldstyle..."
python3 -m pip install git+https://gitlab.com/adamwwolf/dyldstyle.git
echo "dyldstyle has been installed.  wrangle-bundle should be available at $(pwd)/virtualenv/bin/wrangle-bundle."
