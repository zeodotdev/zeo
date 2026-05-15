#!/bin/bash

set -euxo pipefail

python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

dmgbuild -s settings.py -Dapp=../build/kicad-dest/KiCad.app "KiCad" kicad.dmg
