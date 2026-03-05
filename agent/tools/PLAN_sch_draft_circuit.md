# Implementation Plan: `sch_draft_circuit` Tool

## Overview

A new schematic tool that places components and shows intended connections as wiring guide overlays. Integrates with the existing diff view accept/reject system for symbol approval, and provides interactive guide dismissal post-approval.

**Key Principles:**
- Agent handles the "what" (which components, which pins connect)
- User handles the "how" (approving placements, routing wires)
- Wiring recommendations are stored as symbol properties (user-editable)
- Full integration with existing diff system

---

## Tool Interface

### Tool Schema (`tool_schemas.cpp`)

```cpp
LLM_TOOL schDraftCircuit;
schDraftCircuit.name = "sch_draft_circuit";
schDraftCircuit.description =
    "Place components on the schematic and show intended connections as wiring guides. "
    "Components appear in a diff view for approval. Once approved, guide lines show which "
    "pins should be connected. The user manually draws the wires.\n\n"
    "Wiring recommendations are stored in each symbol's 'Agent_Wiring' field, which the user "
    "can view and edit in the symbol properties. Guide lines can be dismissed individually.\n\n"
    "Use this tool for circuits where you want the user to control the final layout. "
    "Place all related components in a single call - do not use multiple calls for the same circuit.\n\n"
    "IMPORTANT: Do not call sch_add or sch_place_companions after this - all symbols should be "
    "placed in one sch_draft_circuit call.\n\n"
    "REQUIRES: Schematic editor must be open with a document loaded.";
schDraftCircuit.input_schema = {
    { "type", "object" },
    { "properties", {
        { "symbols", {
            { "type", "array" },
            { "description", "Array of symbols to place" },
            { "items", {
                { "type", "object" },
                { "properties", {
                    { "lib_id", { { "type", "string" }, { "description", "Library ID (e.g., 'Device:R', 'MCU_ST:STM32F405RGTx')" } } },
                    { "position", { { "type", "array" }, { "items", { { "type", "number" } } }, { "description", "[x, y] in mm. Place symbols with adequate spacing for wiring." } } },
                    { "angle", { { "type", "integer" }, { "description", "Rotation: 0, 90, 180, 270. Default: 0" } } },
                    { "mirror", { { "type", "string" }, { "enum", json::array({"none", "x", "y"}) }, { "description", "Mirror axis. Default: none" } } },
                    { "properties", { { "type", "object" }, { "description", "Symbol properties {Value, Footprint, ...}" } } },
                    { "id", { { "type", "string" }, { "description", "Temporary ID for referencing in connections (e.g., 'mcu', 'r1', 'c_bypass1')" } } }
                }},
                { "required", json::array({ "lib_id", "position", "id" }) }
            }}
        }},
        { "power_symbols", {
            { "type", "array" },
            { "description", "Power symbols to place (VCC, GND, +3V3, etc.)" },
            { "items", {
                { "type", "object" },
                { "properties", {
                    { "name", { { "type", "string" }, { "description", "Power net name (e.g., 'VCC', 'GND', '+3V3')" } } },
                    { "position", { { "type", "array" }, { "items", { { "type", "number" } } }, { "description", "[x, y] in mm" } } },
                    { "angle", { { "type", "integer" }, { "description", "Rotation: 0, 90, 180, 270" } } },
                    { "id", { { "type", "string" }, { "description", "Temporary ID for referencing in connections" } } }
                }},
                { "required", json::array({ "name", "position", "id" }) }
            }}
        }},
        { "connections", {
            { "type", "array" },
            { "description", "Pin-to-pin connections to show as wiring guides. Each connection is [source, target] where each endpoint is 'id:pin' format." },
            { "items", {
                { "type", "array" },
                { "items", { { "type", "string" } } },
                { "minItems", 2 },
                { "maxItems", 2 },
                { "description", "['source_id:pin', 'target_id:pin'] - e.g., ['mcu:PA0', 'r1:1'] or ['vcc1:1', 'c1:1']" }
            }}
        }},
        { "labels", {
            { "type", "array" },
            { "description", "Net labels to place (optional)" },
            { "items", {
                { "type", "object" },
                { "properties", {
                    { "text", { { "type", "string" } } },
                    { "position", { { "type", "array" }, { "items", { { "type", "number" } } } } },
                    { "type", { { "type", "string" }, { "enum", json::array({ "local", "global", "hierarchical" }) } } }
                }}
            }}
        }}
    }},
    { "required", json::array({ "symbols", "connections" }) }
};
```

### Example Tool Call

```json
{
  "symbols": [
    {"lib_id": "MCU_ST:STM32F405RGTx", "position": [100, 80], "id": "mcu"},
    {"lib_id": "Device:C", "position": [60, 50], "angle": 0, "properties": {"Value": "100nF"}, "id": "c1"},
    {"lib_id": "Device:C", "position": [60, 65], "angle": 0, "properties": {"Value": "100nF"}, "id": "c2"},
    {"lib_id": "Device:R", "position": [140, 40], "angle": 270, "properties": {"Value": "10k"}, "id": "r1"}
  ],
  "power_symbols": [
    {"name": "VCC", "position": [55, 45], "angle": 0, "id": "vcc1"},
    {"name": "GND", "position": [55, 55], "angle": 0, "id": "gnd1"},
    {"name": "GND", "position": [55, 70], "angle": 0, "id": "gnd2"}
  ],
  "connections": [
    ["vcc1:1", "c1:1"],
    ["c1:2", "gnd1:1"],
    ["c1:1", "mcu:VDD"],
    ["vcc1:1", "c2:1"],
    ["c2:2", "gnd2:1"],
    ["mcu:PA0", "r1:2"],
    ["r1:1", "vcc1:1"]
  ]
}
```

---

## Data Model: Agent_Wiring Field

### Storage Format

Each symbol stores its wiring recommendations in a standard KiCad field:

```
(symbol (lib_id "Device:R") (at 140 40 270)
  (property "Reference" "R1" ...)
  (property "Value" "10k" ...)
  (property "Agent_Wiring" "1→vcc1:1; 2→mcu:PA0" (effects (hide yes)))
)
```

**Field Format:**
```
<pin>→<target>; <pin>→<target>; ...

Where:
  <pin>    = Pin number or name on this symbol
  <target> = "symbol_ref:pin" or "net_name"

Examples:
  "1→VCC; 2→U1:PA0"           // Pin 1 to VCC net, pin 2 to U1's PA0
  "1→R1:1; 2→GND"             // Pin 1 to R1 pin 1, pin 2 to GND net
  "CLK→U2:CLK; MOSI→U2:DIN"   // Named pins
```

### Benefits of Field-Based Storage

| Aspect | Benefit |
|--------|---------|
| Persistence | Saved with schematic file |
| User editing | Standard properties panel |
| Copy/paste | Recommendations travel with symbol |
| Version control | Single file to track |
| Discoverability | Visible in symbol properties |

---

## Two-Phase Interaction Model

### Phase 1: Diff Review (Pre-Approval)

When agent places symbols, they appear in the **diff overlay** with guide previews:

```
┌─────────────────────────────────────────────────────────────────┐
│                        SCHEMATIC CANVAS                          │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ [Approve All] [Reject All] [View Diff]                  │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│    ╔══════════════════════════════════════════════════════╗     │
│    ║  DIFF OVERLAY - Agent Changes Pending                ║     │
│    ║                                                      ║     │
│    ║   ┌─────────────────┐                               ║     │
│    ║   │      U1         │                               ║     │
│    ║   │   STM32F405     │·············                  ║     │
│    ║   │                 │            ·                  ║     │
│    ║   └─────────────────┘            ·  ┌─────┐         ║     │
│    ║           ·                      ·  │ R1  │         ║     │
│    ║           ·                      ···│     │         ║     │
│    ║           ·                         └─────┘         ║     │
│    ║      ┌────┴────┐                        ·           ║     │
│    ║      │   C1    │                   ┌────┴───┐       ║     │
│    ║      │  100nF  │                   │  VCC   │       ║     │
│    ║      └────┬────┘                   └────────┘       ║     │
│    ║           ·                                         ║     │
│    ║      ┌────┴───┐     [Approve] [Reject]              ║     │
│    ║      │  GND   │     (per-item on hover)             ║     │
│    ║      └────────┘                                     ║     │
│    ║                                                      ║     │
│    ║  ············· = Guide preview (faint, informational)║     │
│    ╚══════════════════════════════════════════════════════╝     │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Behaviors:**
- Green highlight boxes around ADDED symbols
- Faint dashed guide lines show `Agent_Wiring` connections (preview only)
- Hover over symbol → per-item [Approve] [Reject] buttons
- Guide lines are **not interactive** during diff phase
- User CAN start wiring during this phase (changes preserved on approve)

### Phase 2: Active Guides (Post-Approval)

After approval, diff overlay clears and **guides become interactive**:

```
┌─────────────────────────────────────────────────────────────────┐
│                        SCHEMATIC CANVAS                          │
│                                                                  │
│        ┌─────────────────┐                                      │
│        │      U1         │                                      │
│        │   STM32F405     │══════════════╗                       │
│        │                 │              ║  Active guide         │
│        └─────────────────┘              ║  (interactive)        │
│                                    ┌────╨────┐                  │
│                                    │   R1    │                  │
│                                    │   10k   │                  │
│                                    └────┬────┘                  │
│                                         ║                       │
│           Right-click on guide:    ┌────╨────┐                  │
│           ┌──────────────────────┐ │   VCC   │                  │
│           │ ✓ Wire Connection    │ └─────────┘                  │
│           │ ✗ Dismiss Guide      │                              │
│           │ ℹ Edit in Properties │                              │
│           └──────────────────────┘                              │
│                                                                  │
│   ══════════ = Active guide (brighter, interactive)            │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Behaviors:**
- No diff overlay (symbols are approved)
- Guide lines are **interactive** - hover shows endpoints, right-click for menu
- "Dismiss Guide" removes entry from symbol's `Agent_Wiring` field
- "Wire Connection" starts wire tool at source pin
- Completing a wire makes guide disappear (connectivity detected)
- Deleting a wire makes guide reappear (connectivity lost)

---

## Diff System Integration

### Existing Components Used

| Component | Role in sch_draft_circuit |
|-----------|---------------------------|
| `AGENT_CHANGE_TRACKER` | Track placed symbols as ADDED |
| `AGENT_SNAPSHOT_SESSION` | Snapshot before placement for rejection |
| `DIFF_OVERLAY_ITEM` | Render symbol highlights + guide previews |
| `DIFF_MANAGER` | Orchestrate approve/reject flow |

### Extended DIFF_OVERLAY_ITEM

Add guide preview rendering to the existing overlay:

```cpp
// In DIFF_OVERLAY_ITEM

struct WIRING_GUIDE_PREVIEW
{
    VECTOR2I start;              // Source pin position
    VECTOR2I end;                // Target pin position
    KIID     sourceSymbolId;     // For linking to symbol approval
    wxString label;              // "R1:1 → VCC" for tooltip
};

std::vector<WIRING_GUIDE_PREVIEW> m_wiringGuides;

void SetWiringGuides( const std::vector<WIRING_GUIDE_PREVIEW>& aGuides );

// In drawPreviewShape():
void drawWiringGuidePreviews( KIGFX::GAL* aGal ) const
{
    aGal->SetIsStroke( true );
    aGal->SetIsFill( false );
    aGal->SetStrokeColor( COLOR4D( 0.5, 0.6, 0.8, 0.3 ) );  // Very faint
    aGal->SetLineWidth( schIUScale.mmToIU( 0.08 ) );        // Thin

    for( const auto& guide : m_wiringGuides )
    {
        drawDashedLine( aGal, guide.start, guide.end,
                        schIUScale.mmToIU( 1.5 ),   // dash
                        schIUScale.mmToIU( 0.75 ) ); // gap
    }
}
```

### Rejection with User Changes Warning

When user has made changes (wired things, added labels) during diff review:

```
┌──────────────────────────────────────────────────────────────────┐
│ ⚠️  You've made changes while reviewing                          │
│                                                                  │
│ You added wires or labels connected to the agent-placed items.  │
│                                                                  │
│ How would you like to handle your changes?                       │
│                                                                  │
│ ┌────────────────────────────────────────────────────────────┐  │
│ │ [Reject & Remove My Changes]                               │  │
│ │  Remove agent symbols AND your connected wires/labels      │  │
│ └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│ ┌────────────────────────────────────────────────────────────┐  │
│ │ [Reject & Keep My Changes]                                 │  │
│ │  Remove agent symbols, keep your wires (may be orphaned)   │  │
│ └────────────────────────────────────────────────────────────┘  │
│                                                                  │
│ ┌────────────────────────────────────────────────────────────┐  │
│ │ [Cancel]                                                   │  │
│ │  Go back to reviewing                                      │  │
│ └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

```cpp
enum class REJECT_ACTION
{
    CANCEL,
    REJECT_AND_REMOVE_USER_CHANGES,
    REJECT_AND_KEEP_USER_CHANGES
};

REJECT_ACTION SCH_EDIT_FRAME::ShowRejectWarningDialog()
{
    wxDialog dlg( this, wxID_ANY, _( "Reject Agent Changes" ),
                  wxDefaultPosition, wxSize( 450, 280 ) );

    wxBoxSizer* mainSizer = new wxBoxSizer( wxVERTICAL );

    // Warning message
    wxStaticText* message = new wxStaticText( &dlg, wxID_ANY,
        _( "You've made changes while reviewing the agent's work.\n\n"
           "How would you like to handle your changes?" ) );
    mainSizer->Add( message, 0, wxALL, 15 );

    // Option buttons
    wxButton* btnRemove = new wxButton( &dlg, wxID_ANY,
        _( "Reject && Remove My Changes" ) );
    wxStaticText* descRemove = new wxStaticText( &dlg, wxID_ANY,
        _( "Remove agent symbols AND your connected wires/labels" ) );
    descRemove->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );

    wxButton* btnKeep = new wxButton( &dlg, wxID_ANY,
        _( "Reject && Keep My Changes" ) );
    wxStaticText* descKeep = new wxStaticText( &dlg, wxID_ANY,
        _( "Remove agent symbols, keep your wires (may be orphaned)" ) );
    descKeep->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );

    wxButton* btnCancel = new wxButton( &dlg, wxID_CANCEL, _( "Cancel" ) );

    // Layout
    mainSizer->Add( btnRemove, 0, wxEXPAND | wxLEFT | wxRIGHT, 15 );
    mainSizer->Add( descRemove, 0, wxLEFT | wxRIGHT | wxBOTTOM, 20 );
    mainSizer->Add( btnKeep, 0, wxEXPAND | wxLEFT | wxRIGHT, 15 );
    mainSizer->Add( descKeep, 0, wxLEFT | wxRIGHT | wxBOTTOM, 20 );
    mainSizer->Add( btnCancel, 0, wxALIGN_RIGHT | wxALL, 15 );

    dlg.SetSizer( mainSizer );

    REJECT_ACTION result = REJECT_ACTION::CANCEL;

    btnRemove->Bind( wxEVT_BUTTON, [&]( wxCommandEvent& ) {
        result = REJECT_ACTION::REJECT_AND_REMOVE_USER_CHANGES;
        dlg.EndModal( wxID_OK );
    });

    btnKeep->Bind( wxEVT_BUTTON, [&]( wxCommandEvent& ) {
        result = REJECT_ACTION::REJECT_AND_KEEP_USER_CHANGES;
        dlg.EndModal( wxID_OK );
    });

    dlg.ShowModal();
    return result;
}

void SCH_EDIT_FRAME::RejectAgentChangesOnSheet( const wxString& aSheetPath )
{
    REJECT_ACTION action = REJECT_ACTION::REJECT_AND_KEEP_USER_CHANGES;

    // Check if schematic was modified since snapshot
    if( m_snapshotSession && m_snapshotSession->HasUserModifications() )
    {
        action = ShowRejectWarningDialog();

        if( action == REJECT_ACTION::CANCEL )
            return;
    }

    // Proceed with rejection
    bool removeUserChanges = ( action == REJECT_ACTION::REJECT_AND_REMOVE_USER_CHANGES );
    DoRejectAgentChanges( aSheetPath, removeUserChanges );
}

void SCH_EDIT_FRAME::DoRejectAgentChanges( const wxString& aSheetPath,
                                            bool aRemoveUserChanges )
{
    SCH_COMMIT commit( this );

    // Load snapshot
    SCHEMATIC tempSchematic;
    LoadSnapshotInto( &tempSchematic, aSheetPath );

    // Remove agent-placed items (restore from snapshot)
    for( const KIID& id : m_agentChangeTracker->GetTrackedItemsOnSheet( aSheetPath ) )
    {
        AGENT_CHANGE_TYPE changeType = m_agentChangeTracker->GetChangeType( id );

        if( changeType == AGENT_CHANGE_TYPE::ADDED )
        {
            SCH_ITEM* item = GetScreen()->GetItem( id );
            if( item )
                commit.Remove( item );
        }
        // ... handle CHANGED and DELETED ...
    }

    // Optionally remove user's connected wires
    if( aRemoveUserChanges )
    {
        std::vector<SCH_ITEM*> userItems = GetUserAddedItemsConnectedToPending();
        for( SCH_ITEM* item : userItems )
            commit.Remove( item );
    }

    commit.Push( _( "Reject Agent Changes" ) );

    // Cleanup
    m_agentChangeTracker->UntrackItemsOnSheet( aSheetPath );
    RecalculateConnections();
    HardRedraw();
}
```

### Approval Flow

```
User clicks [Approve] (or [Approve All])
    ↓
ApproveAgentItems() / ApproveAgentChangesOnSheet()
    ↓
UntrackItem() for each approved symbol
    ↓
If all items approved:
    - Clear diff overlay
    - End snapshot session
    ↓
Activate wiring guide overlay
    - Scan approved symbols for Agent_Wiring fields
    - Render interactive guides for incomplete connections
    ↓
User can now:
    - Wire connections (guides auto-hide when complete)
    - Dismiss individual guides (modifies Agent_Wiring field)
    - Edit Agent_Wiring in symbol properties
```

### Rejection Flow

```
User clicks [Reject] (or [Reject All])
    ↓
Check for user modifications (wires, labels added during review)
    ↓
If NO modifications:
    → DoRejectAgentChanges( removeUserChanges: false )
    ↓
If HAS modifications:
    → Show 3-option dialog
    ↓
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│  [Reject & Remove My Changes]                               │
│      → DoRejectAgentChanges( removeUserChanges: true )      │
│      → Agent symbols removed                                │
│      → User's connected wires/labels also removed           │
│      → Clean slate                                          │
│                                                             │
│  [Reject & Keep My Changes]                                 │
│      → DoRejectAgentChanges( removeUserChanges: false )     │
│      → Agent symbols removed                                │
│      → User's wires/labels kept (may be orphaned/dangling)  │
│                                                             │
│  [Cancel]                                                   │
│      → Return to diff review                                │
│      → No changes made                                      │
│                                                             │
└─────────────────────────────────────────────────────────────┘
    ↓
DoRejectAgentChanges()
    - Load snapshot
    - Remove ADDED items
    - Restore CHANGED items
    - Re-add DELETED items
    - If removeUserChanges: also remove user's connected items
    ↓
UntrackItem() for rejected items
Clear diff overlay
End snapshot session
    ↓
No wiring guides shown (symbols were rejected)
```

---

## Active Wiring Guide System

### SCH_WIRING_GUIDE_MANAGER

Central class that manages active (post-approval) wiring guides:

```cpp
class SCH_WIRING_GUIDE_MANAGER
{
public:
    struct WiringGuide
    {
        KIID     sourceSymbolId;
        wxString sourcePin;
        VECTOR2I sourcePos;

        wxString targetRef;       // "U1:PA0" or "VCC"
        VECTOR2I targetPos;

        bool     isComplete;      // True if connection exists
        bool     isVisible;       // User can hide individual guides
    };

    // Lifecycle
    void ScanSymbolsForWiring();  // Called after approval, on schematic load
    void RefreshGuideStates();    // Check connectivity, update isComplete

    // Guide operations
    void DismissGuide( const KIID& aSymbolId, const wxString& aPin );
    void SetGuideVisible( const KIID& aSymbolId, const wxString& aPin, bool aVisible );

    // Rendering
    std::vector<WiringGuide> GetActiveGuides() const;  // For overlay

    // Events
    void OnSchematicChanged();    // Recalculate completion states

private:
    SCH_EDIT_FRAME* m_frame;
    std::vector<WiringGuide> m_guides;

    void ParseAgentWiringField( SCH_SYMBOL* aSymbol );
    bool CheckConnectionExists( const VECTOR2I& aStart, const VECTOR2I& aEnd );
};
```

### SCH_WIRING_GUIDE_OVERLAY

Renders active guides with interactivity:

```cpp
class SCH_WIRING_GUIDE_OVERLAY : public KIGFX::PREVIEW::SIMPLE_OVERLAY_ITEM
{
public:
    SCH_WIRING_GUIDE_OVERLAY( SCH_WIRING_GUIDE_MANAGER* aManager );

    // VIEW_ITEM interface
    const BOX2I ViewBBox() const override;
    void ViewDraw( int aLayer, KIGFX::VIEW* aView ) const override;

    // Interaction
    int HitTestGuide( const VECTOR2I& aPos ) const;
    void SetHoveredGuide( int aIndex );

    // Context menu
    void ShowContextMenu( int aGuideIndex, const VECTOR2I& aPos );

protected:
    void drawPreviewShape( KIGFX::VIEW* aView ) const override;

private:
    SCH_WIRING_GUIDE_MANAGER* m_manager;
    int m_hoveredGuideIndex = -1;

    // Visual settings
    static constexpr double GUIDE_WIDTH = 0.15;        // mm
    static constexpr double GUIDE_DASH = 1.2;          // mm
    static constexpr double GUIDE_GAP = 0.6;           // mm
    static constexpr double GUIDE_ALPHA = 0.6;
    static constexpr double GUIDE_HOVER_ALPHA = 0.9;
};
```

### Guide Context Menu

```cpp
void SCH_WIRING_GUIDE_OVERLAY::ShowContextMenu( int aGuideIndex, const VECTOR2I& aPos )
{
    const auto& guide = m_manager->GetActiveGuides()[aGuideIndex];

    wxMenu menu;
    menu.Append( ID_WIRE_THIS, wxString::Format(
        _( "Wire %s to %s" ), guide.sourcePin, guide.targetRef ) );
    menu.Append( ID_DISMISS_GUIDE, _( "Dismiss This Recommendation" ) );
    menu.AppendSeparator();
    menu.Append( ID_EDIT_PROPERTIES, wxString::Format(
        _( "Edit %s Properties..." ), GetSymbolRef( guide.sourceSymbolId ) ) );
    menu.Append( ID_DISMISS_ALL_FOR_SYMBOL, wxString::Format(
        _( "Dismiss All for %s" ), GetSymbolRef( guide.sourceSymbolId ) ) );

    int selection = m_frame->GetPopupMenuSelectionFromUser( menu, aPos );

    switch( selection )
    {
    case ID_WIRE_THIS:
        StartWireToolAtPin( guide.sourceSymbolId, guide.sourcePin );
        break;

    case ID_DISMISS_GUIDE:
        m_manager->DismissGuide( guide.sourceSymbolId, guide.sourcePin );
        break;

    case ID_EDIT_PROPERTIES:
        m_frame->EditSymbolProperties( guide.sourceSymbolId );
        break;

    case ID_DISMISS_ALL_FOR_SYMBOL:
        m_manager->DismissAllForSymbol( guide.sourceSymbolId );
        break;
    }
}
```

### Dismissing a Guide

Modifying the symbol's `Agent_Wiring` field:

```cpp
void SCH_WIRING_GUIDE_MANAGER::DismissGuide( const KIID& aSymbolId, const wxString& aPin )
{
    SCH_SYMBOL* symbol = m_frame->GetScreen()->GetSymbol( aSymbolId );
    if( !symbol )
        return;

    wxString currentWiring = symbol->GetFieldText( wxT( "Agent_Wiring" ) );
    wxString updatedWiring = RemoveWiringEntry( currentWiring, aPin );

    // Use commit for undo support
    SCH_COMMIT commit( m_frame );
    commit.Modify( symbol, m_frame->GetScreen() );

    if( updatedWiring.IsEmpty() )
        symbol->RemoveField( wxT( "Agent_Wiring" ) );
    else
        symbol->GetField( wxT( "Agent_Wiring" ) )->SetText( updatedWiring );

    commit.Push( _( "Dismiss Wiring Recommendation" ) );

    // Refresh guides
    RefreshGuideStates();
    m_frame->GetCanvas()->Refresh();
}

wxString SCH_WIRING_GUIDE_MANAGER::RemoveWiringEntry( const wxString& aWiring,
                                                      const wxString& aPin )
{
    // Parse "1→VCC; 2→U1:PA0" format
    wxArrayString entries = wxSplit( aWiring, ';' );
    wxArrayString remaining;

    for( const wxString& entry : entries )
    {
        wxString trimmed = entry.Trim().Trim( false );
        if( !trimmed.StartsWith( aPin + wxT( "→" ) ) )
            remaining.Add( trimmed );
    }

    return wxJoin( remaining, ';' );
}
```

---

## Connectivity Detection

Guides auto-hide when connections are made:

```cpp
void SCH_WIRING_GUIDE_MANAGER::RefreshGuideStates()
{
    CONNECTION_GRAPH* connGraph = &m_frame->Schematic().ConnectionGraph();

    for( auto& guide : m_guides )
    {
        // Get items at source and target positions
        SCH_ITEM* sourceItem = GetPinAtPosition( guide.sourcePos );
        SCH_ITEM* targetItem = GetPinAtPosition( guide.targetPos );

        if( !sourceItem || !targetItem )
        {
            guide.isComplete = false;
            continue;
        }

        // Check if they're on the same net
        SCH_CONNECTION* sourceConn = sourceItem->Connection();
        SCH_CONNECTION* targetConn = targetItem->Connection();

        guide.isComplete = ( sourceConn && targetConn &&
                             sourceConn->IsSameAs( *targetConn ) );
    }

    // Update overlay
    m_frame->GetCanvas()->GetView()->Update( m_overlay );
    m_frame->GetCanvas()->Refresh();
}

void SCH_WIRING_GUIDE_MANAGER::OnSchematicChanged()
{
    // Called when wires added/removed, connectivity changes
    RefreshGuideStates();
}
```

---

## Symbol Properties Integration

### Agent_Wiring Field in Properties Panel

```
┌─────────────────────────────────────────────────────────────┐
│ R1 Properties                                               │
├─────────────────────────────────────────────────────────────┤
│ Reference:    [R1        ]                                  │
│ Value:        [10k       ]                                  │
│ Footprint:    [Resistor_SMD:R_0603_1608Metric    ] [...]   │
│ Datasheet:    [                                  ]          │
├─────────────────────────────────────────────────────────────┤
│ 🤖 Agent Wiring (click to expand)                      [▼] │
├─────────────────────────────────────────────────────────────┤
│ │ Pin │ Connect To │ Status     │        │                 │
│ ├─────┼────────────┼────────────┼────────┤                 │
│ │ 1   │ VCC        │ ✓ Complete │ [🗑️]   │                 │
│ │ 2   │ U1:PA0     │ ⏳ Pending │ [🗑️]   │                 │
│ └─────┴────────────┴────────────┴────────┘                 │
│                                                             │
│ Raw field: [1→VCC; 2→U1:PA0                         ] [✏️] │
└─────────────────────────────────────────────────────────────┘
```

Users can:
- Click trash icons to dismiss individual recommendations
- Edit the raw field text directly
- See completion status at a glance

---

## Agent Sidebar Panel (Optional Enhancement)

### Connection Guides Panel

```
┌─────────────────────────────────────────┐
│ 🔌 Wiring Guides                    [≡] │
├─────────────────────────────────────────┤
│ ☑ Show guides              [👁 Toggle] │
├─────────────────────────────────────────┤
│ Progress: 5/8 connections               │
│ █████████████░░░░░░░░                   │
├─────────────────────────────────────────┤
│ 🔍 Filter: [____________]               │
├─────────────────────────────────────────┤
│                                         │
│ ▼ R1 (1 pending)                        │
│   └ Pin 2 → U1:PA0      [Zoom] [Dismiss]│
│                                         │
│ ▼ C1 (2 pending)                        │
│   ├ Pin 1 → VCC         [Zoom] [Dismiss]│
│   └ Pin 2 → GND         [Zoom] [Dismiss]│
│                                         │
│ ▶ U1 (0 pending) ✓                      │
│                                         │
├─────────────────────────────────────────┤
│ [Dismiss All Completed] [Clear All]     │
└─────────────────────────────────────────┘
```

---

## Implementation Plan

### Phase 1: Diff Overlay Extension
- [ ] Add `WIRING_GUIDE_PREVIEW` struct to `DIFF_OVERLAY_ITEM`
- [ ] Implement `SetWiringGuides()` and `drawWiringGuidePreviews()`
- [ ] Faint styling for preview guides (30% alpha, thin dashed)

### Phase 2: Agent_Wiring Field Infrastructure
- [ ] Define field format and parsing utilities
- [ ] `ParseAgentWiring()` - string to structured data
- [ ] `SerializeAgentWiring()` - structured data to string
- [ ] `RemoveWiringEntry()` - for dismissal

### Phase 3: Rejection Warning Dialog
- [ ] Track if schematic modified since snapshot (user added wires/labels)
- [ ] `GetUserAddedItemsConnectedToPending()` - find user items connected to agent items
- [ ] `ShowRejectWarningDialog()` - 3-option dialog
- [ ] "Reject & Remove My Changes" - clean removal including user wires
- [ ] "Reject & Keep My Changes" - remove agent items, keep user wires (orphaned)
- [ ] "Cancel" - return to review

### Phase 4: Wiring Guide Manager
- [ ] `SCH_WIRING_GUIDE_MANAGER` class
- [ ] `ScanSymbolsForWiring()` - find all Agent_Wiring fields
- [ ] `RefreshGuideStates()` - check connectivity completion
- [ ] `DismissGuide()` - modify field with undo support

### Phase 5: Active Guide Overlay
- [ ] `SCH_WIRING_GUIDE_OVERLAY` class
- [ ] Render active guides (brighter than preview, 60% alpha)
- [ ] Hit testing for hover/selection
- [ ] Context menu implementation

### Phase 6: Connectivity Integration
- [ ] Hook into schematic change events
- [ ] Auto-refresh guide states on wire add/delete
- [ ] Update overlay when connectivity changes

### Phase 7: Properties Panel Enhancement
- [ ] Custom display for Agent_Wiring field
- [ ] Status column (complete/pending)
- [ ] Per-entry dismiss buttons

### Phase 8: Python Tool
- [ ] `sch_draft_circuit.py` implementation
- [ ] Place symbols with Agent_Wiring fields
- [ ] Build guide previews for diff overlay
- [ ] Proper snapshot session management

### Phase 9: Sidebar Panel (Optional)
- [ ] Agent sidebar panel for guides list
- [ ] Progress indicator
- [ ] Filter/search functionality
- [ ] Bulk operations

---

## Files to Create

| File | Purpose |
|------|---------|
| `eeschema/sch_wiring_guide_manager.h` | Guide management logic |
| `eeschema/sch_wiring_guide_manager.cpp` | Implementation |
| `eeschema/sch_wiring_guide_overlay.h` | Active guide rendering |
| `eeschema/sch_wiring_guide_overlay.cpp` | Implementation |
| `agent/tools/python/schematic/sch_draft_circuit.py` | Python tool |

## Files to Modify

| File | Changes |
|------|---------|
| `include/preview_items/diff_overlay_item.h` | Add guide preview support |
| `common/preview_items/diff_overlay_item.cpp` | Implement guide preview drawing |
| `eeschema/sch_edit_frame.h` | Add manager member, warning check |
| `eeschema/sch_edit_frame.cpp` | Rejection warning dialog, manager lifecycle |
| `eeschema/tools/sch_editor_control.cpp` | Context menu handlers |
| `eeschema/dialogs/dialog_symbol_properties.cpp` | Agent_Wiring field display |
| `agent/tools/tool_schemas.cpp` | Add sch_draft_circuit schema |
| `agent/tools/handlers/python_tool_handler.cpp` | Register tool |

---

## Summary

This tool fundamentally changes the agent's approach to circuit creation:

| Aspect | Old Approach (sch_place_companions) | New Approach (sch_draft_circuit) |
|--------|-------------------------------------|----------------------------------|
| Symbol placement | Multiple tool calls | Single call |
| Wiring | Automatic (buggy, loops) | User-controlled |
| Recommendations | Transient | Stored in symbol field |
| User control | None | Full (approve, reject, dismiss) |
| Diff integration | None | Full integration |
| Persistence | Lost on close | Saved with schematic |
| Token usage | High (loops on failures) | Low (one call + user action) |
