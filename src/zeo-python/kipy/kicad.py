# Copyright The KiCad Developers
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Classes for interacting with KiCad at a high level"""

import glob
import json
import logging
import os
import platform
import random
import string
from tempfile import gettempdir
from typing import Optional, Sequence, Union
from google.protobuf.empty_pb2 import Empty

from kipy.board import Board
from kipy.schematic import Schematic
from kipy.client import KiCadClient, ApiError
from kipy.common_types import Text, TextBox, CompoundShape
from kipy.errors import FutureVersionError
from kipy.geometry import Box2
from kipy.project import Project
from kipy.proto.common import commands
from kipy.proto.common.types import base_types_pb2, DocumentType, DocumentSpecifier
from kipy.proto.common.commands import base_commands_pb2
from kipy.proto.common.commands import project_commands_pb2
from kipy.kicad_api_version import KICAD_API_VERSION


def _default_socket_path() -> str:
    path = os.environ.get('KICAD_API_SOCKET')
    if path is not None:
        return path
    if platform.system() == 'Windows':
        return f'ipc://{gettempdir()}\\kicad\\api.sock'
    else:
        # Check for default socket path of KiCad flatpak on flathub
        home = os.environ.get('HOME')
        if home is not None:
            flatpak_socket_path = f'{home}/.var/app/org.kicad.KiCad/cache/tmp/kicad/api.sock'
            if os.path.exists(flatpak_socket_path):
                return f'ipc://{flatpak_socket_path}'

        return 'ipc:///tmp/kicad/api.sock'

def _random_client_name() -> str:
    return 'anonymous-'+''.join(random.choices(string.ascii_lowercase + string.digits, k=8))

def _default_kicad_token() -> str:
    token = os.environ.get('KICAD_API_TOKEN')
    if token is not None:
        return token
    return ""

class KiCadVersion:
    def __init__(self, major: int, minor: int, patch: int, full_version: str):
        self.major = major
        self.minor = minor
        self.patch = patch
        self.full_version = full_version

    @staticmethod
    def from_proto(proto: base_types_pb2.KiCadVersion) -> 'KiCadVersion':
        return KiCadVersion(proto.major, proto.minor, proto.patch, proto.full_version)

    @staticmethod
    def from_git_describe(describe: str) -> 'KiCadVersion':
        parts = describe.split('-')
        version_part = parts[0]

        try:
            major, minor, patch = map(int, version_part.split('.'))
        except ValueError:
            return KiCadVersion(0, 0, 0, describe)

        if len(parts) > 1:
            additional_info = '-'.join(parts[1:])
            return KiCadVersion(major, minor, patch, f"{version_part}-{additional_info}")

        return KiCadVersion(major, minor, patch, f"{version_part}")

    def __repr__(self):
        return f"{self.major}.{self.minor}.{self.patch} ({self.full_version})"

    def __eq__(self, other):
        if not isinstance(other, KiCadVersion):
            return NotImplemented

        return (
            (self.major, self.minor, self.patch) == (other.major, other.minor, other.patch)
            )

    def __lt__(self, other):
        if not isinstance(other, KiCadVersion):
            return NotImplemented
        return (self.major, self.minor, self.patch) < (other.major, other.minor, other.patch)

    def __le__(self, other):
        return self == other or self < other

    def __gt__(self, other):
        return not self <= other

    def __ge__(self, other):
        return not self < other

_logger = logging.getLogger("kipy")


class KiCad:
    def __init__(self, socket_path: Optional[str]=None,
                 client_name: Optional[str]=None,
                 kicad_token: Optional[str]=None,
                 timeout_ms: int=2000):
        """Creates a connection to a running KiCad instance

        :param socket_path: The path to the IPC API socket (leave default to read from the
            KICAD_API_SOCKET environment variable, which will be set automatically by KiCad when
            launching API plugins, or to use the default platform-dependent socket path if the
            environment variable is not set).
        :param client_name: A unique name identifying this plugin instance.  Leave default to
            generate a random client name.
        :param kicad_token: A token that can be provided to the client to uniquely identify a
            KiCad instance.  Leave default to read from the KICAD_API_TOKEN environment variable.
        :param timeout_ms: The maximum time to wait for a response from KiCad, in milliseconds
        """
        if socket_path is None:
            socket_path = _default_socket_path()
        if client_name is None:
            client_name = _random_client_name()
        if kicad_token is None:
            kicad_token = _default_kicad_token()
        self._client = KiCadClient(socket_path, client_name, kicad_token, timeout_ms)
        self._exec_client: Optional[KiCadClient] = None

    @staticmethod
    def from_client(client: KiCadClient):
        """Creates a KiCad object from an existing KiCad client"""
        k = KiCad.__new__(KiCad)
        k._client = client
        k._exec_client = None
        return k

    def get_version(self) -> KiCadVersion:
        """Returns the KiCad version as a string, including any package-specific info"""
        response = self._client.send(commands.GetVersion(), commands.GetVersionResponse)
        return KiCadVersion.from_proto(response.version)

    def get_api_version(self) -> KiCadVersion:
        """Returns the version of KiCad that this library was built against"""
        return KiCadVersion.from_git_describe(KICAD_API_VERSION)

    def check_version(self) -> bool:
        """Checks if the connected KiCad version matches the version this library was built against"""
        kicad_version = self.get_version()
        api_version = self.get_api_version()

        if kicad_version > api_version:
            raise FutureVersionError(
                f"Warning: Connected KiCad version ({kicad_version}) is newer than "
                f"the API version of kicad-python ({api_version})"
            )

        return True

    def ping(self):
        self._client.send(commands.Ping(), Empty)

    def get_kicad_binary_path(self, binary_name: str) -> str:
        """Returns the full path to the given KiCad binary

        :param binary_name: The short name of the binary, such as `kicad-cli` or `kicad-cli.exe`.
                            If on Windows, an `.exe` extension will be assumed if not present.
        :return: The full path to the binary
        """
        cmd = commands.GetKiCadBinaryPath()
        cmd.binary_name = binary_name
        return self._client.send(cmd, commands.PathResponse).path

    def get_plugin_settings_path(self, identifier: str) -> str:
        """Return a writeable path that a plugin can use for storing persistent data such as
        configuration files, etc.  This path may not yet exist; actual creation of the directory
        for a given plugin is up to the plugin itself.  Files in this path will not be modified if
        the plugin is uninstalled or upgraded.

        :param identifier: should be the full identifier of the plugin (e.g. org.kicad.myplugin)
        :return: a path, with local separators, that the plugin can use for storing settings
        """
        cmd = commands.GetPluginSettingsPath()
        cmd.identifier = identifier
        return self._client.send(cmd, commands.StringResponse).response

    def run_action(self, action: str):
        """Runs a KiCad tool action, if it is available

        WARNING: This is an unstable API and is not intended for use other
        than by API developers. KiCad does not guarantee the stability of
        action names, and running actions may have unintended side effects.
        :param action: the name of a KiCad TOOL_ACTION
        :return: a value from the KIAPI.COMMON.COMMANDS.RUN_ACTION_STATUS enum
        """
        return self._client.send(commands.RunAction(), commands.RunActionResponse)

    def get_open_documents(self, doc_type: DocumentType.ValueType) -> Sequence[DocumentSpecifier]:
        """Retrieves a list of open documents matching the given type"""
        command = commands.GetOpenDocuments()
        command.type = doc_type
        response = self._client.send(command, commands.GetOpenDocumentsResponse)
        return response.documents

    def launch_editor(self, doc_type: str) -> None:
        """Launch an editor via the project manager's KIWAY (non-standalone mode).

        No-op if the editor is already open.

        :param doc_type: 'schematic' or 'pcb'
        """
        cmd = project_commands_pb2.LaunchEditor()
        if doc_type == "schematic":
            cmd.doc_type = DocumentType.DOCTYPE_SCHEMATIC
        elif doc_type == "pcb":
            cmd.doc_type = DocumentType.DOCTYPE_PCB
        else:
            raise ValueError(f"doc_type must be 'schematic' or 'pcb', got '{doc_type}'")
        self._client.send(
            cmd, project_commands_pb2.LaunchEditorResponse, timeout_ms=10000,
        )

    def get_instructions(self) -> str:
        """Fetch the core agent instructions from the C++ app via IPC.

        :return: Core instructions markdown string
        """
        cmd = project_commands_pb2.GetInstructions()
        response = self._client.send(cmd, project_commands_pb2.GetInstructionsResponse)
        return response.instructions_md

    def get_tool_schemas(self) -> list[dict]:
        """Fetch tool schemas from the C++ app via IPC.

        :return: List of tool definition dicts from the manifest
        """
        cmd = project_commands_pb2.GetToolSchemas()
        response = self._client.send(cmd, project_commands_pb2.GetToolSchemasResponse)
        return json.loads(response.manifest_json)

    def execute_tool(self, tool_name: str, tool_args_json: str = "") -> dict:
        """Execute a tool via the project manager's embedded Python executor.

        :param tool_name: Tool name (e.g. 'sch_add', 'pcb_place')
        :param tool_args_json: JSON-encoded tool arguments
        :return: dict with 'success', 'result_json', and 'error_message' keys
        """
        cmd = project_commands_pb2.ExecuteTool()
        cmd.tool_name = tool_name
        cmd.tool_args_json = tool_args_json

        # Use the exec socket for tool execution to avoid blocking the primary socket
        client = self._get_exec_client()

        # C++ side has a 30s Python timeout; use 45s to allow for IPC overhead
        response = client.send(
            cmd, project_commands_pb2.ExecuteToolResponse, timeout_ms=45000
        )

        return {
            "success": response.success,
            "result_json": response.result_json,
            "error_message": response.error_message,
        }

    def _get_exec_client(self) -> KiCadClient:
        """Get or create a KiCadClient for the exec socket.

        The exec socket path is derived from the primary socket by replacing
        '.sock' with '-exec.sock'. Falls back to the primary client if the
        exec socket doesn't exist or can't connect.
        """
        if self._exec_client is not None:
            return self._exec_client

        primary_path = self._client._socket_path  # e.g. "ipc:///tmp/kicad/api.sock"

        # Derive exec socket path: replace '.sock' with '-exec.sock'
        if primary_path.endswith(".sock"):
            exec_path = primary_path[:-5] + "-exec.sock"
        else:
            # Can't derive exec path — use primary
            _logger.debug("Cannot derive exec socket path from %s, using primary", primary_path)
            return self._client

        try:
            self._exec_client = KiCadClient(
                exec_path, self._client.name, self._client.kicad_token, self._client._timeout_ms
            )
            _logger.debug("Created exec client on %s", exec_path)
            return self._exec_client
        except Exception as e:
            _logger.debug("Failed to create exec client on %s: %s, using primary", exec_path, e)
            return self._client

    def get_project(self, document: DocumentSpecifier) -> Project:
        """Returns a Project object for the given document"""
        # Make a copy of document to avoid Project.__init__ mutating the caller's document type
        doc_copy = DocumentSpecifier()
        doc_copy.CopyFrom(document)
        return Project(self._client, doc_copy)

    def get_board(self) -> Board:
        """Retrieves a reference to the PCB open in KiCad, if one exists"""
        try:
            docs = self.get_open_documents(DocumentType.DOCTYPE_PCB)
        except ApiError as e:
            # If the current server doesn't handle PCBs (e.g. we are connected to Eeschema),
            # try to discover other running KiCad instances (e.g. Pcbnew)
            if "no handler available" in str(e):
                discovered = self._discover_board_server()
                if discovered:
                    return discovered
            raise

        if len(docs) == 0:
            raise ApiError("Expected to be able to retrieve at least one board")
        return Board(self._client, docs[0])

    def get_schematic(self) -> Schematic:
        """Retrieves a reference to the Schematic open in KiCad, if one exists"""
        try:
            docs = self.get_open_documents(DocumentType.DOCTYPE_SCHEMATIC)
        except ApiError as e:
            # If the current server doesn't handle schematics (e.g. we are connected to Manager),
            # try to discover other running KiCad instances (e.g. Eeschema)
            if "no handler available" in str(e):
                discovered = self._discover_schematic_server()
                if discovered:
                    return discovered
            raise

        if len(docs) == 0:
            raise ApiError("Expected to be able to retrieve at least one schematic")
        return Schematic(self._client, docs[0])

    def get_mbs_schematic(self) -> Schematic:
        """Retrieves a reference to the multi-board container schematic (.kicad_mbs).

        Returns a `Schematic` because an MBS file IS a schematic on disk —
        the document type differs (`DOCTYPE_MBS_SCHEMATIC`) so the API server
        deterministically routes commands to the MBSCH editor frame even
        when a regular schematic editor is open in the same process.
        Existing operations (symbols, wiring, labels, etc.) work unchanged
        on the returned object. MBS-only commands live under `.multi_board`.
        """
        docs = self.get_open_documents(DocumentType.DOCTYPE_MBS_SCHEMATIC)

        if len(docs) == 0:
            raise ApiError(
                "No multi-board (MBS) schematic editor is open. Open the "
                "container project's .kicad_mbs file first."
            )
        return Schematic(self._client, docs[0])

    def get_schematic_by_project_path(self, abs_project_path: str) -> Schematic:
        """Retrieve the open Schematic editor for a specific sub-project.

        On a multi-board project several SCH editors may be open in peer
        windows (one per sub-project). This method enumerates every open
        DOCTYPE_SCHEMATIC document and returns the one whose
        ProjectSpecifier.path equals ``abs_project_path`` (case-insensitive
        on macOS / Windows). Use the ``absolute_path`` field from
        ``check_status``'s ``sub_projects[]`` array as the argument.

        :param abs_project_path: Absolute path to the target ``.kicad_pro``.
        :raises ApiError: when no open schematic editor matches.
        """
        if not abs_project_path:
            raise ApiError("get_schematic_by_project_path requires a non-empty path")

        docs = self.get_open_documents(DocumentType.DOCTYPE_SCHEMATIC)

        target = abs_project_path.casefold()

        for doc in docs:
            doc_path = doc.project.path or ""
            if doc_path.casefold() == target:
                return Schematic(self._client, doc)

        available = [d.project.path for d in docs if d.project.path]
        raise ApiError(
            f"No open schematic editor matches project path '{abs_project_path}'. "
            f"Open editors: {available or 'none'}"
        )

    def get_board_by_project_path(self, abs_project_path: str) -> Board:
        """Retrieve the open Board editor for a specific sub-project.

        Same selection semantics as ``get_schematic_by_project_path``, but
        for ``DOCTYPE_PCB`` documents.

        :param abs_project_path: Absolute path to the target ``.kicad_pro``.
        :raises ApiError: when no open PCB editor matches.
        """
        if not abs_project_path:
            raise ApiError("get_board_by_project_path requires a non-empty path")

        docs = self.get_open_documents(DocumentType.DOCTYPE_PCB)

        target = abs_project_path.casefold()

        for doc in docs:
            doc_path = doc.project.path or ""
            if doc_path.casefold() == target:
                return Board(self._client, doc)

        available = [d.project.path for d in docs if d.project.path]
        raise ApiError(
            f"No open PCB editor matches project path '{abs_project_path}'. "
            f"Open editors: {available or 'none'}"
        )

    def _get_socket_candidates(self) -> list:
        """Return all candidate IPC socket paths by scanning platform socket directories."""
        if platform.system() == 'Windows':
            socket_dirs = [os.path.join(gettempdir(), 'kicad')]
        elif platform.system() == 'Darwin':
            # Use canonical path only; /tmp is a symlink to /private/tmp on macOS
            socket_dirs = ['/private/tmp/kicad']
        else:
            socket_dirs = ['/tmp/kicad']

        candidates = []
        for socket_dir in socket_dirs:
            candidates.extend(glob.glob(os.path.join(socket_dir, "api*.sock")))

        _logger.debug("Scanning for servers in: %s", socket_dirs)
        _logger.debug("Found candidates: %s", candidates)
        return candidates

    def _discover_server(self, doc_type, wrapper_cls):
        """Scan local sockets for a running editor with the given document type.

        :param doc_type: DocumentType value (DOCTYPE_SCHEMATIC or DOCTYPE_PCB)
        :param wrapper_cls: Class to wrap the result (Schematic or Board)
        """
        for sock in self._get_socket_candidates():
            try:
                _logger.debug("Trying to connect to %s...", sock)
                client = KiCadClient(f"ipc://{sock}", self._client.name, "", 1000)
                cmd = commands.GetOpenDocuments()
                cmd.type = doc_type
                response = client.send(cmd, commands.GetOpenDocumentsResponse)
                _logger.debug("Connection successful, found %d document(s).", len(response.documents))
                if len(response.documents) > 0:
                    return wrapper_cls(client, response.documents[0])
            except Exception as e:
                _logger.debug("Failed to connect/query %s: %s", sock, e)

        return None

    def _discover_schematic_server(self) -> Optional[Schematic]:
        return self._discover_server(DocumentType.DOCTYPE_SCHEMATIC, Schematic)

    def _discover_board_server(self) -> Optional[Board]:
        return self._discover_server(DocumentType.DOCTYPE_PCB, Board)

    # Utility functions

    def get_text_extents(self, text: Text) -> Box2:
        """Returns the bounding box of the given text object"""
        cmd = base_commands_pb2.GetTextExtents()
        cmd.text.CopyFrom(text.proto)
        reply = self._client.send(cmd, base_types_pb2.Box2)
        return Box2.from_proto(reply)

    def get_text_as_shapes(
        self, texts: Union[Text, TextBox, Sequence[Union[Text, TextBox]]]
    ) -> list[CompoundShape]:
        """Returns polygonal shapes representing the given text objects"""
        if isinstance(texts, Text) or isinstance(texts, TextBox):
            texts = [texts]

        cmd = base_commands_pb2.GetTextAsShapes()
        for t in texts:
            inner = base_commands_pb2.TextOrTextBox()
            if isinstance(t, Text):
                inner.text.CopyFrom(t.proto)
            else:
                inner.textbox.CopyFrom(t.proto)
            cmd.text.append(inner)

        reply = self._client.send(cmd, base_commands_pb2.GetTextAsShapesResponse)

        return [CompoundShape(entry.shapes) for entry in reply.text_with_shapes]
