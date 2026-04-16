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
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef KICAD_MULTI_BOARD_PROJECT_H
#define KICAD_MULTI_BOARD_PROJECT_H

#include <kicommon.h>
#include <kiid.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <vector>


/**
 * Identifies one sub-project (a standard single-board project) that is a
 * member of a multi-board project.
 *
 * The sub-project is a normal `.kicad_pro` that remains valid as a
 * standalone project; the multi-board container merely references it by
 * relative path.
 */
struct KICOMMON_API SUB_PROJECT_INFO
{
    KIID     uuid;            ///< Stable identifier within the multi-board project
    wxString name;            ///< Short name / directory basename, e.g. "fc"
    wxString relativePath;    ///< Path to the `.kicad_pro` relative to the multi-board
                              ///< container directory, e.g. "boards/fc/fc.kicad_pro"
    wxString displayName;     ///< Human-friendly name, e.g. "Flight Controller"
    wxString role;            ///< Reserved (e.g. "standard" / "reference"); default "standard"

    SUB_PROJECT_INFO() : role( wxT( "standard" ) ) {}

    bool operator==( const SUB_PROJECT_INFO& aOther ) const
    {
        return uuid == aOther.uuid
               && name == aOther.name
               && relativePath == aOther.relativePath
               && displayName == aOther.displayName
               && role == aOther.role;
    }
};


/**
 * One endpoint of a cross-board net: identifies a specific connector pin
 * on a sub-project.
 */
struct KICOMMON_API CROSS_BOARD_NET_ENDPOINT
{
    KIID     subProjectUuid;    ///< Which sub-project (MULTI_BOARD_PROJECT::GetSubProject)
    wxString componentRef;      ///< Connector reference on that sub-project, e.g. "J1"
    wxString pinNumber;         ///< Pin number on that connector, e.g. "3"
    wxString pinName;           ///< Pin display name / net name, e.g. "UART_TX"

    bool operator==( const CROSS_BOARD_NET_ENDPOINT& aOther ) const
    {
        return subProjectUuid == aOther.subProjectUuid
               && componentRef == aOther.componentRef
               && pinNumber == aOther.pinNumber
               && pinName == aOther.pinName;
    }
};


/**
 * A net that physically connects pins on two or more sub-project boards.
 *
 * Derived from the multi-board schematic (MBS) topology. Refreshed on MBS
 * save; consumed by cross-board ERC and the PCB netlist sync flows.
 */
struct KICOMMON_API CROSS_BOARD_NET
{
    KIID     uuid;      ///< Stable identifier across re-extractions
    wxString name;      ///< Net name (label text or auto-generated)
    std::vector<CROSS_BOARD_NET_ENDPOINT> endpoints;

    bool operator==( const CROSS_BOARD_NET& aOther ) const
    {
        return uuid == aOther.uuid && name == aOther.name && endpoints == aOther.endpoints;
    }
};


/**
 * Container for an Altium-style multi-board project.
 *
 * A multi-board project groups several standalone single-board projects
 * (each a normal `.kicad_pro`) under one parent container. The container
 * file is a `.kicad_multi` JSON document that lists the sub-projects by
 * relative path and references the multi-board schematic (`.kicad_mbs`)
 * and optional 3D assembly file.
 *
 * This class owns the in-memory representation of that container and
 * provides load/save round-trip plus sub-project management. It is
 * intentionally light-weight and independent of PROJECT / PROJECT_FILE,
 * since a multi-board project does not replace those — each sub-project
 * still has its own PROJECT instance when opened.
 */
class KICOMMON_API MULTI_BOARD_PROJECT
{
public:
    /// Current on-disk schema version. Increment when making breaking changes.
    static constexpr int CURRENT_VERSION = 1;

    MULTI_BOARD_PROJECT();

    /**
     * Load a multi-board project from a `.kicad_multi` file on disk.
     *
     * @param aPath absolute path to the `.kicad_multi` file.
     * @return true on success; false leaves the object in a defined-empty state.
     */
    bool LoadFromFile( const wxString& aPath );

    /**
     * Serialize the project to the given `.kicad_multi` path.
     */
    bool SaveToFile( const wxString& aPath ) const;

    const KIID&     GetUuid() const { return m_uuid; }

    const wxString& GetName() const { return m_name; }
    void SetName( const wxString& aName ) { m_name = aName; }

    const wxString& GetMbsFileName() const { return m_mbsFileName; }
    void SetMbsFileName( const wxString& aName ) { m_mbsFileName = aName; }

    const wxString& Get3dAssemblyFileName() const { return m_3dAssemblyFileName; }
    void Set3dAssemblyFileName( const wxString& aName ) { m_3dAssemblyFileName = aName; }

    /**
     * Directory containing the `.kicad_multi` file, used to resolve relative
     * sub-project paths. Set automatically by LoadFromFile / SaveToFile.
     */
    const wxFileName& GetRootDir() const { return m_rootDir; }
    void SetRootDir( const wxFileName& aDir ) { m_rootDir = aDir; }

    const std::vector<SUB_PROJECT_INFO>& GetSubProjects() const { return m_subProjects; }

    SUB_PROJECT_INFO*       GetSubProject( const KIID& aUuid );
    const SUB_PROJECT_INFO* GetSubProject( const KIID& aUuid ) const;

    void AddSubProject( const SUB_PROJECT_INFO& aInfo );
    bool RemoveSubProject( const KIID& aUuid );

    /**
     * Resolve a sub-project entry's relative path to an absolute filesystem path.
     */
    wxFileName ResolveSubProjectPath( const SUB_PROJECT_INFO& aInfo ) const;

    /**
     * Resolve the multi-board schematic (MBS) file to an absolute path.
     *
     * If `m_mbsFileName` is empty this returns an empty wxFileName. Callers
     * typically run EnsureMbsFile() first.
     */
    wxFileName ResolveMbsPath() const;

    /**
     * Make sure the multi-board schematic file exists on disk. If
     * `m_mbsFileName` is empty it will be populated (default:
     * `<containerBasename>_mbs.kicad_sch`). If the file does not exist yet
     * a minimal valid KiCad s-expression schematic is written so it can be
     * opened by eeschema immediately.
     *
     * @param aContainerBasename the basename of the `.kicad_multi` file
     *                           (used to pick a default MBS filename)
     * @return absolute path to the MBS file on success, empty on failure.
     */
    wxFileName EnsureMbsFile( const wxString& aContainerBasename );

    // Cross-board nets (extracted from MBS topology) --------------------------

    const std::vector<CROSS_BOARD_NET>& GetCrossBoardNets() const { return m_crossBoardNets; }

    /**
     * Replace the cross-board nets with the given list. Typically called
     * right after the MBS is saved, with the output of ExtractCrossBoardNets.
     */
    void SetCrossBoardNets( std::vector<CROSS_BOARD_NET> aNets )
    {
        m_crossBoardNets = std::move( aNets );
    }

private:
    KIID        m_uuid;
    wxString    m_name;
    wxFileName  m_rootDir;
    wxString    m_mbsFileName;
    wxString    m_3dAssemblyFileName;
    std::vector<SUB_PROJECT_INFO> m_subProjects;
    std::vector<CROSS_BOARD_NET>  m_crossBoardNets;
};

#endif // KICAD_MULTI_BOARD_PROJECT_H
