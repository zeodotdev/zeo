# Copyright (C) 2026, Zeo <team@zeo.dev>

"""
List and manage boards in a multi-board project.

Actions:
- list: Get all boards in the project
- create: Create a new board
- set_active: Set the active board
- get_active: Get the currently active board
- assign_component: Assign a component to a board
"""
import json

action = TOOL_ARGS.get("action", "list")

def get_project_boards():
    """Get board information from the project."""
    # Note: This requires multi-board support in the IPC API
    # For now, return info about the currently loaded board
    try:
        footprints = board.get_footprints()
        nets = board.get_nets()

        return [{
            "uuid": str(board.get_uuid()) if hasattr(board, 'get_uuid') else "main",
            "name": "Main Board",
            "filename": board.get_filename() if hasattr(board, 'get_filename') else "",
            "is_active": True,
            "component_count": len(footprints),
            "net_count": len(nets)
        }]
    except Exception as e:
        return []

if action == "list":
    boards = get_project_boards()
    print(json.dumps({
        "status": "success",
        "boards": boards,
        "count": len(boards)
    }, indent=2))

elif action == "create":
    name = TOOL_ARGS.get("name", "New Board")
    # Multi-board creation not yet supported in IPC API
    print(json.dumps({
        "status": "error",
        "message": "Board creation via API not yet implemented. Use the Board Hierarchy Pane in PCB editor."
    }))

elif action == "set_active":
    uuid = TOOL_ARGS.get("uuid")
    if not uuid:
        print(json.dumps({
            "status": "error",
            "message": "uuid parameter required"
        }))
    else:
        # Multi-board switching not yet supported in IPC API
        print(json.dumps({
            "status": "error",
            "message": "Board switching via API not yet implemented. Use the Board Hierarchy Pane in PCB editor."
        }))

elif action == "get_active":
    boards = get_project_boards()
    active = next((b for b in boards if b.get("is_active")), None)
    print(json.dumps({
        "status": "success",
        "board": active
    }, indent=2))

elif action == "assign_component":
    ref = TOOL_ARGS.get("ref")
    board_uuid = TOOL_ARGS.get("board_uuid")

    if not ref or not board_uuid:
        print(json.dumps({
            "status": "error",
            "message": "ref and board_uuid parameters required"
        }))
    else:
        # Component assignment not yet supported in IPC API
        print(json.dumps({
            "status": "error",
            "message": "Component assignment via API not yet implemented. Use the Multi-Board Sync dialog in PCB editor."
        }))

else:
    print(json.dumps({
        "status": "error",
        "message": f"Unknown action: {action}. Valid actions: list, create, set_active, get_active, assign_component"
    }))
