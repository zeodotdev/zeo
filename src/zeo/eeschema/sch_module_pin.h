/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef SCH_MODULE_PIN_H
#define SCH_MODULE_PIN_H

#include <kiid.h>
#include <sch_label.h>
#include <sch_sheet_pin.h>  // for SHEET_SIDE enum
#include <pin_type.h>      // ELECTRICAL_PINTYPE

class SCH_MODULE_BLOCK;


/**
 * One pin on a SCH_MODULE_BLOCK.
 *
 * Mirrors SCH_SHEET_PIN's design but is parented to a SCH_MODULE_BLOCK on a
 * multi-board schematic. Inherits SCH_HIERLABEL to reuse:
 *   - dangling-end tracking with SHEET_LABEL_END semantics
 *   - label rendering (text + outward-facing arrow graphic)
 *   - selection, snapping, hit-testing, and connectivity plumbing
 *
 * The pin anchor is AT the block edge; the arrow graphic protrudes OUTWARD
 * from the block (same inversion trick as SCH_SHEET_PIN::CreateGraphicShape).
 */
class SCH_MODULE_PIN : public SCH_HIERLABEL
{
public:
    SCH_MODULE_PIN( SCH_MODULE_BLOCK* aParent = nullptr,
                    const VECTOR2I&   aPos    = VECTOR2I( 0, 0 ),
                    const wxString&   aText   = wxEmptyString );

    ~SCH_MODULE_PIN() override = default;

    static inline bool ClassOf( const EDA_ITEM* aItem )
    {
        return aItem && SCH_MODULE_PIN_T == aItem->Type();
    }

    wxString GetClass() const override { return wxT( "SCH_MODULE_PIN" ); }

    wxString GetFriendlyName() const override { return _( "Module Block Pin" ); }

    bool IsMovableFromAnchorPoint() const override { return true; }

    /**
     * Outward-facing arrow identical to SCH_SHEET_PIN: the base SCH_HIERLABEL
     * shapes are used but INPUT/OUTPUT are swapped so the arrow points AWAY
     * from the module block rather than INTO it.
     */
    void CreateGraphicShape( const RENDER_SETTINGS* aSettings, std::vector<VECTOR2I>& aPoints,
                             const VECTOR2I& aPos ) const override;

    int GetPenWidth() const override;

    void SetSide( SHEET_SIDE aEdge );
    SHEET_SIDE GetSide() const { return m_side; }

    /**
     * Clamp the pin to the nearest edge of the parent module block.
     */
    void ConstrainOnEdge( const VECTOR2I& aPos, bool aAllowEdgeSwitch );

    SCH_MODULE_BLOCK* GetParent() const { return (SCH_MODULE_BLOCK*) m_parent; }

    // Extra metadata tying the pin back to its originating connector in the
    // sub-project (e.g. component J1, pin "3"). Not used for rendering.
    const KIID&     GetPinUuid()      const { return m_pinUuid; }
    void            SetPinUuid( const KIID& aUuid ) { m_pinUuid = aUuid; }

    const wxString& GetComponentRef() const { return m_componentRef; }
    void            SetComponentRef( const wxString& aRef ) { m_componentRef = aRef; }

    const wxString& GetPinNumber()    const { return m_pinNumber; }
    void            SetPinNumber( const wxString& aNum ) { m_pinNumber = aNum; }

    /**
     * Electrical pin type sourced from the connector symbol's pin
     * definition on the sub-project. Mirrors SCH_PIN's `m_type`. Used by
     * MBS-side ERC to drive the standard pin-to-pin connectivity matrix
     * (output↔output collisions, power-in not driven, etc.) — same
     * machinery as regular schematic ERC, just including module pins.
     *
     * Defaults to PT_PASSIVE so blocks created before pin types are
     * scanned trigger no spurious matrix violations.
     */
    ELECTRICAL_PINTYPE GetType() const { return m_electricalType; }
    void               SetType( ELECTRICAL_PINTYPE aType ) { m_electricalType = aType; }

    // SCH_ITEM / EDA_ITEM overrides ------------------------------------------

    void Move( const VECTOR2I& aMoveVector ) override { Offset( aMoveVector ); }

    void MirrorVertically( int aCenter ) override;
    void MirrorHorizontally( int aCenter ) override;
    void Rotate( const VECTOR2I& aCenter, bool aRotateCCW ) override;

    void GetEndPoints( std::vector<DANGLING_END_ITEM>& aItemList ) override;

    bool IsConnectable() const override { return true; }

    bool HasConnectivityChanges( const SCH_ITEM* aItem,
                                 const SCH_SHEET_PATH* aInstance = nullptr ) const override;

    wxString GetItemDescription( UNITS_PROVIDER* aUnitsProvider, bool aFull ) const override;

    BITMAPS GetMenuImage() const override;

    void SetPosition( const VECTOR2I& aPosition ) override { ConstrainOnEdge( aPosition, true ); }

    bool IsPointClickableAnchor( const VECTOR2I& aPos ) const override
    {
        return m_isDangling && GetPosition() == aPos;
    }

    bool HitTest( const VECTOR2I& aPosition, int aAccuracy = 0 ) const override;

    EDA_ITEM* Clone() const override;

    double Similarity( const SCH_ITEM& aOther ) const override;

    bool operator==( const SCH_ITEM& aOther ) const override;

protected:
    void swapData( SCH_ITEM* aItem ) override;

private:
    SHEET_SIDE m_side;

    KIID       m_pinUuid;
    wxString   m_componentRef;   ///< e.g. "J1" — source connector in the sub-project
    wxString   m_pinNumber;      ///< e.g. "3"  — pin number on that connector

    ELECTRICAL_PINTYPE m_electricalType;   ///< Mirrors SCH_PIN::m_type for ERC purposes
};

#endif // SCH_MODULE_PIN_H
