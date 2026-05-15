# Copyright The KiCad Developers
# SPDX-License-Identifier: MIT

"""
Multi-board schematic (MBS) operations.

These methods are valid only when the wrapped Schematic represents a `.kicad_mbs`
file (i.e. the document type is DOCTYPE_MBS_SCHEMATIC). Use
``kicad.get_mbs_schematic()`` to obtain such a Schematic.
"""

from typing import TYPE_CHECKING, List

from kipy.proto.schematic import schematic_commands_pb2

if TYPE_CHECKING:
    from kipy.schematic.base import Schematic


class MultiBoardOperations:
    """Read-only inspection of multi-board container state.

    Slice 1 covers the read surface only (blocks, cross-board nets, container
    info). Edit operations (add_block, refresh, sync_to_pcb, run_erc) land in
    later slices.
    """

    def __init__(self, schematic: "Schematic"):
        self._sch = schematic

    @property
    def _kicad(self):
        return self._sch._kicad

    @property
    def _doc(self):
        return self._sch._doc

    def get_blocks(self) -> List:
        """Return every SCH_MODULE_BLOCK on the active MBS sheet.

        Each block carries: `id` (KIID), `mbs_reference` ("B1"), `component_ref`
        ("J1"), `sub_project_uuid`, `sub_project_path`, `display_name`,
        `position`, `size`, and the list of `pins` (number, label, side,
        electrical type).
        """
        cmd = schematic_commands_pb2.GetModuleBlocks()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(
            cmd, schematic_commands_pb2.GetModuleBlocksResponse
        )
        return list(response.blocks)

    def get_cross_board_nets(self) -> List:
        """Return every cross-board net declared in the enclosing container.

        Each net has a canonical `name` and a list of `endpoints`, where each
        endpoint identifies a (sub_project_uuid, component_ref, pin_number)
        tuple. Empty list when the project is standalone (not multi-board).
        """
        cmd = schematic_commands_pb2.GetCrossBoardNets()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(
            cmd, schematic_commands_pb2.GetCrossBoardNetsResponse
        )
        return list(response.nets)

    def get_container_info(self):
        """Return metadata about the multi-board container that owns this MBS.

        Fields: `container_pro_path`, `mbs_file_path`, `container_name`, and
        the list of `sub_projects` (each with uuid, relative_path,
        absolute_path, name).

        Empty `container_pro_path` indicates this schematic is not part of a
        multi-board container — should never happen for an MBS doc, but the
        caller can defensively check.
        """
        cmd = schematic_commands_pb2.GetMultiBoardContainerInfo()
        cmd.document.CopyFrom(self._doc)
        response = self._kicad.send(
            cmd,
            schematic_commands_pb2.GetMultiBoardContainerInfoResponse,
        )
        return response

    def refresh_from_sub_projects(self, dry_run: bool = True, apply_indices=None):
        """Re-scan every sub-project's connector pins, preview or apply the diff.

        Two modes:
          * ``dry_run=True`` (default): compute the diff and return it via
            ``proposed_changes`` without mutating the MBS. Use this to show
            the user what would change before committing.
          * ``dry_run=False``: apply the changes. Pass ``apply_indices``
            (list of int) to apply only a subset (mirrors the desktop
            dialog's checkbox UX); pass ``None`` or an empty list to
            apply every change.

        Returns a response with:
          * ``dry_run`` (bool) — echoes the request mode
          * ``proposed_changes`` (repeated) — the full diff list, indexed
          * ``blocks_added`` / ``blocks_removed`` / ``pins_added`` /
            ``pins_removed`` / ``pins_renamed`` / ``paths_updated`` /
            ``uuids_stamped`` (int) — applied counts (0 in dry-run mode)
          * ``summary`` (str) — human-readable summary
        """
        cmd = schematic_commands_pb2.RefreshMbsFromSubProjects()
        cmd.document.CopyFrom(self._doc)
        cmd.dry_run = bool(dry_run)

        if apply_indices:
            cmd.apply_indices.extend(int(i) for i in apply_indices)

        return self._kicad.send(
            cmd, schematic_commands_pb2.RefreshMbsFromSubProjectsResponse
        )

    def update_pin(self, pin_uuid: str, *, text=None, electrical_type=None,
                   side=None, position=None):
        """Update an MBS module pin's settable attributes.

        Args:
            pin_uuid: Pin UUID (from get_blocks() -> blocks[].pins[].id.value).
            text: Optional new label text (cross-board net name).
            electrical_type: Optional ElectricalPinType enum value (e.g.
                kiapi.common.types.EPT_OUTPUT).
            side: Optional SheetPinSide enum value (SPS_LEFT/RIGHT/TOP/BOTTOM).
            position: Optional (x_nm, y_nm) tuple for the new absolute position.

        Returns:
            True if any attribute changed, False if pin not found or no-op.
        """
        from kipy.proto.common.types import base_types_pb2

        cmd = schematic_commands_pb2.UpdateModulePin()
        cmd.document.CopyFrom(self._doc)
        cmd.pin_id.value = str(pin_uuid)

        if text is not None:
            cmd.text = str(text)

        if electrical_type is not None:
            cmd.electrical_type = electrical_type

        if side is not None:
            cmd.side = side

        if position is not None:
            cmd.position.x_nm = int(position[0])
            cmd.position.y_nm = int(position[1])

        response = self._kicad.send(
            cmd, schematic_commands_pb2.UpdateModulePinResponse
        )
        return response.updated

    def delete_pin(self, pin_uuid: str) -> bool:
        """Delete a single MBS module pin (without touching the parent block)."""
        cmd = schematic_commands_pb2.DeleteModulePin()
        cmd.document.CopyFrom(self._doc)
        cmd.pin_id.value = str(pin_uuid)
        response = self._kicad.send(
            cmd, schematic_commands_pb2.DeleteModulePinResponse
        )
        return response.deleted

    def get_rules(self):
        """Read the multi_board rule sets from the container .kicad_pro.

        Returns the GetMbsRulesResponse proto. The `rules` field carries
        five repeated lists: min_power_pins, max_length_nm,
        cross_board_diff_pairs, current_rules, voltage_rules. The handler
        reloads from disk before reading so out-of-band edits are visible.
        """
        cmd = schematic_commands_pb2.GetMbsRules()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(
            cmd, schematic_commands_pb2.GetMbsRulesResponse
        )

    def set_rules(self, *, min_power_pins=None, max_length_nm=None,
                  cross_board_diff_pairs=None, current_rules=None,
                  voltage_rules=None) -> bool:
        """Replace one or more multi_board rule sets on the container.

        Each kwarg controls its own rule set independently:
          * Pass a list -> the on-disk set is replaced with those entries
            (an empty list clears the set).
          * Pass None (or omit) -> the set is left untouched.

        Each entry is a dict mirroring the proto fields (e.g.
        {'net_name': '+5V', 'min_pins': 4} for min_power_pins). For
        cross_board_diff_pairs each entry is {'p': 'NET_DP', 'n': 'NET_DN'}.
        """
        cmd = schematic_commands_pb2.SetMbsRules()
        cmd.document.CopyFrom(self._doc)

        if min_power_pins is not None:
            cmd.replace_min_power_pins = True
            for r in min_power_pins:
                msg = cmd.rules.min_power_pins.add()
                msg.net_name = str(r.get('net_name', ''))
                msg.min_pins = int(r.get('min_pins', 0))

        if max_length_nm is not None:
            cmd.replace_max_length_nm = True
            for r in max_length_nm:
                msg = cmd.rules.max_length_nm.add()
                msg.net_name      = str(r.get('net_name', ''))
                msg.max_length_nm = int(r.get('max_length_nm', 0))

        if cross_board_diff_pairs is not None:
            cmd.replace_cross_board_diff_pairs = True
            for r in cross_board_diff_pairs:
                msg = cmd.rules.cross_board_diff_pairs.add()
                msg.p = str(r.get('p', ''))
                msg.n = str(r.get('n', ''))

        if current_rules is not None:
            cmd.replace_current_rules = True
            for r in current_rules:
                msg = cmd.rules.current_rules.add()
                msg.net_name        = str(r.get('net_name', ''))
                msg.expected_amps   = float(r.get('expected_amps', 0.0))
                msg.pin_rating_amps = float(r.get('pin_rating_amps', 0.0))

        if voltage_rules is not None:
            cmd.replace_voltage_rules = True
            for r in voltage_rules:
                msg = cmd.rules.voltage_rules.add()
                msg.net_name                      = str(r.get('net_name', ''))
                msg.expected_amps                 = float(r.get('expected_amps', 0.0))
                msg.max_drop_mv                   = float(r.get('max_drop_mv', 0.0))
                msg.trace_width_um                = float(r.get('trace_width_um', 0.0))
                msg.trace_sheet_r_milliohm_per_sq = float(
                    r.get('trace_sheet_r_milliohm_per_sq', 0.0))
                msg.contact_r_per_pin_milliohm    = float(
                    r.get('contact_r_per_pin_milliohm', 0.0))

        response = self._kicad.send(
            cmd, schematic_commands_pb2.SetMbsRulesResponse
        )
        return response.updated

    def update_block(self, block_uuid: str, *, position=None,
                     mbs_reference=None) -> bool:
        """Update an MBS module block's position and/or mbs_reference."""
        cmd = schematic_commands_pb2.UpdateModuleBlock()
        cmd.document.CopyFrom(self._doc)
        cmd.block_id.value = str(block_uuid)

        if position is not None:
            cmd.position.x_nm = int(position[0])
            cmd.position.y_nm = int(position[1])

        if mbs_reference is not None:
            cmd.mbs_reference = str(mbs_reference)

        response = self._kicad.send(
            cmd, schematic_commands_pb2.UpdateModuleBlockResponse
        )
        return response.updated

    def sync_to_pcb(self):
        """Push the cross-board nets declared on the MBS to each sub-project PCB.

        Equivalent to the MBSCH "Sync to PCB" toolbar button. Fires the same
        text-level pad/net edit used by the toolbar action; safe to call
        multiple times (idempotent on stable input).

        Returns a response with aggregate counts (`sub_projects_touched`,
        `endpoints_applied`, `endpoints_missing`, `nets_renamed`), a list of
        `conflicts` (when several sub-projects use different local net names
        for the same cross-board net — sync picks the alphabetically-first
        as canonical), and a `summary` string.
        """
        cmd = schematic_commands_pb2.SyncCrossBoardNetsToPcb()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(
            cmd, schematic_commands_pb2.SyncCrossBoardNetsToPcbResponse
        )

    # --- MOON-1333 Phase 1 — read-only inspection of MBS replication state ---

    def get_netclass_report(self):
        """Inspect netclass replication state across container + sub-projects.

        Returns the GetMultiBoardNetClassReportResponse proto verbatim. The
        agent-facing tool script (`mbs_get_netclasses.py`) handles unit
        conversion and JSON formatting. Two top-level lists:

          * ``container_classes`` — every netclass on the container, each
            with full ``fields`` and ``status = MBS_NET_CLASS_SOURCE``.
          * ``sub_projects`` — one bucket per entry in the container's
            ``multi_board.boards`` list. Each bucket has ``classes``,
            with ``status`` ∈ {SHARED, LOCAL, CONFLICT}. ``loaded`` flags
            whether the sub-project was already in SETTINGS_MANAGER (we
            transiently load disk-only peers to read them too); a non-
            empty ``read_error`` indicates the disk read failed.

        Status semantics match the desktop Schematic Setup → Net Classes
        Status column (priority differences are correctly ignored).

        Caller must be on an MBSCH document (the active project must be
        a multi-board container); otherwise the IPC returns AS_BAD_REQUEST.
        """
        cmd = schematic_commands_pb2.GetMultiBoardNetClassReport()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(
            cmd, schematic_commands_pb2.GetMultiBoardNetClassReportResponse
        )

    def get_library_report(self):
        """Inspect symbol + footprint library tables across container + sub-projects.

        Returns the GetMultiBoardLibraryReportResponse proto verbatim. The
        agent tool script aggregates this into the mbs_setup `get` response.
        Three top-level lists:

          * ``global_rows`` — user-wide library tables (sym + fp). Reported
            once; identical for every project.
          * ``container_rows`` — the container's project-scoped tables.
          * ``sub_projects`` — one bucket per sub-project. Tables are read
            from each sub-project's ``sym-lib-table`` / ``fp-lib-table``
            file directly, regardless of whether the sub-project is open.

        Per-row ``status`` (SHARED / LOCAL / CONFLICT) is read from the
        ``LIBRARY_TABLE_ROW`` shared / conflict flags MOON-1294 sets during
        reconcile. ``kind`` distinguishes symbol vs footprint rows.

        Caller must be on an MBSCH document.
        """
        cmd = schematic_commands_pb2.GetMultiBoardLibraryReport()
        cmd.document.CopyFrom(self._doc)
        return self._kicad.send(
            cmd, schematic_commands_pb2.GetMultiBoardLibraryReportResponse
        )

    # --- MOON-1333 Phase 2 — netclass mutation on the container ---

    def set_netclass(self, name, **fields):
        """Create or update a netclass on the multi-board container.

        Pre-existing class with the same name -> updated in place;
        new name -> created. The handler runs MultiBoardPropagateNetSettings
        after the mutation so loaded sub-project peers pick up the change
        immediately (silent USE_CONTAINER on conflict, no UI dialog —
        suitable for headless agent calls).

        Args:
            name: The netclass name. Use ``"Default"`` to update the
                default class (cannot be created — it always exists).
            **fields: Optional field overrides. Pass values in IU
                (matching the GetMultiBoardNetClassReport response):
                ``clearance_iu``, ``track_width_iu``, ``via_diameter_iu``,
                ``via_drill_iu``, ``uvia_diameter_iu``, ``uvia_drill_iu``,
                ``diff_pair_width_iu``, ``diff_pair_gap_iu``,
                ``diff_pair_via_gap_iu``, ``wire_width_iu``, ``bus_width_iu``,
                ``line_style``. Plus string fields ``tuning_profile``,
                ``pcb_color_css``, ``schematic_color_css``. Omitted fields
                are cleared on the resulting netclass (consistent with the
                desktop panel's "leave empty -> use container default"
                semantic). Priority is intentionally NOT settable here
                (per-board grid metadata).

        Returns:
            The SetMultiBoardNetClassResponse proto: ``created``,
            ``propagator_ran``, ``sub_projects_touched``, plus per-action
            counters (``classes_added``, ``classes_unchanged``,
            ``classes_overwritten``, ``classes_kept``, ``classes_skipped``).
        """
        cmd = schematic_commands_pb2.SetMultiBoardNetClass()
        cmd.document.CopyFrom(self._doc)
        cmd.fields.name = str(name)

        # Strings
        if 'tuning_profile' in fields:
            cmd.fields.tuning_profile = str(fields['tuning_profile'])
        if 'pcb_color_css' in fields:
            cmd.fields.pcb_color_css = str(fields['pcb_color_css'])
        if 'schematic_color_css' in fields:
            cmd.fields.schematic_color_css = str(fields['schematic_color_css'])

        # Optional ints — proto3 `optional` semantics: assigning the field
        # marks it present, leaving it unset signals "clear the value" to
        # the C++ handler.
        for key in ('clearance_iu', 'track_width_iu', 'via_diameter_iu',
                    'via_drill_iu', 'uvia_diameter_iu', 'uvia_drill_iu',
                    'diff_pair_width_iu', 'diff_pair_gap_iu',
                    'diff_pair_via_gap_iu', 'wire_width_iu',
                    'bus_width_iu', 'line_style'):
            if key in fields and fields[key] is not None:
                setattr(cmd.fields, key, int(fields[key]))

        return self._kicad.send(
            cmd, schematic_commands_pb2.SetMultiBoardNetClassResponse
        )

    def delete_netclass(self, name):
        """Remove a netclass from the container's net_settings.

        Sub-projects keep their own copy (matches desktop behaviour —
        deletion does not propagate; the user explicitly chose physical
        replication, not a runtime overlay). The Default netclass cannot
        be deleted; the handler returns AS_BAD_REQUEST.

        Returns:
            The DeleteMultiBoardNetClassResponse proto with a single
            ``deleted`` bool — false when the named class wasn't present
            (idempotent).
        """
        cmd = schematic_commands_pb2.DeleteMultiBoardNetClass()
        cmd.document.CopyFrom(self._doc)
        cmd.name = str(name)
        return self._kicad.send(
            cmd, schematic_commands_pb2.DeleteMultiBoardNetClassResponse
        )

    # --- MOON-1333 Phase 3 — container-scope library mutations ---

    _LIB_KIND_BY_NAME = {
        'symbol':    1,   # MBS_LIBRARY_SYMBOL
        'sym':       1,
        'footprint': 2,   # MBS_LIBRARY_FOOTPRINT
        'fp':        2,
    }

    @classmethod
    def _resolve_lib_kind(cls, kind):
        if isinstance(kind, int):
            return kind
        normalised = str(kind).strip().lower()
        if normalised not in cls._LIB_KIND_BY_NAME:
            raise ValueError(
                f"unknown library kind '{kind}'; expected 'symbol' or 'footprint'"
            )
        return cls._LIB_KIND_BY_NAME[normalised]

    def add_library(self, kind, nickname, uri, *, type='', description='',
                    options='', enabled=True, visible=True):
        """Add a library row at the multi-board container's scope.

        Replicates to every sub-project's lib-table immediately (M7.1
        cascade in LIBRARY_MANAGER::AddSharedLibrary). If a peer already
        has a non-shared local row with the same nickname AND matching
        URI/options, the helper promotes that row to shared in place;
        otherwise a conflict marker is emplaced on that peer.

        Args:
            kind: 'symbol' / 'footprint' (or the matching MbsLibraryKind int).
            nickname: Library nickname (the LIB_ID prefix).
            uri: Library URI (file path, KiCad ``${PATH_VAR}/...``, etc.).
            type: Optional table-row "type" (file format hint). Empty -> "KiCad".
            description / options: Optional row metadata.
            enabled: Default True. Maps to the inverse of the row's `(disabled)` bit.
            visible: Default True. Maps to the inverse of the row's `(hidden)` bit.

        Returns:
            AddMultiBoardLibraryResponse proto with ``added``,
            ``peers_replicated``, and ``peers_with_conflict``.
        """
        cmd = schematic_commands_pb2.AddMultiBoardLibrary()
        cmd.document.CopyFrom(self._doc)
        cmd.kind = self._resolve_lib_kind(kind)
        cmd.nickname = str(nickname)
        cmd.uri = str(uri)
        cmd.type = str(type)
        cmd.description = str(description)
        cmd.options = str(options)
        cmd.enabled = bool(enabled)
        cmd.visible = bool(visible)
        return self._kicad.send(
            cmd, schematic_commands_pb2.AddMultiBoardLibraryResponse
        )

    def delete_library(self, kind, nickname):
        """Remove a library row from the container; cascades to sub-projects.

        Routes through LIBRARY_MANAGER::RemoveSharedLibrary, which also
        clears the row from every sub-project's lib-table. To clear the
        shared marker on just one sub-project (UnshareLibraryRow) without
        touching the container, use the desktop sub-project lib-table
        panel — that path isn't exposed in this Phase 3 slice.

        Returns:
            DeleteMultiBoardLibraryResponse with ``deleted`` and
            ``peers_cleared``. ``deleted=False`` means the row wasn't on
            the container — call is idempotent.
        """
        cmd = schematic_commands_pb2.DeleteMultiBoardLibrary()
        cmd.document.CopyFrom(self._doc)
        cmd.kind = self._resolve_lib_kind(kind)
        cmd.nickname = str(nickname)
        return self._kicad.send(
            cmd, schematic_commands_pb2.DeleteMultiBoardLibraryResponse
        )

    def share_library(self, kind, nickname, *, source_sub_project_uuid='',
                       source_sub_project_path=''):
        """Promote a sub-project's local library row to container scope.

        Reads the row from the source sub-project's lib-table on disk,
        then calls AddSharedLibrary on the container — which cascades
        the row back to every sub-project (including the source) with
        ``shared=True``.

        Args:
            kind: 'symbol' / 'footprint'.
            nickname: Library nickname to promote.
            source_sub_project_uuid OR source_sub_project_path: Identify
                which sub-project to take the row from. The handler tries
                UUID first then path; pass either (or both — UUID wins).
                The sub-project must be loaded in SETTINGS_MANAGER.

        Returns:
            ShareMultiBoardLibraryResponse with ``shared``,
            ``peers_replicated``, and ``peers_with_conflict``.
        """
        cmd = schematic_commands_pb2.ShareMultiBoardLibrary()
        cmd.document.CopyFrom(self._doc)
        cmd.kind = self._resolve_lib_kind(kind)
        cmd.nickname = str(nickname)
        cmd.source_sub_project_uuid = str(source_sub_project_uuid)
        cmd.source_sub_project_path = str(source_sub_project_path)
        return self._kicad.send(
            cmd, schematic_commands_pb2.ShareMultiBoardLibraryResponse
        )
