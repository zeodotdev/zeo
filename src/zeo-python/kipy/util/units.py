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

def from_mm(value_mm: float) -> int:
    """
    Convert millimeters to KiCad API units (nanometers).

    1mm = 1,000,000 nanometers.

    Uses round() instead of int() to avoid truncation errors that cause
    floating-point precision issues (e.g., 65.2399999 → 65240000 instead
    of 65239999).

    :param value_mm: a quantity in millimeters
    :return: the quantity in KiCad API units (nanometers)
    """
    return round(value_mm * 1_000_000)


def to_mm(value_nm: int) -> float:
    """
    Converts a KiCad API length/distance value (nanometers) to millimeters.
    """
    return float(value_nm) / 1_000_000
