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

#ifndef TERMINAL_COMMAND_VALIDATOR_H
#define TERMINAL_COMMAND_VALIDATOR_H

#include <string>
#include <vector>

/**
 * Result of terminal command validation
 */
struct TerminalValidationResult
{
    bool        valid;
    std::string error;

    TerminalValidationResult() : valid( true ) {}
    explicit TerminalValidationResult( const std::string& aError ) : valid( false ), error( aError ) {}

    static TerminalValidationResult Success() { return TerminalValidationResult(); }
};

/**
 * Validates terminal commands to ensure they don't modify files outside the project directory.
 *
 * This provides a security boundary for AI agent terminal access, preventing the agent
 * from making changes to system files or files outside the user's project.
 */
class TerminalCommandValidator
{
public:
    /**
     * Validate a bash command against the project path restriction.
     *
     * @param aCommand The bash command to validate
     * @param aProjectPath The project directory path (must be canonical/absolute)
     * @return TerminalValidationResult with valid=true if command is safe, or error message if blocked
     */
    static TerminalValidationResult ValidateCommand( const std::string& aCommand,
                                                     const std::string& aProjectPath );

    /**
     * Validate a bash command against multiple allowed directory paths.
     *
     * @param aCommand The bash command to validate
     * @param aAllowedPaths List of allowed directory paths (must be canonical/absolute)
     * @return TerminalValidationResult with valid=true if command is safe, or error message if blocked
     */
    static TerminalValidationResult ValidateCommand( const std::string& aCommand,
                                                     const std::vector<std::string>& aAllowedPaths );

private:
    /**
     * Check if a path is within any of the allowed directories.
     */
    static bool IsPathInAllowedDirs( const std::string& aPath,
                                     const std::vector<std::string>& aAllowedPaths );
    /**
     * Check if a path is within the project directory.
     * Handles relative paths by resolving against project path.
     * Uses canonical path resolution to prevent traversal attacks (e.g., ../).
     */
    static bool IsPathInProject( const std::string& aPath, const std::string& aProjectPath );

    /**
     * Extract paths from a command that could be targets of file modification.
     * Returns paths for commands like: rm, mv, cp, touch, mkdir, rmdir, etc.
     * Also extracts redirect targets (>, >>).
     */
    static std::vector<std::string> ExtractTargetPaths( const std::string& aCommand );

    /**
     * Parse a simple command (no pipes) and extract target paths.
     */
    static std::vector<std::string> ParseSimpleCommand( const std::string& aCommand );

    /**
     * Extract redirect targets (>, >>) from a command.
     */
    static std::vector<std::string> ExtractRedirectTargets( const std::string& aCommand );

    /**
     * Split command by pipes to analyze each segment.
     */
    static std::vector<std::string> SplitByPipes( const std::string& aCommand );

    /**
     * Check if a command is a known file-modifying command.
     */
    static bool IsFileModifyingCommand( const std::string& aCommandName );

    /**
     * Check if a command is a known filesystem browsing command
     * (recursive directory traversal that can trigger macOS permission dialogs).
     */
    static bool IsFilesystemBrowseCommand( const std::string& aCommandName );

    /**
     * Parse command arguments, respecting quotes.
     */
    static std::vector<std::string> ParseArguments( const std::string& aCommand );
};

#endif // TERMINAL_COMMAND_VALIDATOR_H
