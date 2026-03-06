#include "symbol_importer.h"
#include "tools/tool_registry.h"
#include "../../agent_events.h"
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/app.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>

using json = nlohmann::json;


// ---------------------------------------------------------------------------
// Strip Windows carriage returns from S-expression content to prevent
// KiCad parser errors when importing symbols from external sources.
// ---------------------------------------------------------------------------
static void StripCarriageReturns( std::string& s )
{
    s.erase( std::remove( s.begin(), s.end(), '\r' ), s.end() );
}


// ---------------------------------------------------------------------------
// Execute (synchronous — used by MCP tool execution)
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::Execute( const std::string& aToolName,
                                       const nlohmann::json& aInput )
{
    std::string symContent  = aInput.value( "kicad_symbol", "" );
    std::string fpContent   = aInput.value( "kicad_footprint", "" );
    std::string symbolName  = aInput.value( "symbol_name", "" );
    std::string libraryName = aInput.value( "library_name", "project" );
    bool        force       = aInput.value( "force", false );

    if( symContent.empty() )
        return R"({"error": "kicad_symbol is required"})";

    if( libraryName.empty() )
        libraryName = "project";

    std::string projectPath = TOOL_REGISTRY::Instance().GetProjectPath();

    if( projectPath.empty() )
        return R"({"error": "No project open. Open or create a project first."})";

    std::string result = DoImport( symContent, fpContent, symbolName,
                                    libraryName, projectPath, force );

    // Reload libraries on success
    try
    {
        auto resultJson = json::parse( result );
        std::string status = resultJson.value( "status", "" );

        if( status == "created" || status == "already_exists" )
        {
            TOOL_REGISTRY::Instance().ReloadSymbolLib( libraryName );

            if( !fpContent.empty() )
                TOOL_REGISTRY::Instance().ReloadFootprintLib( libraryName );
        }
    }
    catch( ... ) {}

    return result;
}


std::string SYMBOL_IMPORTER::GetDescription( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string name = aInput.value( "symbol_name", "" );

    if( !name.empty() )
        return "Importing symbol: " + name;

    return "Importing KiCad symbol into project library";
}


// ---------------------------------------------------------------------------
// ExecuteAsync — extract params on main thread, then spawn background thread
// ---------------------------------------------------------------------------
void SYMBOL_IMPORTER::ExecuteAsync( const std::string& aToolName,
                                     const nlohmann::json& aInput,
                                     const std::string& aToolUseId,
                                     wxEvtHandler* aEventHandler )
{
    // Field names match cse_get_kicad MCP response: kicad_symbol + kicad_footprint
    std::string symContent  = aInput.value( "kicad_symbol", "" );
    std::string fpContent   = aInput.value( "kicad_footprint", "" );
    std::string symbolName  = aInput.value( "symbol_name", "" );
    std::string libraryName = aInput.value( "library_name", "project" );
    bool        force       = aInput.value( "force", false );

    if( symContent.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name   = aToolName;
        result.result      = R"({"error": "kicad_symbol is required"})";
        result.success     = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    if( libraryName.empty() )
        libraryName = "project";

    // Get project path on main thread before spawning
    std::string projectPath = TOOL_REGISTRY::Instance().GetProjectPath();

    if( projectPath.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name   = aToolName;
        result.result      = R"({"error": "No project open. Open or create a project first."})";
        result.success     = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    wxLogInfo( "SYMBOL_IMPORTER: Starting import (library=%s, symbol=%s, has_footprint=%s)",
               libraryName.c_str(), symbolName.c_str(), fpContent.empty() ? "no" : "yes" );

    std::thread( [=, this]()
    {
        std::string resultStr = DoImport( symContent, fpContent, symbolName,
                                           libraryName, projectPath, force );

        bool success = false;
        try
        {
            auto resultJson = json::parse( resultStr );
            std::string status = resultJson.value( "status", "" );
            success = ( status == "created" || status == "already_exists" );
        }
        catch( ... ) {}

        wxTheApp->CallAfter( [=]()
        {
            if( success )
            {
                wxLogInfo( "SYMBOL_IMPORTER: Reloading symbol library '%s'",
                           libraryName.c_str() );
                TOOL_REGISTRY::Instance().ReloadSymbolLib( libraryName );

                if( !fpContent.empty() )
                {
                    wxLogInfo( "SYMBOL_IMPORTER: Reloading footprint library '%s'",
                               libraryName.c_str() );
                    TOOL_REGISTRY::Instance().ReloadFootprintLib( libraryName );
                }
            }

            ToolExecutionResult result;
            result.tool_use_id = aToolUseId;
            result.tool_name   = aToolName;
            result.result      = resultStr;
            result.success     = success;
            PostToolResult( aEventHandler, result );
        } );
    } ).detach();
}


// ---------------------------------------------------------------------------
// ExtractSymbolName — find the first top-level non-sub-unit symbol name
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::ExtractSymbolName( const std::string& aContent )
{
    // With (kicad_symbol_lib ...) wrapper: top-level symbols are at depth 2.
    // Without wrapper (raw blocks): they are at depth 1.
    size_t libStart    = aContent.find( "(kicad_symbol_lib" );
    int    targetDepth = ( libStart != std::string::npos ) ? 2 : 1;
    size_t scanStart   = ( libStart != std::string::npos ) ? libStart : 0;

    int depth = 0;

    for( size_t i = scanStart; i < aContent.size(); i++ )
    {
        char c = aContent[i];

        if( c == '(' )
        {
            depth++;

            if( depth == targetDepth && i + 8 < aContent.size()
                && aContent.substr( i, 9 ) == "(symbol \"" )
            {
                size_t nameStart = i + 9;
                size_t nameEnd   = aContent.find( '"', nameStart );

                if( nameEnd == std::string::npos )
                    break;

                std::string candidate = aContent.substr( nameStart, nameEnd - nameStart );

                // Skip KiCad sub-unit entries: NAME_N_N (e.g. LM358_0_1, LM358_1_1)
                bool isSubUnit = false;
                size_t lastUs = candidate.rfind( '_' );

                if( lastUs != std::string::npos && lastUs > 0 )
                {
                    std::string lastPart = candidate.substr( lastUs + 1 );
                    bool lastDigits = !lastPart.empty()
                                      && std::all_of( lastPart.begin(), lastPart.end(),
                                                      []( char ch ) { return isdigit( ch ); } );

                    if( lastDigits )
                    {
                        size_t prevUs = candidate.rfind( '_', lastUs - 1 );

                        if( prevUs != std::string::npos )
                        {
                            std::string prevPart = candidate.substr( prevUs + 1,
                                                                      lastUs - prevUs - 1 );
                            bool prevDigits = !prevPart.empty()
                                             && std::all_of( prevPart.begin(), prevPart.end(),
                                                             []( char ch ) { return isdigit( ch ); } );

                            if( prevDigits )
                                isSubUnit = true;
                        }
                    }
                }

                if( !isSubUnit )
                    return candidate;
            }
        }
        else if( c == ')' )
        {
            depth--;

            if( targetDepth == 2 && depth == 0 )
                break;
        }
    }

    return "";
}


// ---------------------------------------------------------------------------
// ExtractSymbolBlocks — strip library wrapper, return inner (symbol ...) blocks
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::ExtractSymbolBlocks( const std::string& aContent )
{
    size_t libStart    = aContent.find( "(kicad_symbol_lib" );
    int    targetDepth = ( libStart != std::string::npos ) ? 2 : 1;
    size_t scanStart   = ( libStart != std::string::npos ) ? libStart : 0;

    std::string result;
    int    depth            = 0;
    bool   inSymbolBlock    = false;
    size_t symbolBlockStart = 0;

    for( size_t i = scanStart; i < aContent.size(); i++ )
    {
        char c = aContent[i];

        if( c == '(' )
        {
            depth++;

            if( depth == targetDepth && !inSymbolBlock
                && i + 8 < aContent.size()
                && aContent.substr( i, 9 ) == "(symbol \"" )
            {
                inSymbolBlock    = true;
                symbolBlockStart = i;
            }
        }
        else if( c == ')' )
        {
            // depth == targetDepth means this ')' closes a top-level symbol block
            if( inSymbolBlock && depth == targetDepth )
            {
                result += aContent.substr( symbolBlockStart, i - symbolBlockStart + 1 );
                result += "\n";
                inSymbolBlock    = false;
                symbolBlockStart = 0;
            }

            depth--;

            if( targetDepth == 2 && depth == 0 )
                break;
        }
    }

    return result;
}


// ---------------------------------------------------------------------------
// ExtractFootprintName — find footprint/module name in a .kicad_mod S-expression
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::ExtractFootprintName( const std::string& aContent )
{
    // KiCad 6+ format: (footprint "NAME" ...)
    // Legacy format:   (module "NAME" ...)
    for( const std::string& tag : { "(footprint \"", "(module \"" } )
    {
        size_t pos = aContent.find( tag );

        if( pos != std::string::npos )
        {
            size_t nameStart = pos + tag.size();
            size_t nameEnd   = aContent.find( '"', nameStart );

            if( nameEnd != std::string::npos )
                return aContent.substr( nameStart, nameEnd - nameStart );
        }
    }

    return "";
}


// ---------------------------------------------------------------------------
// SetFootprintProperty — replace the Footprint property value in symbol content
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::SetFootprintProperty( const std::string& aContent,
                                                    const std::string& aFootprintLibId )
{
    std::string escapedFp;
    escapedFp.reserve( aFootprintLibId.size() );

    for( char c : aFootprintLibId )
    {
        if( c == '"' )       escapedFp += "\\\"";
        else if( c == '\\' ) escapedFp += "\\\\";
        else                 escapedFp += c;
    }

    const std::string searchPrefix = "(property \"Footprint\" \"";
    size_t pos = aContent.find( searchPrefix );

    if( pos == std::string::npos )
        return aContent;

    size_t valueStart = pos + searchPrefix.size();
    size_t valueEnd   = aContent.find( '"', valueStart );

    // Skip escaped quotes inside the value
    while( valueEnd != std::string::npos && valueEnd > 0 && aContent[valueEnd - 1] == '\\' )
        valueEnd = aContent.find( '"', valueEnd + 1 );

    if( valueEnd == std::string::npos )
        return aContent;

    return aContent.substr( 0, valueStart ) + escapedFp + aContent.substr( valueEnd );
}


// ---------------------------------------------------------------------------
// UpdateSymLibTable — ensure the symbol library is registered in sym-lib-table
// ---------------------------------------------------------------------------
bool SYMBOL_IMPORTER::UpdateSymLibTable( const std::string& aProjectPath,
                                          const std::string& aLibraryName )
{
    std::string tablePath   = aProjectPath + "/sym-lib-table";
    std::string libFileName = aLibraryName + ".kicad_sym";
    std::string entryName   = "(name \"" + aLibraryName + "\")";

    std::string existingContent;
    {
        std::ifstream inFile( tablePath );

        if( inFile.is_open() )
            existingContent = std::string( ( std::istreambuf_iterator<char>( inFile ) ),
                                             std::istreambuf_iterator<char>() );
    }

    if( !existingContent.empty() && existingContent.find( entryName ) != std::string::npos )
        return false; // Already registered

    std::string newEntry = "  (lib (name \"" + aLibraryName + "\")"
                           "(type \"KiCad\")"
                           "(uri \"${KIPRJMOD}/" + libFileName + "\")"
                           "(options \"\")"
                           "(descr \"\"))";

    if( existingContent.empty() )
    {
        existingContent = "(sym_lib_table\n  (version 7)\n" + newEntry + "\n)\n";
    }
    else
    {
        size_t lastParen = existingContent.rfind( ')' );

        if( lastParen != std::string::npos )
            existingContent = existingContent.substr( 0, lastParen ) + newEntry + "\n)\n";
        else
            existingContent += newEntry + "\n";
    }

    std::ofstream outFile( tablePath );

    if( !outFile.is_open() )
    {
        wxLogWarning( "SYMBOL_IMPORTER: Could not write sym-lib-table: %s", tablePath.c_str() );
        return false;
    }

    outFile << existingContent;
    outFile.close();

    wxLogInfo( "SYMBOL_IMPORTER: Added '%s' to sym-lib-table", aLibraryName.c_str() );
    return true;
}


// ---------------------------------------------------------------------------
// WriteFootprint — write .kicad_mod into libraryName.pretty/fpName.kicad_mod
// Returns the footprint lib_id (libraryName:fpName), or empty on failure.
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::WriteFootprint( const std::string& aFpContent,
                                              const std::string& aFpName,
                                              const std::string& aLibraryName,
                                              const std::string& aProjectPath,
                                              bool aForce )
{
    wxFileName projDir( wxString::FromUTF8( aProjectPath ), "" );
    std::string prettyDir = projDir.GetPath().ToStdString() + "/" + aLibraryName + ".pretty";
    std::string outputPath = prettyDir + "/" + aFpName + ".kicad_mod";

    // Create .pretty directory if needed
    if( !wxFileName::DirExists( wxString::FromUTF8( prettyDir ) ) )
    {
        wxFileName::Mkdir( wxString::FromUTF8( prettyDir ), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );
        wxLogInfo( "SYMBOL_IMPORTER: Created directory %s", prettyDir.c_str() );
    }

    // Skip if already exists and not forcing
    if( !aForce && wxFileName::FileExists( wxString::FromUTF8( outputPath ) ) )
    {
        wxLogInfo( "SYMBOL_IMPORTER: Footprint '%s' already exists, reusing", aFpName.c_str() );
        return aLibraryName + ":" + aFpName;
    }

    std::ofstream outFile( outputPath );

    if( !outFile.is_open() )
    {
        wxLogWarning( "SYMBOL_IMPORTER: Could not write footprint: %s", outputPath.c_str() );
        return "";
    }

    outFile << aFpContent;
    outFile.close();

    wxLogInfo( "SYMBOL_IMPORTER: Wrote footprint '%s' to %s",
               aFpName.c_str(), outputPath.c_str() );

    return aLibraryName + ":" + aFpName;
}


// ---------------------------------------------------------------------------
// UpdateFpLibTable — ensure footprint library is registered in fp-lib-table
// ---------------------------------------------------------------------------
void SYMBOL_IMPORTER::UpdateFpLibTable( const std::string& aProjectPath,
                                         const std::string& aLibraryName )
{
    std::string tablePath = aProjectPath + "/fp-lib-table";
    std::string entryName = "(name \"" + aLibraryName + "\")";

    std::string existingContent;
    {
        std::ifstream inFile( tablePath );

        if( inFile.is_open() )
            existingContent = std::string( ( std::istreambuf_iterator<char>( inFile ) ),
                                             std::istreambuf_iterator<char>() );
    }

    if( !existingContent.empty() && existingContent.find( entryName ) != std::string::npos )
        return; // Already registered

    std::string newEntry = "  (lib (name \"" + aLibraryName + "\")"
                           "(type \"KiCad\")"
                           "(uri \"${KIPRJMOD}/" + aLibraryName + ".pretty\")"
                           "(options \"\")"
                           "(descr \"\"))";

    if( existingContent.empty() )
    {
        existingContent = "(fp_lib_table\n  (version 7)\n" + newEntry + "\n)\n";
    }
    else
    {
        size_t lastParen = existingContent.rfind( ')' );

        if( lastParen != std::string::npos )
            existingContent = existingContent.substr( 0, lastParen ) + newEntry + "\n)\n";
        else
            existingContent += newEntry + "\n";
    }

    std::ofstream outFile( tablePath );

    if( !outFile.is_open() )
    {
        wxLogWarning( "SYMBOL_IMPORTER: Could not write fp-lib-table: %s", tablePath.c_str() );
        return;
    }

    outFile << existingContent;
    outFile.close();

    wxLogInfo( "SYMBOL_IMPORTER: Added '%s' to fp-lib-table", aLibraryName.c_str() );
}


// ---------------------------------------------------------------------------
// Sanitize a name for use as a KiCad lib_id component
// ---------------------------------------------------------------------------
static std::string SanitizeSymbolName( const std::string& aName )
{
    std::string result;
    result.reserve( aName.size() );

    for( char c : aName )
    {
        if( c == '/' ) result += '-';
        else           result += c;
    }

    size_t start = result.find_first_not_of( " \t\r\n" );
    size_t end   = result.find_last_not_of( " \t\r\n" );

    if( start == std::string::npos )
        return "";

    return result.substr( start, end - start + 1 );
}


// ---------------------------------------------------------------------------
// DoImport — main workflow (runs on background thread)
// ---------------------------------------------------------------------------
std::string SYMBOL_IMPORTER::DoImport( const std::string& aSymContent,
                                        const std::string& aFpContent,
                                        const std::string& aSymbolName,
                                        const std::string& aLibraryName,
                                        const std::string& aProjectPath,
                                        bool aForce )
{
    // Sanitize inputs — external sources (SnapEDA, LCSC, etc.) may use \r\n
    // which the KiCad S-expression parser cannot handle.
    std::string symContent = aSymContent;
    std::string fpContent  = aFpContent;
    StripCarriageReturns( symContent );
    StripCarriageReturns( fpContent );

    // Step 1: Validate symbol content
    if( symContent.find( "(kicad_symbol_lib" ) == std::string::npos
        && symContent.find( "(symbol \"" ) == std::string::npos )
    {
        json err;
        err["error"] = "kicad_symbol does not appear to be valid KiCad symbol data. "
                       "Expected content containing (kicad_symbol_lib ...) or (symbol \"...\").";
        return err.dump();
    }

    // Step 2: Determine canonical symbol name
    std::string canonicalName = aSymbolName;

    if( canonicalName.empty() )
        canonicalName = ExtractSymbolName( symContent );

    if( canonicalName.empty() )
    {
        json err;
        err["error"] = "Could not determine symbol name from kicad_symbol content. "
                       "Please provide symbol_name explicitly.";
        return err.dump();
    }

    canonicalName = SanitizeSymbolName( canonicalName );

    if( canonicalName.empty() )
    {
        json err;
        err["error"] = "Symbol name is empty after sanitization. "
                       "Please provide a valid symbol_name.";
        return err.dump();
    }

    wxLogInfo( "SYMBOL_IMPORTER: Importing '%s' into library '%s'",
               canonicalName.c_str(), aLibraryName.c_str() );

    // Step 3: Extract inner symbol blocks (strip library wrapper)
    std::string symbolBlocks = ExtractSymbolBlocks( symContent );

    if( symbolBlocks.empty() )
    {
        symbolBlocks = symContent; // Fallback: use raw content
        wxLogWarning( "SYMBOL_IMPORTER: ExtractSymbolBlocks returned empty; using raw content" );
    }

    // Step 4: Import footprint if provided
    std::string footprintLibId;

    if( !fpContent.empty() )
    {
        std::string fpName = ExtractFootprintName( fpContent );

        if( fpName.empty() )
        {
            // Fall back to canonical symbol name so the footprint file has a sensible name
            fpName = canonicalName;
            wxLogWarning( "SYMBOL_IMPORTER: Could not extract footprint name; using symbol name '%s'",
                          fpName.c_str() );
        }

        footprintLibId = WriteFootprint( fpContent, fpName, aLibraryName, aProjectPath, aForce );

        if( !footprintLibId.empty() )
            UpdateFpLibTable( aProjectPath, aLibraryName );
        else
            wxLogWarning( "SYMBOL_IMPORTER: Footprint write failed for '%s'", fpName.c_str() );
    }

    // Step 5: Set Footprint property on symbol using the imported footprint lib_id
    if( !footprintLibId.empty() )
        symbolBlocks = SetFootprintProperty( symbolBlocks, footprintLibId );

    // Step 6: Determine output library file path
    wxFileName projDir( wxString::FromUTF8( aProjectPath ), "" );
    std::string libFileName = aLibraryName + ".kicad_sym";
    wxFileName  libPath( projDir.GetPath(), wxString::FromUTF8( libFileName ) );
    std::string outputPath = libPath.GetFullPath().ToStdString();

    wxLogInfo( "SYMBOL_IMPORTER: Symbol output path: %s", outputPath.c_str() );

    // Step 7: Write symbol to library file
    bool        libExists = wxFileName::FileExists( libPath.GetFullPath() );
    std::string symbolTag = "(symbol \"" + canonicalName + "\"";

    if( libExists )
    {
        std::ifstream existingFile( outputPath );
        std::string   existingContent( ( std::istreambuf_iterator<char>( existingFile ) ),
                                          std::istreambuf_iterator<char>() );
        existingFile.close();

        size_t symbolPos = existingContent.find( symbolTag );

        if( symbolPos != std::string::npos )
        {
            if( !aForce )
            {
                json result;
                result["status"]    = "already_exists";
                result["message"]   = "Symbol '" + canonicalName + "' already exists in "
                                      + libFileName + ". Use force=true to replace.";
                result["lib_id"]    = aLibraryName + ":" + canonicalName;
                result["file_path"] = outputPath;

                if( !footprintLibId.empty() )
                    result["footprint_lib_id"] = footprintLibId;

                return result.dump( 2 );
            }

            // force=true: bracket-count to find end of old block, replace it
            wxLogInfo( "SYMBOL_IMPORTER: force=true, replacing '%s'", canonicalName.c_str() );

            int    depth  = 0;
            size_t endPos = symbolPos;

            for( size_t i = symbolPos; i < existingContent.size(); i++ )
            {
                if( existingContent[i] == '(' )      depth++;
                else if( existingContent[i] == ')' ) { depth--; if( depth == 0 ) { endPos = i + 1; break; } }
            }

            existingContent.replace( symbolPos, endPos - symbolPos, symbolBlocks );
        }
        else
        {
            // New symbol in existing library — inject before closing paren
            size_t lastParen = existingContent.rfind( ')' );

            if( lastParen != std::string::npos )
                existingContent = existingContent.substr( 0, lastParen )
                                  + symbolBlocks + "\n)\n";
        }

        std::ofstream outFile( outputPath );

        if( !outFile.is_open() )
        {
            json err;
            err["error"] = "Failed to open library file for writing: " + outputPath;
            return err.dump();
        }

        outFile << existingContent;
        outFile.close();
    }
    else
    {
        // Create new library file
        std::ofstream outFile( outputPath );

        if( !outFile.is_open() )
        {
            json err;
            err["error"] = "Failed to create library file: " + outputPath;
            return err.dump();
        }

        outFile << "(kicad_symbol_lib\n"
                << "\t(version 20241209)\n"
                << "\t(generator \"zeo_agent\")\n"
                << "\t(generator_version \"1.0\")\n"
                << symbolBlocks << "\n)\n";

        outFile.close();
    }

    wxLogInfo( "SYMBOL_IMPORTER: Symbol '%s' written successfully", canonicalName.c_str() );

    // Step 8: Ensure symbol library is in sym-lib-table
    UpdateSymLibTable( aProjectPath, aLibraryName );

    // Step 9: Return result
    json result;
    result["status"]       = "created";
    result["lib_id"]       = aLibraryName + ":" + canonicalName;
    result["symbol_name"]  = canonicalName;
    result["library_name"] = aLibraryName;
    result["file_path"]    = outputPath;

    if( !footprintLibId.empty() )
        result["footprint_lib_id"] = footprintLibId;

    return result.dump( 2 );
}
