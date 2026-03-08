#include "memory_handler.h"
#include "tool_registry.h"
#include <nlohmann/json.hpp>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <fstream>
#include <sstream>

using json = nlohmann::json;


std::string MEMORY_HANDLER::GetMemoriesDir() const
{
    const std::string& projectPath = TOOL_REGISTRY::Instance().GetProjectPath();

    if( projectPath.empty() )
        return "";

    wxFileName dir( wxString::FromUTF8( projectPath ), "" );
    dir.AppendDir( "memories" );
    return dir.GetPath().ToStdString();
}


std::string MEMORY_HANDLER::ResolvePath( const std::string& aVirtualPath, std::string& aError ) const
{
    std::string memoriesDir = GetMemoriesDir();

    if( memoriesDir.empty() )
    {
        aError = "Error: No project is open. Cannot access memory files.";
        return "";
    }

    // Virtual paths must start with /memories
    if( aVirtualPath.find( "/memories" ) != 0 )
    {
        aError = "The path " + aVirtualPath
                 + " does not exist. Please provide a valid path.";
        return "";
    }

    // Map /memories/... to <project>/memories/...
    std::string relative = aVirtualPath.substr( 9 );  // strip "/memories"

    // Reject traversal attempts
    if( relative.find( ".." ) != std::string::npos
        || relative.find( "%2e" ) != std::string::npos
        || relative.find( "%2E" ) != std::string::npos )
    {
        aError = "The path " + aVirtualPath
                 + " does not exist. Please provide a valid path.";
        return "";
    }

    std::string realPath;

    if( relative.empty() || relative == "/" )
    {
        realPath = memoriesDir;
    }
    else
    {
        // Strip leading slash from relative part
        if( !relative.empty() && relative[0] == '/' )
            relative = relative.substr( 1 );

        wxFileName fn( wxString::FromUTF8( memoriesDir ), wxString::FromUTF8( relative ) );
        fn.Normalize( wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE );
        realPath = fn.GetFullPath().ToStdString();
    }

    // Verify resolved path is still within memories dir
    if( realPath.find( memoriesDir ) != 0 )
    {
        aError = "The path " + aVirtualPath
                 + " does not exist. Please provide a valid path.";
        return "";
    }

    return realPath;
}


std::string MEMORY_HANDLER::ExecuteView( const nlohmann::json& aInput )
{
    std::string virtualPath = aInput.value( "path", "" );
    std::string error;
    std::string realPath = ResolvePath( virtualPath, error );

    if( realPath.empty() )
        return error;

    wxFileName fn( wxString::FromUTF8( realPath ) );

    // Check if it's a directory
    if( wxDir::Exists( wxString::FromUTF8( realPath ) ) )
    {
        // List directory contents up to 2 levels deep
        std::ostringstream out;
        out << "Here're the files and directories up to 2 levels deep in "
            << virtualPath << ", excluding hidden items and node_modules:\n";

        // Helper to get human-readable size
        auto humanSize = []( wxULongLong bytes ) -> std::string
        {
            double val = bytes.IsNullable() ? 0.0 : bytes.ToDouble();

            if( val < 1024.0 )
                return std::to_string( (int) val ) + "B";
            else if( val < 1024.0 * 1024.0 )
            {
                char buf[16];
                snprintf( buf, sizeof( buf ), "%.1fK", val / 1024.0 );
                return buf;
            }
            else
            {
                char buf[16];
                snprintf( buf, sizeof( buf ), "%.1fM", val / ( 1024.0 * 1024.0 ) );
                return buf;
            }
        };

        // Recursive listing helper
        std::function<void( const std::string&, const std::string&, int )> listDir;
        listDir = [&]( const std::string& dirPath, const std::string& vPath, int depth )
        {
            if( depth > 2 )
                return;

            wxDir dir( wxString::FromUTF8( dirPath ) );

            if( !dir.IsOpened() )
                return;

            // Get directory size (approximate with 4.0K)
            out << "4.0K\t" << vPath << "\n";

            if( depth >= 2 )
                return;

            wxString filename;
            bool cont = dir.GetFirst( &filename );

            // Collect entries and sort them
            std::vector<std::string> entries;

            while( cont )
            {
                std::string name = filename.ToStdString();

                // Skip hidden items and node_modules
                if( !name.empty() && name[0] != '.' && name != "node_modules" )
                    entries.push_back( name );

                cont = dir.GetNext( &filename );
            }

            std::sort( entries.begin(), entries.end() );

            for( const auto& entry : entries )
            {
                std::string entryPath = dirPath + "/" + entry;
                std::string entryVPath = vPath + "/" + entry;

                wxFileName entryFn( wxString::FromUTF8( entryPath ) );

                if( wxDir::Exists( wxString::FromUTF8( entryPath ) ) )
                {
                    listDir( entryPath, entryVPath, depth + 1 );
                }
                else if( entryFn.FileExists() )
                {
                    wxULongLong fileSize = entryFn.GetSize();
                    out << humanSize( fileSize ) << "\t" << entryVPath << "\n";
                }
            }
        };

        listDir( realPath, virtualPath, 0 );
        return out.str();
    }

    // It's a file
    if( !fn.FileExists() )
        return "The path " + virtualPath + " does not exist. Please provide a valid path.";

    std::ifstream ifs( realPath );

    if( !ifs.is_open() )
        return "Error: Could not open file: " + virtualPath;

    // Check for view_range
    int startLine = 1;
    int endLine = -1;  // -1 means read to end

    if( aInput.contains( "view_range" ) && aInput["view_range"].is_array() )
    {
        auto range = aInput["view_range"];

        if( range.size() >= 1 )
            startLine = range[0].get<int>();

        if( range.size() >= 2 )
            endLine = range[1].get<int>();
    }

    std::ostringstream out;
    out << "Here's the content of " << virtualPath << " with line numbers:\n";

    std::string line;
    int lineNum = 0;
    int linesOutput = 0;

    while( std::getline( ifs, line ) )
    {
        lineNum++;

        if( lineNum > 999999 )
            return "File " + virtualPath + " exceeds maximum line limit of 999,999 lines.";

        if( lineNum < startLine )
            continue;

        if( endLine > 0 && lineNum > endLine )
            break;

        // Right-aligned 6-char line number + tab + content
        char numBuf[8];
        snprintf( numBuf, sizeof( numBuf ), "%6d", lineNum );
        out << numBuf << "\t" << line << "\n";
        linesOutput++;
    }

    return out.str();
}


std::string MEMORY_HANDLER::ExecuteCreate( const nlohmann::json& aInput )
{
    std::string virtualPath = aInput.value( "path", "" );
    std::string fileText = aInput.value( "file_text", "" );
    std::string error;
    std::string realPath = ResolvePath( virtualPath, error );

    if( realPath.empty() )
        return error;

    // Check if file already exists
    wxFileName fn( wxString::FromUTF8( realPath ) );

    if( fn.FileExists() )
        return "Error: File " + virtualPath + " already exists";

    // Create parent directories if needed
    wxFileName dirPath( fn.GetPath(), "" );

    if( !dirPath.DirExists() )
    {
        if( !wxFileName::Mkdir( dirPath.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
            return "Error: Could not create directory for " + virtualPath;
    }

    std::ofstream ofs( realPath, std::ios::out | std::ios::trunc );

    if( !ofs.is_open() )
        return "Error: Could not create file: " + virtualPath;

    ofs << fileText;
    ofs.close();

    if( ofs.fail() )
        return "Error: Failed to write file: " + virtualPath;

    wxLogInfo( "MEMORY_HANDLER: Created memory file '%s'", virtualPath );
    return "File created successfully at: " + virtualPath;
}


std::string MEMORY_HANDLER::ExecuteStrReplace( const nlohmann::json& aInput )
{
    std::string virtualPath = aInput.value( "path", "" );
    std::string oldStr = aInput.value( "old_str", "" );
    std::string newStr = aInput.value( "new_str", "" );
    std::string error;
    std::string realPath = ResolvePath( virtualPath, error );

    if( realPath.empty() )
        return error;

    if( wxDir::Exists( wxString::FromUTF8( realPath ) ) )
    {
        return "Error: The path " + virtualPath
               + " does not exist. Please provide a valid path.";
    }

    wxFileName fn( wxString::FromUTF8( realPath ) );

    if( !fn.FileExists() )
    {
        return "Error: The path " + virtualPath
               + " does not exist. Please provide a valid path.";
    }

    // Read file content
    std::ifstream ifs( realPath );

    if( !ifs.is_open() )
        return "Error: Could not open file: " + virtualPath;

    std::string content( ( std::istreambuf_iterator<char>( ifs ) ),
                         std::istreambuf_iterator<char>() );
    ifs.close();

    // Find occurrences of old_str
    std::vector<int> occurrenceLines;
    size_t searchPos = 0;

    while( ( searchPos = content.find( oldStr, searchPos ) ) != std::string::npos )
    {
        // Count line number
        int lineNum = 1;

        for( size_t i = 0; i < searchPos; i++ )
        {
            if( content[i] == '\n' )
                lineNum++;
        }

        occurrenceLines.push_back( lineNum );
        searchPos += oldStr.length();
    }

    if( occurrenceLines.empty() )
    {
        return "No replacement was performed, old_str `" + oldStr
               + "` did not appear verbatim in " + virtualPath + ".";
    }

    if( occurrenceLines.size() > 1 )
    {
        std::string lines;

        for( size_t i = 0; i < occurrenceLines.size(); i++ )
        {
            if( i > 0 )
                lines += ", ";

            lines += std::to_string( occurrenceLines[i] );
        }

        return "No replacement was performed. Multiple occurrences of old_str `"
               + oldStr + "` in lines: " + lines + ". Please ensure it is unique";
    }

    // Single occurrence — perform replacement
    size_t pos = content.find( oldStr );
    content.replace( pos, oldStr.length(), newStr );

    // Write back
    std::ofstream ofs( realPath, std::ios::out | std::ios::trunc );

    if( !ofs.is_open() )
        return "Error: Could not write file: " + virtualPath;

    ofs << content;
    ofs.close();

    // Build snippet around the replacement
    int replaceLine = occurrenceLines[0];
    std::istringstream iss( content );
    std::string line;
    int lineNum = 0;
    int snippetStart = std::max( 1, replaceLine - 2 );
    int snippetEnd = replaceLine + 5;
    std::ostringstream snippet;

    while( std::getline( iss, line ) )
    {
        lineNum++;

        if( lineNum >= snippetStart && lineNum <= snippetEnd )
        {
            char numBuf[8];
            snprintf( numBuf, sizeof( numBuf ), "%6d", lineNum );
            snippet << numBuf << "\t" << line << "\n";
        }

        if( lineNum > snippetEnd )
            break;
    }

    wxLogInfo( "MEMORY_HANDLER: Replaced text in '%s'", virtualPath );
    return "The memory file has been edited.\n" + snippet.str();
}


std::string MEMORY_HANDLER::ExecuteInsert( const nlohmann::json& aInput )
{
    std::string virtualPath = aInput.value( "path", "" );
    int insertLine = aInput.value( "insert_line", -1 );
    std::string insertText = aInput.value( "insert_text", "" );
    std::string error;
    std::string realPath = ResolvePath( virtualPath, error );

    if( realPath.empty() )
        return error;

    if( wxDir::Exists( wxString::FromUTF8( realPath ) ) )
        return "Error: The path " + virtualPath + " does not exist";

    wxFileName fn( wxString::FromUTF8( realPath ) );

    if( !fn.FileExists() )
        return "Error: The path " + virtualPath + " does not exist";

    // Read all lines
    std::ifstream ifs( realPath );

    if( !ifs.is_open() )
        return "Error: Could not open file: " + virtualPath;

    std::vector<std::string> lines;
    std::string line;

    while( std::getline( ifs, line ) )
        lines.push_back( line );

    ifs.close();

    int nLines = static_cast<int>( lines.size() );

    if( insertLine < 0 || insertLine > nLines )
    {
        return "Error: Invalid `insert_line` parameter: " + std::to_string( insertLine )
               + ". It should be within the range of lines of the file: [0, "
               + std::to_string( nLines ) + "]";
    }

    // Split insert_text into lines
    std::istringstream iss( insertText );
    std::vector<std::string> newLines;
    std::string newLine;

    while( std::getline( iss, newLine ) )
        newLines.push_back( newLine );

    // If insertText ends with newline and getline consumed it, that's fine
    // Insert at position
    lines.insert( lines.begin() + insertLine, newLines.begin(), newLines.end() );

    // Write back
    std::ofstream ofs( realPath, std::ios::out | std::ios::trunc );

    if( !ofs.is_open() )
        return "Error: Could not write file: " + virtualPath;

    for( size_t i = 0; i < lines.size(); i++ )
    {
        ofs << lines[i];

        if( i + 1 < lines.size() )
            ofs << "\n";
    }

    ofs.close();

    wxLogInfo( "MEMORY_HANDLER: Inserted text at line %d in '%s'", insertLine, virtualPath );
    return "The file " + virtualPath + " has been edited.";
}


std::string MEMORY_HANDLER::ExecuteDelete( const nlohmann::json& aInput )
{
    std::string virtualPath = aInput.value( "path", "" );
    std::string error;
    std::string realPath = ResolvePath( virtualPath, error );

    if( realPath.empty() )
        return error;

    // Don't allow deleting the root /memories directory itself
    std::string memoriesDir = GetMemoriesDir();

    if( realPath == memoriesDir )
        return "Error: Cannot delete the root memories directory";

    if( wxDir::Exists( wxString::FromUTF8( realPath ) ) )
    {
        // Recursive directory delete
        wxFileName::Rmdir( wxString::FromUTF8( realPath ), wxPATH_RMDIR_RECURSIVE );
        wxLogInfo( "MEMORY_HANDLER: Deleted directory '%s'", virtualPath );
        return "Successfully deleted " + virtualPath;
    }

    wxFileName fn( wxString::FromUTF8( realPath ) );

    if( !fn.FileExists() )
        return "Error: The path " + virtualPath + " does not exist";

    if( !wxRemoveFile( wxString::FromUTF8( realPath ) ) )
        return "Error: Could not delete " + virtualPath;

    wxLogInfo( "MEMORY_HANDLER: Deleted '%s'", virtualPath );
    return "Successfully deleted " + virtualPath;
}


std::string MEMORY_HANDLER::ExecuteRename( const nlohmann::json& aInput )
{
    std::string oldVirtualPath = aInput.value( "old_path", "" );
    std::string newVirtualPath = aInput.value( "new_path", "" );
    std::string error;

    std::string oldRealPath = ResolvePath( oldVirtualPath, error );

    if( oldRealPath.empty() )
        return "Error: The path " + oldVirtualPath + " does not exist";

    std::string newRealPath = ResolvePath( newVirtualPath, error );

    if( newRealPath.empty() )
        return error;

    // Check source exists
    wxFileName oldFn( wxString::FromUTF8( oldRealPath ) );
    bool sourceIsDir = wxDir::Exists( wxString::FromUTF8( oldRealPath ) );

    if( !sourceIsDir && !oldFn.FileExists() )
        return "Error: The path " + oldVirtualPath + " does not exist";

    // Check destination doesn't exist
    wxFileName newFn( wxString::FromUTF8( newRealPath ) );

    if( newFn.FileExists() || wxDir::Exists( wxString::FromUTF8( newRealPath ) ) )
        return "Error: The destination " + newVirtualPath + " already exists";

    // Create parent directory for destination if needed
    wxFileName destDir( newFn.GetPath(), "" );

    if( !destDir.DirExists() )
    {
        if( !wxFileName::Mkdir( destDir.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
            return "Error: Could not create directory for " + newVirtualPath;
    }

    if( !wxRenameFile( wxString::FromUTF8( oldRealPath ),
                       wxString::FromUTF8( newRealPath ) ) )
    {
        return "Error: Could not rename " + oldVirtualPath + " to " + newVirtualPath;
    }

    wxLogInfo( "MEMORY_HANDLER: Renamed '%s' to '%s'", oldVirtualPath, newVirtualPath );
    return "Successfully renamed " + oldVirtualPath + " to " + newVirtualPath;
}


std::string MEMORY_HANDLER::Execute( const std::string& aToolName,
                                      const nlohmann::json& aInput )
{
    std::string command = aInput.value( "command", "" );

    wxLogInfo( "MEMORY_HANDLER: command=%s", command );

    // Ensure the memories directory exists for any operation
    std::string memoriesDir = GetMemoriesDir();

    if( memoriesDir.empty() )
        return "Error: No project is open. Cannot access memory files.";

    if( !wxDir::Exists( wxString::FromUTF8( memoriesDir ) ) )
    {
        wxFileName::Mkdir( wxString::FromUTF8( memoriesDir ), wxS_DIR_DEFAULT,
                           wxPATH_MKDIR_FULL );
    }

    if( command == "view" )
        return ExecuteView( aInput );
    else if( command == "create" )
        return ExecuteCreate( aInput );
    else if( command == "str_replace" )
        return ExecuteStrReplace( aInput );
    else if( command == "insert" )
        return ExecuteInsert( aInput );
    else if( command == "delete" )
        return ExecuteDelete( aInput );
    else if( command == "rename" )
        return ExecuteRename( aInput );

    return "Error: Unknown memory command '" + command + "'";
}


std::string MEMORY_HANDLER::GetDescription( const std::string& aToolName,
                                             const nlohmann::json& aInput ) const
{
    std::string command = aInput.value( "command", "" );
    std::string path = aInput.value( "path", "" );

    if( command == "view" )
        return "Reading memory: " + path;
    else if( command == "create" )
        return "Creating memory: " + path;
    else if( command == "str_replace" )
        return "Updating memory: " + path;
    else if( command == "insert" )
        return "Inserting into memory: " + path;
    else if( command == "delete" )
        return "Deleting memory: " + path;
    else if( command == "rename" )
        return "Renaming memory: " + aInput.value( "old_path", "" );

    return "Memory operation";
}
