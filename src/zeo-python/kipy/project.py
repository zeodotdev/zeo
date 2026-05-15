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

from typing import Dict, List, Union, overload

from kipy.client import KiCadClient
from kipy.project_types import NetClass, TextVariables
from kipy.proto.common.types import DocumentSpecifier, MapMergeMode, DocumentType
from kipy.proto.common.commands import project_commands_pb2
from kipy.proto.common.types import project_settings_pb2
from google.protobuf.empty_pb2 import Empty


class Project:
    def __init__(self, kicad: KiCadClient, document: DocumentSpecifier):
        self._kicad = kicad
        self._doc = document

        # TODO clean this up; no identifier for project right now
        if self._doc.type != DocumentType.DOCTYPE_PROJECT:
            self._doc.type = DocumentType.DOCTYPE_PROJECT

    @property
    def document(self) -> DocumentSpecifier:
        return self._doc

    @property
    def name(self) -> str:
        """Returns the name of the project"""
        return self._doc.project.name

    @property
    def path(self) -> str:
        return self._doc.project.path

    def get_net_classes(self) -> List[NetClass]:
        """Get all net classes defined in the project.

        Returns:
            List of NetClass objects

        Example:
            >>> net_classes = project.get_net_classes()
            >>> for nc in net_classes:
            ...     print(f"{nc.name}: {nc.wire_width_mm}mm")
        """
        command = project_commands_pb2.GetNetClasses()
        command.document.CopyFrom(self._doc)
        response = self._kicad.send(command, project_commands_pb2.NetClassesResponse)
        return [NetClass(p) for p in response.net_classes]

    def create_net_class(self, net_class: NetClass) -> None:
        """Create a new net class.

        Args:
            net_class: NetClass object to create

        Raises:
            ApiError: If net class already exists or name is invalid

        Example:
            >>> from kipy.project_types import NetClass
            >>> nc = NetClass()
            >>> nc.name = "HighSpeed"
            >>> nc.wire_width_mm = 0.3
            >>> project.create_net_class(nc)
        """
        from kipy.client import ApiError

        command = project_commands_pb2.CreateNetClass()
        command.document.CopyFrom(self._doc)
        command.net_class.CopyFrom(net_class.proto)
        response = self._kicad.send(command, project_commands_pb2.CreateNetClassResponse)

        if response.status != project_commands_pb2.CNCS_OK:
            status_messages = {
                project_commands_pb2.CNCS_ALREADY_EXISTS: f"Net class '{net_class.name}' already exists",
                project_commands_pb2.CNCS_INVALID_NAME: "Invalid net class name",
            }
            error_msg = response.error_message or status_messages.get(
                response.status, "Unknown error"
            )
            raise ApiError(f"Failed to create net class: {error_msg}")

    def delete_net_class(self, name: str) -> None:
        """Delete a net class by name.

        Note: Cannot delete the "Default" net class.

        Args:
            name: Name of the net class to delete

        Raises:
            ApiError: If net class not found or is Default

        Example:
            >>> project.delete_net_class("HighSpeed")
        """
        from kipy.client import ApiError

        command = project_commands_pb2.DeleteNetClass()
        command.document.CopyFrom(self._doc)
        command.name = name
        response = self._kicad.send(command, project_commands_pb2.DeleteNetClassResponse)

        if response.status != project_commands_pb2.DNCS_OK:
            status_messages = {
                project_commands_pb2.DNCS_NOT_FOUND: f"Net class '{name}' not found",
                project_commands_pb2.DNCS_CANNOT_DELETE_DEFAULT: "Cannot delete the Default net class",
            }
            error_msg = response.error_message or status_messages.get(
                response.status, "Unknown error"
            )
            raise ApiError(f"Failed to delete net class: {error_msg}")

    def update_net_class(self, net_class: NetClass) -> None:
        """Update an existing net class.

        Args:
            net_class: NetClass object with updated properties

        Example:
            >>> nc = project.get_net_classes()[0]
            >>> nc.wire_width = 500000  # 0.5mm
            >>> project.update_net_class(nc)
        """
        self.set_net_classes([net_class], merge_mode=MapMergeMode.MMM_MERGE)

    def set_net_classes(
        self,
        net_classes: List[NetClass],
        merge_mode: MapMergeMode.ValueType = MapMergeMode.MMM_MERGE,
    ) -> None:
        """Set net classes in the project.

        Args:
            net_classes: List of NetClass objects
            merge_mode: MMM_MERGE to update/add, MMM_REPLACE to replace all

        Example:
            >>> project.set_net_classes([nc1, nc2], merge_mode=MapMergeMode.MMM_MERGE)
        """
        command = project_commands_pb2.SetNetClasses()
        command.document.CopyFrom(self._doc)
        for nc in net_classes:
            command.net_classes.append(nc.proto)
        command.merge_mode = merge_mode
        self._kicad.send(command, Empty)

    @overload
    def expand_text_variables(self, text: str) -> str:
        ...

    @overload
    def expand_text_variables(self, text: List[str]) -> List[str]:
        ...

    def expand_text_variables(self, text: Union[str, List[str]]) -> Union[str, List[str]]:
        command = project_commands_pb2.ExpandTextVariables()
        command.document.CopyFrom(self._doc)
        if isinstance(text, list):
            command.text.extend(text)
        else:
            command.text.append(text)
        response = self._kicad.send(command, project_commands_pb2.ExpandTextVariablesResponse)
        return (
            [text for text in response.text]
            if isinstance(text, list)
            else response.text[0]
            if len(response.text) > 0
            else ""
        )

    def get_text_variables(self) -> TextVariables:
        command = project_commands_pb2.GetTextVariables()
        command.document.CopyFrom(self._doc)
        response = self._kicad.send(command, project_settings_pb2.TextVariables)
        return TextVariables(response)

    def set_text_variables(
        self, variables: TextVariables, merge_mode: MapMergeMode.ValueType = MapMergeMode.MMM_MERGE
    ):
        command = project_commands_pb2.SetTextVariables()
        command.document.CopyFrom(self._doc)
        command.merge_mode = merge_mode
        command.variables.CopyFrom(variables.proto)
        self._kicad.send(command, Empty)

    def get_net_class_assignments(self) -> List[Dict[str, str]]:
        """Get all net class pattern assignments.

        Returns:
            List of assignment dicts with 'pattern' and 'netclass' keys

        Example:
            >>> assignments = project.get_net_class_assignments()
            >>> for a in assignments:
            ...     print(f"{a['pattern']} -> {a['netclass']}")
        """
        command = project_commands_pb2.GetNetClassAssignments()
        command.document.CopyFrom(self._doc)
        response = self._kicad.send(
            command, project_commands_pb2.GetNetClassAssignmentsResponse
        )
        return [
            {"pattern": a.pattern, "netclass": a.netclass}
            for a in response.assignments
        ]

    def set_net_class_assignments(self, assignments: List[Dict[str, str]]) -> None:
        """Replace all net class pattern assignments.

        Args:
            assignments: List of dicts with 'pattern' and 'netclass' keys

        Example:
            >>> project.set_net_class_assignments([
            ...     {"pattern": "VCC*", "netclass": "Power"},
            ...     {"pattern": "GND*", "netclass": "Power"},
            ...     {"pattern": "CLK*", "netclass": "HighSpeed"},
            ... ])
        """
        command = project_commands_pb2.SetNetClassAssignments()
        command.document.CopyFrom(self._doc)
        for a in assignments:
            assignment = command.assignments.add()
            assignment.pattern = a.get("pattern", "")
            assignment.netclass = a.get("netclass", "Default")
        self._kicad.send(command, Empty)
