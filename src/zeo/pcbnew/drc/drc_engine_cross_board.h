/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef DRC_ENGINE_CROSS_BOARD_H
#define DRC_ENGINE_CROSS_BOARD_H

#include <kiid.h>
#include <wx/string.h>

#include <map>
#include <memory>
#include <vector>

class BOARD;
class PROJECT;
class REPORTER;


/**
 * Severity levels for cross-board DRC violations.
 */
#ifdef ERROR
#undef ERROR  // Windows wingdi.h defines ERROR as 0, conflicting with the enumerator below.
#endif
enum class CROSS_BOARD_DRC_SEVERITY
{
    INFO,
    WARNING,
    ERROR
};


/**
 * Category of cross-board DRC check.
 */
enum class CROSS_BOARD_DRC_TYPE
{
    CONNECTOR_PIN_COUNT_MISMATCH,
    CONNECTOR_NET_NAME_MISMATCH,
    CONNECTOR_ELECTRICAL_TYPE_MISMATCH,
    SIGNAL_LENGTH_EXCEEDED,
    SIGNAL_LENGTH_MISMATCH,
    POWER_INSUFFICIENT_PINS,
    POWER_VOLTAGE_DROP,
    NET_INCOMPLETE,
    NET_ORPHAN_CONNECTOR,
    UNASSIGNED_CONNECTOR_PIN
};


/**
 * A single cross-board DRC violation.
 */
struct CROSS_BOARD_DRC_ITEM
{
    CROSS_BOARD_DRC_TYPE        type;
    CROSS_BOARD_DRC_SEVERITY    severity;
    wxString                    message;
    wxString                    details;

    // Location info
    KIID                        board1Uuid;
    wxString                    board1Name;
    KIID                        board2Uuid;
    wxString                    board2Name;
    wxString                    connectorRef;
    wxString                    pinNumber;
    wxString                    netName;

    CROSS_BOARD_DRC_ITEM() :
            type( CROSS_BOARD_DRC_TYPE::NET_INCOMPLETE ),
            severity( CROSS_BOARD_DRC_SEVERITY::WARNING )
    {}

    wxString GetTypeString() const;
    wxString GetSeverityString() const;
};


/**
 * Rules for cross-board connector matching.
 */
struct CROSS_BOARD_CONNECTOR_RULES
{
    bool checkPinCountMatch = true;
    bool checkNetNameMatch = true;
    bool checkElectricalTypeMatch = true;

    // Net name aliases (e.g., "DATA_OUT" ↔ "DATA_IN")
    std::map<wxString, wxString> netAliases;
};


/**
 * Rules for cross-board signal integrity.
 */
struct CROSS_BOARD_SIGNAL_RULES
{
    bool    enabled = false;

    // Maximum total trace length across all boards (nm)
    int     maxTotalLength = 0;

    // Length matching tolerance for differential pairs (nm)
    int     diffPairLengthTolerance = 0;

    // Maximum stub length at connector transition (nm)
    int     maxConnectorStub = 0;

    // Impedance continuity check
    bool    checkImpedanceContinuity = false;
    int     impedanceTolerancePercent = 10;
};


/**
 * Rules for cross-board power distribution.
 */
struct CROSS_BOARD_POWER_RULES
{
    bool enabled = false;

    // Minimum connector pins for power nets (net name → min pins)
    std::map<wxString, int> minPowerPins;

    // Current capacity validation
    bool    checkCurrentCapacity = false;
    double  pinCurrentRatingAmps = 1.0;

    // Voltage drop check
    bool    checkVoltageDrop = false;
    double  maxVoltageDropMv = 100.0;
};


/**
 * Performs DRC checks that span multiple boards.
 *
 * This engine validates:
 * - Connector pin matching between boards
 * - Net name consistency across connectors
 * - Signal integrity constraints (length, impedance)
 * - Power distribution adequacy
 * - Cross-board net completeness
 */
class DRC_ENGINE_CROSS_BOARD
{
public:
    /**
     * Create a cross-board DRC engine.
     * @param aProject The project containing all boards
     */
    explicit DRC_ENGINE_CROSS_BOARD( PROJECT* aProject );

    ~DRC_ENGINE_CROSS_BOARD() = default;

    /**
     * Set the reporter for progress/status messages.
     */
    void SetReporter( REPORTER* aReporter ) { m_reporter = aReporter; }

    /**
     * Set connector matching rules.
     */
    void SetConnectorRules( const CROSS_BOARD_CONNECTOR_RULES& aRules )
    {
        m_connectorRules = aRules;
    }

    /**
     * Set signal integrity rules.
     */
    void SetSignalRules( const CROSS_BOARD_SIGNAL_RULES& aRules )
    {
        m_signalRules = aRules;
    }

    /**
     * Set power distribution rules.
     */
    void SetPowerRules( const CROSS_BOARD_POWER_RULES& aRules )
    {
        m_powerRules = aRules;
    }

    /**
     * Run all enabled cross-board DRC checks.
     */
    void RunAllChecks();

    /**
     * Check connector matching (pin counts, net names, electrical types).
     */
    void CheckConnectorMatching();

    /**
     * Check signal integrity constraints.
     */
    void CheckSignalIntegrity();

    /**
     * Check power distribution adequacy.
     */
    void CheckPowerDistribution();

    /**
     * Check that all cross-board nets are complete.
     */
    void CheckNetCompleteness();

    /**
     * Get all violations found during checks.
     */
    const std::vector<CROSS_BOARD_DRC_ITEM>& GetViolations() const { return m_violations; }

    /**
     * Clear all violations.
     */
    void ClearViolations() { m_violations.clear(); }

    /**
     * Get count of errors (highest severity).
     */
    int GetErrorCount() const;

    /**
     * Get count of warnings.
     */
    int GetWarningCount() const;

    /**
     * Pre-seed the board cache with an in-memory BOARD so the engine
     * uses the live, possibly-unsaved state for that sub-project rather
     * than re-reading the .kicad_pcb file from disk. The engine does
     * not take ownership.
     */
    void RegisterInMemoryBoard( const KIID& aSubProjectUuid, BOARD* aBoard )
    {
        if( aBoard )
            m_boardCache[aSubProjectUuid] = aBoard;
    }

private:
    /**
     * Load a board by UUID from the project.
     */
    BOARD* GetBoardByUuid( const KIID& aBoardUuid );

    /**
     * Add a violation to the results.
     */
    void AddViolation( CROSS_BOARD_DRC_TYPE aType, CROSS_BOARD_DRC_SEVERITY aSeverity,
                       const wxString& aMessage, const wxString& aDetails = wxEmptyString );

    /**
     * Check if two net names match (considering aliases).
     */
    bool NetNamesMatch( const wxString& aNet1, const wxString& aNet2 ) const;

    PROJECT*                            m_project;
    REPORTER*                           m_reporter;

    // Rules
    CROSS_BOARD_CONNECTOR_RULES         m_connectorRules;
    CROSS_BOARD_SIGNAL_RULES            m_signalRules;
    CROSS_BOARD_POWER_RULES             m_powerRules;

    // Results
    std::vector<CROSS_BOARD_DRC_ITEM>   m_violations;

    // Cached board pointers. Entries either alias an externally-owned
    // BOARD or point into m_loadedSubBoards (owned by this engine).
    std::map<KIID, BOARD*>                m_boardCache;

    // Sub-project boards loaded on demand via the multi-board sub-project
    // loader. Owner-managed per R9 of MULTI_BOARD_REFACTOR_PLAN.md.
    std::vector<std::unique_ptr<BOARD>>   m_loadedSubBoards;

    // Current violation context
    KIID                                m_currentBoard1Uuid;
    wxString                            m_currentBoard1Name;
    KIID                                m_currentBoard2Uuid;
    wxString                            m_currentBoard2Name;
    wxString                            m_currentConnectorRef;
};

#endif // DRC_ENGINE_CROSS_BOARD_H
