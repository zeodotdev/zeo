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

use std::path::{Path, PathBuf};
use walkdir::{DirEntry, WalkDir};

pub fn generate_protobuf_bindings() {
    println!("cargo::rerun-if-changed=kicad/api/proto");

    let api_path = PathBuf::from("kicad/api/proto");

    if !api_path.exists() || !api_path.is_dir() {
        println!("The KiCad API input files are not present. Try running `git submodule init --update --recursive`.");
        std::process::exit(1);
    }

    let mut protos: Vec<PathBuf> = vec![];
    let walker = WalkDir::new(api_path).into_iter();

    fn is_proto(e: &DirEntry) -> bool {
        e.file_name()
            .to_str()
            .map(|s| s.ends_with(".proto"))
            .unwrap_or(false)
    }

    for entry in walker.filter_entry(|e| is_proto(e) || e.file_type().is_dir()) {
        if !entry.as_ref().unwrap().file_type().is_dir() {
            protos.push(entry.unwrap().into_path());
        }
    }

    protobuf_codegen::Codegen::new()
        .protoc()
        .protoc_extra_arg("--experimental_allow_proto3_optional")
        .include(Path::new("kicad/api/proto"))
        .inputs(&protos)
        .cargo_out_dir("proto")
        .run_from_script();
}
