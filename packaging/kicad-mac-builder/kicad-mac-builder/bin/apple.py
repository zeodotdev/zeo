#!/usr/bin/env python3

import argparse
import logging
import os
import subprocess
import time
import sys

# if we want to run on Apple Silicon, we need to be signed--but adhoc is fine
# if we want to notarize, we need hardened runtime on
# if we want to run Python on Apple Silicon, and we are using hardened Runtime, we need a non-adhoc signature

# From Apple: "Important: While the --deep option can be applied to a signing operation, this is not recommended. We
# recommend that you sign code inside out in individual stages (as Xcode does automatically). Signing with --deep is
# for emergency repairs and temporary adjustments only."

# When we used the name of the certificate, instead of the hex ID, we saw frequent segfaults while signing.

logging.basicConfig(level=logging.DEBUG)


def get_kicad_paths_for_signing(dotapp_path):
    to_sign = []

    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/eeschema.app/Contents/MacOS/eeschema"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/eeschema.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/gerbview.app/Contents/MacOS/gerbview"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/gerbview.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/pcbnew.app/Contents/MacOS/pcbnew"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/pcbnew.app"))
    to_sign.append(
        os.path.join(dotapp_path, "Contents/Applications/bitmap2component.app/Contents/MacOS/bitmap2component"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/bitmap2component.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/pcb_calculator.app/Contents/MacOS/pcb_calculator"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/pcb_calculator.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/pl_editor.app/Contents/MacOS/pl_editor"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/pl_editor.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/agent.app/Contents/MacOS/agent"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/agent.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/terminal.app/Contents/MacOS/terminal"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Applications/terminal.app"))

    # pynche tool - include both 3.9 and 3.10
    to_sign.append(os.path.join(dotapp_path,
                                "Contents/Frameworks/Python.framework/Versions/Current/share/doc/python3.9/examples/Tools/pynche"))
    to_sign.append(os.path.join(dotapp_path,
                                "Contents/Frameworks/Python.framework/Versions/Current/share/doc/python3.10/examples/Tools/pynche"))
    to_sign.append(os.path.join(dotapp_path,
                                "Contents/Frameworks/Python.framework/Versions/Current/Resources/Python.app/Contents/MacOS/Python"))

    for root, dirnames, filenames in os.walk(
            os.path.join(dotapp_path, "Contents/Frameworks/Python.framework/Versions/Current")):
        for filename in filenames:
            if filename.endswith(".so") or filename.endswith(".dylib") or filename.endswith(".a") or filename.endswith(".o"):
                to_sign.append(os.path.join(root, filename))

    # Python bin executables - include both 3.9 and 3.10 versions
    for x in ['2to3',
              '2to3-3.9',
              '2to3-3.10',
              'helpviewer',
              'idle3',
              'idle3.9',
              'idle3.10',
              'img2png',
              'img2py',
              'img2xpm',
              'normalizer',
              'pip3',
              'pip3.9',
              'pip3.10',
              'pycrust',
              'pydoc3',
              'pydoc3.9',
              'pydoc3.10',
              'pyshell',
              'pyslices',
              'pyslicesshell',
              'python3',
              'python3-config',
              'python3-intel64',
              'python3.9',
              'python3.9-config',
              'python3.9-intel64',
              'python3.10',
              'python3.10-config',
              'python3.10-intel64',
              'pywxrc',
              'wheel',
              'wxdemo',
              'wxdocs',
              'wxget',
              ]:
        to_sign.append(
            os.path.join(dotapp_path, f"Contents/Frameworks/Python.framework/Versions/Current/bin/{x}"))

    # Python config object files - include both 3.9 and 3.10
    to_sign.append(
        os.path.join(dotapp_path, f"Contents/Frameworks/Python.framework/Versions/Current/lib/python3.9/config-3.9-darwin/python.o"))
    to_sign.append(
        os.path.join(dotapp_path, f"Contents/Frameworks/Python.framework/Versions/Current/lib/python3.10/config-3.10-darwin/python.o"))

    to_sign.append(
        os.path.join(dotapp_path, "Contents/Frameworks/Python.framework/Versions/Current/Resources/Python.app"))
    to_sign.append(os.path.join(dotapp_path, "Contents/Frameworks/Python.framework"))

    for root, dirnames, filenames in os.walk(os.path.join(dotapp_path, "Contents/Frameworks")):
        if "Python.framework" in root:
            continue
        for filename in filenames:
            if filename.endswith(".dylib"):
                to_sign.append(os.path.join(root, filename))

    for root, dirnames, filenames in os.walk(os.path.join(dotapp_path, "Contents/Resources")):
        if "Python.framework" in root:
            continue
        for filename in filenames:
            if filename.endswith(".so"):
                to_sign.append(os.path.join(root, filename))

    for root, dirnames, filenames in os.walk(os.path.join(dotapp_path, "Contents/PlugIns")):
        if "Python.framework" in root:
            continue
        for filename in filenames:
            to_sign.append(os.path.join(root, filename))

    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/dxf2idf"))
    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/idf2vrml"))
    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/idfcyl"))
    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/idfrect"))
    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/kicad-cli"))
    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/crashpad_handler"))
    to_sign.append(os.path.join(dotapp_path, "Contents/MacOS/Zeo"))

    to_sign.append(dotapp_path)

    return to_sign


def sign(dotapp_path, key_label, hardened_runtime, secure_timestamp, entitlements_path=None):
    logging.info("Signing {}".format(dotapp_path))
    start_time = time.monotonic()
    for path in get_kicad_paths_for_signing(dotapp_path):
        sign_file(path, key_label, hardened_runtime, secure_timestamp, entitlements_path)
    elapsed_time = time.monotonic() - start_time
    logging.debug("Signing took {} seconds".format(elapsed_time))


def sign_file(path, key_label, hardened_runtime, secure_timestamp, entitlements_path=None):
    if not os.path.exists(path):
        logging.debug("Skipping signing of {} (does not exist)".format(path))
        return

    cmd = ["codesign", "--sign", key_label, "--force"]
    if hardened_runtime:
        cmd.extend(["--options", "runtime"])
    if entitlements_path:
        cmd.extend(["--entitlements", entitlements_path])
    if secure_timestamp:
        cmd.append("--timestamp")

    cmd.append(path)
    logging.debug("Running {}".format(" ".join(cmd)))
    subprocess.run(cmd, check=True)


def has_secure_timestamp(path):
    logging.info("Checking {} has a secure timestamp".format(path))
    cmd = ["codesign", "-dvv", path]
    logging.debug("Running {}".format(" ".join(cmd)))
    completed = subprocess.run(cmd, capture_output=True, check=True)

    stderr = completed.stderr.decode('utf-8')
    if "Signed Time" in stderr:
        return False
    if "Timestamp=" not in stderr:
        return False
    return True

def verify_signing(dotapp_path, verify_timestamps=True):
    logging.info("Verifying signing of {}".format(dotapp_path))
    logging.debug("Verifying with --strict")
    cmd = ["codesign", "-vvv", "--deep", "--strict", dotapp_path]
    logging.debug("Running {}".format(" ".join(cmd)))
    subprocess.run(cmd, check=True)

    if verify_timestamps:
        check_timestamps = [dotapp_path]
        with os.scandir(os.path.join(dotapp_path, "Contents", "MacOS")) as entries:
            for entry in entries:
                check_timestamps.append(entry.path)
        for path in check_timestamps:
            if not has_secure_timestamp(path):
                raise Exception("{} does not have a secure timestamp".format(path))
            else:
                logging.debug("{} has a secure timestamp".format(path))


def parse_args(arg_list=sys.argv[1:]):
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", help="modify output verbosity",
                        action="store_true")
    parser.add_argument("-q", "--quiet", help="Reduce output",
                        action="store_true")

    subparsers = parser.add_subparsers(dest='subparser_name',
                                       help='sub-command help')
    sign_parser = subparsers.add_parser('sign')
    sign_parser.add_argument('--hardened-runtime',
                             action="store_true",
                             help="Enable Apple's Hardened Runtime. Enforces entitlements. " \
                                  "Required for notarization. " \
                                  "Not compatible with ad-hoc signing (on Apple Silicon?)")
    sign_parser.add_argument("--certificate-id",
                             required=True,
                             help="Signing certificate ID.  It is best if this is the 40 character hex ID from "
                                  "`security find-identity -v`.  Use - for ad-hoc signing.")
    sign_parser.add_argument("--entitlements", help="Optional path to entitlements plist.")
    sign_parser.add_argument("--timestamp",
                             action="store_true",
                             help="Enable Secure Timestamps.")
    sign_parser.add_argument("path", help="Path to the .app")

    args = parser.parse_args(arg_list)

    if args.verbose and args.quiet:
        raise argparse.ArgumentError("--verbose and --quiet cannot be specified at the same time.")

    return args


def handle_signing(dotapp_path, certificate_hex_id, hardened_runtime, secure_timestamp, entitlements_path):
    sign(dotapp_path, certificate_hex_id, hardened_runtime, secure_timestamp, entitlements_path)
    # Ad-hoc signatures don't seem to be able to keep secure timestamps...
    verify_signing(dotapp_path,
                   verify_timestamps=certificate_hex_id != "-")
    print("Done. Signed and verified {}".format(dotapp_path))

def main():
    args = parse_args()
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    if args.quiet:
        logging.getLogger().setLevel(logging.WARNING)
    else:
        logging.getLogger().setLevel(logging.INFO)

    if "path" in args and args.path.endswith(".app/"):
        args.path = args.path[:-1]

    if args.subparser_name == "sign":
        handle_signing(args.path,
                       args.certificate_id,
                       args.hardened_runtime,
                       args.timestamp,
                       args.entitlements)

if __name__ == "__main__":
    main()
