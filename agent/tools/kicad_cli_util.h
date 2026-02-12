#ifndef KICAD_CLI_UTIL_H
#define KICAD_CLI_UTIL_H

#include <string>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

namespace KiCadCliUtil
{

/**
 * Locate the kicad-cli binary next to the running executable.
 * On macOS this is in the same MacOS directory inside the app bundle.
 *
 * @return Full path to kicad-cli, or empty string if not found.
 */
inline std::string GetKicadCliPath()
{
    wxString exePathStr = wxStandardPaths::Get().GetExecutablePath();
    wxFileName exePath( exePathStr );
    wxFileName cliPath( exePath.GetPath(), "kicad-cli" );

    wxLogInfo( "KiCadCLI: Executable path: %s", exePathStr );
    wxLogInfo( "KiCadCLI: Looking for kicad-cli at: %s", cliPath.GetFullPath() );

    if( cliPath.FileExists() )
        return cliPath.GetFullPath().ToStdString();

    wxLogWarning( "KiCadCLI: kicad-cli not found at %s", cliPath.GetFullPath() );
    return std::string();
}


/**
 * Get the Frameworks directory path for setting DYLD_LIBRARY_PATH on macOS.
 * kicad-cli needs this to find dylibs in the app bundle.
 *
 * @return The Frameworks directory path string.
 */
inline std::string GetFrameworksDir()
{
    wxFileName exePath( wxStandardPaths::Get().GetExecutablePath() );
    wxFileName frameworksDir( exePath.GetPath(), "" );
    frameworksDir.RemoveLastDir();
    frameworksDir.AppendDir( "Frameworks" );
    return frameworksDir.GetPath().ToStdString();
}


/**
 * Build a command prefix for running kicad-cli with proper environment.
 * Includes DYLD_LIBRARY_PATH on macOS and quotes the binary path.
 *
 * NOTE: Currently macOS-only. The DYLD_LIBRARY_PATH environment variable
 * and Frameworks directory layout are specific to macOS app bundles.
 * Other platforms would need LD_LIBRARY_PATH (Linux) or PATH (Windows).
 *
 * @return Command prefix string like: DYLD_LIBRARY_PATH="..." "/path/to/kicad-cli"
 *         or empty string if kicad-cli is not found.
 */
inline std::string GetKicadCliCommandPrefix()
{
    std::string cliPath = GetKicadCliPath();
    if( cliPath.empty() )
        return std::string();

    std::string prefix = "DYLD_LIBRARY_PATH=\"" + GetFrameworksDir()
                         + "\" \"" + cliPath + "\"";
    return prefix;
}

/**
 * Locate the Freerouting JAR file in the app bundle.
 * On macOS this is in Contents/SharedSupport/freerouting/freerouting.jar
 *
 * @return Full path to freerouting.jar, or empty string if not found.
 */
inline std::string GetFreeroutingJarPath()
{
    wxString exePathStr = wxStandardPaths::Get().GetExecutablePath();
    wxFileName exePath( exePathStr );

    // Navigate from MacOS to SharedSupport/freerouting
    // App structure: Zener.app/Contents/MacOS/kicad -> Zener.app/Contents/SharedSupport/freerouting
    wxFileName jarPath( exePath.GetPath(), "" );
    jarPath.RemoveLastDir();  // Remove MacOS
    jarPath.AppendDir( "SharedSupport" );
    jarPath.AppendDir( "freerouting" );
    jarPath.SetFullName( "freerouting.jar" );

    wxLogInfo( "Freerouting: Looking for JAR at: %s", jarPath.GetFullPath() );

    if( jarPath.FileExists() )
        return jarPath.GetFullPath().ToStdString();

    wxLogWarning( "Freerouting: JAR not found at %s", jarPath.GetFullPath() );
    return std::string();
}


/**
 * Get the path to the Java executable.
 * Uses system Java on macOS.
 *
 * @return Path to java executable, or "java" if not found explicitly.
 */
inline std::string GetJavaPath()
{
    // On macOS, /usr/bin/java is a stub that invokes the real Java
    // It will show the "install Java" dialog if Java isn't installed
    if( wxFileName::FileExists( "/usr/bin/java" ) )
        return "/usr/bin/java";

    // Fall back to PATH lookup
    return "java";
}

} // namespace KiCadCliUtil

#endif // KICAD_CLI_UTIL_H
