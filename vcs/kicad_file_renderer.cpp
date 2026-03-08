#include "kicad_file_renderer.h"
#include "../agent/tools/util/process_util.h"
#include "../agent/tools/util/kicad_cli_util.h"

#include <sexpr/sexpr_parser.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

#include <fstream>
#include <functional>


// ── Helpers ──────────────────────────────────────────────────────────────────

/**
 * Simple scope guard for temp-directory cleanup on all exit paths.
 */
class RendererScopeGuard
{
public:
    explicit RendererScopeGuard( std::function<void()> aFn ) : m_fn( std::move( aFn ) ) {}
    ~RendererScopeGuard() { if( m_fn ) m_fn(); }

    RendererScopeGuard( const RendererScopeGuard& ) = delete;
    RendererScopeGuard& operator=( const RendererScopeGuard& ) = delete;

private:
    std::function<void()> m_fn;
};


/**
 * Recursively remove all files and the directory itself.
 */
static void RemoveTempDir( const wxString& aDir )
{
    wxDir dir( aDir );
    if( !dir.IsOpened() )
        return;

    wxString entry;

    // Remove sub-directory contents first
    bool cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_DIRS );
    while( cont )
    {
        wxString subDir = aDir + wxFileName::GetPathSeparator() + entry;
        wxDir    sub( subDir );

        if( sub.IsOpened() )
        {
            wxString subFile;
            bool     sc = sub.GetFirst( &subFile );

            while( sc )
            {
                wxRemoveFile( subDir + wxFileName::GetPathSeparator() + subFile );
                sc = sub.GetNext( &subFile );
            }
        }

        wxFileName::Rmdir( subDir );
        cont = dir.GetNext( &entry );
    }

    // Remove top-level files
    cont = dir.GetFirst( &entry, wxEmptyString, wxDIR_FILES );
    while( cont )
    {
        wxRemoveFile( aDir + wxFileName::GetPathSeparator() + entry );
        cont = dir.GetNext( &entry );
    }

    wxFileName::Rmdir( aDir );
}




/**
 * Read a file into a string.
 */
static std::string ReadFile( const std::string& aPath )
{
    std::ifstream f( aPath, std::ios::binary | std::ios::ate );

    if( !f.is_open() )
        return std::string();

    auto size = f.tellg();

    if( size <= 0 )
        return std::string();

    f.seekg( 0 );
    std::string content( static_cast<size_t>( size ), '\0' );
    f.read( &content[0], size );
    return content;
}


// ── KICAD_FILE_RENDERER implementation ──────────────────────────────────────

std::string KICAD_FILE_RENDERER::GetCliPrefix()
{
    if( !m_cliPrefix.empty() )
        return m_cliPrefix;

    m_cliPrefix = KiCadCliUtil::GetKicadCliCommandPrefix();
    return m_cliPrefix;
}


std::string KICAD_FILE_RENDERER::ExportToSvg( const std::string& aContent, bool aIsSch )
{
    const std::string cliPrefix = GetCliPrefix();

    if( cliPrefix.empty() )
    {
        wxLogWarning( "RENDERER: kicad-cli not available — SVG export skipped" );
        return std::string();
    }

    // Create isolated temp directory
    std::string tempDirStr = ProcessUtil::MakeTempDir( "zeo_vcs_" );

    if( tempDirStr.empty() )
    {
        wxLogError( "RENDERER: Failed to create temp dir" );
        return std::string();
    }

    wxString tempDir = wxString::FromUTF8( tempDirStr );
    RendererScopeGuard cleanup( [tempDir]() { RemoveTempDir( tempDir ); } );

    // Write content to a temp file with the correct extension
    std::string ext      = aIsSch ? ".kicad_sch" : ".kicad_pcb";
    std::string inFile   = tempDir.ToStdString() + "/diff_input" + ext;

    {
        std::ofstream f( inFile, std::ios::binary );

        if( !f.is_open() )
        {
            wxLogError( "RENDERER: Failed to write temp file" );
            return std::string();
        }

        f.write( aContent.c_str(), static_cast<std::streamsize>( aContent.size() ) );
    }

    std::string svgPath;
    std::string out, err;

    if( aIsSch )
    {
        std::string outDir = tempDir.ToStdString() + "/svg_out";
        wxFileName::Mkdir( wxString::FromUTF8( outDir ), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

        std::string cmd = cliPrefix
                          + " sch export svg --exclude-drawing-sheet --no-background-color"
                          + " --theme _builtin_default"
                          + " -o \"" + outDir + "\" \"" + inFile + "\"";

        int exitCode = ProcessUtil::RunCommand( cmd, out, err, 30 );

        if( !err.empty() )
            wxLogDebug( "RENDERER: sch export stderr: %s", err.c_str() );

        if( exitCode != 0 )
        {
            wxLogError( "RENDERER: kicad-cli sch export failed (exit %d)", exitCode );
            return std::string();
        }

        // Find the first SVG produced (one SVG per sheet; we want the root sheet)
        wxDir dir( wxString::FromUTF8( outDir ) );

        if( dir.IsOpened() )
        {
            wxString svgFile;

            if( dir.GetFirst( &svgFile, "*.svg", wxDIR_FILES ) )
                svgPath = outDir + "/" + svgFile.ToStdString();
        }
    }
    else // PCB
    {
        svgPath = tempDir.ToStdString() + "/diff_out.svg";

        std::string cmd = cliPrefix
                          + " pcb export svg"
                          + " --exclude-drawing-sheet --fit-page-to-board"
                          + " --theme _builtin_default"
                          + " --layers F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,Edge.Cuts"
                          + " -o \"" + svgPath + "\" \"" + inFile + "\"";

        int exitCode = ProcessUtil::RunCommand( cmd, out, err, 30 );

        if( !err.empty() )
            wxLogDebug( "RENDERER: pcb export stderr: %s", err.c_str() );

        if( exitCode != 0 )
        {
            wxLogError( "RENDERER: kicad-cli pcb export failed (exit %d)", exitCode );
            return std::string();
        }
    }

    if( svgPath.empty() || !wxFileName::FileExists( wxString::FromUTF8( svgPath ) ) )
    {
        wxLogError( "RENDERER: SVG output file not found at: %s", svgPath.c_str() );
        return std::string();
    }

    std::string svgContent = ReadFile( svgPath );

    return svgContent;
}


std::map<std::string, KICAD_FILE_RENDERER::ItemInfo>
KICAD_FILE_RENDERER::ExtractItems( const std::string&       aContent,
                                    const std::set<std::string>& aTypes )
{
    std::map<std::string, ItemInfo> result;

    if( aContent.empty() )
        return result;

    SEXPR::PARSER parser;
    std::unique_ptr<SEXPR::SEXPR> root;

    try
    {
        root = parser.Parse( aContent );
    }
    catch( const std::exception& e )
    {
        wxLogError( "RENDERER: S-expression parse error: %s", e.what() );
        return result;
    }

    if( !root || !root->IsList() )
        return result;

    // Root is (kicad_sch ...) or (kicad_pcb ...)
    // Child 0 is the file-type tag; iterate from child 1 for top-level items.
    const size_t n = root->GetNumberOfChildren();

    for( size_t i = 1; i < n; ++i )
    {
        SEXPR::SEXPR* child = root->GetChild( i );

        if( !child || !child->IsList() )
            continue;

        const size_t childN = child->GetNumberOfChildren();

        if( childN < 1 )
            continue;

        SEXPR::SEXPR* tag = child->GetChild( 0 );

        if( !tag || !tag->IsSymbol() )
            continue;

        const std::string itemType = tag->GetSymbol();

        if( aTypes.find( itemType ) == aTypes.end() )
            continue;

        // Walk sub-children for (uuid "…") and (at X Y …)
        std::string uuid;
        float       x = 0.0f;
        float       y = 0.0f;

        for( size_t j = 1; j < childN; ++j )
        {
            SEXPR::SEXPR* sub = child->GetChild( j );

            if( !sub || !sub->IsList() || sub->GetNumberOfChildren() < 2 )
                continue;

            SEXPR::SEXPR* subTag = sub->GetChild( 0 );

            if( !subTag || !subTag->IsSymbol() )
                continue;

            const std::string subTagStr = subTag->GetSymbol();

            if( subTagStr == "uuid" )
            {
                SEXPR::SEXPR* val = sub->GetChild( 1 );

                if( val )
                {
                    if( val->IsString() )       uuid = val->GetString();
                    else if( val->IsSymbol() )  uuid = val->GetSymbol();
                }
            }
            else if( subTagStr == "at" && sub->GetNumberOfChildren() >= 3 )
            {
                SEXPR::SEXPR* xVal = sub->GetChild( 1 );
                SEXPR::SEXPR* yVal = sub->GetChild( 2 );

                if( xVal )
                {
                    if( xVal->IsDouble() )       x = static_cast<float>( xVal->GetDouble() );
                    else if( xVal->IsInteger() ) x = static_cast<float>( xVal->GetInteger() );
                }

                if( yVal )
                {
                    if( yVal->IsDouble() )       y = static_cast<float>( yVal->GetDouble() );
                    else if( yVal->IsInteger() ) y = static_cast<float>( yVal->GetInteger() );
                }
            }
        }

        if( uuid.empty() )
            continue;

        ItemInfo info;
        info.type       = itemType;
        info.x          = x;
        info.y          = y;
        info.serialized = child->AsString();

        result.emplace( std::move( uuid ), std::move( info ) );
    }

    return result;
}


nlohmann::json KICAD_FILE_RENDERER::GetVisualDiff( const std::string& aOldContent,
                                                     const std::string& aNewContent,
                                                     const std::string& aFilePath )
{
    wxFileName fn( wxString::FromUTF8( aFilePath ) );
    wxString   ext = fn.GetExt().Lower();
    bool       isSch = ( ext == "kicad_sch" );
    bool       isPcb = ( ext == "kicad_pcb" );

    if( !isSch && !isPcb )
    {
        return nlohmann::json{
            { "success", false },
            { "error",   "Unsupported file type: " + aFilePath }
        };
    }

    // Item types whose UUIDs we track for change detection
    std::set<std::string> trackedTypes;

    if( isSch )
    {
        trackedTypes = {
            "symbol", "wire", "junction", "no_connect", "bus", "bus_entry",
            "label", "global_label", "hierarchical_label", "text", "text_box",
            "sheet", "image"
        };
    }
    else
    {
        trackedTypes = {
            "footprint", "segment", "arc", "via", "zone",
            "gr_line", "gr_arc", "gr_circle", "gr_rect", "gr_poly", "gr_text",
            "dimension"
        };
    }

    wxLogDebug( "RENDERER: Extracting items from old content (%zu bytes)", aOldContent.size() );
    auto oldItems = ExtractItems( aOldContent, trackedTypes );

    wxLogDebug( "RENDERER: Extracting items from new content (%zu bytes)", aNewContent.size() );
    auto newItems = ExtractItems( aNewContent, trackedTypes );

    wxLogDebug( "RENDERER: old=%zu items, new=%zu items", oldItems.size(), newItems.size() );

    // Build changedItems list: added / modified / deleted
    nlohmann::json changedItems = nlohmann::json::array();

    for( const auto& [uuid, newInfo] : newItems )
    {
        auto oldIt = oldItems.find( uuid );

        if( oldIt == oldItems.end() )
        {
            changedItems.push_back( {
                { "uuid",       uuid },
                { "changeType", "added" },
                { "itemType",   newInfo.type },
                { "x",          newInfo.x },
                { "y",          newInfo.y }
            } );
        }
        else if( oldIt->second.serialized != newInfo.serialized )
        {
            changedItems.push_back( {
                { "uuid",       uuid },
                { "changeType", "modified" },
                { "itemType",   newInfo.type },
                { "x",          newInfo.x },
                { "y",          newInfo.y }
            } );
        }
    }

    for( const auto& [uuid, oldInfo] : oldItems )
    {
        if( newItems.find( uuid ) == newItems.end() )
        {
            changedItems.push_back( {
                { "uuid",       uuid },
                { "changeType", "deleted" },
                { "itemType",   oldInfo.type },
                { "x",          oldInfo.x },
                { "y",          oldInfo.y }
            } );
        }
    }

    wxLogDebug( "RENDERER: %zu changed items; exporting SVGs…", changedItems.size() );

    // Export both versions to SVG (empty string if kicad-cli unavailable or content empty)
    std::string beforeSvg, afterSvg;

    if( !aOldContent.empty() )
        beforeSvg = ExportToSvg( aOldContent, isSch );

    if( !aNewContent.empty() )
        afterSvg = ExportToSvg( aNewContent, isSch );

    wxLogDebug( "RENDERER: beforeSvg=%zu bytes, afterSvg=%zu bytes",
                beforeSvg.size(), afterSvg.size() );

    return nlohmann::json{
        { "success",      true },
        { "fileIsNew",    aOldContent.empty() },
        { "beforeSvg",    beforeSvg },
        { "afterSvg",     afterSvg },
        { "changedItems", changedItems },
        { "isSch",        isSch }
    };
}
