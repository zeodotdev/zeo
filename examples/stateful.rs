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

/// # Stateful KiCad Connection Example
///
/// This example demonstrates a stateful connection pattern for interacting with KiCad's API.
/// The stateful example creates a connection under the control of a supervisor and extracts
/// PCB board document information.
///
/// The idea is that in general one could leave this connection open and periodically check for
/// updates or changes in the KiCad design, and there are placeholder functions for future
/// enhancements such as getting board tracks or components in the `ClientState::Connected` state.
///
/// To run this example, make sure you have KiCad installed and running. Then while in the directory
/// of this crate, execute the following command in your terminal:
/// ```bash
/// cargo run --example stateful
/// ```
///
use kicad::{KiCad, KiCadConnectionConfig};
use prettytable::{row, Table};

struct BoardProjectData {
    project: String,
    path: String,
    name: String,
}

fn display_banner_message() {
    let os_name = sys_info::os_type().unwrap_or_else(|_| "Unknown OS".to_string());
    let os_release = sys_info::os_release().unwrap_or_else(|_| "Unknown OS Release".to_string());
    let banner_string = format!(
        "KiCad API Example: Stateful Connection to KiCad\n\
         ---------------------------------------------------\n\
         Running on OS: {} {}\n",
        os_name, os_release
    );
    println!("{}", banner_string);
}

#[derive(Debug, Clone, PartialEq)]
enum ClientState {
    Disconnected,
    Connecting,
    Connected,
    Error(String),
    Terminated,
}

struct Supervisor {
    state: ClientState,
    kicad: Option<KiCad>,
}

impl Supervisor {
    fn new() -> Self {
        let kicad = None;
        let state = ClientState::Disconnected;
        Self { state, kicad }
    }

    fn process_pcb_document(&self) -> Result<BoardProjectData, String> {
        if let Some(kicad) = &self.kicad {
            match kicad.get_open_board() {
                Ok(board) => {
                    println!("Current board: {:?}", board);

                    let board_project_data = BoardProjectData {
                        project: board.project_name().unwrap_or("Unknown").to_string(),
                        path: board.project_path().unwrap_or("Unknown").to_string(),
                        name: board.name().to_string(),
                    };

                    Ok(board_project_data)
                }
                Err(e) => Err(format!("Failed to get current board: {:?}", e)),
            }
        } else {
            Err("KiCad instance is not initialized.".to_string())
        }
    }

    fn try_connect(&mut self) -> Result<(), String> {
        let connection_attempt = KiCad::new(KiCadConnectionConfig {
            client_name: String::from("kicad-stateful-client"),
            ..Default::default()
        });

        match connection_attempt {
            Ok(instance) => {
                self.kicad = Some(instance);
                self.state = ClientState::Connected;
                Ok(())
            }
            Err(e) => {
                self.state = ClientState::Error(format!("{:?}", e));
                Err(format!("Failed to connect to KiCad: {:?}", e))
            }
        }
    }

    fn update(&mut self) {
        match &mut self.state {
            ClientState::Disconnected => {
                println!("Client is disconnected.");
                self.state = ClientState::Connecting;
            }
            ClientState::Connecting => {
                println!("Client is trying to connect to KiCad...");
                let try_connect_result = self.try_connect();
                if let Err(err) = try_connect_result {
                    self.state = ClientState::Error(err);
                } else if let Some(kicad) = &self.kicad {
                    println!(
                        "Connected to KiCad version: {}",
                        kicad.get_version().unwrap()
                    );
                    match self.process_pcb_document() {
                        Ok(board_data) => {
                            println!("\nProcessing successful.\n");

                            let mut table = Table::new();
                            table.add_row(row![b->"Property", b->"Value"]);
                            table.add_row(row!["Project Name", board_data.project]);
                            table.add_row(row!["Project Path", board_data.path]);
                            table.add_row(row!["Board Name", board_data.name]);

                            table.printstd();
                        }
                        Err(e) => {
                            println!("Error obtaining PCB board: {}", e);
                            self.state = ClientState::Error(e);
                        }
                    }

                    self.state = ClientState::Connected;
                }
            }
            ClientState::Connected => {
                // In a real application, this state would handle ongoing operations
                // For example:
                // get_board_tracks();
                // get_board_components();
            }
            ClientState::Error(err) => {
                println!("Client encountered an error: {}", err);
                println!("Client is now terminating.");
                self.state = ClientState::Terminated;
            }

            ClientState::Terminated => {
                println!("Client has terminated.");
            }
        }
    }
}

fn main() {
    display_banner_message();

    let mut supervisor = Supervisor::new();

    loop {
        supervisor.update();

        if supervisor.state == ClientState::Terminated {
            break;
        }

        std::thread::sleep(std::time::Duration::from_secs(2));
    }
    println!("Exiting the KiCad stateful client example.");
}
