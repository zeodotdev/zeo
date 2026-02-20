#ifndef SCREENSHOT_HANDLER_H
#define SCREENSHOT_HANDLER_H

#include "../tool_handler.h"

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

    std::vector<std::string> GetToolNames() const override { return { "screenshot" }; }
    std::string Execute( const std::string& aToolName,
                         const nlohmann::json& aInput ) override;
    std::string GetDescription( const std::string& aToolName,
                                const nlohmann::json& aInput ) const override;

private:

    /**
     * Execute the screenshot tool.
     * @param aInput JSON with "file_path" parameter
     * @return JSON string with image envelope or error string
     */
    std::string ExecuteScreenshot( const nlohmann::json& aInput );

    /**
     * Export SVG via IPC from the in-memory editor state.
     * Sends an export_screenshot command to the appropriate editor frame.
     * @param aIsSchematic true for schematic, false for PCB
     * @param aTempDir Temporary directory for output
     * @param aFilePath Path to the schematic file (used to identify sub-sheets)
     * @return Path to the exported SVG, or empty on failure
     */
    std::string ExportViaIpc( bool aIsSchematic, const std::string& aTempDir,
                              const std::string& aFilePath = std::string() );

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
     * Crop background from a PNG and resize to fit API limits.
     * Composites any alpha channel onto the given background color, scans for
     * content pixels that differ from the background, crops with padding, and
     * resizes so longest side <= 1568px.
     * @param aPngPath Path to the PNG (modified in-place)
     * @param aBgR Background red component
     * @param aBgG Background green component
     * @param aBgB Background blue component
     * @return true on success
     */
    bool CropAndResize( const std::string& aPngPath,
                        unsigned char aBgR, unsigned char aBgG, unsigned char aBgB );

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
