/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include "file_writer.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>
#include <filesystem>
#include <regex>
#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include <unistd.h>
#endif

namespace FileWriter
{

bool ReadFile( const std::string& aFilePath, std::string& aContent )
{
    std::ifstream file( aFilePath );
    if( !file.is_open() )
        return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    aContent = buffer.str();
    return true;
}


WriteResult WriteFileSafe( const std::string& aFilePath, const std::string& aContent,
                           bool aCreateBackup )
{
    std::string backupPath;

    // Create backup if requested and file exists
    if( aCreateBackup && FileExists( aFilePath ) )
    {
        backupPath = aFilePath + ".bak";

        // Read original content
        std::string originalContent;
        if( ReadFile( aFilePath, originalContent ) )
        {
            // Write backup
            std::ofstream backupFile( backupPath );
            if( backupFile.is_open() )
            {
                backupFile << originalContent;
                backupFile.close();
            }
            else
            {
                return WriteResult( "Failed to create backup file: " + backupPath );
            }
        }
    }

    // Write to temporary file first
    std::string tempPath = aFilePath + ".tmp";
    {
        std::ofstream tempFile( tempPath );
        if( !tempFile.is_open() )
            return WriteResult( "Failed to create temporary file: " + tempPath );

        tempFile << aContent;
        tempFile.close();

        if( tempFile.fail() )
        {
            std::remove( tempPath.c_str() );
            return WriteResult( "Failed to write content to temporary file" );
        }
    }

    // Rename temp file to target (atomic on most filesystems)
    if( std::rename( tempPath.c_str(), aFilePath.c_str() ) != 0 )
    {
        std::remove( tempPath.c_str() );
        return WriteResult( "Failed to rename temporary file to target" );
    }

    return WriteResult::Success( backupPath );
}


bool FileExists( const std::string& aFilePath )
{
    struct stat buffer;
    return ( stat( aFilePath.c_str(), &buffer ) == 0 );
}


std::string GetDirectory( const std::string& aFilePath )
{
    size_t pos = aFilePath.find_last_of( "/\\" );
    if( pos == std::string::npos )
        return ".";
    return aFilePath.substr( 0, pos );
}


std::string GetFilename( const std::string& aFilePath )
{
    size_t pos = aFilePath.find_last_of( "/\\" );
    if( pos == std::string::npos )
        return aFilePath;
    return aFilePath.substr( pos + 1 );
}


std::string GetExtension( const std::string& aFilePath )
{
    std::string filename = GetFilename( aFilePath );
    size_t pos = filename.find_last_of( '.' );
    if( pos == std::string::npos )
        return "";
    return filename.substr( pos );
}


PathValidationResult ValidatePathInProject( const std::string& aFilePath,
                                            const std::string& aProjectPath )
{
    if( aProjectPath.empty() )
    {
        // No project path set - allow any path
        return PathValidationResult::Success( aFilePath );
    }

    try
    {
        std::filesystem::path fsPath( aFilePath );
        std::filesystem::path projectPath( aProjectPath );

        // Resolve relative paths against project directory
        if( fsPath.is_relative() )
            fsPath = projectPath / fsPath;

        // Get canonical paths to resolve .. and symlinks
        // Use weakly_canonical for the file path since it may not exist yet
        auto canonicalProject = std::filesystem::canonical( projectPath );
        auto canonicalFile = std::filesystem::weakly_canonical( fsPath );

        // Check if file path starts with project path
        auto projectStr = canonicalProject.string();
        auto fileStr = canonicalFile.string();

        // Ensure project path ends with separator for proper prefix matching
        if( !projectStr.empty() && projectStr.back() != '/' )
            projectStr += '/';

        // Check if file is within project directory (or is the project dir itself)
        if( fileStr.find( projectStr ) != 0 && fileStr != canonicalProject.string() )
        {
            return PathValidationResult( "File path must be within project directory: " +
                                         aProjectPath + " (resolved path: " + fileStr + ")" );
        }

        return PathValidationResult::Success( canonicalFile.string() );
    }
    catch( const std::filesystem::filesystem_error& e )
    {
        return PathValidationResult( "Invalid file path: " + std::string( e.what() ) );
    }
}


std::string ExtractSchematicRootUuid( const std::string& aContent )
{
    // Look for the first (uuid "...") or (uuid ...) after (kicad_sch
    // The root UUID is typically the first UUID in the file, right after the header
    std::regex uuidRegex( R"(\(uuid\s+\"?([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\"?\))" );
    std::smatch match;

    if( std::regex_search( aContent, match, uuidRegex ) && match.size() > 1 )
    {
        return match[1].str();
    }

    return "";
}


WriteResult AddSchematicToProject( const std::string& aProjectPath,
                                   const std::string& aSchematicUuid,
                                   const std::string& aSheetName )
{
    // Find the .kicad_pro file in the project directory
    std::string projectFile;
    for( const auto& entry : std::filesystem::directory_iterator( aProjectPath ) )
    {
        if( entry.path().extension() == ".kicad_pro" )
        {
            projectFile = entry.path().string();
            break;
        }
    }

    if( projectFile.empty() )
        return WriteResult( "No .kicad_pro file found in project directory" );

    // Read the project file
    std::string content;
    if( !ReadFile( projectFile, content ) )
        return WriteResult( "Failed to read project file: " + projectFile );

    // Parse as JSON
    nlohmann::json projectJson;
    try
    {
        projectJson = nlohmann::json::parse( content );
    }
    catch( const nlohmann::json::parse_error& e )
    {
        return WriteResult( "Failed to parse project file as JSON: " + std::string( e.what() ) );
    }

    // Get or create the sheets array
    if( !projectJson.contains( "sheets" ) )
    {
        projectJson["sheets"] = nlohmann::json::array();
    }

    // Check if the sheet already exists (by UUID)
    auto& sheets = projectJson["sheets"];
    for( const auto& sheet : sheets )
    {
        if( sheet.is_array() && sheet.size() >= 1 && sheet[0] == aSchematicUuid )
        {
            // Sheet already exists, no need to add
            return WriteResult::Success();
        }
    }

    // Add the new sheet
    sheets.push_back( nlohmann::json::array( { aSchematicUuid, aSheetName } ) );

    // Write back the project file
    std::string newContent = projectJson.dump( 2 );
    return WriteFileSafe( projectFile, newContent, true );
}

} // namespace FileWriter
