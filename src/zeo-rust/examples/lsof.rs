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

use kicad::{DocumentType, KiCad, KiCadConnectionConfig};

fn main() {
    let k = KiCad::new(KiCadConnectionConfig::default()).expect("KiCad not running!");

    let docs_str = match k.get_open_documents(DocumentType::DOCTYPE_PCB) {
        Ok(docs) if docs.len() > 0 => docs
            .into_iter()
            .map(|doc| format!("{}", doc))
            .collect::<Vec<String>>()
            .join(", "),
        Ok(_) => String::from("none"),
        Err(e) => format!("error: {}", e),
    };

    println!("KiCad files open: {}", docs_str);
}
