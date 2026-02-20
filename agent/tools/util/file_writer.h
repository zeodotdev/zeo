#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include <string>

namespace FileWriter
{

/**
 * Result of a write operation.
 */
struct WriteResult
{
    bool        success;
    std::string error;
    std::string backupPath;  // Path to backup file if created

    WriteResult() : success( true ) {}
    WriteResult( const std::string& aError ) : success( false ), error( aError ) {}

    static WriteResult Success( const std::string& aBackupPath = "" )
    {
        WriteResult r;
        r.backupPath = aBackupPath;
        return r;
    }
};

/**
 * Read the entire contents of a file.
 * @param aFilePath Path to the file to read.
 * @param aContent Output string to receive the file contents.
 * @return true on success, false on error.
 */
bool ReadFile( const std::string& aFilePath, std::string& aContent );

/**
 * Write content to a file safely using atomic write pattern.
 * This writes to a temporary file first, then renames to ensure atomicity.
 * @param aFilePath Path to the file to write.
 * @param aContent The content to write.
 * @param aCreateBackup If true and file exists, create a .bak backup first.
 * @return WriteResult with success status and backup path if created.
 */
WriteResult WriteFileSafe( const std::string& aFilePath, const std::string& aContent,
                           bool aCreateBackup = true );

/**
 * Check if a file exists.
 * @param aFilePath Path to check.
 * @return true if file exists.
 */
bool FileExists( const std::string& aFilePath );

/**
 * Get the directory portion of a file path.
 * @param aFilePath The full file path.
 * @return The directory path (without trailing separator).
 */
std::string GetDirectory( const std::string& aFilePath );

/**
 * Get the filename portion of a path (with extension).
 * @param aFilePath The full file path.
 * @return The filename.
 */
std::string GetFilename( const std::string& aFilePath );

/**
 * Get the file extension.
 * @param aFilePath The file path.
 * @return The extension including the dot (e.g., ".kicad_sch").
 */
std::string GetExtension( const std::string& aFilePath );

/**
 * Result of path validation.
 */
struct PathValidationResult
{
    bool        valid;
    std::string error;
    std::string resolvedPath;  // Canonical absolute path

    PathValidationResult() : valid( true ) {}
    PathValidationResult( const std::string& aError ) : valid( false ), error( aError ) {}

    static PathValidationResult Success( const std::string& aResolvedPath )
    {
        PathValidationResult r;
        r.resolvedPath = aResolvedPath;
        return r;
    }
};

/**
 * Validate that a file path is within a project directory.
 * Resolves relative paths against the project directory.
 * Normalizes paths to handle .. and symlinks.
 *
 * @param aFilePath The file path to validate (absolute or relative).
 * @param aProjectPath The project directory path.
 * @return PathValidationResult with resolved path or error message.
 */
PathValidationResult ValidatePathInProject( const std::string& aFilePath,
                                            const std::string& aProjectPath );

/**
 * Extract the root UUID from schematic content.
 * This is the first (uuid "...") after (kicad_sch in the file.
 *
 * @param aContent The schematic file content.
 * @return The root UUID, or empty string if not found.
 */
std::string ExtractSchematicRootUuid( const std::string& aContent );

} // namespace FileWriter

#endif // FILE_WRITER_H
