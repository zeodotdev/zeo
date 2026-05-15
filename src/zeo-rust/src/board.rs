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

use std::vec::IntoIter;

use enum_as_inner::EnumAsInner;
use protobuf::{well_known_types::any::Any, EnumOrUnknown};

use crate::protos::{base_types::*, board_types, editor_commands::*, enums::*};
use crate::{Coord, KiCad, KiCadError, KiCadResult};

#[derive(Debug)]
pub struct Board<'a> {
    pub(crate) kicad: &'a KiCad,
    pub(crate) doc: DocumentSpecifier,
}

impl Board<'_> {
    fn create_item_header(&self) -> ItemHeader {
        let mut header = ItemHeader::new();
        header.document = Some(self.doc.clone()).into();
        header
    }

    fn unpack_any(any: &Any) -> KiCadResult<BoardItem> {
        match any.type_url.as_str() {
            "type.googleapis.com/kiapi.board.types.Track" => Ok(BoardItem::Track(Track(
                Any::unpack::<board_types::Track>(any)?.unwrap(),
            ))),
            "type.googleapis.com/kiapi.board.types.Arc" => Ok(BoardItem::ArcTrack(ArcTrack(
                Any::unpack::<board_types::Arc>(any)?.unwrap(),
            ))),
            "type.googleapis.com/kiapi.board.types.Via" => {
                Ok(BoardItem::Via(Via(
                    Any::unpack::<board_types::Via>(any)?.unwrap()
                )))
            }
            _ => Err(KiCadError::ApiError(format!(
                "unknown type: {}",
                any.type_url.as_str()
            ))),
        }
    }

    pub fn name(&self) -> &str {
        self.doc.board_filename()
    }

    pub fn project_name(&self) -> Option<&str> {
        self.doc.project.as_ref().map(|p| p.name.as_str())
    }

    pub fn project_path(&self) -> Option<&str> {
        self.doc.project.as_ref().map(|p| p.path.as_str())
    }

    pub fn create_items(
        &self,
        items: Vec<impl GenericBoardItem>,
    ) -> KiCadResult<Vec<ItemCreationResult>> {
        let mut message = CreateItems::new();
        message.header = Some(self.create_item_header()).into();
        message
            .items
            .extend(items.iter().map(GenericBoardItem::pack));

        let reply = self.kicad.send_request::<_, CreateItemsResponse>(message)?;
        self.check_item_request_status(&reply.status)?;

        Ok(reply.created_items)
    }

    /// Return an Iterator over all the items with the specified [`ObjectType`][KiCadObjectType]
    pub fn items(&self, types: &[KiCadObjectType]) -> KiCadResult<Items> {
        let mut message = GetItems::new();
        message.header = Some(self.create_item_header()).into();
        message
            .types
            .extend(types.iter().map(|t| EnumOrUnknown::new(*t)));

        let reply = self.kicad.send_request::<_, GetItemsResponse>(message)?;
        self.check_item_request_status(&reply.status)?;

        // The collecting is required to check if there were any errors while unpacking
        let items: KiCadResult<Vec<_>> = reply.items.iter().map(Self::unpack_any).collect();
        Ok(Items {
            iter: items?.into_iter(),
        })
    }

    // TODO: if we want this to match the old bindings, this should return
    // an enum of { Track, ArcTrack, Via } - should create a trait for the
    // shared properties between them?
    pub fn tracks(&self) -> KiCadResult<Tracks> {
        Ok(self.items(&[KiCadObjectType::KOT_PCB_TRACE])?.tracks())
    }

    pub fn update_items(
        &self,
        items: &[impl GenericBoardItem],
    ) -> KiCadResult<Vec<ItemUpdateResult>> {
        let mut message = UpdateItems::new();
        message.header = Some(self.create_item_header()).into();
        message.items.extend(items.iter().map(|t| t.pack()));

        let reply = self.kicad.send_request::<_, UpdateItemsResponse>(message)?;
        self.check_item_request_status(&reply.status)?;
        Ok(reply.updated_items)
    }

    pub fn delete_items(
        &self,
        items: &[impl GenericBoardItem],
    ) -> KiCadResult<Vec<ItemDeletionResult>> {
        let mut message = DeleteItems::new();
        message.header = Some(self.create_item_header()).into();
        message
            .item_ids
            .extend(items.iter().map(|i| i.id().clone()));

        let reply = self.kicad.send_request::<_, DeleteItemsResponse>(message)?;
        self.check_item_request_status(&reply.status)?;
        Ok(reply.deleted_items)
    }

    fn check_item_request_status(&self, r: &EnumOrUnknown<ItemRequestStatus>) -> KiCadResult<()> {
        match r.enum_value_or_default() {
            ItemRequestStatus::IRS_OK => Ok(()),
            ItemRequestStatus::IRS_DOCUMENT_NOT_FOUND => Err(KiCadError::ApiError(format!(
                "KiCad reported that the file {} is not open",
                self.name()
            ))),
            _ => Err(
                // TODO: real error
                KiCadError::ApiError(String::from("unexpected item request status")),
            ),
        }
    }
}

#[derive(Debug, EnumAsInner)]
pub enum BoardItem {
    Track(Track),
    ArcTrack(ArcTrack),
    Via(Via),
}

pub trait GenericBoardItem {
    fn id(&self) -> &KIID;

    fn pack(&self) -> Any;
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Track(board_types::Track);

impl Track {
    pub fn new() -> Self {
        Track(board_types::Track::new())
    }

    pub fn start(&self) -> (Coord, Coord) {
        (self.0.start.x_nm, self.0.start.y_nm)
    }

    pub fn set_start(&mut self, (x, y): (Coord, Coord)) {
        let start = self.0.start.mut_or_insert_default();
        start.x_nm = x;
        start.y_nm = y;
    }

    pub fn end(&self) -> (Coord, Coord) {
        (self.0.end.x_nm, self.0.end.y_nm)
    }

    pub fn set_end(&mut self, (x, y): (Coord, Coord)) {
        let end = self.0.end.mut_or_insert_default();
        end.x_nm = x;
        end.y_nm = y;
    }

    pub fn width(&self) -> Coord {
        self.0.width.get_or_default().value_nm
    }

    pub fn set_width(&mut self, width: Coord) {
        self.0.width.mut_or_insert_default().value_nm = width
    }

    pub fn locked(&self) -> bool {
        self.0.locked.enum_value_or_default() == LockedState::LS_LOCKED
    }

    pub fn layer(&self) -> board_types::BoardLayer {
        self.0.layer.unwrap()
    }

    pub fn net(&self) -> &board_types::Net {
        self.0.net.get_or_default()
    }
}

impl GenericBoardItem for Track {
    fn id(&self) -> &KIID {
        self.0.id.get_or_default()
    }

    fn pack(&self) -> Any {
        Any::pack(&self.0).unwrap_or_default()
    }
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct ArcTrack(board_types::Arc);

impl ArcTrack {
    pub fn new() -> Self {
        ArcTrack(board_types::Arc::new())
    }

    pub fn start(&self) -> (Coord, Coord) {
        (self.0.start.x_nm, self.0.start.y_nm)
    }

    pub fn set_start(&mut self, (x, y): (Coord, Coord)) {
        let start = self.0.start.mut_or_insert_default();
        start.x_nm = x;
        start.y_nm = y;
    }

    pub fn mid(&self) -> (Coord, Coord) {
        (self.0.mid.x_nm, self.0.mid.y_nm)
    }

    pub fn set_mid(&mut self, (x, y): (Coord, Coord)) {
        let mid = self.0.mid.mut_or_insert_default();
        mid.x_nm = x;
        mid.y_nm = y;
    }

    pub fn end(&self) -> (Coord, Coord) {
        (self.0.end.x_nm, self.0.end.y_nm)
    }

    pub fn set_end(&mut self, (x, y): (Coord, Coord)) {
        let end = self.0.end.mut_or_insert_default();
        end.x_nm = x;
        end.y_nm = y;
    }

    pub fn width(&self) -> Coord {
        self.0.width.get_or_default().value_nm
    }

    pub fn set_width(&mut self, width: Coord) {
        self.0.width.mut_or_insert_default().value_nm = width
    }

    pub fn locked(&self) -> bool {
        self.0.locked.enum_value_or_default() == LockedState::LS_LOCKED
    }

    pub fn layer(&self) -> board_types::BoardLayer {
        self.0.layer.unwrap()
    }

    pub fn set_layer(&mut self, layer: board_types::BoardLayer) {
        self.0.layer = layer.into();
    }

    pub fn net(&self) -> &board_types::Net {
        self.0.net.get_or_default()
    }
}

impl GenericBoardItem for ArcTrack {
    fn id(&self) -> &KIID {
        self.0.id.get_or_default()
    }

    fn pack(&self) -> Any {
        Any::pack(&self.0).unwrap_or_default()
    }
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Via(board_types::Via);

impl Via {
    pub fn new() -> Self {
        Via(board_types::Via::new())
    }

    pub fn position(&self) -> (Coord, Coord) {
        (self.0.position.x_nm, self.0.position.y_nm)
    }

    pub fn set_position(&mut self, (x, y): (Coord, Coord)) {
        let position = self.0.position.mut_or_insert_default();
        position.x_nm = x;
        position.y_nm = y;
    }

    pub fn locked(&self) -> bool {
        self.0.locked.enum_value_or_default() == LockedState::LS_LOCKED
    }

    pub fn pad_stack(&self) -> &board_types::PadStack {
        self.0.pad_stack.get_or_default()
    }

    pub fn net(&self) -> &board_types::Net {
        self.0.net.get_or_default()
    }
}

impl GenericBoardItem for Via {
    fn id(&self) -> &KIID {
        self.0.id.get_or_default()
    }

    fn pack(&self) -> Any {
        Any::pack(&self.0).unwrap_or_default()
    }
}

/// Immutable [`BoardItem`] iterator.
///
/// This struct id created by the [`items`][Board.items] method on [`Board`]
#[derive(Debug)]
pub struct Items {
    iter: IntoIter<BoardItem>,
}

impl Items {
    /// Return only the Track Items
    pub fn tracks(self) -> Tracks {
        Tracks::new(self)
    }
}

impl Iterator for Items {
    type Item = BoardItem;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next()
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.iter.size_hint()
    }
}

#[derive(Debug)]
pub struct Tracks {
    iter: Items,
}

impl Tracks {
    pub(crate) fn new(items: Items) -> Self {
        Tracks { iter: items }
    }
}

impl Iterator for Tracks {
    type Item = Track;

    fn next(&mut self) -> Option<Self::Item> {
        for item in self.iter.by_ref() {
            if let BoardItem::Track(pad) = item {
                return Some(pad);
            }
        }
        None
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, self.iter.size_hint().1)
    }
}
