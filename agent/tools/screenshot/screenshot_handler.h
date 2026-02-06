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

#ifndef SCREENSHOT_HANDLER_H
#define SCREENSHOT_HANDLER_H

#include "tool_handler.h"

#include <functional>
#include <vector>

/**
 * Tool handler for capturing screenshots of schematics and PCBs.
 *
 * Exports a clean render using kicad-cli (SVG export), converts to PNG via
 * sips (macOS-only), and returns the image as base64-encoded data for the
 * LLM to inspect visually.
 *
 * NOTE: Currently macOS-only due to sips dependency for SVG-to-PNG conversion.
 */
class SCREENSHOT_HANDLER : public TOOL_HANDLER
{
public:
    SCREENSHOT_HANDLER() = default;
    ~SCREENSHOT_HANDLER() override = default;

    bool CanHandle( const std::string& aToolName ) const override;
    std::string Execute( const std::string& aToolName,
                         const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;
    void SetProjectPath( const std::string& aPath ) override { m_projectPath = aPath; }

private:
    std::string m_projectPath;

    /**
     * Execute the screenshot tool.
     * @param aInput JSON with "file_path" parameter
     * @return JSON string with __has_image envelope or error string
     */
    std::string ExecuteScreenshot( const nlohmann::json& aInput );

    /**
     * Export a schematic to SVG using kicad-cli.
     * @param aFilePath Path to the .kicad_sch file
     * @param aTempDir Temporary directory for output
     * @return Path to the exported SVG, or empty on failure
     */
    std::string ExportSchematicSvg( const std::string& aFilePath,
                                    const std::string& aTempDir );

    /**
     * Export a PCB to SVG using kicad-cli.
     * @param aFilePath Path to the .kicad_pcb file
     * @param aTempDir Temporary directory for output
     * @return Path to the exported SVG, or empty on failure
     */
    std::string ExportPcbSvg( const std::string& aFilePath,
                              const std::string& aTempDir );

    /**
     * Convert an SVG file to PNG using sips (macOS built-in).
     * NOTE: macOS-only. Will fail on other platforms.
     * @param aSvgPath Input SVG path
     * @param aPngPath Output PNG path
     * @return true on success
     */
    bool ConvertSvgToPng( const std::string& aSvgPath,
                          const std::string& aPngPath );

    /**
     * Crop whitespace from a PNG and resize to fit API limits.
     * Composites any alpha channel onto white, scans for non-white content,
     * crops with padding, and resizes so longest side <= 1568px.
     * @param aPngPath Path to the PNG (modified in-place)
     * @return true on success
     */
    bool CropAndResize( const std::string& aPngPath );

    /**
     * Read a file and return its contents as a base64-encoded string.
     * @param aFilePath Path to the file to encode
     * @return Base64 string, or empty on failure
     */
    std::string ReadFileAsBase64( const std::string& aFilePath );

    /**
     * Run a shell command and capture stdout + stderr separately.
     * Uses fork/exec with pipe-based capture and a timeout.
     * @param aCommand The command to run
     * @param aStdout Output: captured stdout
     * @param aStderr Output: captured stderr
     * @param aTimeoutSec Maximum seconds to wait before killing the child (default 30)
     * @return The process exit code (-1 on failure to launch or timeout)
     */
    int RunCommand( const std::string& aCommand, std::string& aStdout,
                    std::string& aStderr, int aTimeoutSec = 30 );

    /**
     * Validate that a path is safe for use in shell commands.
     * Rejects paths containing shell metacharacters that could enable injection.
     * @param aPath The file path to validate
     * @return true if the path is safe
     */
    static bool IsPathSafeForShell( const std::string& aPath );
};

#endif // SCREENSHOT_HANDLER_H
