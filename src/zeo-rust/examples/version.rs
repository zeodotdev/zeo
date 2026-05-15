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

use kicad::{KiCad, KiCadConnectionConfig};

fn main() {
    println!("kicad-rs api version {}", kicad::API_VERSION);

    let k = KiCad::new(KiCadConnectionConfig {
        client_name: String::from("version-example"),
        ..Default::default()
    })
    .expect("KiCad not running!");

    println!("Connected to KiCad {}", k.get_version().unwrap());
}
