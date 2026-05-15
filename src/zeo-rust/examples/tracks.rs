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

use rand::prelude::*;

use kicad::board::ArcTrack;
use kicad::BoardLayer;
use kicad::{from_mm, KiCad, KiCadConnectionConfig};

fn main() {
    let mut rng = rand::thread_rng();

    let kicad = KiCad::new(KiCadConnectionConfig::default()).expect("KiCad not running");
    let board = kicad.get_open_board().expect("no board is open");
    let mut tracks = board.tracks().unwrap();

    for track in &mut tracks {
        track.set_start((
            from_mm(rng.gen_range(10..100)),
            from_mm(rng.gen_range(10..100)),
        ));
    }

    board.update_items(&tracks).unwrap();

    let mut arcs = vec![ArcTrack::new(); 5];

    for arc in &mut arcs {
        arc.set_start((
            from_mm(rng.gen_range(10..100)),
            from_mm(rng.gen_range(10..100)),
        ));
        arc.set_end((
            from_mm(rng.gen_range(100..200)),
            from_mm(rng.gen_range(100..200)),
        ));
        arc.set_width(from_mm(rng.gen_range(0.25..1.5)));
        arc.set_layer(BoardLayer::BL_F_Cu);
    }

    board.create_items(arcs).unwrap();
}
