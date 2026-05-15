#!/usr/bin/env python3

# Try not to use any packages that aren't included with Python, please.

import argparse
import errno
import os
import subprocess
import sys

DEFAULT_KICAD_GIT_URL = "https://gitlab.com/kicad/code/kicad.git"

def get_number_of_cores():
    return int(subprocess.check_output("sysctl -n hw.ncpu", shell=True).strip())

def get_local_macos_version():
    return subprocess.check_output("sw_vers -productVersion | cut -d. -f1-2", shell=True).decode('utf-8').strip()

def get_host_architecture():
    return subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"]).decode('utf-8').strip()

def host_is_apple_silicon():
    # We have to be careful here.  Most ways of checking, like uname or arch or things will be overriden if we're in a Rosetta terminal, for instance
    return 'Apple' in get_host_architecture()

def get_env_architecture():
    return subprocess.check_output("arch").decode('utf-8').strip()


def get_brew_config():
    return subprocess.check_output("brew config", shell=True).decode('utf-8').strip()


def get_brew_rosetta():
    brew_config = get_brew_config()
    brew_rosetta = None
    for line in brew_config.splitlines():
        if line.startswith("Rosetta 2:"):
            brew_rosetta = line.split(":", maxsplit=1)[1].strip().lower()
            if brew_rosetta == "true":
                brew_rosetta = True
            elif brew_rosetta == "false":
                brew_rosetta = False
            else:
                raise ValueError(f"Unexpected value for Rosetta 2: {brew_rosetta}")
    return brew_rosetta


def get_brew_macos_arch():
    brew_config = get_brew_config()
    brew_macos_arch = None
    for line in brew_config.splitlines():
        if line.startswith("macOS:"):
            brew_macos = line.split(":", maxsplit=1)[1].strip()
            brew_macos_arch = brew_macos.split("-")[1]
            if brew_macos_arch not in ('x86_64', 'arm64'):
                raise ValueError(f"Unexpected value for macOS architecture: {brew_macos_arch}")
    return brew_macos_arch


def parse_args(args):
    docs_tarball_url_default = "https://docs.kicad.org/kicad-doc-HEAD.tar.gz"

    parser = argparse.ArgumentParser(description='Build and package KiCad for macOS. Part of kicad-mac-builder.',
                                     epilog="Further details are available in the README file.")
    parser.add_argument("--build-dir",
                        help="Path that will store the build files. Will be created if possible if it doesn't exist. Defaults to \"build/\" next to build.py.",
                        default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "build"),
                        required=False)
    parser.add_argument("--dmg-dir",
                        help="Path that will store the output dmgs for packaging targets.  Defaults to \"dmg/\" in the build directory.",
                        required=False)
    parser.add_argument("--jobs",
                        help="Tell make to build using this number of parallel jobs. Defaults to the number of cores.",
                        type=int,
                        required=False,
                        default=get_number_of_cores()
                        )
    parser.add_argument("--release",
                        help="Build for a release.",
                        action="store_true"
                        )
    parser.add_argument("--extra-version",
                        help="Sets the version to the git version, a hyphen, and then this string.",
                        required=False)
    parser.add_argument("--build-type",
                        help="Build type passed to CMake like Debug, Release, or RelWithDebInfo.  Defaults to RelWithDebInfo, unless --release is set."
                        )
    parser.add_argument("--kicad-git-url",
                        help="KiCad source code git url.  Defaults to {}. Conflicts with --kicad-source-dir.".format(DEFAULT_KICAD_GIT_URL))
    parser.add_argument("--kicad-ref",
                        help="KiCad source code git tag, commit, or branch to build from. Defaults to origin/master.",
                        )
    parser.add_argument("--kicad-source-dir",
                        help="KiCad source directory to use as-is to build from.  Will not be patched, and cannot create a release.",
                        )
    parser.add_argument("--symbols-ref",
                        help="KiCad symbols git tag, commit, or branch to build from. Defaults to origin/master.",
                        )
    parser.add_argument("--footprints-ref",
                        help="KiCad footprints git tag, commit, or branch to build from. Defaults to origin/master.",
                        )
    parser.add_argument("--packages3d-ref",
                        help="KiCad packages3d git tag, commit, or branch to build from. Defaults to origin/master.",
                        )
    parser.add_argument("--templates-ref",
                        help="KiCad templates git tag, commit, or branch to build from. Defaults to origin/master.",
                        )
    parser.add_argument("--docs-tarball-url",
                        help="URL to download the documentation tar.gz from. Defaults to {}".format(
                            docs_tarball_url_default),
                        )
    parser.add_argument("--skip-docs-update",
                        help="Skip updating the docs, if they've already been downloaded. Cannot be used to create a release.",
                        action="store_true"
                        )
    parser.add_argument("--release-name",
                        help="Overrides the main component of the DMG filename.",
                        )
    parser.add_argument("--macos-min-version",
                        help="Minimum macOS version to build for. You must have the appropriate XCode SDK installed. "
                             " Defaults to the macOS version of this computer.",
                        default="12",
                        )
    parser.add_argument("--arch",
                        choices=['x86_64', 'arm64'],
                        help="Target architecture. Required on Apple Silicon. Universal (combined) builds are not yet supported. Not all combinations of options are valid (targeting arm64 and macOS 10.15, for instance).",
                        required=host_is_apple_silicon())
    parser.add_argument("--no-retry-failed-build",
                        help="By default, if make fails and the number of jobs is greater than one, build.py will "
                             "rebuild using a single job to create a clearer error message. This flag disables that "
                             "behavior.",
                        action='store_false',
                        dest="retry_failed_build"
                        )
    parser.add_argument("--target",
                        help="List of make targets to build. By default, downloads and builds everything, but does "
                             "not package any DMGs. Use package-kicad-nightly for the nightly DMG, "
                             "package-extras for the extras DMG, and package-kicad-unified for the all-in-one "
                             "DMG. See the documentation for details.",
                        nargs="+",
                        )
    parser.add_argument("--extra-bundle-fix-dir",
                        dest="extra_bundle_fix_dir",
                        help="Extra directory to pass to fixup_bundle for KiCad.app.")
    parser.add_argument("--extra-kicad-cmake-args",
                        help="Use something like '-DFOO=\"bar\"' to add FOO=bar to KiCad's CMake args.",
                        required=False)

    parser.add_argument("--redistributable",
                        action="store_true",
                        help="Fix KiCad bundle to work on other machines. This requires wrangle-bundle from dyldstyle, and is implied for releases, packaged builds, and notarized builds.")

    signing_group = parser.add_argument_group('signing and notarization', description="By default, kicad-mac-builder uses ad-hoc signing and doesn't submit targets for notarization.")
    signing_group.add_argument("--signing-identity",
                        dest="signing_identity",
                        help="Signing identity passed to codesign for signing .apps and .dmgs.")
    signing_group.add_argument("--signing-certificate-id",
                               dest="signing_certificate_id",
                               default="-",
                               help="40 character hex ID for the signing certificate from `security find-identity -v`.  Defaults to '-', which is only valid for adhoc signing.")
    signing_group.add_argument("--apple-developer-username",
                        dest="apple_developer_username",
                        help="Apple Developer username for notarizing .apps and .dmgs.")
    signing_group.add_argument("--apple-developer-password-keychain-name",
                        dest="apple_developer_password_keychain_name",
                        help="Apple Developer password keychain name for notarizing .apps and .dmgs. "
                        "See `man altool` for more details.")
    signing_group.add_argument("--app-notarization-id",
                        dest="app_notarization_id",
                        help="notarization ID used for tracking notarization submissions for .apps")
    signing_group.add_argument("--dmg-notarization-id",
                        dest="dmg_notarization_id",
                        help="notarization ID used for tracking notarization submissions for .dmgs")
    signing_group.add_argument("--asc-provider",
                        dest="asc_provider",
                        help="provider passed to `xcrun altool` for notarization")
    signing_group.add_argument('--hardened-runtime', action='store_true',
                               help="Enable Hardened Runtime. Does not work with adhoc signing, but this is required for release builds.")

    parsed_args = parser.parse_args(args)

    if parsed_args.target is None:
        parsed_args.target = []

    if parsed_args.release and parsed_args.kicad_source_dir:
        parser.error("KiCad source directory builds cannot be release builds.")

    if parsed_args.release and parsed_args.skip_docs_update:
        parser.error("Release builds cannot skip docs updates.")

    # if parsed_args.release and not parsed_args.hardened_runtime:
    #     parser.error("Release builds must use Hardened Runtime.")

    if parsed_args.hardened_runtime and parsed_args.signing_identity in (None, "-"):
        parser.error("Hardened Runtime requires a non-adhoc signing identity.")

    if (parsed_args.kicad_ref or parsed_args.kicad_git_url) and parsed_args.kicad_source_dir:
        parser.error("KiCad source directory builds cannot also specify KiCad git details.")

    if parsed_args.kicad_source_dir:
        parsed_args.kicad_source_dir = os.path.realpath(parsed_args.kicad_source_dir)
    elif not parsed_args.kicad_git_url:
        parsed_args.kicad_git_url = DEFAULT_KICAD_GIT_URL

    macos_major_version = int(parsed_args.macos_min_version.split(".")[0])
    if parsed_args.arch == "arm64" and macos_major_version < 11:
        parser.error("arm64 builds must target macOS 11 or greater.")
    # TODO prevent more footgun situations with min version and arch and things, maybe?

    if parsed_args.arch == "arm64" and not host_is_apple_silicon():
        parser.error("Cannot target arm64 on x86_64 host.")

    if parsed_args.arch == "x86_64" and host_is_apple_silicon():
        # check on Rosetta
        # I'm not sure if we *should* need Rosetta, but right now
        # it seems to need it
        try:
            subprocess.check_call(["pgrep", "-q", "oahd"])
        except subprocess.CalledProcessError:
            parser.error("Building KiCad x86_64 on arm64 requires Rosetta. "
                         "It doesn't appear to be installed. "
                         "One way to install it is with `/usr/sbin/softwareupdate --install-rosetta`.")

    if parsed_args.release:
        if parsed_args.build_type is None:
            parsed_args.build_type = "Release"
        elif parsed_args.build_type != "Release":
            parser.error("Release builds imply --build-type Release.")

        if parsed_args.kicad_ref is None or \
                parsed_args.symbols_ref is None or \
                parsed_args.footprints_ref is None or \
                parsed_args.packages3d_ref is None or \
                parsed_args.templates_ref is None or \
                parsed_args.docs_tarball_url is None or \
                parsed_args.release_name is None:
            parser.error(
                "Release builds require --kicad-ref, --symbols-ref, --footprints-ref, --packages3d-ref, "
                "--templates-ref, --docs-tarball-url, and --release-name.")

        parsed_args.redistributable = True
    else:
        # not stable

        default_refs = ["symbols_ref", "footprints_ref", "packages3d_ref", "templates_ref"]

        if not parsed_args.kicad_source_dir:
            default_refs.append("kicad_ref")

        # handle defaults--can't do in argparse because they're conditionally required
        for ref in default_refs:
            if getattr(parsed_args, ref) is None:
                setattr(parsed_args, ref, "origin/master")
        if parsed_args.docs_tarball_url is None:
            parsed_args.docs_tarball_url = docs_tarball_url_default
        if parsed_args.build_type is None:
            parsed_args.build_type = "RelWithDebInfo"

    # Before Python 3.4, __file__ might not be absolute, so let's lock this down before we do any chdir'ing

    parsed_args.kicad_mac_builder_cmake_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kicad-mac-builder")

    if parsed_args.target and any([target.startswith("package") for target in parsed_args.target]):
        parsed_args.redistributable = True
    if parsed_args.app_notarization_id or parsed_args.dmg_notarization_id:
        parsed_args.redistributable = True
    return parsed_args


def get_make_command(args):
    make_command = ["make", "-j{}".format(args.jobs)]
    if args.target:
        make_command.extend(args.target)

    return make_command

def build(args, new_path):

    try:
        os.makedirs(args.build_dir)
    except OSError as exception:
        if exception.errno != errno.EEXIST:
            raise

    os.chdir(args.build_dir)

    cmake_command = ["cmake",
                     "-DMACOS_MIN_VERSION={}".format(args.macos_min_version),
                     "-DDOCS_TARBALL_URL={}".format(args.docs_tarball_url),
                     "-DFOOTPRINTS_TAG={}".format(args.footprints_ref),
                     "-DPACKAGES3D_TAG={}".format(args.packages3d_ref),
                     "-DSYMBOLS_TAG={}".format(args.symbols_ref),
                     "-DTEMPLATES_TAG={}".format(args.templates_ref),
                     "-DKICAD_CMAKE_BUILD_TYPE={}".format(args.build_type),
                     ]

    if args.arch:
        cmake_command.append(f"-DCMAKE_APPLE_SILICON_PROCESSOR={args.arch}")

    if args.kicad_source_dir:
        cmake_command.append("-DKICAD_SOURCE_DIR={}".format(args.kicad_source_dir))
    else:
        if args.kicad_git_url:
            cmake_command.append("-DKICAD_URL={}".format(args.kicad_git_url))
        if args.kicad_ref:
            cmake_command.append("-DKICAD_TAG={}".format(args.kicad_ref))

    if args.skip_docs_update:
        cmake_command.append("-DSKIP_DOCS_UPDATE=ON")

    if args.dmg_dir:
        cmake_command.append("-DDMG_DIR={}".format(args.dmg_dir))

    if args.extra_version:
        cmake_command.append("-DKICAD_VERSION_EXTRA={}".format(args.extra_version))

    if args.extra_bundle_fix_dir:
        cmake_command.append("-DMACOS_EXTRA_BUNDLE_FIX_DIRS={}".format(args.extra_bundle_fix_dir))

    if args.extra_kicad_cmake_args:
        cmake_command.append("-DKICAD_CMAKE_ARGS_EXTRA='{}'".format(args.extra_kicad_cmake_args))

    if args.signing_identity:
        cmake_command.append("-DSIGNING_IDENTITY={}".format(args.signing_identity))

    if args.signing_certificate_id:
        cmake_command.append("-DSIGNING_CERTIFICATE_ID={}".format(args.signing_certificate_id))

    if args.apple_developer_username:
        cmake_command.append("-DAPPLE_DEVELOPER_USERNAME={}".format(args.apple_developer_username))

    if args.apple_developer_password_keychain_name:
        cmake_command.append("-DAPPLE_DEVELOPER_PASSWORD_KEYCHAIN_NAME={}".format(args.apple_developer_password_keychain_name))

    if args.app_notarization_id:
        cmake_command.append("-DAPP_NOTARIZATION_ID={}".format(args.app_notarization_id))

    if args.dmg_notarization_id:
        cmake_command.append("-DDMG_NOTARIZATION_ID={}".format(args.dmg_notarization_id))

    if args.asc_provider:
        cmake_command.append("-DASC_PROVIDER={}".format(args.asc_provider))

    if args.hardened_runtime:
        cmake_command.append("-DHARDENED_RUNTIME=ON")
    else:
        cmake_command.append("-DHARDENED_RUNTIME=OFF")

    if args.release_name:
        cmake_command.append("-DRELEASE_NAME={}".format(args.release_name))

    cmake_command.append(args.kicad_mac_builder_cmake_dir)

    print("Running {}".format(" ".join(cmake_command)), flush=True)
    try:
        subprocess.check_call(cmake_command, env=dict(os.environ, PATH=new_path))
    except subprocess.CalledProcessError:
        print("Error while running cmake. Please report this issue if you cannot fix it after reading the README.", flush=True)
        raise

    make_command = get_make_command(args)
    print("Running {}".format(" ".join(make_command)), flush=True)
    # COPYFILE_DISABLE prevents macOS copyfile() from copying immutable extended
    # attributes (com.apple.provenance) that cause cmake file(INSTALL) to fail
    # with "Operation not permitted" on macOS Sequoia+.
    make_env = dict(os.environ, PATH=new_path, COPYFILE_DISABLE="1")
    try:
        subprocess.check_call(["taskpolicy", "-a"] + make_command, env=make_env)
    except subprocess.CalledProcessError:
        if args.retry_failed_build and args.jobs > 1:
            print("Error while running make.", flush=True)
            print_summary(args)
            print("Rebuilding with a single job. If this consistently occurs, " \
                  "please report this issue. ", flush=True)
            args.jobs = 1
            make_command = get_make_command(args)
            print("Running {}".format(" ".join(make_command)), flush=True)
            try:
                subprocess.check_call(["taskpolicy", "-a"] + make_command, env=make_env)
            except subprocess.CalledProcessError:
                print("Error while running make after rebuilding with a single job. Please report this issue if you " \
                      "cannot fix it after reading the README.", flush=True)
                print_summary(args)
                raise
        else:
            print("Error while running make. It may be helpful to rerun with a single make job. Please report this " \
                  "issue if you cannot fix it after reading the README.", flush=True)
            print_summary(args)
            raise

    had_package_targets = any(target.startswith("package-") for target in args.target)
    if had_package_targets:
        dmg_location = args.dmg_dir
        if dmg_location is None:
            dmg_location = os.path.join(args.build_dir, "dmg")
        print("Output DMGs should be located in {}".format(dmg_location), flush=True)

    print("Build complete.", flush=True)

def print_summary(args):
    print("build.py argument summary:", flush=True)
    for attr in sorted(args.__dict__):
        print("{}: {}".format(attr, getattr(args, attr)), flush=True)

def main():
    parsed_args = parse_args(sys.argv[1:])
    print_summary(parsed_args)

    which_brew = subprocess.check_output("which brew", shell=True).decode('utf-8').strip()
    brew_prefix = subprocess.check_output("brew --prefix", shell=True).decode('utf-8').strip()

    print(f"The detected host architecture is {get_host_architecture()}")
    print(f"The output of 'arch' is '{get_env_architecture()}'")

    print(f"The first brew on the path is: {which_brew}")
    print(f"Its prefix is: {brew_prefix}")

    brew_macos_arch = get_brew_macos_arch()
    brew_rosetta = get_brew_rosetta()
    if parsed_args.arch == "x86_64":
        # on x86_64, we expect brew rosetta to be none (if on x86) or true (if on arm64)
        # and brew macos to contain x86_64
        errors = []
        if brew_rosetta and get_host_architecture() == "x86_64":
            errors.append("`brew config` reports using Rosetta 2, but we're trying to build for x86_64.")

        if "x86_64" not in brew_macos_arch:
            errors.append(
                "`brew config` doesn't report x86_64 in the macOS line, but we're trying to build for x86_64.")

        if errors:
            errors.append(
                "Check your PATH, and if you're on arm64, you may need to prefix the build.py command with `arch -x86_64 `. See the README.")
            print("\n".join(errors))
            sys.exit(1)

    elif parsed_args.arch == "arm64":
        # on arm64, we expect brew_rosetta to be false, and brew macos to contain arm64
        errors = []
        if brew_rosetta:
            errors.append("`brew config` reports using Rosetta 2, but we're trying to build for arm64.")

        if "arm64" not in brew_macos_arch:
            errors.append("`brew config` doesn't report arm64 in the macOS line, but we're trying to build for arm64.")

        if errors:
            errors.append("Check your PATH. See the README.")
            print("\n".join(errors))
            sys.exit(1)


    gettext_path = "{}/bin".format(subprocess.check_output("brew --prefix gettext", shell=True).decode('utf-8').strip())
    bison_path = "{}/bin".format(subprocess.check_output("brew --prefix bison", shell=True).decode('utf-8').strip())
    new_path = ":".join((gettext_path, bison_path, os.environ["PATH"]))
    print(f"Updated PATH is: {new_path}")

    print("\nYou can change these settings.  Run ./build.py --help for details.", flush=True)
    print("\nDepending upon build configuration, what has already been downloaded, what has already been built, " \
          "the computer and the network connection, this may take multiple hours and approximately 30G of disk space.", flush=True)
    print("\nYou can stop the build at any time by pressing Control-C.\n", flush=True)
    build(parsed_args, new_path)


if __name__ == "__main__":
    main()
