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

#include "terminal_command_validator.h"
#include <filesystem>
#include <algorithm>
#include <regex>
#include <set>
#include <cstdlib>

namespace fs = std::filesystem;


/**
 * Expand shell-style tilde (~) to the user's home directory.
 *
 * This handles the common case of ~/path (current user's home).
 * Note: Does not handle ~username/path syntax.
 *
 * @param aPath The path that may contain a leading tilde
 * @return The path with tilde expanded, or the original path if no expansion needed
 */
static std::string ExpandTilde( const std::string& aPath )
{
    if( aPath.empty() || aPath[0] != '~' )
        return aPath;

    // Handle ~/ (current user's home) or standalone ~
    if( aPath.length() == 1 || aPath[1] == '/' )
    {
        const char* home = std::getenv( "HOME" );
        if( home )
            return std::string( home ) + aPath.substr( 1 );
    }

    // For ~username syntax, we don't expand (would require getpwnam)
    // Return original path - it will fail validation as expected
    return aPath;
}

// Commands that modify files (the target file is typically the last argument)
static const std::set<std::string> FILE_MODIFY_COMMANDS = {
    "rm",      // Remove files
    "rmdir",   // Remove directories
    "mv",      // Move/rename files
    "cp",      // Copy files
    "touch",   // Create/modify files
    "mkdir",   // Create directories
    "chmod",   // Change permissions
    "chown",   // Change ownership
    "chgrp",   // Change group
    "ln",      // Create links
    "install", // Install files
    "dd",      // Disk dump (writes to of= target)
    "tee",     // Write to files
    "truncate",// Truncate files
};

// Commands that are always blocked (too dangerous even within project)
static const std::set<std::string> BLOCKED_COMMANDS = {
    "sudo",    // Privilege escalation
    "su",      // Switch user
    "doas",    // Alternative to sudo
};

// Safe system paths that are allowed as redirect targets
static const std::set<std::string> SAFE_REDIRECT_TARGETS = {
    "/dev/null",
    "/dev/stdout",
    "/dev/stderr"
};


bool TerminalCommandValidator::IsFileModifyingCommand( const std::string& aCommandName )
{
    // Extract just the command name (handle paths like /bin/rm)
    std::string cmdName = aCommandName;
    size_t lastSlash = cmdName.find_last_of( '/' );
    if( lastSlash != std::string::npos )
        cmdName = cmdName.substr( lastSlash + 1 );

    return FILE_MODIFY_COMMANDS.count( cmdName ) > 0;
}


bool TerminalCommandValidator::IsPathInProject( const std::string& aPath,
                                                const std::string& aProjectPath )
{
    if( aProjectPath.empty() )
        return true; // No restriction if no project path set

    try
    {
        // Expand tilde before processing - the filesystem API doesn't understand ~
        std::string expandedPath = ExpandTilde( aPath );
        fs::path fsPath( expandedPath );
        fs::path projectPath( aProjectPath );

        // Resolve relative paths against project directory
        if( fsPath.is_relative() )
            fsPath = projectPath / fsPath;

        // Get canonical paths to resolve .. and symlinks
        // Use weakly_canonical for paths that may not exist yet
        auto canonicalProject = fs::canonical( projectPath );
        auto canonicalFile = fs::weakly_canonical( fsPath );

        // Check if file path starts with project path
        auto projectStr = canonicalProject.string();
        auto fileStr = canonicalFile.string();

        // Ensure project path ends with separator for proper prefix matching
        if( !projectStr.empty() && projectStr.back() != '/' )
            projectStr += '/';

        // Check if file is within project directory (or is the project dir itself)
        return ( fileStr.find( projectStr ) == 0 || fileStr == canonicalProject.string() );
    }
    catch( const fs::filesystem_error& )
    {
        // If we can't resolve the path, be conservative and reject it
        return false;
    }
}


std::vector<std::string> TerminalCommandValidator::ParseArguments( const std::string& aCommand )
{
    std::vector<std::string> args;
    std::string current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escaped = false;

    for( size_t i = 0; i < aCommand.length(); i++ )
    {
        char c = aCommand[i];

        if( escaped )
        {
            current += c;
            escaped = false;
            continue;
        }

        if( c == '\\' && !inSingleQuote )
        {
            escaped = true;
            continue;
        }

        if( c == '\'' && !inDoubleQuote )
        {
            inSingleQuote = !inSingleQuote;
            continue;
        }

        if( c == '"' && !inSingleQuote )
        {
            inDoubleQuote = !inDoubleQuote;
            continue;
        }

        if( std::isspace( c ) && !inSingleQuote && !inDoubleQuote )
        {
            if( !current.empty() )
            {
                args.push_back( current );
                current.clear();
            }
            continue;
        }

        current += c;
    }

    if( !current.empty() )
        args.push_back( current );

    return args;
}


std::vector<std::string> TerminalCommandValidator::ExtractRedirectTargets( const std::string& aCommand )
{
    std::vector<std::string> targets;

    // Look for redirect operators: >, >>, 1>, 2>, &>
    // This is a simplified parser - it won't handle all edge cases
    std::regex redirectRegex( R"((?:^|[^>])(>{1,2}|[12]>|&>)\s*([^\s;|&]+))" );
    std::sregex_iterator iter( aCommand.begin(), aCommand.end(), redirectRegex );
    std::sregex_iterator end;

    while( iter != end )
    {
        std::smatch match = *iter;
        if( match.size() > 2 )
        {
            std::string target = match[2].str();
            // Remove quotes if present
            if( target.length() >= 2 )
            {
                if( ( target.front() == '"' && target.back() == '"' ) ||
                    ( target.front() == '\'' && target.back() == '\'' ) )
                {
                    target = target.substr( 1, target.length() - 2 );
                }
            }
            targets.push_back( target );
        }
        ++iter;
    }

    return targets;
}


std::vector<std::string> TerminalCommandValidator::SplitByPipes( const std::string& aCommand )
{
    std::vector<std::string> segments;
    std::string current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escaped = false;

    for( size_t i = 0; i < aCommand.length(); i++ )
    {
        char c = aCommand[i];

        if( escaped )
        {
            current += c;
            escaped = false;
            continue;
        }

        if( c == '\\' )
        {
            escaped = true;
            current += c;
            continue;
        }

        if( c == '\'' && !inDoubleQuote )
        {
            inSingleQuote = !inSingleQuote;
            current += c;
            continue;
        }

        if( c == '"' && !inSingleQuote )
        {
            inDoubleQuote = !inDoubleQuote;
            current += c;
            continue;
        }

        // Check for pipe (only if not in quotes)
        if( c == '|' && !inSingleQuote && !inDoubleQuote )
        {
            if( !current.empty() )
            {
                segments.push_back( current );
                current.clear();
            }
            continue;
        }

        current += c;
    }

    if( !current.empty() )
        segments.push_back( current );

    return segments;
}


std::vector<std::string> TerminalCommandValidator::ParseSimpleCommand( const std::string& aCommand )
{
    std::vector<std::string> paths;
    std::vector<std::string> args = ParseArguments( aCommand );

    if( args.empty() )
        return paths;

    std::string cmdName = args[0];

    // Extract just the command name (handle paths like /bin/rm)
    size_t lastSlash = cmdName.find_last_of( '/' );
    if( lastSlash != std::string::npos )
        cmdName = cmdName.substr( lastSlash + 1 );

    // Handle specific commands
    if( cmdName == "rm" || cmdName == "rmdir" || cmdName == "touch" ||
        cmdName == "mkdir" || cmdName == "chmod" || cmdName == "chown" ||
        cmdName == "chgrp" || cmdName == "truncate" )
    {
        // These commands: all non-flag arguments are targets
        for( size_t i = 1; i < args.size(); i++ )
        {
            if( args[i][0] != '-' ) // Skip flags
                paths.push_back( args[i] );
        }
    }
    else if( cmdName == "mv" || cmdName == "cp" || cmdName == "ln" || cmdName == "install" )
    {
        // These commands: the last argument is the destination (or all targets for mv -t)
        // For simplicity, we check all non-flag arguments as potential targets
        for( size_t i = 1; i < args.size(); i++ )
        {
            if( args[i][0] != '-' )
                paths.push_back( args[i] );
        }
    }
    else if( cmdName == "tee" )
    {
        // tee writes to specified files
        for( size_t i = 1; i < args.size(); i++ )
        {
            if( args[i][0] != '-' )
                paths.push_back( args[i] );
        }
    }
    else if( cmdName == "dd" )
    {
        // dd has of=<file> for output
        for( size_t i = 1; i < args.size(); i++ )
        {
            if( args[i].substr( 0, 3 ) == "of=" )
            {
                paths.push_back( args[i].substr( 3 ) );
            }
        }
    }

    return paths;
}


std::vector<std::string> TerminalCommandValidator::ExtractTargetPaths( const std::string& aCommand )
{
    std::vector<std::string> allPaths;

    // Split by pipes and analyze each segment
    std::vector<std::string> segments = SplitByPipes( aCommand );

    for( const auto& segment : segments )
    {
        // Extract redirect targets from each segment
        std::vector<std::string> redirectTargets = ExtractRedirectTargets( segment );
        allPaths.insert( allPaths.end(), redirectTargets.begin(), redirectTargets.end() );

        // Parse the command to extract file operation targets
        std::vector<std::string> cmdPaths = ParseSimpleCommand( segment );
        allPaths.insert( allPaths.end(), cmdPaths.begin(), cmdPaths.end() );
    }

    return allPaths;
}


TerminalValidationResult TerminalCommandValidator::ValidateCommand( const std::string& aCommand,
                                                                     const std::string& aProjectPath )
{
    if( aProjectPath.empty() )
    {
        // No project path set - block all potentially dangerous commands
        return TerminalValidationResult(
            "Error: No project directory set. Terminal commands are disabled for safety." );
    }

    // Parse the command to check for blocked commands
    std::vector<std::string> args = ParseArguments( aCommand );
    if( !args.empty() )
    {
        std::string cmdName = args[0];
        size_t lastSlash = cmdName.find_last_of( '/' );
        if( lastSlash != std::string::npos )
            cmdName = cmdName.substr( lastSlash + 1 );

        if( BLOCKED_COMMANDS.count( cmdName ) > 0 )
        {
            return TerminalValidationResult(
                "Error: Command '" + cmdName + "' is not allowed for security reasons." );
        }
    }

    // Check for command chaining with && or ; that might include blocked commands
    // Split by && and ; (respecting quotes) and check each command
    std::string current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool escaped = false;
    std::vector<std::string> chainedCommands;

    for( size_t i = 0; i < aCommand.length(); i++ )
    {
        char c = aCommand[i];

        if( escaped )
        {
            current += c;
            escaped = false;
            continue;
        }

        if( c == '\\' )
        {
            escaped = true;
            current += c;
            continue;
        }

        if( c == '\'' && !inDoubleQuote )
        {
            inSingleQuote = !inSingleQuote;
            current += c;
            continue;
        }

        if( c == '"' && !inSingleQuote )
        {
            inDoubleQuote = !inDoubleQuote;
            current += c;
            continue;
        }

        if( !inSingleQuote && !inDoubleQuote )
        {
            // Check for && or ;
            if( c == ';' || ( c == '&' && i + 1 < aCommand.length() && aCommand[i + 1] == '&' ) )
            {
                if( !current.empty() )
                {
                    chainedCommands.push_back( current );
                    current.clear();
                }
                if( c == '&' )
                    i++; // Skip the second &
                continue;
            }
        }

        current += c;
    }

    if( !current.empty() )
        chainedCommands.push_back( current );

    // Validate each chained command
    for( const auto& cmd : chainedCommands )
    {
        std::vector<std::string> cmdArgs = ParseArguments( cmd );
        if( !cmdArgs.empty() )
        {
            std::string cmdName = cmdArgs[0];
            size_t lastSlash = cmdName.find_last_of( '/' );
            if( lastSlash != std::string::npos )
                cmdName = cmdName.substr( lastSlash + 1 );

            if( BLOCKED_COMMANDS.count( cmdName ) > 0 )
            {
                return TerminalValidationResult(
                    "Error: Command '" + cmdName + "' is not allowed for security reasons." );
            }
        }
    }

    // Extract all target paths from the command
    std::vector<std::string> targetPaths = ExtractTargetPaths( aCommand );

    // Validate each target path
    for( const auto& path : targetPaths )
    {
        // Skip validation for safe system redirect targets
        if( SAFE_REDIRECT_TARGETS.count( path ) > 0 )
            continue;

        if( !IsPathInProject( path, aProjectPath ) )
        {
            return TerminalValidationResult(
                "Error: Cannot modify files outside the project directory. "
                "Blocked path: " + path + "\n"
                "Project directory: " + aProjectPath );
        }
    }

    return TerminalValidationResult::Success();
}
