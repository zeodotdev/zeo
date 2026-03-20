/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014 CERN
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */


#ifndef MAIL_TYPE_H_
#define MAIL_TYPE_H_

/**
 * The set of mail types sendable via #KIWAY::ExpressMail() and supplied as
 * the @a aCommand parameter to that function.
 *
 * Such mail will be received in KIWAY_PLAYER::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent ) and
 * aEvent.Command() will match aCommand to KIWAY::ExpressMail().
 */
enum MAIL_T
{
    MAIL_CROSS_PROBE,       // PCB<->SCH, CVPCB->SCH cross-probing.
    MAIL_SELECTION,         // SCH<->PCB selection synchronization.
    MAIL_SELECTION_FORCE,   // Explicit selection of SCH->PCB selection synchronization.
    MAIL_ASSIGN_FOOTPRINTS, // CVPCB->SCH footprint stuffing
    MAIL_SCH_SAVE,          // CVPCB->SCH save the schematic
    MAIL_EESCHEMA_NETLIST,  // SCH->CVPCB netlist immediately after launching CVPCB
    MAIL_SYMBOL_NETLIST,    // SCH->FP_CHOOSER symbol pin & fp_filter information
    MAIL_PCB_UPDATE,        // SCH->PCB forward update
    MAIL_SCH_UPDATE,        // PCB->SCH forward update
    MAIL_IMPORT_FILE,       // Import a different format file
    MAIL_SCH_GET_NETLIST,   // Fetch a netlist from schematics
    MAIL_SCH_GET_ITEM,      // Fetch item from KIID
    MAIL_PCB_GET_NETLIST,   // Fetch a netlist from PCB layout
    MAIL_PCB_UPDATE_LINKS,  // Update the schematic symbol paths in the PCB's footprints
    MAIL_SCH_REFRESH,       // Tell the schematic editor to refresh the display.
    MAIL_ADD_LOCAL_LIB,     // Add a local library to the project library table
    MAIL_LIB_EDIT,
    MAIL_FP_EDIT,
    MAIL_RELOAD_LIB,               // Reload Library List if one was added
    MAIL_RELOAD_PLUGINS,           // Reload python plugins
    MAIL_REFRESH_SYMBOL,           // Refresh symbol in symbol viewer
    MAIL_SCH_NAVIGATE_TO_SHEET,    // Navigate to sheet by filename if in hierarchy

    // Zeo Agent communication
    MAIL_AGENT_REQUEST,       // Agent -> Editor request (JSON payload)
    MAIL_AGENT_RESPONSE,      // Editor -> Agent response (JSON payload)
    MAIL_SHOW_DIFF,           // Agent -> Editor: Show diff overlay (JSON payload)
    MAIL_AUTH_STATE_CHANGED,  // Launcher -> Agent: Auth state changed (UI update only)
    MAIL_AUTH_POINTER,        // Launcher -> Agent: Pass shared AGENT_AUTH pointer (payload: uintptr_t as string)
    MAIL_AGENT_HAS_CHANGES,   // Agent -> Editor: Query pending changes (payload set to "true"/"false")
    MAIL_AGENT_APPROVE,       // Agent -> Editor: Approve pending changes
    MAIL_AGENT_REJECT,        // Agent -> Editor: Reject pending changes
    MAIL_AGENT_DIFF_CLEARED,  // Editor -> Agent: Diff overlay was dismissed (payload: "sch" or "pcb")
    MAIL_AGENT_VIEW_CHANGES,  // Agent -> Editor: Bring editor to front and zoom to changes
    MAIL_AGENT_CHECK_CHANGES, // Editor -> Agent: Tracked items changed, re-check pending changes

    // Concurrent editing - transaction management
    MAIL_AGENT_BEGIN_TRANSACTION,   // Agent -> Editor: Begin a new agent transaction (JSON: {sheet_uuid})
    MAIL_AGENT_END_TRANSACTION,     // Agent -> Editor: End agent transaction (JSON: {commit: bool})
    MAIL_AGENT_WORKING_SET,         // Agent -> Editor: Update working set of items (JSON: {items: [uuid...]})

    // Concurrent editing - conflict detection
    MAIL_CONFLICT_DETECTED,         // Editor -> Agent: User modified an item in agent's working set (JSON: {item_uuid, property, user_value, agent_value})
    MAIL_CONFLICT_RESOLVED,         // Agent -> Editor: Conflict resolution decision (JSON: {item_uuid, resolution: "keep_user"|"keep_agent"|"merge"})

    // Concurrent editing - sheet targeting
    MAIL_AGENT_TARGET_SHEET,        // Agent -> Editor: Set target sheet for operations (JSON: {sheet_uuid})
    MAIL_AGENT_GET_CURRENT_SHEET,   // Agent -> Editor: Query current sheet UUID (response via MAIL_AGENT_RESPONSE)
    MAIL_AGENT_RESET_TARGET_SHEET,  // Agent -> Editor: Clear target sheet for new conversation turn

    // File edit session management (for direct file writes by agent)
    MAIL_AGENT_FILE_EDIT_BEGIN,     // Agent -> Editor: Start file edit session (JSON: {file_path, sheets: [uuid...]})
    MAIL_AGENT_FILE_EDIT_COMPLETE,  // Agent -> Editor: File written + lint passed, reload now
    MAIL_AGENT_FILE_EDIT_ABORT,     // Agent -> Editor: Abort file edit session

    // Diff overlay refresh
    MAIL_AGENT_REFRESH_DIFF,        // Agent -> Editor: Refresh diff overlays (items may have moved)

    // Diff frame content delivery
    MAIL_SCH_DIFF_CONTENT,          // SCH -> SCH_DIFF: Send before/after content (JSON: {before_path, after_path, sheet_path})

    // MCP tool execution (synchronous via project manager)
    MAIL_MCP_EXECUTE_TOOL,          // ProjectMgr -> Terminal: Execute tool (payload: JSON {tool_name, tool_args_json}, result written back)
    MAIL_MCP_GET_TOOL_SCHEMAS,      // ProjectMgr -> Terminal: Get tool manifest JSON (empty payload, manifest written back)
    MAIL_MCP_EXECUTE_AGENT_TOOL,    // ProjectMgr -> Agent: Execute C++ handler tool (payload: JSON {tool_name, tool_args_json}, result written back)

    // Cancel in-flight tool execution
    MAIL_CANCEL_TOOL_EXECUTION,     // Agent -> Terminal: Cancel running Python script and child processes

    // VCS app communication
    MAIL_VCS_AUTH_COMPLETE,         // Launcher -> VCS: GitHub OAuth complete (JSON: {username})
    MAIL_VCS_REFRESH                // Agent -> VCS: Project files changed, auto-init if needed and refresh
};



#endif // MAIL_TYPE_H_
