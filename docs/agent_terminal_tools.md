# Agent Terminal Tools Reference

The KiCad Agent has access to the `run_terminal_command` tool, which executes commands in the built-in terminal. The terminal supports three modes: `sys` (system shell), `sch` (schematic python), and `pcb` (pcb python).

### Terminal Management
- `run_terminal_command create [mode]`: Create a new **Developer** terminal tab.
- `run_terminal_command create_agent [mode]`: Create a new **Agent** terminal tab (Green style).
- `run_terminal_command list`: List all open terminal tabs (showing ID, Title, and Type).
- `run_terminal_command close [id]`: Close a specific terminal tab.
- `run_terminal_command [id] [mode] [command]`: Run a command in a specific tab.

### Modes
- `sys`: System Shell (bash/zsh).
- `pcb`: PCB Scripting (Python with `pcbnew` loaded).
- `sch`: Schematic Scripting (Python with `kiutils` loaded).

### Examples
- Create a new PCB Agent terminal: `run_terminal_command create_agent pcb`
- List terminals: `run_terminal_command list`
- Run command in tab 1: `run_terminal_command 1 pcb print(board.GetTracks())`

## Tool Usage
**Format:** `run_terminal_command [mode] [command]`

### 1. `sys` (System Shell)
Executes command system shell commands. Use this for file system operations, git, or checking environment.

**Examples:**
- List files: `run_terminal_command sys ls -la`
- Check git status: `run_terminal_command sys git status`
- Read a file: `run_terminal_command sys cat project.txt`

### 2. `pcb` (PCB Editor Scripting)
Executes Python code in the context of the PCB Editor.
The environment pre-loads `pcbnew` and the current board object as `board` (and `p`).

**Common Operations:**

*   **Get Components (Footprints):**
    ```python
    run_terminal_command pcb for f in board.GetFootprints(): print(f"{f.GetReference()}: {f.GetValue()} @ {f.GetPosition()}")
    ```

*   **Get Tracks:**
    ```python
    run_terminal_command pcb print(f"Total Tracks: {len(board.GetTracks())}")
    ```

*   **Get Nets:**
    ```python
    run_terminal_command pcb for net_code, net in board.GetNetsByName().items(): print(f"{net.GetNetname()}: {net_code}")
    ```

*   **Board Info:**
    ```python
    run_terminal_command pcb print(board.GetBoardSetup().GetCopperLayerCount())
    ```

### 3. `sch` (Schematic Editor Scripting)
Executes Python code in the context of the Schematic Editor using the `kiutils` library.
The environment pre-loads the current schematic object as `schematic` (and `sch`).

**Common Operations:**

*   **List Sheets:**
    ```python
    run_terminal_command sch for sheet in schematic.schematic.sheets: print(sheet.sheet.sheet_name)
    ```

*   **Get Symbols:**
    ```python
    run_terminal_command sch for symbol in schematic.schematic.symbol_instances: print(f"{symbol.reference}: {symbol.value}")
    ```

*   **Get Lib Symbols:**
    ```python
    run_terminal_command sch print(len(schematic.libSymbols))
    ```
