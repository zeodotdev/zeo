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

} // namespace KiCadCliUtil

#endif // KICAD_CLI_UTIL_H
