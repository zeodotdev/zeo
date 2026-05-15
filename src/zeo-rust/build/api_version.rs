// This program source code file is part of KiCad, a free EDA CAD application.
//
// Copyright (C) KiCad Developers
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program.  If not, see <http://www.gnu.org/licenses/>.

use regex::Regex;
use std::path::PathBuf;

/// Create a const `API_VERSION` and write it to `OUT_DIR/version.rs`
///
/// Use `git describe` if available, or `kicad/cmake/KiCadVersion.cmake`
/// to obtain the version information.
pub fn create_api_version_information() {
    if let Some(version) = git_describe() {
        let semver = parse_semver(&version);
        let (major, minor, patch) = match semver {
            Some((major, minor, patch)) => (major, minor, patch),
            None => {
                println!(
                    "cargo::warning=Failed to parse semantic version for KiCad from \"{}\" (using git describe)",
                    &version
                );
                (0, 0, 0)
            }
        };
        write_version_file(&version, major, minor, patch);
    } else {
        version_information_from_cmake();
    }
}

/// Find the `KICAD_SEMANTIC_VERSION` in `kicad/cmake/KiCadVersion.cmake`
///
/// Errors if the cmake file can not be found or parsing and/or writing the generated file fails.
fn version_information_from_cmake() {
    println!("cargo::rerun-if-changed=kicad/cmake/KiCadVersion.cmake");

    let kicad_version_file = std::fs::read_to_string("kicad/cmake/KiCadVersion.cmake")
        .expect("Could not read KiCadVersion.cmake. Try running `git submodule init`.");

    let re = Regex::new(r#"set\(\s*KICAD_SEMANTIC_VERSION\s*"(?<semver>.+)"\s*\)"#).unwrap();

    let mut semver = None;
    for line in kicad_version_file.lines().filter(|l| l.starts_with("set")) {
        if let Some(captures) = re.captures(line) {
            if let Some(v) = captures.name("semver") {
                semver = Some(v.as_str());
                break;
            }
        }
    }

    let semver = semver.unwrap_or_else(|| {
        println!("cargo::error=KICAD_SEMANTIC_VERSION not found in KiCadVersion.cmake");
        std::process::exit(1);
    });

    let (major, minor, patch) = parse_semver(semver).unwrap_or_else(|| {
        println!(
            "cargo::error=Failed to parse semantic version for KiCad from \"{}\"",
            semver
        );
        std::process::exit(1);
    });

    write_version_file(semver, major, minor, patch);
}

/// Parse semver (major, minor, patch) from a semver string
///
/// Returns `None` if parsing fails.
fn parse_semver(version: &str) -> Option<(u32, u32, u32)> {
    let re = Regex::new(r"^(?<MAJOR>\d+)\.(?<MINOR>\d+)\.(?<PATCH>\d+).*").unwrap();

    let captures = re.captures(version);

    if let Some(captures) = captures {
        match (
            captures.name("MAJOR"),
            captures.name("MINOR"),
            captures.name("PATCH"),
        ) {
            (Some(major), Some(minor), Some(patch)) => {
                let major = major.as_str().parse().unwrap_or(0_u32);
                let minor = minor.as_str().parse().unwrap_or(0_u32);
                let patch = patch.as_str().parse().unwrap_or(0_u32);
                Some((major, minor, patch))
            }
            _ => None,
        }
    } else {
        None
    }
}

/// Write a `version.rs` file to `OUT_DIR`
///
/// Creates a const `API_VERSION` and writes it to
/// `OUT_DIR/version.rs`
fn write_version_file(full: &str, major: u32, minor: u32, patch: u32) {
    use quote::quote;
    let version_ts = quote! {
        /// The KiCad / IPC API version of this library
        ///
        /// This can be used to check for semver compatibility against a running instance of KiCad.
        pub const API_VERSION: crate::KiCadVersion = crate::KiCadVersion {
            major: #major,
            minor: #minor,
            patch: #patch,
            full: ::std::borrow::Cow::Borrowed(#full),
        };
    };

    let content = syn::parse_file(&version_ts.to_string()).unwrap();
    let pretty_content = prettyplease::unparse(&content);

    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let path = out_dir.join("version.rs");

    std::fs::write(&path, pretty_content).expect("failed to write version.rs");
}

/// Run git describe on the `kicad` submodule
///
/// Returns `None` on any failure.
/// Issues a warning when the base repository can be opened,
/// but the operation fails in any other way.
fn git_describe() -> Option<String> {
    use git2::{DescribeOptions, Repository};

    let repo = Repository::open(".").ok()?;
    let submod = repo
        .find_submodule("kicad")
        .map_err(|e| {
            println!("cargo::warning=Failed to find kicad submodule: {e}");
            e
        })
        .ok()?;
    let kicad_repo = submod
        .open()
        .map_err(|e| {
            println!("cargo::warning=Failed to open kicad repository: {e}");
            e
        })
        .ok()?;
    let describe = kicad_repo
        .describe(&DescribeOptions::new())
        .map_err(|e| {
            println!("cargo::warning=Failed to run git describe on kicad submodule: {e}");
            e
        })
        .ok()?;

    describe
        .format(None)
        .map_err(|e| {
            println!("cargo::warning=Failed to format git describe output: {e}");
            e
        })
        .ok()
}
