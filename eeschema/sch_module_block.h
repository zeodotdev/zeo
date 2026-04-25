/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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
 */

#ifndef SCH_MODULE_BLOCK_H
#define SCH_MODULE_BLOCK_H

#include <sch_item.h>

#include <kiid.h>
#include <wx/string.h>

#include <vector>

class SCH_MODULE_PIN;


/**
 * A visual placeholder for a sub-project on a multi-board schematic (MBS).
 *
 * `SCH_MODULE_BLOCK` renders as a labeled rectangle whose edges carry
 * hierarchical-label-style pins (SCH_MODULE_PIN), one per interface
 * connector pin of the referenced sub-project. Wires drawn between pins
 * of different module blocks constitute cross-board nets.
 *
 * Pins are owned SCH_HIERLABEL-derived children; their anchor point sits
 * AT the block edge (not outside it) so wires terminate at the edge just
 * like with a hierarchical sheet.
 */
class SCH_MODULE_BLOCK : public SCH_ITEM
{
public:
    // Schematic internal units are 100 nm per IU (see SCH_IU_PER_MM).
    // 1 mm == 10 000 IU.  1 mil == 254 IU.
    static constexpr int DEFAULT_WIDTH   = 400000;   ///< 40 mm
    static constexpr int DEFAULT_HEIGHT  = 600000;   ///< 60 mm

    SCH_MODULE_BLOCK( const VECTOR2I& aPos = VECTOR2I( 0, 0 ) );

    SCH_MODULE_BLOCK( const SCH_MODULE_BLOCK& aOther );
    SCH_MODULE_BLOCK& operator=( const SCH_MODULE_BLOCK& aOther );

    ~SCH_MODULE_BLOCK() override;

    static inline bool ClassOf( const EDA_ITEM* aItem )
    {
        return aItem && SCH_MODULE_BLOCK_T == aItem->Type();
    }

    wxString GetClass() const override { return wxT( "SCH_MODULE_BLOCK" ); }

    // Accessors --------------------------------------------------------------

    const KIID&     GetSubProjectUuid() const { return m_subProjectUuid; }
    void SetSubProjectUuid( const KIID& aUuid ) { m_subProjectUuid = aUuid; }

    const wxString& GetSubProjectPath() const { return m_subProjectPath; }
    void SetSubProjectPath( const wxString& aPath ) { m_subProjectPath = aPath; }

    /**
     * Connector reference that this block represents on the sub-project,
     * e.g. "J1". Sourced from the sub-project's connector symbol and
     * considered read-only in the MBS — renaming happens on the sub-
     * project schematic, then flows in via RefreshMbsFromSubProjects.
     */
    const wxString& GetComponentRef() const { return m_componentRef; }
    void SetComponentRef( const wxString& aRef ) { m_componentRef = aRef; }

    /**
     * MBS-scoped annotation (e.g. "B1"). Unique across the multi-board
     * schematic, assigned by the annotator. Gives cross-probing and
     * display code a stable identifier that doesn't collide when two
     * sub-projects happen to share a connector ref.
     */
    const wxString& GetMbsReference() const { return m_mbsReference; }
    void SetMbsReference( const wxString& aRef ) { m_mbsReference = aRef; }

    const wxString& GetDisplayName() const { return m_displayName; }
    void SetDisplayName( const wxString& aName ) { m_displayName = aName; }

    const VECTOR2I& GetSize() const { return m_size; }
    void SetSize( const VECTOR2I& aSize ) { m_size = aSize; }

    const std::vector<SCH_MODULE_PIN*>& GetPins() const { return m_pins; }
    std::vector<SCH_MODULE_PIN*>&       GetPins() { return m_pins; }

    /**
     * Take ownership of the pin and register it as a child of this block.
     * The pin's parent pointer is updated to `this`.
     */
    void AddPin( SCH_MODULE_PIN* aPin );

    /**
     * Remove the pin from the block and delete it. Returns true if removed.
     */
    bool RemovePin( SCH_MODULE_PIN* aPin );

    // SCH_ITEM required overrides --------------------------------------------

    const BOX2I GetBoundingBox() const override;

    std::vector<int> ViewGetLayers() const override;

    VECTOR2I GetPosition() const override { return m_pos; }
    void     SetPosition( const VECTOR2I& aPos ) override;

    void Move( const VECTOR2I& aDelta ) override;

    void MirrorHorizontally( int aCenter ) override {}
    void MirrorVertically( int aCenter ) override {}
    void Rotate( const VECTOR2I& aCenter, bool aRotateCCW ) override {}

    bool HitTest( const VECTOR2I& aPosition, int aAccuracy = 0 ) const override;
    bool HitTest( const BOX2I& aRect, bool aContained, int aAccuracy = 0 ) const override;

    // Connectivity -----------------------------------------------------------

    bool IsConnectable() const override { return true; }

    void RunOnChildren( const std::function<void( SCH_ITEM* )>& aFunction,
                        RECURSE_MODE aMode ) override;

    /**
     * Surface child pins to the inspector when requested, mirroring
     * SCH_SHEET::Visit. Without this, the selection collector only
     * ever sees the block itself (because pins aren't in the screen
     * RTree as direct items) and pin-priority fall-through in
     * GuessSelectionCandidates never fires for module pins.
     */
    INSPECT_RESULT Visit( INSPECTOR aInspector, void* testData,
                          const std::vector<KICAD_T>& aScanTypes ) override;

    bool CanConnect( const SCH_ITEM* aItem ) const override
    {
        return ( aItem->Type() == SCH_LINE_T && aItem->GetLayer() == LAYER_WIRE )
               || aItem->Type() == SCH_NO_CONNECT_T
               || aItem->Type() == SCH_JUNCTION_T
               || aItem->Type() == SCH_LABEL_T
               || aItem->Type() == SCH_GLOBAL_LABEL_T
               || aItem->Type() == SCH_HIER_LABEL_T;
    }

    std::vector<VECTOR2I> GetConnectionPoints() const override;

    void GetEndPoints( std::vector<DANGLING_END_ITEM>& aItemList ) override;

    bool HasConnectivityChanges( const SCH_ITEM* aItem,
                                 const SCH_SHEET_PATH* aInstance = nullptr ) const override;

private:
    bool doIsConnected( const VECTOR2I& aPosition ) const override;

    EDA_ITEM* Clone() const override;

    wxString GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const override;

    BITMAPS GetMenuImage() const override;

    double Similarity( const SCH_ITEM& aOther ) const override;
    bool operator==( const SCH_ITEM& aOther ) const override;

    void Plot( PLOTTER* aPlotter, bool aBackground, const SCH_PLOT_OPTS& aPlotOpts,
               int aUnit, int aBodyStyle, const VECTOR2I& aOffset, bool aDimmed ) override
    {
    }

    void Serialize( google::protobuf::Any& aContainer ) const override {}
    bool Deserialize( const google::protobuf::Any& aContainer ) override { return false; }

#if defined(DEBUG)
    void Show( int nestLevel, std::ostream& os ) const override { ShowDummy( os ); }
#endif

protected:
    void swapData( SCH_ITEM* aItem ) override;

private:
    void clearPins();
    void copyPinsFrom( const SCH_MODULE_BLOCK& aOther );

    VECTOR2I                     m_pos;              ///< Top-left corner
    VECTOR2I                     m_size;             ///< Width x Height
    KIID                         m_subProjectUuid;   ///< Sub-project reference
    wxString                     m_subProjectPath;   ///< Path relative to MBS dir
    wxString                     m_componentRef;     ///< Sub-project local connector ref, e.g. "J1"
    wxString                     m_mbsReference;     ///< MBS-scoped annotation, e.g. "B1"
    wxString                     m_displayName;      ///< "fc/J1", "Flight Controller", etc.
    std::vector<SCH_MODULE_PIN*> m_pins;             ///< Owned pin children
};

#endif // SCH_MODULE_BLOCK_H
