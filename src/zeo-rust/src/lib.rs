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

//! # KiCad IPC API Bindings
//!
//! `kicad-api-rs` is a library providing bindings to the KiCad IPC API, which allows external
//! applications to communicate with a running instance of KiCad.  This can be used to build
//! plugins that are launched by KiCad, but also to enable standalone applications to interface
//! with KiCad.

use nng::{Protocol, Socket};
use num_traits::{FromPrimitive, Num, ToPrimitive};
use protobuf::well_known_types::any::Any;
use protobuf::{EnumOrUnknown, Message, MessageFull};
use rand::distr::{Alphanumeric, SampleString};
use std::cell::RefCell;
use std::env;
use std::fmt::Display;

#[macro_use]
extern crate quick_error;

pub mod board;
mod protos;

mod api_version;

use crate::board::Board;
use crate::protos::base_commands::*;
use crate::protos::base_types::DocumentSpecifier;
use crate::protos::editor_commands::*;
use crate::protos::envelope::*;

pub use crate::protos::base_types::DocumentType;
pub use crate::protos::board_types::BoardLayer;

pub use api_version::*;

quick_error! {
    #[derive(Debug)]
    pub enum KiCadError {
        ConnectionFailed(err: nng::Error) {
            display("could not connect to KiCad: {}", err)
            from()
            from(e: (nng::Message, nng::Error)) -> (e.1)
        }
        ProtocolError(err: protobuf::Error) {
            display("could not decode message: {}", err)
            from()
        }
        ApiError(msg: String) {
            from()
        }
    }
}

type KiCadResult<T> = Result<T, KiCadError>;

/// Represents a connection to KiCad and its top-level API calls.
#[derive(Debug)]
pub struct KiCad {
    socket: Box<Socket>,
    config: RefCell<KiCadConnectionConfig>,
}

/// Describes a particular version of KiCad
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct KiCadVersion<'a> {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
    /// Full version string.
    ///
    /// Can include additional information like release candidate specifiers and commit hashes.
    pub full: std::borrow::Cow<'a, str>,
}

impl KiCadVersion<'_> {
    /// Turn this version into a owned one, cloning the version string if its not already owned.
    pub fn into_owned(self) -> KiCadVersion<'static> {
        KiCadVersion {
            major: self.major,
            minor: self.minor,
            patch: self.patch,
            full: std::borrow::Cow::Owned(self.full.into_owned()),
        }
    }
    pub fn new(major: u32, minor: u32, patch: u32, full: String) -> KiCadVersion<'static> {
        KiCadVersion {
            major,
            minor,
            patch,
            full: full.into(),
        }
    }
}

impl Display for KiCadVersion<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.full)
    }
}

impl From<protos::base_types::KiCadVersion> for KiCadVersion<'_> {
    fn from(value: protos::base_types::KiCadVersion) -> Self {
        Self {
            major: value.major,
            minor: value.minor,
            patch: value.patch,
            full: value.full_version.into(),
        }
    }
}
impl<'a> From<&'a protos::base_types::KiCadVersion> for KiCadVersion<'a> {
    fn from(value: &'a protos::base_types::KiCadVersion) -> Self {
        Self {
            major: value.major,
            minor: value.minor,
            patch: value.patch,
            full: (&value.full_version).into(),
        }
    }
}

// KiCad uses 64-bit nanometers for all measurements in the API
type Coord = i64;

pub fn to_mm(iu: Coord) -> f64 {
    iu as f64 / 1_000_000f64
}

pub fn from_mm<F: Num + ToPrimitive + FromPrimitive>(x: F) -> Coord {
    (x * F::from_f64(1_000_000f64).unwrap()).to_i64().unwrap()
}

/// Configuration options passed to KiCad::new()
#[derive(Debug)]
pub struct KiCadConnectionConfig {
    /// The path to KiCad's IPC server socket.  Leave default to use the platform-dependent
    /// default path.  KiCad will provide this value in an environment variable when launching
    /// API plugins.
    pub socket_path: String,

    /// The name of this API client.  Leave default to generate a random client name.  This name
    /// should uniquely identify a running instance of the client, especially if the user may
    /// launch more than one instance of the client at a time.
    pub client_name: String,

    /// A token identifying a running instance of KiCad.  Leave default to not specify a KiCad
    /// instance.  The first command sent to KiCad will include that KiCad instance's token in the
    /// response, which should then be used on subsequent commands to ensure the client can detect
    /// if a different KiCad instance is responding (for example, if KiCad is closed and
    /// re-opened by the user).
    pub kicad_token: String,
}

impl Default for KiCadConnectionConfig {
    fn default() -> Self {
        let socket_path = match env::consts::OS {
            "windows" => {
                format!(
                    "ipc://{}\\kicad\\api.sock",
                    env::temp_dir().to_str().unwrap()
                )
            }
            _ => String::from("ipc:///tmp/kicad/api.sock"),
        };

        let mut client_name: String = Alphanumeric.sample_string(&mut rand::rng(), 8);
        client_name.insert_str(0, "anonymous-");

        Self {
            socket_path,
            client_name,
            kicad_token: String::new(),
        }
    }
}

impl KiCad {
    pub fn new(config: KiCadConnectionConfig) -> KiCadResult<KiCad> {
        let socket = Socket::new(Protocol::Req0)?;
        socket.dial(&config.socket_path)?;

        Ok(KiCad {
            socket: Box::new(socket),
            config: RefCell::new(config),
        })
    }

    fn send_envelope(&self, req: ApiRequest) -> KiCadResult<ApiResponse> {
        self.socket.send(req.write_to_bytes()?.as_slice())?;
        let response = ApiResponse::parse_from_bytes(self.socket.recv()?.as_slice())?;

        match response.status.status.enum_value_or_default() {
            ApiStatusCode::AS_OK => {
                let mut config = self.config.borrow_mut();

                if config.kicad_token.is_empty() {
                    config.kicad_token = String::from(&response.header.kicad_token);
                }

                Ok(response)
            }
            _ => Err(KiCadError::ApiError(format!(
                "KiCad API returned error: {}",
                response.status.error_message
            ))),
        }
    }

    fn send_request<T: MessageFull, U: MessageFull>(&self, message: T) -> KiCadResult<U> {
        let mut req = ApiRequest::new();

        req.header = Some(ApiRequestHeader::new()).into();
        let header = req.header.as_mut().unwrap();

        {
            let config = self.config.borrow();
            header.client_name = config.client_name.clone();
            header.kicad_token = config.kicad_token.clone();
        }

        req.message = Some(Any::pack(&message)?).into();
        let rep = self.send_envelope(req)?;
        let message = Any::unpack::<U>(rep.message.get_or_default())?;
        match message {
            Some(message) => Ok(message),
            None => Err(KiCadError::ApiError(format!(
                "could not unpack {} from API response",
                U::descriptor().name()
            ))),
        }
    }

    pub fn get_version(&self) -> KiCadResult<KiCadVersion> {
        let reply: GetVersionResponse = self.send_request(GetVersion::new())?;
        Ok(reply.version.get_or_default().clone().into())
    }

    pub fn get_open_documents(
        &self,
        doc_type: DocumentType,
    ) -> KiCadResult<Vec<DocumentSpecifier>> {
        let mut message = GetOpenDocuments::new();
        message.type_ = EnumOrUnknown::from(doc_type);
        Ok(self
            .send_request::<_, GetOpenDocumentsResponse>(message)?
            .documents)
    }

    pub fn get_board(&self, doc: &DocumentSpecifier) -> KiCadResult<Board> {
        Ok(Board {
            kicad: self,
            doc: doc.clone(),
        })
    }

    pub fn get_open_board(&self) -> KiCadResult<Board> {
        let docs = self
            .get_open_documents(DocumentType::DOCTYPE_PCB)
            .unwrap_or_default();

        match docs.first() {
            Some(doc) => self.get_board(doc),
            _ => Err(KiCadError::ApiError(String::from("no boards are open"))),
        }
    }
}
