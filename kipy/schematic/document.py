# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Document operations: create, open, close, save, revert.
"""

from typing import TYPE_CHECKING, List, Optional

from google.protobuf.empty_pb2 import Empty
from kipy.proto.common.commands import editor_commands_pb2
from kipy.proto.common.types import DocumentType, DocumentSpecifier

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class DocumentOperations:
    """Document management operations."""

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    # =========================================================================
    # Document Query Operations
    # =========================================================================

    def get_open_documents(
        self,
        doc_type: int = DocumentType.DOCTYPE_SCHEMATIC,
    ) -> List[DocumentSpecifier]:
        """Get all open documents of the specified type.

        Args:
            doc_type: Document type (default: schematic)
                - DocumentType.DOCTYPE_SCHEMATIC
                - DocumentType.DOCTYPE_PCB
                - DocumentType.DOCTYPE_PROJECT

        Returns:
            List of DocumentSpecifier objects

        Example:
            >>> from kipy.proto.common.types import DocumentType
            >>> docs = sch.document.get_open_documents(DocumentType.DOCTYPE_SCHEMATIC)
            >>> for doc in docs:
            ...     print(f"Open: {doc.board_filename}")
        """
        cmd = editor_commands_pb2.GetOpenDocuments()
        cmd.type = doc_type
        response = self._kicad.send(cmd, editor_commands_pb2.GetOpenDocumentsResponse)
        return list(response.documents)

    # =========================================================================
    # Document Create/Open Operations
    # =========================================================================

    def create(
        self,
        path: str,
        template_path: Optional[str] = None,
    ) -> DocumentSpecifier:
        """Create a new schematic document.

        Args:
            path: Full path for the new file
            template_path: Optional template file to copy from

        Returns:
            DocumentSpecifier for the created document

        Example:
            >>> doc = sch.document.create("/path/to/new_schematic.kicad_sch")
        """
        cmd = editor_commands_pb2.CreateDocument()
        cmd.type = DocumentType.DOCTYPE_SCHEMATIC
        cmd.path = path
        if template_path:
            cmd.template_path = template_path

        response = self._kicad.send(cmd, editor_commands_pb2.CreateDocumentResponse)
        return response.document

    def open(self, path: str) -> DocumentSpecifier:
        """Open an existing schematic document.

        Args:
            path: Full path to the schematic file

        Returns:
            DocumentSpecifier for the opened document

        Example:
            >>> doc = sch.document.open("/path/to/existing.kicad_sch")
        """
        cmd = editor_commands_pb2.OpenDocument()
        cmd.type = DocumentType.DOCTYPE_SCHEMATIC
        cmd.path = path

        response = self._kicad.send(cmd, editor_commands_pb2.OpenDocumentResponse)
        return response.document

    # =========================================================================
    # Document Save Operations
    # =========================================================================

    def save(self) -> None:
        """Save the current schematic to disk.

        Example:
            >>> sch.document.save()
        """
        cmd = editor_commands_pb2.SaveDocument()
        cmd.document.CopyFrom(self._doc)
        self._kicad.send(cmd, Empty)

    def save_copy(self, path: str) -> None:
        """Save a copy of the schematic to a new location.

        The new copy is NOT opened; the current document remains active.

        Args:
            path: Path for the copy

        Example:
            >>> sch.document.save_copy("/path/to/backup.kicad_sch")
        """
        cmd = editor_commands_pb2.SaveCopyOfDocument()
        cmd.document.CopyFrom(self._doc)
        cmd.path = path
        self._kicad.send(cmd, Empty)

    # =========================================================================
    # Document Close/Revert Operations
    # =========================================================================

    def close(
        self,
        save_changes: bool = False,
        force: bool = False,
    ) -> None:
        """Close the current schematic document.

        Args:
            save_changes: If True, save pending changes before closing
            force: If True, close without prompting even with unsaved changes

        Example:
            >>> sch.document.close(save_changes=True)
            >>> # Or force close without saving:
            >>> sch.document.close(force=True)
        """
        cmd = editor_commands_pb2.CloseDocument()
        cmd.document.CopyFrom(self._doc)
        cmd.save_changes = save_changes
        cmd.force = force
        self._kicad.send(cmd, Empty)

    def revert(self) -> None:
        """Revert the schematic to the last saved state.

        Discards all unsaved changes and reloads from disk.

        Example:
            >>> sch.document.revert()
        """
        cmd = editor_commands_pb2.RevertDocument()
        cmd.document.CopyFrom(self._doc)
        self._kicad.send(cmd, Empty)
