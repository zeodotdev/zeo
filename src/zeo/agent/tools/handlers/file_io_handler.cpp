#include "file_io_handler.h"
#include "tool_registry.h"
#include <nlohmann/json.hpp>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <fstream>
#include <sstream>

using json = nlohmann::json;


// File extensions that are never allowed to be written
static const std::vector<std::string> BLOCKED_EXTENSIONS = {
    ".kicad_sch", ".kicad_pcb", ".kicad_pro", ".kicad_sym", ".kicad_mod",
    ".kicad_wks", ".kicad_dru",
    ".exe", ".dll", ".so", ".dylib", ".bin", ".o", ".a",
    ".zip", ".tar", ".gz", ".bz2", ".7z", ".rar",
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".svg", ".ico",
    ".pdf", ".doc", ".docx", ".xls", ".xlsx"
};


std::string FILE_IO_HANDLER::ValidatePath( const std::string& aPath, bool aIsWrite ) const
{
    if( aPath.empty() )
        return "Error: file_path is required";

    // Must be an absolute path
    wxFileName fn( wxString::FromUTF8( aPath ) );

    if( !fn.IsAbsolute() )
        return "Error: file_path must be an absolute path";

    // Normalize to resolve ".." and symlinks
    fn.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );
    std::string normalized = fn.GetFullPath().ToStdString();

    // Block writes to sensitive directories
    if( aIsWrite )
    {
        // Check blocked extensions
        std::string ext = fn.GetExt().ToStdString();

        if( !ext.empty() )
        {
            std::string dotExt = "." + ext;

            for( const auto& blocked : BLOCKED_EXTENSIONS )
            {
                if( dotExt == blocked )
                    return "Error: Cannot write files with extension '" + dotExt
                           + "'. Use the appropriate KiCad tool instead.";
            }
        }

        // Block writes outside the project directory
        const std::string& projectPath = TOOL_REGISTRY::Instance().GetProjectPath();

        if( !projectPath.empty() )
        {
            // Allow writes within the project directory tree
            if( normalized.find( projectPath ) != 0 )
            {
                return "Error: file_path must be within the project directory ("
                       + projectPath + ")";
            }
        }
    }

    return "";  // Valid
}


std::string FILE_IO_HANDLER::ExecuteWrite( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    std::string content = aInput.value( "content", "" );

    std::string error = ValidatePath( filePath, true );

    if( !error.empty() )
        return error;

    wxFileName fn( wxString::FromUTF8( filePath ) );
    fn.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );

    // Create parent directories if they don't exist
    wxFileName dirPath( fn.GetPath(), "" );

    if( !dirPath.DirExists() )
    {
        if( !wxFileName::Mkdir( dirPath.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        {
            return "Error: Could not create directory: " + dirPath.GetPath().ToStdString();
        }
    }

    bool existed = fn.FileExists();

    // Write the file
    std::string normalizedPath = fn.GetFullPath().ToStdString();
    std::ofstream ofs( normalizedPath, std::ios::out | std::ios::trunc );

    if( !ofs.is_open() )
        return "Error: Could not open file for writing: " + normalizedPath;

    ofs << content;
    ofs.close();

    if( ofs.fail() )
        return "Error: Failed to write file: " + normalizedPath;

    json result = {
        { "status", "success" },
        { "file_path", normalizedPath },
        { "action", existed ? "overwritten" : "created" },
        { "bytes_written", content.size() }
    };

    wxLogInfo( "FILE_IO_HANDLER: %s file '%s' (%zu bytes)",
               existed ? "Overwrote" : "Created",
               normalizedPath, content.size() );

    return result.dump( 2 );
}


std::string FILE_IO_HANDLER::ExecuteRead( const nlohmann::json& aInput )
{
    std::string filePath = aInput.value( "file_path", "" );
    int offset = aInput.value( "offset", 0 );       // 0-based line offset
    int limit = aInput.value( "limit", 2000 );       // max lines to read

    std::string error = ValidatePath( filePath, false );

    if( !error.empty() )
        return error;

    wxFileName fn( wxString::FromUTF8( filePath ) );
    fn.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );
    std::string normalizedPath = fn.GetFullPath().ToStdString();

    if( !fn.FileExists() )
        return "Error: File not found: " + normalizedPath;

    std::ifstream ifs( normalizedPath );

    if( !ifs.is_open() )
        return "Error: Could not open file for reading: " + normalizedPath;

    // Read lines with offset and limit
    std::string line;
    std::ostringstream output;
    int lineNum = 0;
    int linesRead = 0;

    while( std::getline( ifs, line ) )
    {
        if( lineNum >= offset )
        {
            if( linesRead >= limit )
                break;

            output << line << "\n";
            linesRead++;
        }

        lineNum++;
    }

    int totalLines = lineNum;

    // If we stopped early because of getline ending, totalLines is correct.
    // But if we hit the limit, count remaining lines.
    if( linesRead >= limit )
    {
        while( std::getline( ifs, line ) )
            totalLines++;
    }

    json result = {
        { "status", "success" },
        { "file_path", normalizedPath },
        { "content", output.str() },
        { "lines_read", linesRead },
        { "total_lines", totalLines }
    };

    if( offset > 0 )
        result["offset"] = offset;

    if( linesRead < totalLines - offset )
        result["truncated"] = true;

    return result.dump( 2 );
}


std::string FILE_IO_HANDLER::Execute( const std::string& aToolName, const nlohmann::json& aInput )
{
    if( aToolName == "file_write" )
        return ExecuteWrite( aInput );
    else if( aToolName == "file_read" )
        return ExecuteRead( aInput );

    return "Error: Unknown tool '" + aToolName + "'";
}


std::string FILE_IO_HANDLER::GetDescription( const std::string& aToolName,
                                              const nlohmann::json& aInput ) const
{
    std::string filePath = aInput.value( "file_path", "" );

    // Extract just the filename for display
    if( !filePath.empty() )
    {
        wxFileName fn( wxString::FromUTF8( filePath ) );
        std::string name = fn.GetFullName().ToStdString();

        if( aToolName == "file_write" )
            return "Writing " + name;
        else
            return "Reading " + name;
    }

    if( aToolName == "file_write" )
        return "Write File";
    else
        return "Read File";
}
