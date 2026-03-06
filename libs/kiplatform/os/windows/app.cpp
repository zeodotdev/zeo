/*
* This program source code file is part of KiCad, a free EDA CAD application.
*
* Copyright (C) 2020 Mark Roszko <mark.roszko@gmail.com>
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

#include <kiplatform/app.h>

#include <wx/app.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/tokenzr.h>
#include <wx/window.h>
#if wxCHECK_VERSION( 3, 3, 0 )
#include <wx/msw/darkmode.h>
#endif

#include <windows.h>
#include <strsafe.h>
#include <config.h>
#include <versionhelpers.h>
#include <iostream>
#include <cstdio>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#if defined( _MSC_VER )
#include <werapi.h>     // issues on msys2
#endif

#ifdef _WIN32
extern "C"
{
    // So there exists this malware called Nahimic by A-Volute, which is marketed as an audio enhancement
    // software. In reality it's an aggressive form of malware that injects itself wildly into every process
    // on the system for god knows what reason. It even includes a tracking/analytics package, <insert tinfoil hat>
    // Our problem is this garbage basically bugs out OpenGL (why an audio driver does that, who knows, its made by morons)
    // And then we get issues reported both in our issue tracker and sentry reports as a result
    // At least these malware authors were nice to include a dumb "disable" trick where it checks if the exe is exporting
    // a symbol called NoHotPatch, so here we are.
    // Hopefully this works and stops the bug reports. Apparently the worst part is this malware aggressively gets reinstalled
    // by awful low-tier motherboard vendors like MSI, Alienware and others who bundled it into their driver packages
    // and distributed it over Windows Update
    // Did I mention they clearly had issues with other apps so instead of fixing their malware, they blacklisted a hundred common
    // apps and even some games in their own config? Obviously kicad isn't on that blacklist :(
    // This malware seems to no longer be distributed as Nahimic and replaced with "Sonar" by SteelSeries.
    // Time will tell if it's the same garbage, I'm not volunteering to install it.
    __declspec(dllexport) void NoHotPatch()
    {
        // this is a intentionally empty function
        return;
    }
}
#endif

#if wxCHECK_VERSION( 3, 3, 0 )
class KICAD_DARK_MODE_SETTINGS : public wxDarkModeSettings
{
public:
    wxColour GetColour( wxSystemColour index ) override
    {
        switch( index )
        {
            // This fixes "Control Light"
        case wxSYS_COLOUR_3DLIGHT:
            return wxColour( 0x2B2B2B );

        default: return wxDarkModeSettings::GetColour( index );
        }
    }
};
#endif


static LONG WINAPI ZeoCrashHandler( EXCEPTION_POINTERS* pExInfo )
{
    // Write crash info to a file before dying
    FILE* f = _wfopen( L"C:\\Users\\jared\\AppData\\Roaming\\kicad\\logs\\crash_info.log", L"w" );
    if( f )
    {
        EXCEPTION_RECORD* rec = pExInfo->ExceptionRecord;
        CONTEXT* ctx = pExInfo->ContextRecord;
        fprintf( f, "Exception code: 0x%08lX\n", rec->ExceptionCode );
        fprintf( f, "Exception address: 0x%p\n", rec->ExceptionAddress );
        fprintf( f, "Access address: 0x%p\n",
                 rec->NumberParameters >= 2 ? (void*)rec->ExceptionInformation[1] : nullptr );
        fprintf( f, "Access type: %s\n",
                 rec->NumberParameters >= 1 ?
                     ( rec->ExceptionInformation[0] == 0 ? "READ" :
                       rec->ExceptionInformation[0] == 1 ? "WRITE" : "EXECUTE" ) : "?" );
#ifdef _M_X64
        fprintf( f, "RIP: 0x%p\n", (void*)ctx->Rip );
        fprintf( f, "RSP: 0x%p\n", (void*)ctx->Rsp );
        fprintf( f, "RBP: 0x%p\n", (void*)ctx->Rbp );
        fprintf( f, "RAX: 0x%p\n", (void*)ctx->Rax );
        fprintf( f, "RBX: 0x%p\n", (void*)ctx->Rbx );
        fprintf( f, "RCX: 0x%p\n", (void*)ctx->Rcx );
        fprintf( f, "RDX: 0x%p\n", (void*)ctx->Rdx );
        fprintf( f, "R8:  0x%p\n", (void*)ctx->R8 );
        fprintf( f, "R9:  0x%p\n", (void*)ctx->R9 );

        // Walk the stack
        fprintf( f, "\nStack trace:\n" );
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        SymInitialize( process, NULL, TRUE );

        STACKFRAME64 frame = {};
        frame.AddrPC.Offset = ctx->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;

        for( int i = 0; i < 30; i++ )
        {
            if( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, process, thread,
                              &frame, ctx, NULL,
                              SymFunctionTableAccess64, SymGetModuleBase64, NULL ) )
                break;

            DWORD64 addr = frame.AddrPC.Offset;
            char symBuf[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;

            DWORD64 displacement = 0;
            HMODULE hMod = NULL;
            char modName[MAX_PATH] = "???";
            if( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                    (LPCSTR)addr, &hMod ) )
                GetModuleFileNameA( hMod, modName, MAX_PATH );

            // Get just the filename
            char* lastSlash = strrchr( modName, '\\' );
            char* modShort = lastSlash ? lastSlash + 1 : modName;

            if( SymFromAddr( process, addr, &displacement, sym ) )
                fprintf( f, "  [%d] %s!%s+0x%llx (0x%p)\n", i, modShort, sym->Name, displacement, (void*)addr );
            else
                fprintf( f, "  [%d] %s+0x%llx (0x%p)\n", i, modShort, addr - (DWORD64)hMod, (void*)addr );
        }

        SymCleanup( process );
#endif
        fflush( f );
        fclose( f );
    }

    // Also write a minidump
    HANDLE dumpFile = CreateFileW( L"C:\\Users\\jared\\AppData\\Roaming\\kicad\\logs\\crash.dmp",
                                   GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if( dumpFile != INVALID_HANDLE_VALUE )
    {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pExInfo;
        mdei.ClientPointers = FALSE;
        MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(),
                           dumpFile, MiniDumpNormal, &mdei, NULL, NULL );
        CloseHandle( dumpFile );
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool KIPLATFORM::APP::Init()
{
    // Install crash handler to capture stack traces
    SetUnhandledExceptionFilter( ZeoCrashHandler );

#if defined( _MSC_VER ) && defined( DEBUG )
    // wxWidgets turns on leak dumping in debug but its "flawed" and will falsely dump
    // for half a hour _CRTDBG_ALLOC_MEM_DF is the usual default for MSVC.
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF );
#endif

#if defined( DEBUG )
    // undo wxwidgets trying to hide errors
    SetErrorMode( 0 );
#else
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX );
#endif

    // remove CWD from the dll search paths
    // just the smallest of security tweaks as we do load DLLs on demand
    SetDllDirectory( wxT( "" ) );

    // Moves the CWD to the end of the search list for spawning processes
    SetSearchPathMode( BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT );

    // In order to support GUI and CLI
    // Let's attach to console when it's possible, or allocate if requested.
    AttachConsole( wxGetEnv( wxS( "KICAD_ALLOC_CONSOLE" ), nullptr ) );

    // Redirect log output to a timestamped file instead of the default wxLogGui
    // which shows popup dialogs for wxLogWarning/wxLogMessage on Windows.
    // Log file: %APPDATA%/Zeo/logs/agent-YYYY-MM-DD-HHMMSS.log
    {
        wxLog::EnableLogging( true );
        wxLog::SetLogLevel( wxLOG_Trace );

        wxString logDir = wxStandardPaths::Get().GetUserDataDir() + "\\logs";
        wxFileName::Mkdir( logDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

        wxString timestamp = wxDateTime::Now().Format( wxS( "%Y-%m-%d-%H%M%S" ) );
        wxString logPath = logDir + "\\agent-" + timestamp + ".log";
        FILE* logFile = _wfopen( logPath.wc_str(), L"w" );

        if( logFile )
        {
            // Timestamped logger matching macOS pattern
            class wxLogTimestampedFile : public wxLog
            {
            public:
                wxLogTimestampedFile( FILE* f ) : m_fp( f ) {}
            protected:
                void DoLogRecord( wxLogLevel, const wxString& msg,
                                  const wxLogRecordInfo& info ) override
                {
                    wxLongLong_t msEpoch = info.timestampMS;
                    wxDateTime dt( (time_t)( msEpoch / 1000 ) );
                    int ms = (int)( msEpoch % 1000 );
                    wxString ts = dt.Format( wxS( "%H:%M:%S" ) )
                                + wxString::Format( wxS( ".%03d" ), ms );
                    wxStringTokenizer tok( msg, wxS( "\n" ), wxTOKEN_RET_EMPTY );
                    while( tok.HasMoreTokens() )
                    {
                        wxString line = tok.GetNextToken();
                        fprintf( m_fp, "%s %s\n", ts.utf8_str().data(), line.utf8_str().data() );
                    }
                    fflush( m_fp );
                }
            private:
                FILE* m_fp;
            };
            wxLog::SetActiveTarget( new wxLogTimestampedFile( logFile ) );
        }
        else
        {
            wxLog::SetActiveTarget( new wxLogStderr );
        }

        wxLog::AddTraceMask( wxS( "Agent" ) );
        wxLog::AddTraceMask( wxS( "webview" ) );
    }

    // It may be useful to log up to traces in a console, but in Release builds the log level changes to Info
    // Also we have to force the active target to stderr or else it goes to the void
    bool forceLog = wxGetEnv( wxS( "KICAD_FORCE_CONSOLE_TRACE" ), nullptr );

    if( forceLog )
    {
        wxLog::EnableLogging( true );
#ifndef DEBUG
        wxLog::SetLogLevel( wxLOG_Trace );
#endif
    }

#if wxCHECK_VERSION( 3, 3, 0 )
    wxTheApp->MSWEnableDarkMode( 0, new KICAD_DARK_MODE_SETTINGS() );
#endif

    return true;
}


bool KIPLATFORM::APP::AttachConsole( bool aTryAlloc )
{
    if( ::AttachConsole( ATTACH_PARENT_PROCESS ) || ( aTryAlloc && ::AllocConsole() ) )
    {
        #if !defined( __MINGW32__ ) // These redirections create problems on mingw:
                                    // Nothing is printed to the console

        if( ::GetStdHandle( STD_INPUT_HANDLE ) != INVALID_HANDLE_VALUE )
        {
            freopen( "CONIN$", "r", stdin );
            setvbuf( stdin, NULL, _IONBF, 0 );
        }

        if( ::GetStdHandle( STD_OUTPUT_HANDLE ) != INVALID_HANDLE_VALUE )
        {
            freopen( "CONOUT$", "w", stdout );
            setvbuf( stdout, NULL, _IONBF, 0 );
        }

        if( ::GetStdHandle( STD_ERROR_HANDLE ) != INVALID_HANDLE_VALUE )
        {
            freopen( "CONOUT$", "w", stderr );
            setvbuf( stderr, NULL, _IONBF, 0 );
        }
        #endif

        std::ios::sync_with_stdio( true );

        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();
        std::wcin.clear();
        std::cin.clear();

        return true;
    }

    return false;
}


bool KIPLATFORM::APP::IsOperatingSystemUnsupported()
{
#if defined( PYTHON_VERSION_MAJOR ) && ( ( PYTHON_VERSION_MAJOR == 3 && PYTHON_VERSION_MINOR >= 8 ) \
             || PYTHON_VERSION_MAJOR > 3 )
    // Python 3.8 switched to Windows 8+ API, we do not support Windows 7 and will not
    // attempt to hack around it. A normal user will never get here because the Python DLL
    // is missing dependencies - and because it is not dynamically loaded, KiCad will not even
    // start without patching Python or its WinAPI dependency. This is just to create a nag dialog
    // for those who run patched Python and prevent them from submitting bug reports.
    return !IsWindows8OrGreater();
#else
    return false;
#endif
}


bool KIPLATFORM::APP::RegisterApplicationRestart( const wxString& aCommandLine )
{
    // Command line arguments with spaces require quotes.
    wxString restartCmd = wxS( "\"" ) + aCommandLine + wxS( "\"" );

    // Ensure we don't exceed the maximum allowable size
    if( restartCmd.length() > RESTART_MAX_CMD_LINE - 1 )
    {
        return false;
    }

    HRESULT hr = S_OK;

    hr = ::RegisterApplicationRestart( restartCmd.wc_str(), RESTART_NO_PATCH );

    return SUCCEEDED( hr );
}


bool KIPLATFORM::APP::UnregisterApplicationRestart()
{
    // Note, this isn't required to be used on Windows if you are just closing the program
    return SUCCEEDED( ::UnregisterApplicationRestart() );
}


bool KIPLATFORM::APP::SupportsShutdownBlockReason()
{
    return true;
}


void KIPLATFORM::APP::RemoveShutdownBlockReason( wxWindow* aWindow )
{
    // Destroys any block reason that may have existed
    ShutdownBlockReasonDestroy( aWindow->GetHandle() );
}


void KIPLATFORM::APP::SetShutdownBlockReason( wxWindow* aWindow, const wxString& aReason )
{
    // Sets up the pretty message on the shutdown page on why it's being "blocked"
    // This is used in conjunction with handling WM_QUERYENDSESSION (wxCloseEvent)
    // ShutdownBlockReasonCreate does not block by itself

    ShutdownBlockReasonDestroy( aWindow->GetHandle() ); // Destroys any existing or nonexisting reason

    ShutdownBlockReasonCreate( aWindow->GetHandle(), aReason.wc_str() );
}


void KIPLATFORM::APP::ForceTimerMessagesToBeCreatedIfNecessary()
{
    // Taken from https://devblogs.microsoft.com/oldnewthing/20191108-00/?p=103080
    MSG msg;
    PeekMessage( &msg, nullptr, WM_TIMER, WM_TIMER, PM_NOREMOVE );
}


void KIPLATFORM::APP::AddDynamicLibrarySearchPath( const wxString& aPath )
{
    SetDllDirectory( aPath.c_str() );
}
