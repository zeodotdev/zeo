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

} // namespace FileWriter
