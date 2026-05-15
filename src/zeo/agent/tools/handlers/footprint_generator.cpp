#include "footprint_generator.h"
#include "tools/tool_registry.h"
#include <zeo/agent_auth.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/log.h>
#include <wx/app.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <thread>

using json = nlohmann::json;


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string SafeStr( const json& j, const char* key, const std::string& def )
{
    auto it = j.find( key );
    if( it == j.end() || it->is_null() )
        return def;
    return it->get<std::string>();
}

static double SafeDouble( const json& j, const char* key, double def )
{
    auto it = j.find( key );
    if( it == j.end() || it->is_null() )
        return def;
    return it->get<double>();
}

static int SafeInt( const json& j, const char* key, int def )
{
    auto it = j.find( key );
    if( it == j.end() || it->is_null() )
        return def;
    return it->get<int>();
}

static std::string UrlEncode( const std::string& s )
{
    KICAD_CURL_EASY curlHelper;
    char* encoded = curl_easy_escape( curlHelper.GetCurl(), s.c_str(),
                                       static_cast<int>( s.size() ) );
    std::string result = encoded ? encoded : s;
    if( encoded )
        curl_free( encoded );
    return result;
}

/**
 * Format a double with no trailing zeros.
 */
static std::string FmtDim( double v )
{
    char buf[32];
    snprintf( buf, sizeof( buf ), "%.3f", v );
    std::string s = buf;

    // Strip trailing zeros after decimal
    size_t dot = s.find( '.' );
    if( dot != std::string::npos )
    {
        size_t last = s.find_last_not_of( '0' );
        if( last == dot )
            s = s.substr( 0, dot );
        else
            s = s.substr( 0, last + 1 );
    }

    return s;
}


// ---------------------------------------------------------------------------
// Execute (synchronous — used by MCP tool execution)
// ---------------------------------------------------------------------------
std::string FOOTPRINT_GENERATOR::Execute( const std::string& aToolName,
                                           const nlohmann::json& aInput )
{
    std::string partNumber = aInput.value( "part_number", "" );
    std::string manufacturer = aInput.value( "manufacturer", "" );
    std::string datasheetUrl = aInput.value( "datasheet_url", "" );
    std::string componentId = aInput.value( "component_id", "" );
    std::string libraryName = aInput.value( "library_name", "" );
    bool force = aInput.value( "force", false );

    if( datasheetUrl.empty() )
        return R"({"error": "datasheet_url is required"})";

    if( libraryName.empty() )
        libraryName = "project";

    std::string projectPath = TOOL_REGISTRY::Instance().GetProjectPath();

    if( projectPath.empty() )
        return R"({"error": "No project open. Open or create a project first."})";

    std::string result = DoGenerate( partNumber, manufacturer, datasheetUrl,
                                      componentId, libraryName, projectPath, force );

    // Reload footprint library on success
    try
    {
        auto resultJson = json::parse( result );
        std::string status = SafeStr( resultJson, "status", "" );

        if( status == "created" )
            TOOL_REGISTRY::Instance().ReloadFootprintLib( libraryName );
    }
    catch( ... ) {}

    return result;
}


std::string FOOTPRINT_GENERATOR::GetDescription( const std::string& aToolName,
                                                   const nlohmann::json& aInput ) const
{
    std::string part = aInput.value( "part_number", "" );
    return "Generating KiCad footprint for " + part;
}


// ---------------------------------------------------------------------------
// ExecuteAsync
// ---------------------------------------------------------------------------
void FOOTPRINT_GENERATOR::ExecuteAsync( const std::string& aToolName,
                                         const nlohmann::json& aInput,
                                         const std::string& aToolUseId,
                                         wxEvtHandler* aEventHandler )
{
    std::string partNumber = aInput.value( "part_number", "" );
    std::string manufacturer = aInput.value( "manufacturer", "" );
    std::string datasheetUrl = aInput.value( "datasheet_url", "" );
    std::string componentId = aInput.value( "component_id", "" );
    std::string libraryName = aInput.value( "library_name", "" );
    bool force = aInput.value( "force", false );

    if( datasheetUrl.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "datasheet_url is required"})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    if( libraryName.empty() )
        libraryName = "project";

    const auto& reg = TOOL_REGISTRY::Instance();
    std::string projectPath = reg.GetProjectPath();

    if( projectPath.empty() )
    {
        ToolExecutionResult result;
        result.tool_use_id = aToolUseId;
        result.tool_name = aToolName;
        result.result = R"({"error": "No project open. Open or create a project first."})";
        result.success = false;
        PostToolResult( aEventHandler, result );
        return;
    }

    wxLogInfo( "FOOTPRINT_GENERATOR: Starting for %s (datasheet=%s)",
               partNumber.c_str(), datasheetUrl.c_str() );

    std::thread( [=, this]()
    {
        std::string resultStr = DoGenerate( partNumber, manufacturer, datasheetUrl,
                                             componentId, libraryName, projectPath,
                                             force );

        bool success = false;
        try
        {
            auto resultJson = json::parse( resultStr );
            success = !resultJson.contains( "error" );
        }
        catch( ... ) {}

        wxTheApp->CallAfter( [=]()
        {
            if( success )
            {
                try
                {
                    auto resultJson = json::parse( resultStr );
                    std::string status = SafeStr( resultJson, "status", "" );

                    if( status == "created" )
                    {
                        wxLogInfo( "FOOTPRINT_GENERATOR: Reloading footprint library '%s'",
                                   libraryName.c_str() );
                        TOOL_REGISTRY::Instance().ReloadFootprintLib( libraryName );
                    }
                }
                catch( ... ) {}
            }

            ToolExecutionResult result;
            result.tool_use_id = aToolUseId;
            result.tool_name = "generate_footprint";
            result.result = resultStr;
            result.success = success;
            PostToolResult( aEventHandler, result );
        } );
    } ).detach();
}


// ---------------------------------------------------------------------------
// DoGenerate
// ---------------------------------------------------------------------------
std::string FOOTPRINT_GENERATOR::DoGenerate( const std::string& aPartNumber,
                                              const std::string& aManufacturer,
                                              const std::string& aDatasheetUrl,
                                              const std::string& aComponentId,
                                              const std::string& aLibraryName,
                                              const std::string& aProjectPath,
                                              bool aForce )
{
    // Step 1: Fetch component data from DB
    ComponentData data;
    bool haveData = FetchComponentData( aPartNumber, aManufacturer, data,
                                         aComponentId, aDatasheetUrl );

    // Step 2: Auto-extract if needed
    if( !haveData && !aDatasheetUrl.empty() )
    {
        wxLogInfo( "FOOTPRINT_GENERATOR: No extraction data, triggering extraction" );
        std::string newComponentId = TriggerExtractionSync( aPartNumber, aManufacturer,
                                                             aDatasheetUrl );

        if( newComponentId.empty() )
        {
            json err;
            err["error"] = "Datasheet extraction failed. Check the URL is accessible.";
            return err.dump();
        }

        haveData = FetchComponentData( aPartNumber, aManufacturer, data, newComponentId );
    }

    if( !haveData || data.package.pin_count == 0 )
    {
        json err;
        err["error"] = "No package data available. Provide a datasheet_url to extract data.";
        return err.dump();
    }

    wxLogInfo( "FOOTPRINT_GENERATOR: Package %s, %d pins, %.1fx%.1fmm, pitch=%.2fmm, EP=%.1fx%.1fmm",
               data.package.package_type.c_str(), data.package.pin_count,
               data.package.body_width_mm, data.package.body_length_mm,
               data.package.pin_pitch_mm,
               data.package.ep_width_mm, data.package.ep_length_mm );

    // Step 3: Try to match a standard KiCad library footprint
    std::string matchedLibId = MatchStandardFootprint( data.package );

    if( !matchedLibId.empty() )
    {
        wxLogInfo( "FOOTPRINT_GENERATOR: Matched standard footprint: %s", matchedLibId.c_str() );
        json result;
        result["status"] = "matched";
        result["message"] = "Matched existing KiCad library footprint";
        result["lib_id"] = matchedLibId;
        return result.dump( 2 );
    }

    // Step 4: Generate custom footprint
    std::string fpName = BuildFootprintName( data.package );

    // Create .pretty directory if needed
    wxFileName projDir( wxString::FromUTF8( aProjectPath ), "" );
    std::string prettyDir = projDir.GetPath().ToStdString() + "/" + aLibraryName + ".pretty";

    if( !wxFileName::DirExists( wxString::FromUTF8( prettyDir ) ) )
    {
        wxFileName::Mkdir( wxString::FromUTF8( prettyDir ), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );
        wxLogInfo( "FOOTPRINT_GENERATOR: Created directory %s", prettyDir.c_str() );
    }

    std::string outputPath = prettyDir + "/" + fpName + ".kicad_mod";

    // Check if already exists
    if( !aForce && wxFileName::FileExists( wxString::FromUTF8( outputPath ) ) )
    {
        json result;
        result["status"] = "already_exists";
        result["message"] = "Footprint '" + fpName + "' already exists in " + aLibraryName
                            + ".pretty. Use force=true to regenerate.";
        result["lib_id"] = aLibraryName + ":" + fpName;
        result["file_path"] = outputPath;
        return result.dump( 2 );
    }

    // Generate the .kicad_mod content
    std::string content = GenerateQfnFootprint( data.package, fpName );

    if( content.empty() )
    {
        json err;
        err["error"] = "Unsupported package type for footprint generation: "
                        + data.package.package_type;
        return err.dump();
    }

    std::ofstream outFile( outputPath );
    if( !outFile.is_open() )
    {
        json err;
        err["error"] = "Failed to create footprint file: " + outputPath;
        return err.dump();
    }

    outFile << content;
    outFile.close();

    // Ensure fp-lib-table has the library entry
    EnsureFpLibTableEntry( aProjectPath, aLibraryName );

    wxLogInfo( "FOOTPRINT_GENERATOR: Wrote %s (%d pins)", outputPath.c_str(),
               data.package.pin_count );

    json result;
    result["status"] = "created";
    result["message"] = "Footprint '" + fpName + "' generated with "
                        + std::to_string( data.package.pin_count ) + " pads";
    result["lib_id"] = aLibraryName + ":" + fpName;
    result["file_path"] = outputPath;
    result["pin_count"] = data.package.pin_count;
    return result.dump( 2 );
}


// ---------------------------------------------------------------------------
// MatchStandardFootprint — search KiCad library filenames for a match
// ---------------------------------------------------------------------------
std::string FOOTPRINT_GENERATOR::MatchStandardFootprint( const PackageData& aPkg )
{
    // Determine which .pretty library directory to search based on package type
    std::string pkgType = aPkg.package_type;
    std::transform( pkgType.begin(), pkgType.end(), pkgType.begin(), ::toupper );

    std::string libDir;
    std::string libName;

    if( pkgType.find( "QFN" ) != std::string::npos ||
        pkgType.find( "DFN" ) != std::string::npos )
    {
        libName = "Package_DFN_QFN";
    }
    else if( pkgType.find( "SOIC" ) != std::string::npos ||
             pkgType.find( "SOP" ) != std::string::npos ||
             pkgType.find( "TSSOP" ) != std::string::npos ||
             pkgType.find( "MSOP" ) != std::string::npos ||
             pkgType.find( "SSOP" ) != std::string::npos )
    {
        libName = "Package_SO";
    }
    else if( pkgType.find( "QFP" ) != std::string::npos ||
             pkgType.find( "LQFP" ) != std::string::npos ||
             pkgType.find( "TQFP" ) != std::string::npos )
    {
        libName = "Package_QFP";
    }
    else if( pkgType.find( "BGA" ) != std::string::npos )
    {
        libName = "Package_BGA";
    }
    else
    {
        return "";
    }

    // Find the footprint library directory
    // Check common locations: KICAD9_FOOTPRINT_DIR environment variable or standard paths
    wxString footprintDir;
    wxArrayString searchPaths;

    // Standard macOS bundle path
    searchPaths.Add( "/Applications/Zeo.app/Contents/SharedSupport/footprints" );

    // Build tree path
    searchPaths.Add( wxString::FromUTF8(
        TOOL_REGISTRY::Instance().GetProjectPath() + "/../../libraries/kicad-footprints" ) );

    // Try env variable
    wxString envDir;
    if( wxGetEnv( "KICAD9_FOOTPRINT_DIR", &envDir ) && !envDir.IsEmpty() )
        searchPaths.Insert( envDir, 0 );

    for( const auto& basePath : searchPaths )
    {
        wxString candidate = basePath + "/" + wxString::FromUTF8( libName ) + ".pretty";
        if( wxFileName::DirExists( candidate ) )
        {
            footprintDir = candidate;
            break;
        }
    }

    if( footprintDir.IsEmpty() )
    {
        wxLogTrace( "Agent", "FOOTPRINT_GENERATOR: Could not find %s.pretty library",
                    libName.c_str() );
        return "";
    }

    // Build the expected filename pattern
    // QFN format: QFN-{pins}-1EP_{W}x{H}mm_P{pitch}mm_EP{epW}x{epH}mm.kicad_mod
    //
    // We search for files matching pin count, body size, and pitch.
    // Then find the closest EP size match.

    wxArrayString files;
    wxDir::GetAllFiles( footprintDir, &files, "*.kicad_mod", wxDIR_FILES );

    std::string bestMatch;
    double bestEpDelta = 999.0;

    std::string bodyStr = FmtDim( aPkg.body_width_mm ) + "x" + FmtDim( aPkg.body_length_mm ) + "mm";
    std::string pitchStr = "P" + FmtDim( aPkg.pin_pitch_mm ) + "mm";
    std::string pinStr = std::to_string( aPkg.pin_count );

    wxLogTrace( "Agent", "FOOTPRINT_GENERATOR: Searching for %s-pin, %s, %s",
                pinStr.c_str(), bodyStr.c_str(), pitchStr.c_str() );

    for( const auto& filePath : files )
    {
        wxFileName fn( filePath );
        std::string name = fn.GetName().ToStdString();

        // Must contain pin count, body size, and pitch
        if( name.find( pinStr ) == std::string::npos )
            continue;
        if( name.find( bodyStr ) == std::string::npos )
            continue;
        if( name.find( pitchStr ) == std::string::npos )
            continue;

        // Skip ThermalVias variants — prefer the base footprint
        if( name.find( "ThermalVias" ) != std::string::npos )
            continue;

        // If no thermal pad, accept any match
        if( !aPkg.has_thermal_pad )
        {
            bestMatch = name;
            bestEpDelta = 0;
            break;
        }

        // Parse EP dimensions from filename: EP{W}x{H}mm
        size_t epPos = name.find( "EP" );
        if( epPos == std::string::npos )
            continue;

        std::string epStr = name.substr( epPos + 2 );
        size_t xPos = epStr.find( 'x' );
        size_t mmPos = epStr.find( "mm" );

        if( xPos == std::string::npos || mmPos == std::string::npos )
            continue;

        double epW = 0, epH = 0;
        try
        {
            epW = std::stod( epStr.substr( 0, xPos ) );
            epH = std::stod( epStr.substr( xPos + 1, mmPos - xPos - 1 ) );
        }
        catch( ... ) { continue; }

        double delta = std::abs( epW - aPkg.ep_width_mm ) + std::abs( epH - aPkg.ep_length_mm );

        if( delta < bestEpDelta )
        {
            bestEpDelta = delta;
            bestMatch = name;
        }
    }

    // Accept match if EP dimensions are within 0.5mm total
    if( !bestMatch.empty() && bestEpDelta <= 0.5 )
        return libName + ":" + bestMatch;

    // If we had a body+pitch+pin match but EP was too different, log it
    if( !bestMatch.empty() )
    {
        wxLogTrace( "Agent", "FOOTPRINT_GENERATOR: Closest match %s but EP delta=%.2f > 0.5",
                    bestMatch.c_str(), bestEpDelta );
    }

    return "";
}


// ---------------------------------------------------------------------------
// BuildFootprintName
// ---------------------------------------------------------------------------
std::string FOOTPRINT_GENERATOR::BuildFootprintName( const PackageData& aPkg )
{
    // e.g., "QFN-64-1EP_7.5x7.5mm_P0.4mm_EP5.5x5.5mm"
    std::string prefix = "QFN";

    std::string pkgType = aPkg.package_type;
    std::transform( pkgType.begin(), pkgType.end(), pkgType.begin(), ::toupper );

    if( pkgType.find( "DFN" ) != std::string::npos )
        prefix = "DFN";

    std::string name = prefix + "-" + std::to_string( aPkg.pin_count );

    if( aPkg.has_thermal_pad )
        name += "-1EP";

    name += "_" + FmtDim( aPkg.body_width_mm ) + "x" + FmtDim( aPkg.body_length_mm ) + "mm";
    name += "_P" + FmtDim( aPkg.pin_pitch_mm ) + "mm";

    if( aPkg.has_thermal_pad && aPkg.ep_width_mm > 0 && aPkg.ep_length_mm > 0 )
        name += "_EP" + FmtDim( aPkg.ep_width_mm ) + "x" + FmtDim( aPkg.ep_length_mm ) + "mm";

    return name;
}


// ---------------------------------------------------------------------------
// GenerateQfnFootprint — generate .kicad_mod S-expression for QFN/DFN
// ---------------------------------------------------------------------------
std::string FOOTPRINT_GENERATOR::GenerateQfnFootprint( const PackageData& aPkg,
                                                         const std::string& aName )
{
    if( aPkg.pin_count < 4 || aPkg.pin_pitch_mm <= 0 )
        return "";

    int pinsPerSide = aPkg.pin_count / 4;
    double bodyW = aPkg.body_width_mm;
    double bodyH = aPkg.body_length_mm;
    double halfW = bodyW / 2.0;
    double halfH = bodyH / 2.0;
    double pitch = aPkg.pin_pitch_mm;

    // Determine pad dimensions from datasheet recommended land pattern,
    // or compute from terminal dimensions, or use IPC-7351 defaults
    double padW, padL;

    if( aPkg.pad_width_mm > 0 && aPkg.pad_length_mm > 0 )
    {
        padW = aPkg.pad_width_mm;
        padL = aPkg.pad_length_mm;
    }
    else if( aPkg.terminal_width_mm > 0 && aPkg.terminal_length_mm > 0 )
    {
        // IPC-7351B nominal: pad = terminal + solder fillet
        padW = aPkg.terminal_width_mm + 0.05;
        padL = aPkg.terminal_length_mm + 0.35;
    }
    else
    {
        // IPC-7351B defaults based on pitch
        padW = pitch * 0.50;
        padL = std::max( 0.6, pitch * 1.75 );
    }

    // Pad center distance from origin (along the axis perpendicular to the body edge)
    // The pad extends from (halfW - some inset) outward
    double padCenter = halfW - 0.0 + padL / 2.0;  // Pads start at body edge

    // Silkscreen and courtyard offsets
    double silkOffset = 0.11;  // Gap from pad to silk
    double crtYdClearance = 0.25;

    double silkEdge = halfW + silkOffset;
    double padOuterEdge = padCenter + padL / 2.0;
    double crtYdPad = padOuterEdge + crtYdClearance;
    double crtYdBody = halfW + crtYdClearance;

    // First/last pad Y positions for silk gap calculation
    double firstPadY = -( pinsPerSide - 1 ) * pitch / 2.0;
    double lastPadY = ( pinsPerSide - 1 ) * pitch / 2.0;
    double silkGapY = lastPadY + padW / 2.0 + silkOffset;

    std::ostringstream ss;
    ss << std::fixed;
    ss.precision( 4 );

    ss << "(footprint \"" << aName << "\"\n";
    ss << "\t(version 20241229)\n";
    ss << "\t(generator \"zeo_agent\")\n";
    ss << "\t(layer \"F.Cu\")\n";
    ss << "\t(descr \"Generated by Zeo agent from datasheet extraction\")\n";
    ss << "\t(tags \"" << aPkg.package_type << " NoLead\")\n";

    // Reference
    ss << "\t(property \"Reference\" \"REF**\"\n";
    ss << "\t\t(at 0 " << -( crtYdPad + 1.0 ) << " 0)\n";
    ss << "\t\t(layer \"F.SilkS\")\n";
    ss << "\t\t(effects (font (size 1 1) (thickness 0.15)))\n";
    ss << "\t)\n";

    // Value
    ss << "\t(property \"Value\" \"" << aName << "\"\n";
    ss << "\t\t(at 0 " << ( crtYdPad + 1.0 ) << " 0)\n";
    ss << "\t\t(layer \"F.Fab\")\n";
    ss << "\t\t(effects (font (size 1 1) (thickness 0.15)))\n";
    ss << "\t)\n";

    ss << "\t(attr smd)\n";

    // --- Silkscreen corner marks (F.SilkS) ---
    // Four corners, with gaps for pads
    auto emitSilkCorner = [&]( double cx, double cy, double dirX, double dirY )
    {
        double cornerX = cx * silkEdge;
        double cornerY = cy * silkEdge;
        double gapX = cx * silkGapY;  // Where pads start on adjacent side
        double gapY = cy * silkGapY;

        // Horizontal line from corner
        ss << "\t(fp_line (start " << cornerX << " " << cornerY << ") "
           << "(end " << cx * silkEdge << " " << gapY << ") "
           << "(stroke (width 0.12) (type solid)) (layer \"F.SilkS\"))\n";

        // Vertical line from corner
        ss << "\t(fp_line (start " << cornerX << " " << cornerY << ") "
           << "(end " << gapX << " " << cy * silkEdge << ") "
           << "(stroke (width 0.12) (type solid)) (layer \"F.SilkS\"))\n";
    };

    emitSilkCorner( -1, -1, 1, 1 );
    emitSilkCorner(  1, -1, -1, 1 );
    emitSilkCorner(  1,  1, -1, -1 );
    emitSilkCorner( -1,  1, 1, -1 );

    // Pin 1 marker (triangle on silk)
    double markerX = -( padOuterEdge + 0.3 );
    double markerY = firstPadY;
    ss << "\t(fp_poly (pts "
       << "(xy " << markerX << " " << markerY << ") "
       << "(xy " << ( markerX - 0.33 ) << " " << ( markerY - 0.24 ) << ") "
       << "(xy " << ( markerX - 0.33 ) << " " << ( markerY + 0.24 ) << ")"
       << ") (stroke (width 0.12) (type solid)) (fill yes) (layer \"F.SilkS\"))\n";

    // --- Courtyard (F.CrtYd) — simplified rectangle ---
    ss << "\t(fp_line (start " << -crtYdPad << " " << -crtYdPad << ") "
       << "(end " << crtYdPad << " " << -crtYdPad << ") "
       << "(stroke (width 0.05) (type solid)) (layer \"F.CrtYd\"))\n";
    ss << "\t(fp_line (start " << crtYdPad << " " << -crtYdPad << ") "
       << "(end " << crtYdPad << " " << crtYdPad << ") "
       << "(stroke (width 0.05) (type solid)) (layer \"F.CrtYd\"))\n";
    ss << "\t(fp_line (start " << crtYdPad << " " << crtYdPad << ") "
       << "(end " << -crtYdPad << " " << crtYdPad << ") "
       << "(stroke (width 0.05) (type solid)) (layer \"F.CrtYd\"))\n";
    ss << "\t(fp_line (start " << -crtYdPad << " " << crtYdPad << ") "
       << "(end " << -crtYdPad << " " << -crtYdPad << ") "
       << "(stroke (width 0.05) (type solid)) (layer \"F.CrtYd\"))\n";

    // --- Fabrication layer body outline with pin-1 chamfer (F.Fab) ---
    double chamfer = std::min( 0.75, halfW * 0.25 );
    ss << "\t(fp_poly (pts "
       << "(xy " << -halfW + chamfer << " " << -halfH << ") "
       << "(xy " << halfW << " " << -halfH << ") "
       << "(xy " << halfW << " " << halfH << ") "
       << "(xy " << -halfW << " " << halfH << ") "
       << "(xy " << -halfW << " " << -halfH + chamfer << ")"
       << ") (stroke (width 0.1) (type solid)) (fill no) (layer \"F.Fab\"))\n";

    // Reference on Fab layer
    ss << "\t(fp_text user \"${REFERENCE}\" (at 0 0 0) (layer \"F.Fab\") "
       << "(effects (font (size 0.75 0.75) (thickness 0.11))))\n";

    // --- Paste apertures for exposed pad ---
    if( aPkg.has_thermal_pad && aPkg.ep_width_mm > 0 && aPkg.ep_length_mm > 0 )
    {
        double epW = aPkg.ep_width_mm;
        double epH = aPkg.ep_length_mm;

        // Subdivide into grid for solder paste (target ~1mm apertures)
        int gridX = std::max( 2, (int)std::round( epW / 1.0 ) );
        int gridY = std::max( 2, (int)std::round( epH / 1.0 ) );

        double gap = 0.2;
        double apertureW = ( epW - gap * ( gridX - 1 ) ) / gridX;
        double apertureH = ( epH - gap * ( gridY - 1 ) ) / gridY;

        for( int ix = 0; ix < gridX; ix++ )
        {
            for( int iy = 0; iy < gridY; iy++ )
            {
                double ax = -epW / 2.0 + apertureW / 2.0 + ix * ( apertureW + gap );
                double ay = -epH / 2.0 + apertureH / 2.0 + iy * ( apertureH + gap );

                ss << "\t(pad \"\" smd roundrect (at " << ax << " " << ay << ") "
                   << "(size " << apertureW << " " << apertureH << ") "
                   << "(layers \"F.Paste\") (roundrect_rratio 0.25))\n";
            }
        }
    }

    // --- Signal pads ---
    // QFN numbering: left (1..pps), bottom (pps+1..2*pps), right (2*pps+1..3*pps), top (3*pps+1..4*pps)
    // Left/right pads: horizontal (size padL padW)
    // Top/bottom pads: vertical (size padW padL)

    int pinNum = 1;

    // Left side — pins go top to bottom
    for( int i = 0; i < pinsPerSide; i++ )
    {
        double y = firstPadY + i * pitch;
        ss << "\t(pad \"" << pinNum++ << "\" smd roundrect "
           << "(at " << -padCenter << " " << y << ") "
           << "(size " << padL << " " << padW << ") "
           << "(layers \"F.Cu\" \"F.Mask\" \"F.Paste\") (roundrect_rratio 0.25))\n";
    }

    // Bottom side — pins go left to right
    for( int i = 0; i < pinsPerSide; i++ )
    {
        double x = firstPadY + i * pitch;
        ss << "\t(pad \"" << pinNum++ << "\" smd roundrect "
           << "(at " << x << " " << padCenter << ") "
           << "(size " << padW << " " << padL << ") "
           << "(layers \"F.Cu\" \"F.Mask\" \"F.Paste\") (roundrect_rratio 0.25))\n";
    }

    // Right side — pins go bottom to top
    for( int i = 0; i < pinsPerSide; i++ )
    {
        double y = lastPadY - i * pitch;
        ss << "\t(pad \"" << pinNum++ << "\" smd roundrect "
           << "(at " << padCenter << " " << y << ") "
           << "(size " << padL << " " << padW << ") "
           << "(layers \"F.Cu\" \"F.Mask\" \"F.Paste\") (roundrect_rratio 0.25))\n";
    }

    // Top side — pins go right to left
    for( int i = 0; i < pinsPerSide; i++ )
    {
        double x = lastPadY - i * pitch;
        ss << "\t(pad \"" << pinNum++ << "\" smd roundrect "
           << "(at " << x << " " << -padCenter << ") "
           << "(size " << padW << " " << padL << ") "
           << "(layers \"F.Cu\" \"F.Mask\" \"F.Paste\") (roundrect_rratio 0.25))\n";
    }

    // --- Exposed/thermal pad ---
    if( aPkg.has_thermal_pad && aPkg.ep_width_mm > 0 && aPkg.ep_length_mm > 0 )
    {
        ss << "\t(pad \"" << pinNum << "\" smd rect (at 0 0) "
           << "(size " << aPkg.ep_width_mm << " " << aPkg.ep_length_mm << ") "
           << "(property pad_prop_heatsink) "
           << "(layers \"F.Cu\" \"F.Mask\") (zone_connect 2))\n";
    }

    ss << "\t(embedded_fonts no)\n";
    ss << ")\n";

    return ss.str();
}


// ---------------------------------------------------------------------------
// FetchComponentData
// ---------------------------------------------------------------------------
bool FOOTPRINT_GENERATOR::FetchComponentData( const std::string& aPartNumber,
                                               const std::string& aManufacturer,
                                               ComponentData& aData,
                                               const std::string& aComponentId,
                                               const std::string& aDatasheetUrl )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return false;

    std::string accessToken = auth->GetAccessToken();
    if( accessToken.empty() )
        return false;

    std::string url = supabaseUrl + "/rest/v1/components?select="
        "id,part_number,manufacturer,description,datasheet_url,extraction_status,"
        "component_packages(id,package_type,pin_count,pin_pitch_mm,"
            "length_mm,width_mm,height_mm,"
            "has_thermal_pad,thermal_pad_length_mm,thermal_pad_width_mm,"
            "terminal_width_mm,terminal_length_mm,pad_width_mm,pad_length_mm)";

    if( !aDatasheetUrl.empty() )
        url += "&datasheet_url=eq." + UrlEncode( aDatasheetUrl );
    else if( !aComponentId.empty() )
        url += "&id=eq." + UrlEncode( aComponentId );
    else
        url += "&part_number=eq." + UrlEncode( aPartNumber );

    url += "&limit=1";

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + accessToken );
        curl.SetHeader( "apikey", anonKey );
        curl.SetHeader( "Accept", "application/json" );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();
        if( httpCode < 200 || httpCode >= 300 )
            return false;

        auto response = json::parse( curl.GetBuffer() );
        if( !response.is_array() || response.empty() )
            return false;

        auto& comp = response[0];

        if( SafeStr( comp, "extraction_status", "" ) != "completed" )
            return false;

        aData.part_number = SafeStr( comp, "part_number", aPartNumber );
        aData.manufacturer = SafeStr( comp, "manufacturer", aManufacturer );
        aData.description = SafeStr( comp, "description", "" );
        aData.datasheet_url = SafeStr( comp, "datasheet_url", "" );

        if( comp.contains( "component_packages" ) && !comp["component_packages"].empty() )
        {
            auto& pkg = comp["component_packages"][0]; // Use first package

            aData.package.package_type = SafeStr( pkg, "package_type", "" );
            aData.package.pin_count = SafeInt( pkg, "pin_count", 0 );
            aData.package.pin_pitch_mm = SafeDouble( pkg, "pin_pitch_mm", 0 );
            aData.package.body_width_mm = SafeDouble( pkg, "length_mm", 0 );
            aData.package.body_length_mm = SafeDouble( pkg, "width_mm", 0 );

            auto it = pkg.find( "has_thermal_pad" );
            aData.package.has_thermal_pad = ( it != pkg.end() && !it->is_null() && it->get<bool>() );

            aData.package.ep_width_mm = SafeDouble( pkg, "thermal_pad_width_mm", 0 );
            aData.package.ep_length_mm = SafeDouble( pkg, "thermal_pad_length_mm", 0 );
            aData.package.terminal_width_mm = SafeDouble( pkg, "terminal_width_mm", 0 );
            aData.package.terminal_length_mm = SafeDouble( pkg, "terminal_length_mm", 0 );
            aData.package.pad_width_mm = SafeDouble( pkg, "pad_width_mm", 0 );
            aData.package.pad_length_mm = SafeDouble( pkg, "pad_length_mm", 0 );
        }

        return aData.package.pin_count > 0;
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "FOOTPRINT_GENERATOR: Fetch exception: %s", e.what() );
        return false;
    }
}


// ---------------------------------------------------------------------------
// TriggerExtractionSync
// ---------------------------------------------------------------------------
std::string FOOTPRINT_GENERATOR::TriggerExtractionSync( const std::string& aPartNumber,
                                                          const std::string& aManufacturer,
                                                          const std::string& aDatasheetUrl )
{
    const auto& reg = TOOL_REGISTRY::Instance();
    AGENT_AUTH* auth = reg.GetAuth();
    const std::string& supabaseUrl = reg.GetSupabaseUrl();
    const std::string& anonKey = reg.GetSupabaseAnonKey();

    if( !auth || supabaseUrl.empty() )
        return "";

    std::string accessToken = auth->GetAccessToken();
    if( accessToken.empty() )
        return "";

    json body;
    body["datasheet_url"] = aDatasheetUrl;
    body["mode"] = "sync";

    if( !aPartNumber.empty() )
        body["part_number"] = aPartNumber;
    if( !aManufacturer.empty() )
        body["manufacturer"] = aManufacturer;

    std::string url = supabaseUrl + "/functions/v1/extract-datasheet";

    try
    {
        KICAD_CURL_EASY curl;
        curl.SetURL( url );
        curl.SetFollowRedirects( true );
        curl.SetHeader( "Authorization", "Bearer " + accessToken );
        curl.SetHeader( "apikey", anonKey );
        curl.SetHeader( "Content-Type", "application/json" );
        curl.SetPostFields( body.dump() );
        curl_easy_setopt( curl.GetCurl(), CURLOPT_TIMEOUT, 180L );
        curl.Perform();

        long httpCode = curl.GetResponseStatusCode();

        if( httpCode >= 200 && httpCode < 300 )
        {
            auto resultJson = json::parse( curl.GetBuffer() );
            if( SafeStr( resultJson, "extraction_status", "" ) == "completed" )
                return SafeStr( resultJson, "component_id", "" );
        }

        return "";
    }
    catch( const std::exception& e )
    {
        wxLogTrace( "Agent", "FOOTPRINT_GENERATOR: Extraction exception: %s", e.what() );
        return "";
    }
}


// ---------------------------------------------------------------------------
// EnsureFpLibTableEntry
// ---------------------------------------------------------------------------
void FOOTPRINT_GENERATOR::EnsureFpLibTableEntry( const std::string& aProjectPath,
                                                   const std::string& aLibraryName )
{
    wxFileName projDir( wxString::FromUTF8( aProjectPath ), "" );
    wxFileName libTablePath( projDir.GetPath(), "fp-lib-table" );
    std::string tablePath = libTablePath.GetFullPath().ToStdString();

    std::string entry = "(lib (name \"" + aLibraryName
                         + "\")(type \"KiCad\")(uri \"${KIPRJMOD}/" + aLibraryName
                         + ".pretty\")(options \"\")(descr \"Agent-generated footprints\"))";

    if( wxFileName::FileExists( libTablePath.GetFullPath() ) )
    {
        std::ifstream file( tablePath );
        std::string content( ( std::istreambuf_iterator<char>( file ) ),
                               std::istreambuf_iterator<char>() );
        file.close();

        // Check if library already registered
        if( content.find( "\"" + aLibraryName + "\"" ) != std::string::npos )
            return;

        // Append before closing paren
        size_t lastParen = content.rfind( ')' );
        if( lastParen != std::string::npos )
        {
            std::string newContent = content.substr( 0, lastParen )
                                     + "  " + entry + "\n)\n";
            std::ofstream outFile( tablePath );
            outFile << newContent;
            outFile.close();
        }
    }
    else
    {
        // Create new fp-lib-table
        std::ofstream outFile( tablePath );
        outFile << "(fp_lib_table\n"
                << "  (version 7)\n"
                << "  " << entry << "\n"
                << ")\n";
        outFile.close();
    }

    wxLogInfo( "FOOTPRINT_GENERATOR: Ensured fp-lib-table entry for '%s'", aLibraryName.c_str() );
}
