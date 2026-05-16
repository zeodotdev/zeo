/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

// Console-mode launcher for the Zeo MCP server on Windows.
//
// Embeds the same python311.dll that Zeo.exe links against, points PYTHONHOME
// at bin/ (where Lib/ already lives for the GUI's scripting console), and
// hands off to kipy.mcp.server.run.
//
// Registered with Claude Code via:
//   claude mcp add zeo -s user -- "C:\Program Files\Zeo\bin\zeo-mcp.exe"

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <windows.h>
#include <filesystem>

namespace fs = std::filesystem;

int wmain()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW( nullptr, exePath, MAX_PATH );
    fs::path binDir = fs::path( exePath ).parent_path();

    PyConfig config;
    PyConfig_InitIsolatedConfig( &config );
    config.use_environment = 0;
    config.parse_argv = 0;

    PyConfig_SetString( &config, &config.home, binDir.wstring().c_str() );

    config.module_search_paths_set = 1;
    auto lib  = ( binDir / L"Lib" ).wstring();
    auto site = ( binDir / L"Lib" / L"site-packages" ).wstring();
    auto dlls = ( binDir / L"DLLs" ).wstring();  // C extension modules (_socket.pyd, _asyncio.pyd, …)
    PyWideStringList_Append( &config.module_search_paths, lib.c_str() );
    PyWideStringList_Append( &config.module_search_paths, dlls.c_str() );
    PyWideStringList_Append( &config.module_search_paths, site.c_str() );

    PyStatus s = Py_InitializeFromConfig( &config );
    PyConfig_Clear( &config );
    if( PyStatus_Exception( s ) )
    {
        Py_ExitStatusException( s );
        return 1;
    }

    int rc = PyRun_SimpleString(
        "import asyncio\n"
        "from kipy.mcp.server import run\n"
        "asyncio.run(run())\n"
    );

    Py_Finalize();
    return rc;
}
