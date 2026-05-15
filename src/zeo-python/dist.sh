#!/bin/sh
# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Prepares the distribution packages

set -e

# Check if we're in the root directory of the project
if [ ! -f "pyproject.toml" ]; then
    echo "Error: This script must be run from the root directory of the project."
    exit 1
fi

git submodule update --init --recursive

poetry build

echo "Check that the version below looks correct:"
echo "------"
cat kipy/kicad_api_version.py
echo "------"

# Poetry does not have a way to disable platform-specific wheels directly,
# so we need to manually remove them after building.
for wheel in dist/*.whl; do
    if ! echo "$wheel" | grep -q "any.whl$" && echo "$wheel" | grep -q "\-[^-]*\-[^-]*\.whl$"; then
        echo "Removing platform-specific wheel: $(basename "$wheel")"
        rm "$wheel"
    else
        echo "Keeping wheel: $(basename "$wheel")"
    fi
done

echo "Ready to run 'poetry publish' to upload the package."
