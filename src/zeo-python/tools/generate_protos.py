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

import argparse
import fnmatch
import os
import platform
import re
import shutil
import subprocess

_default_protoc = "protoc.exe" if platform.system() == "Windows" else "protoc"
_default_protol = "protol.exe" if platform.system() == "Windows" else "protol"

def generate_protos(input_path: str, output_path: str, protoc: str = _default_protoc,
                    protol: str = _default_protol):
    try:
        os.mkdir(output_path)
    except FileExistsError:
        pass

    proto_sources = []

    for root, _, files in os.walk(input_path):
        for item in fnmatch.filter(files, "*.proto"):
            proto_sources.append(os.path.join(input_path, str(root), item))

    # Build protoc command — only add --mypy_out if the plugin is available
    protoc_cmd = [protoc,
           "--python_out=" + output_path,
           "--proto_path=" + input_path,
           *proto_sources]

    if shutil.which("protoc-gen-mypy"):
        protoc_cmd.insert(2, "--mypy_out=" + output_path)

    print("Generating Python classes from protobuf files...")
    result = subprocess.run(protoc_cmd)

    if result.returncode != 0:
        print(f"Warning: protoc failed with exit code {result.returncode}")
        return

    # Post-process with protoletariat if available
    if shutil.which(protol):
        print("Post-processing with protoletariat...")
        subprocess.run([protol,
               "--dont-create-package",
               "--in-place",
               "--exclude-google-imports",
               "--python-out", output_path,
               "protoc",
               "--proto-path", input_path,
               *proto_sources])
    else:
        print("protoletariat not found, fixing imports inline...")
        _fix_imports(input_path, output_path)


def _fix_imports(input_path: str, output_path: str):
    """Rewrite protoc-generated imports so they work within the Python package.

    protoc generates absolute imports based on the proto package path
    (e.g., ``from common.types import enums_pb2``). These need the Python
    package prefix prepended (e.g., ``from kipy.proto.common.types import ...``).
    This is what protoletariat does; this function is a minimal fallback.
    """
    # Derive the Python package prefix from the output directory.
    # e.g., output_path ending in "kipy/proto" -> prefix "kipy.proto"
    pkg_prefix = os.path.relpath(output_path).replace(os.sep, ".")

    # Find top-level proto package directories (e.g., common, board, schematic)
    proto_packages = sorted(
        entry for entry in os.listdir(input_path)
        if os.path.isdir(os.path.join(input_path, entry))
    )

    if not proto_packages:
        return

    # Build a regex that matches "from <proto_pkg>" at the start of an import
    pkg_pattern = "|".join(re.escape(p) for p in proto_packages)
    import_re = re.compile(r"^(from )(" + pkg_pattern + r")([\s.])", re.MULTILINE)

    for root, _, files in os.walk(output_path):
        for filename in fnmatch.filter(files, "*_pb2.py") + fnmatch.filter(files, "*_pb2.pyi"):
            filepath = os.path.join(root, filename)

            with open(filepath, "r") as fh:
                content = fh.read()

            fixed = import_re.sub(r"\g<1>" + pkg_prefix + r".\2\3", content)

            if fixed != content:
                with open(filepath, "w") as fh:
                    fh.write(fixed)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()

    parser.add_argument("--input", default="kicad/api/proto")
    parser.add_argument("--output", default="kipy/proto")
    parser.add_argument("--protoc", help="Path to protoc", default=_default_protoc)
    parser.add_argument("--protol", help="Path to protoletariat", default=_default_protol)

    args = parser.parse_args()

    output_path = os.path.abspath(args.output)
    input_path = os.path.abspath(args.input)

    generate_protos(input_path, output_path, args.protoc, args.protol)
