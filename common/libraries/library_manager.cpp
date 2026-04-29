/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * @author Jon Evans <jon@craftyjon.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <chrono>
#include <common.h>
#include <env_vars.h>
#include <fstream>
#include <list>
#include <magic_enum.hpp>
#include <nlohmann/json.hpp>
#include <thread_pool.h>
#include <ranges>
#include <unordered_set>

#include <paths.h>
#include <pgm_base.h>
#include <project.h>
#include <project/project_file.h>
#include <richio.h>
#include <trace_helpers.h>
#include <wildcards_and_files_ext.h>

#include <libraries/library_manager.h>

using namespace std::chrono_literals;
#include <settings/kicad_settings.h>
#include <settings/settings_manager.h>
#include <wx/dir.h>
#include <wx/log.h>

struct LIBRARY_MANAGER_INTERNALS
{
    std::vector<LIBRARY_TABLE> tables;
};


LIBRARY_MANAGER::LIBRARY_MANAGER()
{
}


LIBRARY_MANAGER::~LIBRARY_MANAGER() = default;


void LIBRARY_MANAGER::loadTables( const wxString& aTablePath, LIBRARY_TABLE_SCOPE aScope,
                                  std::vector<LIBRARY_TABLE_TYPE> aTablesToLoad )
{
    auto getTarget =
            [&]() -> std::map<LIBRARY_TABLE_TYPE, std::unique_ptr<LIBRARY_TABLE>>&
            {
                switch( aScope )
                {
                case LIBRARY_TABLE_SCOPE::GLOBAL:
                    return m_tables;

                case LIBRARY_TABLE_SCOPE::PROJECT:
                    return m_projectTables;

                default:
                    wxCHECK_MSG( false, m_tables, "Invalid scope passed to loadTables" );
                }
            };

    std::map<LIBRARY_TABLE_TYPE, std::unique_ptr<LIBRARY_TABLE>>& aTarget = getTarget();

    if( aTablesToLoad.size() == 0 )
        aTablesToLoad = { LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_TYPE::FOOTPRINT, LIBRARY_TABLE_TYPE::DESIGN_BLOCK };

    for( LIBRARY_TABLE_TYPE type : aTablesToLoad )
    {
        aTarget.erase( type );

        wxFileName fn( aTablePath, tableFileName( type ) );

        if( fn.IsFileReadable() )
        {
            std::unique_ptr<LIBRARY_TABLE> table = std::make_unique<LIBRARY_TABLE>( fn, aScope );

            if( table->Type() != type )
            {
                auto actualName = magic_enum::enum_name( table->Type() );
                auto expectedName = magic_enum::enum_name( type );
                wxLogWarning( wxS( "Library table '%s' has type %s but expected %s; skipping" ),
                              fn.GetFullPath(),
                              wxString( actualName.data(), actualName.size() ),
                              wxString( expectedName.data(), expectedName.size() ) );
                continue;
            }

            aTarget[type] = std::move( table );
            loadNestedTables( *aTarget[type] );
        }
        else
        {
            wxLogTrace( traceLibraries, "No library table found at %s", fn.GetFullPath() );
        }
    }
}


void LIBRARY_MANAGER::loadNestedTables( LIBRARY_TABLE& aRootTable )
{
    std::unordered_set<wxString> seenTables;

    std::function<void( LIBRARY_TABLE& )> processOneTable =
            [&]( LIBRARY_TABLE& aTable )
            {
                seenTables.insert( aTable.Path() );

                if( !aTable.IsOk() )
                    return;

                for( LIBRARY_TABLE_ROW& row : aTable.Rows() )
                {
                    if( row.Type() == LIBRARY_TABLE_ROW::TABLE_TYPE_NAME )
                    {
                        wxFileName file( ExpandURI( row.URI(), Pgm().GetSettingsManager().Prj() ) );

                        // URI may be relative to parent
                        file.MakeAbsolute( wxFileName( aTable.Path() ).GetPath() );

                        WX_FILENAME::ResolvePossibleSymlinks( file );
                        wxString src = file.GetFullPath();

                        if( seenTables.contains( src ) )
                        {
                            wxLogTrace( traceLibraries, "Library table %s has already been loaded!", src );
                            row.SetOk( false );
                            row.SetErrorDescription( _( "A reference to this library table already exists" ) );
                            continue;
                        }

                        auto child = std::make_unique<LIBRARY_TABLE>( file, aRootTable.Scope() );

                        processOneTable( *child );

                        if( !child->IsOk() )
                        {
                            row.SetOk( false );
                            row.SetErrorDescription( child->ErrorDescription() );
                        }

                        m_childTables.insert( { row.URI(), std::move( child ) } );
                    }
                }
            };

    processOneTable( aRootTable );
}


wxString LIBRARY_MANAGER::tableFileName( LIBRARY_TABLE_TYPE aType )
{
    switch( aType )
    {
    case LIBRARY_TABLE_TYPE::SYMBOL:        return FILEEXT::SymbolLibraryTableFileName;
    case LIBRARY_TABLE_TYPE::FOOTPRINT:     return FILEEXT::FootprintLibraryTableFileName;
    case LIBRARY_TABLE_TYPE::DESIGN_BLOCK:  return FILEEXT::DesignBlockLibraryTableFileName;
    default:                                wxCHECK( false, wxEmptyString );
    }
}


void LIBRARY_MANAGER::createEmptyTable( LIBRARY_TABLE_TYPE aType, LIBRARY_TABLE_SCOPE aScope )
{
    if( aScope == LIBRARY_TABLE_SCOPE::GLOBAL )
    {
        wxCHECK( !m_tables.contains( aType ), /* void */ );
        wxFileName fn( PATHS::GetUserSettingsPath(), tableFileName( aType ) );

        m_tables[aType] = std::make_unique<LIBRARY_TABLE>( fn, LIBRARY_TABLE_SCOPE::GLOBAL );
        m_tables[aType]->SetType( aType );
    }
    else if( aScope == LIBRARY_TABLE_SCOPE::PROJECT )
    {
        wxCHECK( !m_projectTables.contains( aType ), /* void */ );
        wxFileName fn( Pgm().GetSettingsManager().Prj().GetProjectDirectory(), tableFileName( aType ) );

        m_projectTables[aType] = std::make_unique<LIBRARY_TABLE>( fn, LIBRARY_TABLE_SCOPE::PROJECT );
        m_projectTables[aType]->SetType( aType );
    }
}


class PCM_LIB_TRAVERSER final : public wxDirTraverser
{
public:
    explicit PCM_LIB_TRAVERSER( const wxString& aBasePath, LIBRARY_MANAGER& aManager,
                                const wxString& aPrefix ) :
            m_manager( aManager ),
            m_project( Pgm().GetSettingsManager().Prj() ),
            m_path_prefix( aBasePath ),
            m_lib_prefix( aPrefix )
    {
        wxFileName f( aBasePath, "" );
        m_prefix_dir_count = f.GetDirCount();

        m_symbolTable = m_manager.Table( LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_SCOPE::GLOBAL ).value_or( nullptr );
        m_fpTable = m_manager.Table( LIBRARY_TABLE_TYPE::FOOTPRINT, LIBRARY_TABLE_SCOPE::GLOBAL ).value_or( nullptr );
        m_designBlockTable = m_manager.Table( LIBRARY_TABLE_TYPE::DESIGN_BLOCK, LIBRARY_TABLE_SCOPE::GLOBAL )
                                      .value_or( nullptr );
    }

    /// Handles symbol library files, minimum nest level 2
    wxDirTraverseResult OnFile( const wxString& aFilePath ) override
    {
        wxFileName file = wxFileName::FileName( aFilePath );

        // consider a file to be a lib if it's name ends with .kicad_sym and
        // it is under $KICADn_3RD_PARTY/symbols/<pkgid>/ i.e. has nested level of at least +2
        if( file.GetExt() == wxT( "kicad_sym" )
                && file.GetDirCount() >= m_prefix_dir_count + 2
                && file.GetDirs()[m_prefix_dir_count] == wxT( "symbols" ) )
        {
            addRowIfNecessary( m_symbolTable, file, ADD_MODE::AM_FILE, 10 );
        }

        return wxDIR_CONTINUE;
    }

    /// Handles footprint library and design block library directories, minimum nest level 3
    wxDirTraverseResult OnDir( const wxString& dirPath ) override
    {
        static wxString designBlockExt = wxString::Format( wxS( ".%s" ), FILEEXT::KiCadDesignBlockLibPathExtension );
        wxFileName dir = wxFileName::DirName( dirPath );

        // consider a directory to be a lib if it's name ends with .pretty and
        // it is under $KICADn_3RD_PARTY/footprints/<pkgid>/ i.e. has nested level of at least +3
        if( dirPath.EndsWith( wxS( ".pretty" ) )
                && dir.GetDirCount() >= m_prefix_dir_count + 3
                && dir.GetDirs()[m_prefix_dir_count] == wxT( "footprints" ) )
        {
            addRowIfNecessary( m_fpTable, dir, ADD_MODE::AM_DIRECTORY, 7 );
        }
        else if( dirPath.EndsWith( designBlockExt )
                 && dir.GetDirCount() >= m_prefix_dir_count + 3
                 && dir.GetDirs()[m_prefix_dir_count] == wxT( "design_blocks" ) )
        {
            addRowIfNecessary( m_designBlockTable, dir, ADD_MODE::AM_DIRECTORY, designBlockExt.Len() );
        }

        return wxDIR_CONTINUE;
    }

    std::set<LIBRARY_TABLE*> Modified() const { return m_modified; }

private:
    void ensureUnique( LIBRARY_TABLE* aTable, const wxString& aBaseName, wxString& aNickname ) const
    {
        if( aTable->HasRow( aNickname ) )
        {
            int increment = 1;

            do
            {
                aNickname = wxString::Format( "%s%s_%d", m_lib_prefix, aBaseName, increment );
                increment++;
            } while( aTable->HasRow( aNickname ) );
        }
    }

    enum class ADD_MODE
    {
        AM_FILE,
        AM_DIRECTORY
    };

    void addRowIfNecessary( LIBRARY_TABLE* aTable, const wxFileName& aSource, ADD_MODE aMode,
                            int aExtensionLength )
    {
        wxString versionedPath = wxString::Format( wxS( "${%s}" ),
        ENV_VAR::GetVersionedEnvVarName( wxS( "3RD_PARTY" ) ) );

        wxArrayString parts = aSource.GetDirs();
        parts.RemoveAt( 0, m_prefix_dir_count );
        parts.Insert( versionedPath, 0 );

        if( aMode == ADD_MODE::AM_FILE )
            parts.Add( aSource.GetFullName() );

        wxString libPath = wxJoin( parts, '/' );

        if( !aTable->HasRowWithURI( libPath, m_project ) )
        {
            wxString name = parts.Last().substr( 0, parts.Last().length() - aExtensionLength );
            wxString nickname = wxString::Format( "%s%s", m_lib_prefix, name );

            ensureUnique( aTable, name, nickname );

            wxLogTrace( traceLibraries, "Manager: Adding PCM lib '%s' as '%s'", libPath, nickname );

            LIBRARY_TABLE_ROW& row = aTable->InsertRow();

            row.SetNickname( nickname );
            row.SetURI( libPath );
            row.SetType( wxT( "KiCad" ) );
            row.SetDescription( _( "Added by Plugin and Content Manager" ) );
            m_modified.insert( aTable );
        }
        else
        {
            wxLogTrace( traceLibraries, "Manager: Not adding existing PCM lib '%s'", libPath );
        }
    }

private:
    LIBRARY_MANAGER& m_manager;
    const PROJECT&   m_project;
    wxString         m_path_prefix;
    wxString         m_lib_prefix;
    size_t           m_prefix_dir_count;
    std::set<LIBRARY_TABLE*> m_modified;

    LIBRARY_TABLE*   m_symbolTable;
    LIBRARY_TABLE*   m_fpTable;
    LIBRARY_TABLE*   m_designBlockTable;
};


wxString LIBRARY_MANAGER::DefaultGlobalTablePath( LIBRARY_TABLE_TYPE aType )
{
    wxString basePath = PATHS::GetUserSettingsPath();

    wxFileName fn( basePath, tableFileName( aType ) );
    fn.Normalize( FN_NORMALIZE_FLAGS | wxPATH_NORM_ENV_VARS );

    return fn.GetFullPath();
}


bool LIBRARY_MANAGER::IsTableValid( const wxString& aPath )
{
    if( wxFileName fn( aPath ); fn.IsFileReadable() )
    {
        LIBRARY_TABLE temp( fn, LIBRARY_TABLE_SCOPE::GLOBAL );

        if( temp.IsOk() )
            return true;
    }

    return false;
}


bool LIBRARY_MANAGER::GlobalTablesValid()
{
    return InvalidGlobalTables().empty();
}


std::vector<LIBRARY_TABLE_TYPE> LIBRARY_MANAGER::InvalidGlobalTables()
{
    std::vector<LIBRARY_TABLE_TYPE> invalidTables;
    wxString basePath = PATHS::GetUserSettingsPath();

    for( LIBRARY_TABLE_TYPE tableType : { LIBRARY_TABLE_TYPE::SYMBOL,
                                          LIBRARY_TABLE_TYPE::FOOTPRINT,
                                          LIBRARY_TABLE_TYPE::DESIGN_BLOCK } )
    {
        wxFileName fn( basePath, tableFileName( tableType ) );

        if( !IsTableValid( fn.GetFullPath() ) )
            invalidTables.emplace_back( tableType );
    }

    return invalidTables;
}


bool LIBRARY_MANAGER::CreateGlobalTable( LIBRARY_TABLE_TYPE aType, bool aPopulateDefaultLibraries )
{
    wxFileName fn( DefaultGlobalTablePath( aType ) );

    LIBRARY_TABLE table( fn, LIBRARY_TABLE_SCOPE::GLOBAL );
    table.SetType( aType );
    table.Rows().clear();

    wxFileName defaultLib( PATHS::GetStockTemplatesPath(), tableFileName( aType ) );

    if( !defaultLib.IsFileReadable() )
    {
        wxLogTrace( traceLibraries, "Warning: couldn't read default library table for %s at '%s'",
                    magic_enum::enum_name( aType ), defaultLib.GetFullPath() );
    }

    if( aPopulateDefaultLibraries )
    {
        LIBRARY_TABLE_ROW& chained = table.InsertRow();
        chained.SetType( LIBRARY_TABLE_ROW::TABLE_TYPE_NAME );
        chained.SetNickname( wxT( "KiCad" ) );
        chained.SetDescription( _( "KiCad Default Libraries" ) );
        chained.SetURI( defaultLib.GetFullPath() );
    }

    try
    {
        PRETTIFIED_FILE_OUTPUTFORMATTER formatter( fn.GetFullPath(), KICAD_FORMAT::FORMAT_MODE::LIBRARY_TABLE );
        table.Format( &formatter );
    }
    catch( IO_ERROR& e )
    {
        wxLogTrace( traceLibraries, "Exception while saving: %s", e.What() );
        return false;
    }

    return true;
}


void LIBRARY_MANAGER::LoadGlobalTables( std::initializer_list<LIBRARY_TABLE_TYPE> aTablesToLoad )
{
    // Cancel any in-progress load
    {
        std::scoped_lock lock( m_adaptersMutex );

        for( const std::unique_ptr<LIBRARY_MANAGER_ADAPTER>& adapter : m_adapters | std::views::values )
            adapter->GlobalTablesChanged( aTablesToLoad );
    }

    loadTables( PATHS::GetUserSettingsPath(), LIBRARY_TABLE_SCOPE::GLOBAL, aTablesToLoad );

    SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();
    KICAD_SETTINGS*   settings = mgr.GetAppSettings<KICAD_SETTINGS>( "kicad" );

    wxCHECK( settings, /* void */ );

    const ENV_VAR_MAP& vars = Pgm().GetLocalEnvVariables();
    std::optional<wxString> packagesPath = ENV_VAR::GetVersionedEnvVarValue( vars, wxT( "3RD_PARTY" ) );

    if( packagesPath && settings->m_PcmLibAutoAdd )
    {
        // Scan for libraries in PCM packages directory
        wxFileName d( *packagesPath, "" );

        if( d.DirExists() )
        {
            PCM_LIB_TRAVERSER traverser( *packagesPath, *this, settings->m_PcmLibPrefix );
            wxDir             dir( d.GetPath() );

            dir.Traverse( traverser );

            for( LIBRARY_TABLE* table : traverser.Modified() )
            {
                table->Save().map_error(
                        []( const LIBRARY_ERROR& aError )
                        {
                            wxLogTrace( traceLibraries, wxT( "Warning: save failed after PCM auto-add: %s" ),
                                        aError.message );
                        } );
            }
        }
    }

    auto cleanupRemovedPCMLibraries =
            [&]( LIBRARY_TABLE_TYPE aType )
            {
                LIBRARY_TABLE* table = Table( aType, LIBRARY_TABLE_SCOPE::GLOBAL ).value_or( nullptr );
                wxCHECK( table, /* void */ );

                auto toErase = std::ranges::remove_if( table->Rows(),
                        [&]( const LIBRARY_TABLE_ROW& aRow )
                        {
                            wxString path = GetFullURI( &aRow, true );
                            return path.StartsWith( *packagesPath ) && !wxFile::Exists( path );
                        } );

                table->Rows().erase( toErase.begin(), toErase.end() );

                if( !toErase.empty() )
                {
                    table->Save().map_error(
                            []( const LIBRARY_ERROR& aError )
                            {
                                wxLogTrace( traceLibraries, wxT( "Warning: save failed after PCM auto-remove: %s" ),
                                            aError.message );
                            } );
                }
            };

    if( packagesPath && settings->m_PcmLibAutoRemove )
    {
        cleanupRemovedPCMLibraries( LIBRARY_TABLE_TYPE::SYMBOL );
        cleanupRemovedPCMLibraries( LIBRARY_TABLE_TYPE::FOOTPRINT );
        cleanupRemovedPCMLibraries( LIBRARY_TABLE_TYPE::DESIGN_BLOCK );
    }
}


void LIBRARY_MANAGER::LoadProjectTables( std::initializer_list<LIBRARY_TABLE_TYPE> aTablesToLoad )
{
    LoadProjectTables( Pgm().GetSettingsManager().Prj().GetProjectDirectory(), aTablesToLoad );
}


void LIBRARY_MANAGER::ProjectChanged()
{
    LoadProjectTables( Pgm().GetSettingsManager().Prj().GetProjectDirectory() );

    std::scoped_lock lock( m_adaptersMutex );

    for( const std::unique_ptr<LIBRARY_MANAGER_ADAPTER>& adapter : m_adapters | std::views::values )
        adapter->ProjectChanged();
}


void LIBRARY_MANAGER::AbortAsyncLoads()
{
    std::scoped_lock lock( m_adaptersMutex );

    for( const std::unique_ptr<LIBRARY_MANAGER_ADAPTER>& adapter : m_adapters | std::views::values )
        adapter->AbortAsyncLoad();
}


void LIBRARY_MANAGER::RegisterAdapter( LIBRARY_TABLE_TYPE aType,
                                       std::unique_ptr<LIBRARY_MANAGER_ADAPTER>&& aAdapter )
{
    std::scoped_lock lock( m_adaptersMutex );

    wxCHECK_MSG( !m_adapters.contains( aType ), /**/, "You should only register an adapter once!" );

    m_adapters[aType] = std::move( aAdapter );
}


bool LIBRARY_MANAGER::RemoveAdapter( LIBRARY_TABLE_TYPE aType, LIBRARY_MANAGER_ADAPTER* aAdapter )
{
    std::scoped_lock lock( m_adaptersMutex );
    if( !m_adapters.contains( aType ) )
        return false;

    if( m_adapters[aType].get() != aAdapter )
        return false;

    m_adapters.erase( aType );
    return true;
}


std::optional<LIBRARY_MANAGER_ADAPTER*> LIBRARY_MANAGER::Adapter( LIBRARY_TABLE_TYPE aType ) const
{
    std::scoped_lock lock( m_adaptersMutex );

    if( m_adapters.contains( aType ) )
        return m_adapters.at( aType ).get();

    return std::nullopt;
}


std::optional<LIBRARY_TABLE*> LIBRARY_MANAGER::Table( LIBRARY_TABLE_TYPE aType,
                                                      LIBRARY_TABLE_SCOPE aScope )
{
    switch( aScope )
    {
    case LIBRARY_TABLE_SCOPE::BOTH:
    case LIBRARY_TABLE_SCOPE::UNINITIALIZED:
        wxCHECK_MSG( false, std::nullopt, "Table() requires a single scope" );

    case LIBRARY_TABLE_SCOPE::GLOBAL:
    {
        if( !m_tables.contains( aType ) )
        {
            wxLogTrace( traceLibraries, "WARNING: missing global table (%s)",
                        magic_enum::enum_name( aType ) );
            return std::nullopt;
        }

        return m_tables.at( aType ).get();
    }

    case LIBRARY_TABLE_SCOPE::PROJECT:
    {
        // TODO: handle multiple projects
        if( !m_projectTables.contains( aType ) )
        {
            if( !Pgm().GetSettingsManager().Prj().IsNullProject() )
                createEmptyTable( aType, LIBRARY_TABLE_SCOPE::PROJECT );
            else
                return std::nullopt;
        }

        return m_projectTables.at( aType ).get();
    }
    }

    return std::nullopt;
}


std::vector<LIBRARY_TABLE_ROW*> LIBRARY_MANAGER::Rows( LIBRARY_TABLE_TYPE aType, LIBRARY_TABLE_SCOPE aScope,
                                                       bool aIncludeInvalid ) const
{
    std::map<wxString, LIBRARY_TABLE_ROW*> rows;
    std::vector<wxString> rowOrder;

    std::list<std::ranges::ref_view<const std::map<LIBRARY_TABLE_TYPE, std::unique_ptr<LIBRARY_TABLE>>>> tables;

    switch( aScope )
    {
    case LIBRARY_TABLE_SCOPE::GLOBAL:
        tables = { std::views::all( m_tables ) };
        break;

    case LIBRARY_TABLE_SCOPE::PROJECT:
        tables = { std::views::all( m_projectTables ) };
        break;

    case LIBRARY_TABLE_SCOPE::BOTH:
        tables = { std::views::all( m_tables ), std::views::all( m_projectTables ) };
        break;

    case LIBRARY_TABLE_SCOPE::UNINITIALIZED:
        wxFAIL;
    }

    std::function<void(const std::unique_ptr<LIBRARY_TABLE>&)> processTable =
            [&]( const std::unique_ptr<LIBRARY_TABLE>& aTable )
            {
                if( aTable->Type() != aType )
                    return;

                if( aTable->IsOk() || aIncludeInvalid )
                {
                    for( LIBRARY_TABLE_ROW& row : aTable->Rows() )
                    {
                        if( row.IsOk() || aIncludeInvalid )
                        {
                            if( row.Type() == LIBRARY_TABLE_ROW::TABLE_TYPE_NAME )
                            {
                                if( !m_childTables.contains( row.URI() ) )
                                    continue;

                                // Don't include libraries from disabled or hidden nested tables
                                if( row.Disabled() || row.Hidden() )
                                    continue;

                                processTable( m_childTables.at( row.URI() ) );
                            }
                            else
                            {
                                if( !rows.contains( row.Nickname() ) )
                                    rowOrder.emplace_back( row.Nickname() );

                                rows[ row.Nickname() ] = &row;
                            }
                        }
                    }
                }
            };

    for( const std::unique_ptr<LIBRARY_TABLE>& table :
         std::views::join( tables ) | std::views::values )
    {
        processTable( table );
    }

    std::vector<LIBRARY_TABLE_ROW*> ret;

    for( const wxString& row : rowOrder )
        ret.emplace_back( rows[row] );

    return ret;
}


std::optional<LIBRARY_TABLE_ROW*> LIBRARY_MANAGER::GetRow( LIBRARY_TABLE_TYPE  aType, const wxString& aNickname,
                                                           LIBRARY_TABLE_SCOPE aScope ) const
{
    for( LIBRARY_TABLE_ROW* row : Rows( aType, aScope, true ) )
    {
        if( row->Nickname() == aNickname )
            return row;
    }

    return std::nullopt;
}


std::optional<LIBRARY_TABLE_ROW*> LIBRARY_MANAGER::FindRowByURI( LIBRARY_TABLE_TYPE aType,
        const wxString& aUri,
        LIBRARY_TABLE_SCOPE aScope ) const
{
    for( LIBRARY_TABLE_ROW* row : Rows( aType, aScope, true ) )
    {
        if( UrisAreEquivalent( GetFullURI( row, true ), aUri ) )
            return row;
    }

    return std::nullopt;
}


void LIBRARY_MANAGER::ReloadLibraryEntry( LIBRARY_TABLE_TYPE aType, const wxString& aNickname,
                                          LIBRARY_TABLE_SCOPE aScope )
{
    if( std::optional<LIBRARY_MANAGER_ADAPTER*> adapter = Adapter( aType ); adapter )
        ( *adapter )->ReloadLibraryEntry( aNickname, aScope );
}


// =============================================================================
// Multi-board container library replication (M7.1.A)
// =============================================================================

namespace
{
struct CONTAINER_SCOPE_TARGETS
{
    wxFileName              containerProPath;  ///< invalid when no container
    std::vector<wxFileName> peerProPaths;      ///< sub-project .kicad_pro paths
};


/**
 * Resolve the multi-board container scope for @a aSubject and enumerate
 * its sub-project members. @a aSubject may be the container itself
 * (MBSCH frame) or any sub-project.
 *
 * Reads the container's `.kicad_pro` directly via JSON (not through
 * `SETTINGS_MANAGER::LoadProject`), so the container PROJECT is never
 * pulled into memory just to support library replication. This matches
 * the locked-in decision that sub-projects stay individually openable.
 */
CONTAINER_SCOPE_TARGETS resolveContainerScope( const PROJECT& aSubject )
{
    CONTAINER_SCOPE_TARGETS result;

    wxFileName containerPro;

    if( aSubject.GetProjectFile().IsMultiBoardContainer() )
    {
        containerPro = wxFileName( aSubject.GetProjectFullName() );
    }
    else
    {
        wxString containerPath = aSubject.GetContainerProjectPath();

        if( containerPath.IsEmpty() )
            return result;

        containerPro = wxFileName( containerPath );
    }

    containerPro.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    result.containerProPath = containerPro;

    std::ifstream stream( containerPro.GetFullPath().fn_str() );

    if( !stream.is_open() )
        return result;

    nlohmann::json j;

    try
    {
        stream >> j;
    }
    catch( const nlohmann::json::exception& )
    {
        return result;
    }

    auto mb = j.find( "multi_board" );

    if( mb == j.end() || !mb->is_object() )
        return result;

    auto subs = mb->find( "sub_projects" );

    if( subs == mb->end() || !subs->is_array() )
        return result;

    wxFileName containerDir( containerPro );
    containerDir.SetFullName( wxEmptyString );

    for( const auto& sub : *subs )
    {
        // Field name is `path` in the JSON (see SUB_PROJECT_INFO's
        // `to_json` in project_file.cpp).
        auto rel = sub.find( "path" );

        if( rel == sub.end() || !rel->is_string() )
            continue;

        wxString relPath = wxString::FromUTF8( rel->get<std::string>().c_str() );
        wxFileName resolved( relPath );

        if( !resolved.IsAbsolute() )
            resolved.MakeAbsolute( containerDir.GetFullPath() );

        resolved.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
        result.peerProPaths.emplace_back( resolved );
    }

    return result;
}


wxFileName libTableFilePath( const wxFileName& aProjectPro, LIBRARY_TABLE_TYPE aType )
{
    wxString fileName;

    switch( aType )
    {
    case LIBRARY_TABLE_TYPE::SYMBOL:
        fileName = FILEEXT::SymbolLibraryTableFileName;
        break;

    case LIBRARY_TABLE_TYPE::FOOTPRINT:
        fileName = FILEEXT::FootprintLibraryTableFileName;
        break;

    case LIBRARY_TABLE_TYPE::DESIGN_BLOCK:
        fileName = FILEEXT::DesignBlockLibraryTableFileName;
        break;

    default:
        return wxFileName();
    }

    wxFileName projectDir( aProjectPro );
    projectDir.SetFullName( wxEmptyString );
    return wxFileName( projectDir.GetFullPath(), fileName );
}


/// Build a `shared=true` copy of @a aSrc suitable for a peer's lib-table.
/// Conflict rows carry the same content but never serve lookups.
LIBRARY_TABLE_ROW buildSharedRowCopy( const LIBRARY_TABLE_ROW& aSrc, bool aConflict )
{
    LIBRARY_TABLE_ROW row;
    row.SetNickname( aSrc.Nickname() );
    row.SetURI( aSrc.URI() );
    row.SetType( aSrc.Type() );
    row.SetOptions( aSrc.Options() );
    row.SetDescription( aSrc.Description() );
    row.SetDisabled( aSrc.Disabled() );
    row.SetHidden( aSrc.Hidden() );
    row.SetShared( true );
    row.SetConflict( aConflict );
    row.SetOk();
    row.SetScope( LIBRARY_TABLE_SCOPE::PROJECT );
    return row;
}


/// True when @a aActive points at the same `.kicad_pro` as @a aTarget.
bool isSameProject( const wxString& aActiveFullName, const wxFileName& aTarget )
{
    if( aActiveFullName.IsEmpty() )
        return false;

    wxFileName active( aActiveFullName );
    active.Normalize( wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS );
    return active.SameAs( aTarget );
}


/// True when two rows have the same library content (URI/type/options).
/// Nickname is excluded — it's the lookup key, not part of the content.
/// Used to decide whether a non-shared local row in a sub-project is
/// already in-sync with the container's row (e.g. after unshare).
bool rowsHaveSameContent( const LIBRARY_TABLE_ROW& aLhs, const LIBRARY_TABLE_ROW& aRhs )
{
    return aLhs.URI() == aRhs.URI()
        && aLhs.Type() == aRhs.Type()
        && aLhs.Options() == aRhs.Options();
}
} // anonymous namespace


void LIBRARY_MANAGER::LoadProjectTables( const wxString& aProjectPath,
                                         std::initializer_list<LIBRARY_TABLE_TYPE> aTablesToLoad )
{
    if( wxFileName::IsDirReadable( aProjectPath ) )
    {
        loadTables( aProjectPath, LIBRARY_TABLE_SCOPE::PROJECT, aTablesToLoad );

        // M7.1.A: pull in shared rows from the multi-board container, if
        // any. No-op for standalone projects or for the container itself.
        // Reconciles each table type that was actually loaded.
        std::vector<LIBRARY_TABLE_TYPE> reconcileTypes( aTablesToLoad );

        if( reconcileTypes.empty() )
            reconcileTypes = { LIBRARY_TABLE_TYPE::SYMBOL,
                               LIBRARY_TABLE_TYPE::FOOTPRINT,
                               LIBRARY_TABLE_TYPE::DESIGN_BLOCK };

        for( LIBRARY_TABLE_TYPE type : reconcileTypes )
            reconcileSharedRowsWithContainer( type );
    }
    else
    {
        m_projectTables.clear();
        wxLogTrace( traceLibraries, "New project path %s is not readable, not loading project tables", aProjectPath );
    }
}


void LIBRARY_MANAGER::reconcileSharedRowsWithContainer( LIBRARY_TABLE_TYPE aType )
{
    PROJECT& project = Pgm().GetSettingsManager().Prj();

    if( project.IsNullProject() )
        return;

    // The container is its own source of truth — nothing to reconcile.
    if( project.GetProjectFile().IsMultiBoardContainer() )
        return;

    wxString containerPath = project.GetContainerProjectPath();

    if( containerPath.IsEmpty() )
        return;

    if( !m_projectTables.contains( aType ) )
        return;

    LIBRARY_TABLE& projectTable = *m_projectTables.at( aType );

    // Read the container's lib-table directly, without loading the
    // container PROJECT. Locked-in decision #8: each sub-project stays
    // individually openable, so the container's lifetime is decoupled
    // from sub-project library resolution.
    wxFileName containerProFn( containerPath );
    wxFileName containerTablePath = libTableFilePath( containerProFn, aType );

    if( !containerTablePath.IsOk() || !containerTablePath.FileExists() )
    {
        // Container has no lib-table of this type. Drop any orphaned
        // shared rows we may have inherited from a previous container
        // state.
        std::vector<LIBRARY_TABLE_ROW>& rows = projectTable.Rows();
        size_t                          before = rows.size();

        rows.erase( std::remove_if( rows.begin(), rows.end(),
                            []( const LIBRARY_TABLE_ROW& row )
                            {
                                return row.Shared() || row.Conflict();
                            } ),
                    rows.end() );

        if( rows.size() != before )
        {
            projectTable.SetType( aType );
            (void) projectTable.Save();
        }

        return;
    }

    LIBRARY_TABLE containerTable( containerTablePath, LIBRARY_TABLE_SCOPE::PROJECT );

    if( !containerTable.IsOk() )
        return;

    std::vector<LIBRARY_TABLE_ROW>& projectRows = projectTable.Rows();
    bool                            changed = false;

    // Pass 1: drop shared/conflict rows whose nickname is no longer in
    // the container — the source disappeared, the copy follows.
    {
        std::unordered_set<wxString> containerNicknames;

        for( const LIBRARY_TABLE_ROW& row : containerTable.Rows() )
            containerNicknames.insert( row.Nickname() );

        size_t before = projectRows.size();

        projectRows.erase( std::remove_if( projectRows.begin(), projectRows.end(),
                                  [&]( const LIBRARY_TABLE_ROW& row )
                                  {
                                      if( !row.Shared() && !row.Conflict() )
                                          return false;

                                      return !containerNicknames.contains( row.Nickname() );
                                  } ),
                           projectRows.end() );

        if( projectRows.size() != before )
            changed = true;
    }

    // Pass 2: add or refresh each container row in the project table.
    for( const LIBRARY_TABLE_ROW& containerRow : containerTable.Rows() )
    {
        LIBRARY_TABLE_ROW* existing = nullptr;
        LIBRARY_TABLE_ROW* localMatch = nullptr;

        // Walk all rows — there can be both a shared/conflict row AND a
        // local non-shared row with the same nickname (the post-unshare
        // or post-conflict state).
        for( LIBRARY_TABLE_ROW& row : projectRows )
        {
            if( row.Nickname() != containerRow.Nickname() )
                continue;

            if( row.Shared() || row.Conflict() )
                existing = &row;
            else
                localMatch = &row;
        }

        bool localCollision = ( localMatch != nullptr )
                              && !rowsHaveSameContent( *localMatch, containerRow );

        if( existing )
        {
            // Refresh content if drifted.
            bool needsUpdate = existing->URI() != containerRow.URI()
                            || existing->Type() != containerRow.Type()
                            || existing->Options() != containerRow.Options()
                            || existing->Description() != containerRow.Description()
                            || existing->Conflict() != localCollision
                            || !existing->Shared();

            if( needsUpdate )
            {
                existing->SetURI( containerRow.URI() );
                existing->SetType( containerRow.Type() );
                existing->SetOptions( containerRow.Options() );
                existing->SetDescription( containerRow.Description() );
                existing->SetShared( true );
                existing->SetConflict( localCollision );
                changed = true;
            }
        }
        else if( localMatch && !localCollision )
        {
            // Local row already carries the same content as the container
            // row (post-unshare steady state). Nothing to do — the user
            // has effectively opted out for this board, and the lib
            // resolves correctly via the local row.
        }
        else
        {
            // No existing shared row, and either no local row at all or
            // a local row with different content (genuine conflict).
            projectRows.emplace_back(
                    buildSharedRowCopy( containerRow, /*aConflict=*/localCollision ) );
            changed = true;
        }
    }

    if( changed )
    {
        projectTable.SetType( aType );
        (void) projectTable.Save();
    }
}


void LIBRARY_MANAGER::ReloadTables( LIBRARY_TABLE_SCOPE aScope,
                                    std::initializer_list<LIBRARY_TABLE_TYPE> aTablesToLoad )
{
    if( aScope == LIBRARY_TABLE_SCOPE::PROJECT )
        LoadProjectTables( aTablesToLoad );
    else
        LoadGlobalTables( aTablesToLoad );
}


std::optional<wxString> LIBRARY_MANAGER::GetFullURI( LIBRARY_TABLE_TYPE aType, const wxString& aNickname,
                                                     bool aSubstituted ) const
{
    if( std::optional<const LIBRARY_TABLE_ROW*> result = GetRow( aType, aNickname ) )
        return GetFullURI( *result, aSubstituted );

    return std::nullopt;
}


wxString LIBRARY_MANAGER::GetFullURI( const LIBRARY_TABLE_ROW* aRow, bool aSubstituted )
{
    if( aSubstituted )
        return ExpandEnvVarSubstitutions( aRow->URI(), &Pgm().GetSettingsManager().Prj() );

    return aRow->URI();
}


wxString LIBRARY_MANAGER::ExpandURI( const wxString& aShortURI, const PROJECT& aProject )
{
    wxLogNull doNotLog; // We do our own error reporting; we don't want to hear about missing envvars

    wxFileName path( ExpandEnvVarSubstitutions( aShortURI, &aProject ) );
    path.MakeAbsolute();
    return path.GetFullPath();
}


bool LIBRARY_MANAGER::UrisAreEquivalent( const wxString& aURI1, const wxString& aURI2 )
{
    // Avoid comparing filenames as wxURIs
    if( aURI1.Find( "://" ) != wxNOT_FOUND )
    {
        // found as full path
        return aURI1 == aURI2;
    }
    else
    {
        const wxFileName fn1( aURI1 );
        const wxFileName fn2( aURI2 );

        // This will also test if the file is a symlink so if we are comparing
        // a symlink to the same real file, the comparison will be true.  See
        // wxFileName::SameAs() in the wxWidgets source.

        // found as full path and file name
        return fn1 == fn2;
    }
}


//////  LIBRARY_MANAGER_ADAPTER
///
///


LIBRARY_MANAGER_ADAPTER::LIBRARY_MANAGER_ADAPTER( LIBRARY_MANAGER& aManager ) :
        m_manager( aManager )
{
}


LIBRARY_MANAGER_ADAPTER::~LIBRARY_MANAGER_ADAPTER()
{
}


LIBRARY_MANAGER& LIBRARY_MANAGER_ADAPTER::Manager() const
{
    return m_manager;
}


void LIBRARY_MANAGER_ADAPTER::ProjectChanged()
{
    abortLoad();

    {
        std::unique_lock lock( m_librariesMutex );
        m_libraries.clear();
    }
}


void LIBRARY_MANAGER_ADAPTER::AbortAsyncLoad()
{
    abortLoad();
}


void LIBRARY_MANAGER_ADAPTER::GlobalTablesChanged( std::initializer_list<LIBRARY_TABLE_TYPE> aChangedTables )
{
    bool me = aChangedTables.size() == 0;

    for( LIBRARY_TABLE_TYPE type : aChangedTables )
    {
        if( type == Type() )
        {
            me = true;
            break;
        }
    }

    if( !me )
        return;

    abortLoad();

    {
        std::unique_lock lock( globalLibsMutex() );
        globalLibs().clear();
    }
}


void LIBRARY_MANAGER_ADAPTER::CheckTableRow( LIBRARY_TABLE_ROW& aRow )
{
    abortLoad();

    LIBRARY_RESULT<IO_BASE*> plugin = createPlugin( &aRow );

    if( plugin.has_value() )
    {
        LIB_DATA lib;
        lib.row = &aRow;
        lib.plugin.reset( *plugin );

        std::optional<LIB_STATUS> status = LoadOne( &lib );

        if( status.has_value() )
        {
            aRow.SetOk( status.value().load_status == LOAD_STATUS::LOADED );

            if( status.value().error.has_value() )
                aRow.SetErrorDescription( status.value().error.value().message );
        }
    }
    else if( plugin.error().message == LIBRARY_TABLE_OK().message )
    {
        aRow.SetOk( true );
        aRow.SetErrorDescription( wxEmptyString );
    }
    else
    {
        aRow.SetOk( false );
        aRow.SetErrorDescription( plugin.error().message );
    }
}



LIBRARY_TABLE* LIBRARY_MANAGER_ADAPTER::GlobalTable() const
{
    wxCHECK( m_manager.Table( Type(), LIBRARY_TABLE_SCOPE::GLOBAL ), nullptr );
    return *m_manager.Table( Type(), LIBRARY_TABLE_SCOPE::GLOBAL );
}


std::optional<LIBRARY_TABLE*> LIBRARY_MANAGER_ADAPTER::ProjectTable() const
{
    return m_manager.Table( Type(), LIBRARY_TABLE_SCOPE::PROJECT );
}


std::optional<wxString> LIBRARY_MANAGER_ADAPTER::FindLibraryByURI( const wxString& aURI ) const
{
    for( const LIBRARY_TABLE_ROW* row : m_manager.Rows( Type() ) )
    {
        if( LIBRARY_MANAGER::UrisAreEquivalent( row->URI(), aURI ) )
            return row->Nickname();
    }

    return std::nullopt;
}


std::vector<wxString> LIBRARY_MANAGER_ADAPTER::GetLibraryNames() const
{
    std::vector<wxString> ret;
    std::vector<LIBRARY_TABLE_ROW*> rows = m_manager.Rows( Type() );

    wxLogTrace( traceLibraries, "GetLibraryNames: checking %zu rows from table", rows.size() );

    for( const LIBRARY_TABLE_ROW* row : rows )
    {
        wxString nickname = row->Nickname();
        std::optional<const LIB_DATA*> loaded = fetchIfLoaded( nickname );

        if( loaded )
            ret.emplace_back( nickname );
    }

    wxLogTrace( traceLibraries, "GetLibraryNames: returning %zu of %zu libraries", ret.size(), rows.size() );
    return ret;
}


bool LIBRARY_MANAGER_ADAPTER::HasLibrary( const wxString& aNickname, bool aCheckEnabled ) const
{
    std::optional<const LIB_DATA*> r = fetchIfLoaded( aNickname );

    if( r.has_value() )
        return !aCheckEnabled || !r.value()->row->Disabled();

    return false;
}


bool LIBRARY_MANAGER_ADAPTER::DeleteLibrary( const wxString& aNickname )
{
    if( LIBRARY_RESULT<LIB_DATA*> result = loadIfNeeded( aNickname ); result.has_value() )
    {
        LIB_DATA* data = *result;
        std::map<std::string, UTF8> options = data->row->GetOptionsMap();

        try
        {
            return data->plugin->DeleteLibrary( getUri( data->row ), &options );
        }
        catch( ... )
        {
            return false;
        }
    }

    return false;
}


std::optional<wxString> LIBRARY_MANAGER_ADAPTER::GetLibraryDescription( const wxString& aNickname ) const
{
    if( std::optional<const LIB_DATA*> optRow = fetchIfLoaded( aNickname ); optRow )
        return ( *optRow )->row->Description();

    return std::nullopt;
}


std::vector<LIBRARY_TABLE_ROW*> LIBRARY_MANAGER_ADAPTER::Rows( LIBRARY_TABLE_SCOPE aScope,
                                                               bool aIncludeInvalid ) const
{
    return m_manager.Rows( Type(), aScope, aIncludeInvalid );
}


std::optional<LIBRARY_TABLE_ROW*> LIBRARY_MANAGER_ADAPTER::GetRow( const wxString &aNickname,
                                                                   LIBRARY_TABLE_SCOPE aScope ) const
{
    return m_manager.GetRow( Type(), aNickname, aScope );
}


std::optional<LIBRARY_TABLE_ROW*> LIBRARY_MANAGER_ADAPTER::FindRowByURI(
        const wxString& aUri,
        LIBRARY_TABLE_SCOPE aScope ) const
{
    return m_manager.FindRowByURI( Type(), aUri, aScope );
}


void LIBRARY_MANAGER_ADAPTER::abortLoad()
{
    {
        std::lock_guard lock( m_loadMutex );

        if( m_futures.empty() )
            return;

        wxLogTrace( traceLibraries, "Aborting library load..." );
        m_abort.store( true );
    }

    BlockUntilLoaded();
    wxLogTrace( traceLibraries, "Aborted" );

    {
        std::lock_guard lock( m_loadMutex );
        m_abort.store( false );
        m_futures.clear();
        m_loadTotal.store( 0 );
        m_loadCount.store( 0 );
    }
}


std::optional<float> LIBRARY_MANAGER_ADAPTER::AsyncLoadProgress() const
{
    size_t total = m_loadTotal.load();

    if( total == 0 )
        return std::nullopt;

    size_t loaded = m_loadCount.load();
    return loaded / static_cast<float>( total );
}


void LIBRARY_MANAGER_ADAPTER::BlockUntilLoaded()
{
    wxLogTrace( traceLibraries, "BlockUntilLoaded: entry, acquiring m_loadMutex" );
    std::unique_lock<std::mutex> asyncLock( m_loadMutex );

    wxLogTrace( traceLibraries, "BlockUntilLoaded: waiting on %zu futures", m_futures.size() );

    for( const std::future<void>& future : m_futures )
        future.wait();

    wxLogTrace( traceLibraries, "BlockUntilLoaded: all futures complete, loadCount=%zu, loadTotal=%zu",
                m_loadCount.load(), m_loadTotal.load() );
}


bool LIBRARY_MANAGER_ADAPTER::IsLibraryLoaded( const wxString& aNickname )
{
    {
        std::shared_lock lock( m_librariesMutex );

        if( auto it = m_libraries.find( aNickname ); it != m_libraries.end() )
            return it->second.status.load_status == LOAD_STATUS::LOADED;
    }

    {
        std::shared_lock lock( globalLibsMutex() );

        if( auto it = globalLibs().find( aNickname ); it != globalLibs().end() )
            return it->second.status.load_status == LOAD_STATUS::LOADED;
    }

    return false;
}


std::optional<LIBRARY_ERROR> LIBRARY_MANAGER_ADAPTER::LibraryError( const wxString& aNickname ) const
{
    {
        std::shared_lock lock( m_librariesMutex );

        if( auto it = m_libraries.find( aNickname ); it != m_libraries.end() )
            return it->second.status.error;
    }

    {
        std::shared_lock lock( globalLibsMutex() );

        if( auto it = globalLibs().find( aNickname ); it != globalLibs().end() )
            return it->second.status.error;
    }

    return std::nullopt;
}


std::vector<std::pair<wxString, LIB_STATUS>> LIBRARY_MANAGER_ADAPTER::GetLibraryStatuses() const
{
    std::vector<std::pair<wxString, LIB_STATUS>> ret;

    for( const LIBRARY_TABLE_ROW* row : m_manager.Rows( Type() ) )
    {
        if( row->Disabled() )
            continue;

        if( std::optional<LIB_STATUS> result = GetLibraryStatus( row->Nickname() ) )
        {
            ret.emplace_back( std::make_pair( row->Nickname(), *result ) );
        }
        else
        {
            // This should probably never happen, but until that can be proved...
            ret.emplace_back( std::make_pair( row->Nickname(),
                                              LIB_STATUS( {
                                                  .load_status = LOAD_STATUS::LOAD_ERROR,
                                                  .error = LIBRARY_ERROR( _( "Library not found in library table" ) )
                                              } ) ) );
        }
    }

    return ret;
}


wxString LIBRARY_MANAGER_ADAPTER::GetLibraryLoadErrors() const
{
    wxString errors;

    for( const auto& [nickname, status] : GetLibraryStatuses() )
    {
        if( status.load_status == LOAD_STATUS::LOAD_ERROR && status.error )
        {
            if( !errors.IsEmpty() )
                errors += wxS( "\n" );

            errors += wxString::Format( _( "Library '%s': %s" ),
                                         nickname, status.error->message );
        }
    }

    return errors;
}


void LIBRARY_MANAGER_ADAPTER::ReloadLibraryEntry( const wxString& aNickname, LIBRARY_TABLE_SCOPE aScope )
{
    auto reloadScope =
            [&]( LIBRARY_TABLE_SCOPE aScopeToReload, std::map<wxString, LIB_DATA>& aTarget,
                 std::shared_mutex& aMutex )
            {
                bool wasLoaded = false;

                {
                    std::unique_lock lock( aMutex );
                    auto it = aTarget.find( aNickname );

                    if( it != aTarget.end() && it->second.plugin )
                    {
                        wasLoaded = true;
                        aTarget.erase( it );
                    }
                }

                if( wasLoaded )
                {
                    LIBRARY_RESULT<LIB_DATA*> result = loadFromScope( aNickname, aScopeToReload, aTarget, aMutex );

                    if( !result.has_value() )
                    {
                        wxLogTrace( traceLibraries, "ReloadLibraryEntry: failed to reload %s (%s): %s",
                                    aNickname, magic_enum::enum_name( aScopeToReload ),
                                    result.error().message );
                    }
                }
            };

    switch( aScope )
    {
    case LIBRARY_TABLE_SCOPE::GLOBAL:
        reloadScope( LIBRARY_TABLE_SCOPE::GLOBAL, globalLibs(), globalLibsMutex() );
        break;

    case LIBRARY_TABLE_SCOPE::PROJECT:
        reloadScope( LIBRARY_TABLE_SCOPE::PROJECT, m_libraries, m_librariesMutex );
        break;

    case LIBRARY_TABLE_SCOPE::BOTH:
    case LIBRARY_TABLE_SCOPE::UNINITIALIZED:
        reloadScope( LIBRARY_TABLE_SCOPE::PROJECT, m_libraries, m_librariesMutex );
        reloadScope( LIBRARY_TABLE_SCOPE::GLOBAL, globalLibs(), globalLibsMutex() );
        break;
    }
}


bool LIBRARY_MANAGER_ADAPTER::IsWritable( const wxString& aNickname ) const
{
    if( std::optional<const LIB_DATA*> result = fetchIfLoaded( aNickname ) )
    {
        const LIB_DATA* rowData = *result;
        return rowData->plugin->IsLibraryWritable( getUri( rowData->row ) );
    }

    return false;
}


bool LIBRARY_MANAGER_ADAPTER::CreateLibrary( const wxString& aNickname )
{
    if( LIBRARY_RESULT<LIB_DATA*> result = loadIfNeeded( aNickname ); result.has_value() )
    {
        LIB_DATA* data = *result;
        std::map<std::string, UTF8> options = data->row->GetOptionsMap();

        try
        {
            data->plugin->CreateLibrary( getUri( data->row ), &options );
            return true;
        }
        catch( ... )
        {
            return false;
        }
    }

    return false;
}


wxString LIBRARY_MANAGER_ADAPTER::getUri( const LIBRARY_TABLE_ROW* aRow )
{
    return LIBRARY_MANAGER::ExpandURI( aRow->URI(), Pgm().GetSettingsManager().Prj() );
}


std::optional<const LIB_DATA*> LIBRARY_MANAGER_ADAPTER::fetchIfLoaded( const wxString& aNickname ) const
{
    {
        std::shared_lock lock( m_librariesMutex );

        if( auto it = m_libraries.find( aNickname ); it != m_libraries.end() )
        {
            if( it->second.status.load_status == LOAD_STATUS::LOADED )
                return &it->second;

            return std::nullopt;
        }
    }

    {
        std::shared_lock lock( globalLibsMutex() );

        if( auto it = globalLibs().find( aNickname ); it != globalLibs().end() )
        {
            if( it->second.status.load_status == LOAD_STATUS::LOADED )
                return &it->second;

            return std::nullopt;
        }
    }

    return std::nullopt;
}


std::optional<LIB_DATA*> LIBRARY_MANAGER_ADAPTER::fetchIfLoaded( const wxString& aNickname )
{
    {
        std::shared_lock lock( m_librariesMutex );

        if( auto it = m_libraries.find( aNickname ); it != m_libraries.end() )
        {
            if( it->second.status.load_status == LOAD_STATUS::LOADED )
                return &it->second;

            return std::nullopt;
        }
    }

    {
        std::shared_lock lock( globalLibsMutex() );

        if( auto it = globalLibs().find( aNickname ); it != globalLibs().end() )
        {
            if( it->second.status.load_status == LOAD_STATUS::LOADED )
                return &it->second;

            return std::nullopt;
        }
    }

    return std::nullopt;
}


LIBRARY_RESULT<LIB_DATA*> LIBRARY_MANAGER_ADAPTER::loadFromScope( const wxString& aNickname,
                                                                  LIBRARY_TABLE_SCOPE aScope,
                                                                  std::map<wxString, LIB_DATA>& aTarget,
                                                                  std::shared_mutex& aMutex )
{
    bool present = false;

    {
        std::shared_lock lock( aMutex );
        present = aTarget.contains( aNickname ) && aTarget.at( aNickname ).plugin;
    }

    if( !present )
    {
        if( auto result = m_manager.GetRow( Type(), aNickname, aScope ) )
        {
            const LIBRARY_TABLE_ROW* row = *result;
            wxLogTrace( traceLibraries, "Library %s (%s) not yet loaded, will attempt...",
                        aNickname, magic_enum::enum_name( aScope ) );

            if( LIBRARY_RESULT<IO_BASE*> plugin = createPlugin( row ); plugin.has_value() )
            {
                std::unique_lock lock( aMutex );

                aTarget[ row->Nickname() ].status.load_status = LOAD_STATUS::LOADING;
                aTarget[ row->Nickname() ].row = row;
                aTarget[ row->Nickname() ].plugin.reset( *plugin );

                return &aTarget.at( aNickname );
            }
            else
            {
                return tl::unexpected( plugin.error() );
            }
        }

        return nullptr;
    }

    std::shared_lock lock( aMutex );
    return &aTarget.at( aNickname );
}


LIBRARY_RESULT<LIB_DATA*> LIBRARY_MANAGER_ADAPTER::loadIfNeeded( const wxString& aNickname )
{
    LIBRARY_RESULT<LIB_DATA*> result = loadFromScope( aNickname, LIBRARY_TABLE_SCOPE::PROJECT,
                                                      m_libraries, m_librariesMutex );

    if( !result.has_value() || *result )
        return result;

    result = loadFromScope( aNickname, LIBRARY_TABLE_SCOPE::GLOBAL, globalLibs(), globalLibsMutex() );

    if( !result.has_value() || *result )
        return result;

    wxString msg = wxString::Format( _( "Library %s not found" ), aNickname );
    return tl::unexpected( LIBRARY_ERROR( msg ) );
}


std::optional<LIB_STATUS> LIBRARY_MANAGER_ADAPTER::GetLibraryStatus( const wxString& aNickname ) const
{
    {
        std::shared_lock lock( m_librariesMutex );

        if( auto it = m_libraries.find( aNickname ); it != m_libraries.end() )
            return it->second.status;
    }

    {
        std::shared_lock lock( globalLibsMutex() );

        if( auto it = globalLibs().find( aNickname ); it != globalLibs().end() )
            return it->second.status;
    }

    return std::nullopt;
}


void LIBRARY_MANAGER_ADAPTER::AsyncLoad()
{
    std::unique_lock<std::mutex> asyncLock( m_loadMutex, std::try_to_lock );

    if( !asyncLock )
        return;

    std::erase_if( m_futures,
                   []( std::future<void>& aFuture )
                   {
                       return aFuture.valid()
                              && aFuture.wait_for( 0s ) == std::future_status::ready;
                   } );

    if( !m_futures.empty() )
    {
        wxLogTrace( traceLibraries, "Cannot AsyncLoad, futures from a previous call remain!" );
        return;
    }

    std::vector<LIBRARY_TABLE_ROW*> rows = m_manager.Rows( Type() );

    m_loadTotal.store( rows.size() );
    m_loadCount.store( 0 );

    if( rows.empty() )
    {
        wxLogTrace( traceLibraries, "AsyncLoad: no libraries left to load; exiting" );
        return;
    }

    thread_pool& tp = GetKiCadThreadPool();

    auto check =
            [&]( const wxString& aLib, std::map<wxString, LIB_DATA>& aMap, std::shared_mutex& aMutex )
            {
                std::shared_lock lock( aMutex );

                if( auto it = aMap.find( aLib ); it != aMap.end() )
                {
                    LOAD_STATUS status = it->second.status.load_status;

                    if( status == LOAD_STATUS::LOADED || status == LOAD_STATUS::LOADING )
                        return true;
                }

                return false;
            };

    for( const LIBRARY_TABLE_ROW* row : rows )
    {
        wxString nickname = row->Nickname();
        LIBRARY_TABLE_SCOPE scope = row->Scope();

        if( check( nickname, m_libraries, m_librariesMutex ) )
        {
            m_loadTotal.fetch_sub( 1 );
            continue;
        }

        if( check( nickname, globalLibs(), globalLibsMutex() ) )
        {
            m_loadTotal.fetch_sub( 1 );
            continue;
        }

        // Resolve the URI on the main thread. URI expansion accesses PROJECT data
        // (text variables, env vars) that is not thread-safe.
        wxString uri = getUri( row );

        m_futures.emplace_back( tp.submit_task(
                [this, nickname, scope, uri]()
                {
                    if( m_abort.load() )
                        return;

                    LIBRARY_RESULT<LIB_DATA*> result = loadIfNeeded( nickname );

                    if( result.has_value() )
                    {
                        LIB_DATA* lib = *result;

                        try
                        {
                            {
                                std::unique_lock lock( scope == LIBRARY_TABLE_SCOPE::GLOBAL ? globalLibsMutex()
                                                                                            : m_librariesMutex );
                                lib->status.load_status = LOAD_STATUS::LOADING;
                            }

                            enumerateLibrary( lib, uri );

                            {
                                std::unique_lock lock( scope == LIBRARY_TABLE_SCOPE::GLOBAL ? globalLibsMutex()
                                                                                            : m_librariesMutex );
                                lib->status.load_status = LOAD_STATUS::LOADED;
                            }
                        }
                        catch( IO_ERROR& e )
                        {
                            std::unique_lock lock( scope == LIBRARY_TABLE_SCOPE::GLOBAL ? globalLibsMutex()
                                                                                        : m_librariesMutex );
                            lib->status.load_status = LOAD_STATUS::LOAD_ERROR;
                            lib->status.error = LIBRARY_ERROR( { e.What() } );
                            wxLogTrace( traceLibraries, "%s: plugin threw exception: %s", nickname, e.What() );
                        }
                    }
                    else
                    {
                        std::unique_lock lock( scope == LIBRARY_TABLE_SCOPE::GLOBAL ? globalLibsMutex()
                                                                                    : m_librariesMutex );

                        std::map<wxString, LIB_DATA>& target = ( scope == LIBRARY_TABLE_SCOPE::GLOBAL ) ? globalLibs()
                                                                                                        : m_libraries;

                        target[nickname].status = LIB_STATUS( {
                                .load_status = LOAD_STATUS::LOAD_ERROR,
                                .error = result.error()
                        } );
                    }

                    ++m_loadCount;
                }, BS::pr::lowest ) );
    }

    size_t total = m_loadTotal.load();

    if( total )
        wxLogTrace( traceLibraries, "Started async load of %zu libraries", total );
}


LIBRARY_RESULT<void> LIBRARY_MANAGER::AddSharedLibrary( LIBRARY_TABLE_TYPE aType,
                                                        const LIBRARY_TABLE_ROW& aRow,
                                                        const PROJECT& aSubject )
{
    CONTAINER_SCOPE_TARGETS targets = resolveContainerScope( aSubject );

    if( !targets.containerProPath.IsOk() )
        return tl::unexpected( LIBRARY_ERROR(
                _( "Cannot add shared library: project has no multi-board container" ) ) );

    auto writeRowToTable = [&]( const wxFileName& aProjectPro,
                                bool aIsContainer ) -> LIBRARY_RESULT<void>
    {
        wxFileName tablePath = libTableFilePath( aProjectPro, aType );

        if( !tablePath.IsOk() )
            return tl::unexpected( LIBRARY_ERROR( _( "Invalid library table type" ) ) );

        // If the file doesn't exist, LIBRARY_TABLE leaves m_rows empty
        // and we'll create it on Save() below.
        LIBRARY_TABLE table( tablePath, LIBRARY_TABLE_SCOPE::PROJECT );
        table.SetType( aType );

        bool replaced = false;
        bool collisionWithLocal = false;

        for( LIBRARY_TABLE_ROW& existing : table.Rows() )
        {
            if( existing.Nickname() != aRow.Nickname() )
                continue;

            if( aIsContainer || existing.Shared() )
            {
                // Container row OR an already-shared peer row: refresh
                // content. Idempotent on repeated calls.
                existing.SetURI( aRow.URI() );
                existing.SetType( aRow.Type() );
                existing.SetOptions( aRow.Options() );
                existing.SetDescription( aRow.Description() );
                existing.SetConflict( false );
                replaced = true;
            }
            else
            {
                // Peer has a non-shared local row with the same nickname.
                // If its content already matches what we want to share,
                // promote it (set shared=true) — the user's two manual
                // adds are effectively the same row. Otherwise it's a
                // genuine collision and we'll emplace a conflict marker
                // alongside the local row below.
                if( existing.URI() == aRow.URI()
                    && existing.Type() == aRow.Type()
                    && existing.Options() == aRow.Options() )
                {
                    existing.SetDescription( aRow.Description() );
                    existing.SetShared( true );
                    existing.SetConflict( false );
                    replaced = true;
                }
                else
                {
                    collisionWithLocal = true;
                }
            }
            break;
        }

        if( !replaced )
        {
            if( aIsContainer )
            {
                LIBRARY_TABLE_ROW row = aRow;
                row.SetShared( false );
                row.SetConflict( false );
                row.SetOk();
                row.SetScope( LIBRARY_TABLE_SCOPE::PROJECT );
                table.Rows().emplace_back( row );
            }
            else
            {
                table.Rows().emplace_back(
                        buildSharedRowCopy( aRow, /*aConflict=*/collisionWithLocal ) );
            }
        }

        return table.Save();
    };

    // Container first, peers second. If the container write fails we
    // leave peers untouched rather than half-propagating a row that
    // never landed in the source of truth.
    LIBRARY_RESULT<void> r = writeRowToTable( targets.containerProPath, /*aIsContainer=*/true );

    if( !r.has_value() )
        return r;

    for( const wxFileName& peer : targets.peerProPaths )
    {
        r = writeRowToTable( peer, /*aIsContainer=*/false );

        if( !r.has_value() )
            return r;
    }

    // If the active project just had its on-disk lib-table changed,
    // refresh in-memory state so the UI reflects the new row immediately.
    PROJECT& activePrj = Pgm().GetSettingsManager().Prj();

    if( !activePrj.IsNullProject() )
    {
        wxString activeFullName = activePrj.GetProjectFullName();
        bool isTarget = isSameProject( activeFullName, targets.containerProPath );

        if( !isTarget )
        {
            for( const wxFileName& peer : targets.peerProPaths )
            {
                if( isSameProject( activeFullName, peer ) )
                {
                    isTarget = true;
                    break;
                }
            }
        }

        if( isTarget )
            LoadProjectTables( activePrj.GetProjectDirectory(), { aType } );
    }

    return LIBRARY_RESULT<void>();
}


LIBRARY_RESULT<void> LIBRARY_MANAGER::RemoveSharedLibrary( LIBRARY_TABLE_TYPE aType,
                                                           const wxString& aNickname,
                                                           const PROJECT& aSubject )
{
    CONTAINER_SCOPE_TARGETS targets = resolveContainerScope( aSubject );

    if( !targets.containerProPath.IsOk() )
        return tl::unexpected( LIBRARY_ERROR(
                _( "Cannot remove shared library: project has no multi-board container" ) ) );

    auto removeFromTable = [&]( const wxFileName& aProjectPro,
                                bool aIsContainer ) -> LIBRARY_RESULT<void>
    {
        wxFileName tablePath = libTableFilePath( aProjectPro, aType );

        if( !tablePath.IsOk() )
            return tl::unexpected( LIBRARY_ERROR( _( "Invalid library table type" ) ) );

        LIBRARY_TABLE table( tablePath, LIBRARY_TABLE_SCOPE::PROJECT );

        // Missing file is fine — there's nothing to remove.
        if( !table.IsOk() )
            return LIBRARY_RESULT<void>();

        table.SetType( aType );

        std::vector<LIBRARY_TABLE_ROW>& rows = table.Rows();
        bool changed = false;

        rows.erase( std::remove_if( rows.begin(), rows.end(),
                            [&]( const LIBRARY_TABLE_ROW& row )
                            {
                                if( row.Nickname() != aNickname )
                                    return false;

                                // Container: cascade-delete any matching row.
                                // Peer: only drop shared/conflict rows so a
                                // user's local row with the same nickname
                                // (collision case) is preserved.
                                if( aIsContainer || row.Shared() || row.Conflict() )
                                {
                                    changed = true;
                                    return true;
                                }

                                return false;
                            } ),
                    rows.end() );

        if( !changed )
            return LIBRARY_RESULT<void>();

        return table.Save();
    };

    LIBRARY_RESULT<void> r = removeFromTable( targets.containerProPath, /*aIsContainer=*/true );

    if( !r.has_value() )
        return r;

    for( const wxFileName& peer : targets.peerProPaths )
    {
        r = removeFromTable( peer, /*aIsContainer=*/false );

        if( !r.has_value() )
            return r;
    }

    PROJECT& activePrj = Pgm().GetSettingsManager().Prj();

    if( !activePrj.IsNullProject() )
        LoadProjectTables( activePrj.GetProjectDirectory(), { aType } );

    return LIBRARY_RESULT<void>();
}


void LIBRARY_MANAGER::PropagateContainerLibTable( LIBRARY_TABLE_TYPE aType )
{
    PROJECT& project = Pgm().GetSettingsManager().Prj();

    if( project.IsNullProject() || !project.GetProjectFile().IsMultiBoardContainer() )
        return;

    if( !m_projectTables.contains( aType ) )
        return;

    LIBRARY_TABLE& containerTable = *m_projectTables.at( aType );

    CONTAINER_SCOPE_TARGETS targets = resolveContainerScope( project );

    if( !targets.containerProPath.IsOk() )
        return;

    // Build the set of nicknames currently in the container's table —
    // peer rows whose nickname has dropped out get cleaned up.
    std::unordered_set<wxString> containerNicknames;

    for( const LIBRARY_TABLE_ROW& row : containerTable.Rows() )
        containerNicknames.insert( row.Nickname() );

    for( const wxFileName& peerPro : targets.peerProPaths )
    {
        wxFileName peerTablePath = libTableFilePath( peerPro, aType );

        if( !peerTablePath.IsOk() )
            continue;

        // Open peer's table from disk. If absent, LIBRARY_TABLE leaves
        // m_rows empty and we'll create the file on Save() below.
        LIBRARY_TABLE peerTable( peerTablePath, LIBRARY_TABLE_SCOPE::PROJECT );
        peerTable.SetType( aType );

        std::vector<LIBRARY_TABLE_ROW>& peerRows = peerTable.Rows();
        bool                            changed = false;

        // Drop orphaned shared/conflict rows whose nickname is no
        // longer in the container.
        size_t before = peerRows.size();

        peerRows.erase( std::remove_if( peerRows.begin(), peerRows.end(),
                                [&]( const LIBRARY_TABLE_ROW& row )
                                {
                                    if( !row.Shared() && !row.Conflict() )
                                        return false;

                                    return !containerNicknames.contains( row.Nickname() );
                                } ),
                        peerRows.end() );

        if( peerRows.size() != before )
            changed = true;

        // Add or refresh shared rows from the container. Mirror the
        // logic in reconcileSharedRowsWithContainer so unshared local
        // rows with matching content are left alone (they're already
        // in sync via the local entry).
        for( const LIBRARY_TABLE_ROW& containerRow : containerTable.Rows() )
        {
            LIBRARY_TABLE_ROW* existing = nullptr;
            LIBRARY_TABLE_ROW* localMatch = nullptr;

            for( LIBRARY_TABLE_ROW& row : peerRows )
            {
                if( row.Nickname() != containerRow.Nickname() )
                    continue;

                if( row.Shared() || row.Conflict() )
                    existing = &row;
                else
                    localMatch = &row;
            }

            bool localCollision = ( localMatch != nullptr )
                                  && !rowsHaveSameContent( *localMatch, containerRow );

            if( existing )
            {
                bool needsUpdate = existing->URI() != containerRow.URI()
                                || existing->Type() != containerRow.Type()
                                || existing->Options() != containerRow.Options()
                                || existing->Description() != containerRow.Description()
                                || existing->Conflict() != localCollision
                                || !existing->Shared();

                if( needsUpdate )
                {
                    existing->SetURI( containerRow.URI() );
                    existing->SetType( containerRow.Type() );
                    existing->SetOptions( containerRow.Options() );
                    existing->SetDescription( containerRow.Description() );
                    existing->SetShared( true );
                    existing->SetConflict( localCollision );
                    changed = true;
                }
            }
            else if( localMatch && !localCollision )
            {
                // Unshared local row already carries the same content —
                // user opted out on this board, leave it alone.
            }
            else
            {
                peerRows.emplace_back(
                        buildSharedRowCopy( containerRow, /*aConflict=*/localCollision ) );
                changed = true;
            }
        }

        if( changed )
            (void) peerTable.Save();
    }
}


LIBRARY_RESULT<void> LIBRARY_MANAGER::UnshareLibraryRow( LIBRARY_TABLE_TYPE aType,
                                                         const wxString& aNickname,
                                                         const PROJECT& aSubject )
{
    wxFileName subjectPro( aSubject.GetProjectFullName() );

    if( !subjectPro.IsOk() )
        return tl::unexpected( LIBRARY_ERROR( _( "Cannot unshare: invalid project" ) ) );

    wxFileName tablePath = libTableFilePath( subjectPro, aType );
    LIBRARY_TABLE table( tablePath, LIBRARY_TABLE_SCOPE::PROJECT );

    if( !table.IsOk() )
        return tl::unexpected( LIBRARY_ERROR(
                _( "Cannot unshare: project has no library table" ) ) );

    table.SetType( aType );

    bool changed = false;

    for( LIBRARY_TABLE_ROW& row : table.Rows() )
    {
        if( row.Nickname() == aNickname && ( row.Shared() || row.Conflict() ) )
        {
            row.SetShared( false );
            row.SetConflict( false );
            changed = true;
            break;
        }
    }

    if( !changed )
        return tl::unexpected( LIBRARY_ERROR(
                _( "Cannot unshare: no shared row with that nickname" ) ) );

    LIBRARY_RESULT<void> r = table.Save();

    if( !r.has_value() )
        return r;

    PROJECT& activePrj = Pgm().GetSettingsManager().Prj();

    if( isSameProject( activePrj.GetProjectFullName(), subjectPro ) )
        LoadProjectTables( activePrj.GetProjectDirectory(), { aType } );

    return LIBRARY_RESULT<void>();
}
