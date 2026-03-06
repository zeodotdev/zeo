/*
 * IPC shell blocking global state - shared across all kifaces via kicommon.dll
 */

#include <api/api_handler_editor.h>
#include <wx/app.h>
#include <mutex>
#include <vector>
#include <functional>

// Unique key for IPC shell blocking flag in wxApp client data
static const char* IPC_SHELL_BLOCKING_KEY = "IPC_SHELL_BLOCKING";

// Deferred operations stay in static storage since they're executed by the kiface that queued them
static std::vector<std::function<void()>> s_deferredOperations;
static std::mutex s_deferredMutex;

void SetIPCShellBlocking( bool aBlocking )
{
    if( wxTheApp )
    {
        // Use client data pointer as a boolean flag (non-null = blocking)
        wxTheApp->SetClientData( aBlocking ? (void*)IPC_SHELL_BLOCKING_KEY : nullptr );
    }
}

bool IsIPCShellBlocking()
{
    bool blocking = wxTheApp && wxTheApp->GetClientData() != nullptr;
    return blocking;
}

void QueueDeferredOperation( std::function<void()> aOperation )
{
    std::lock_guard<std::mutex> lock( s_deferredMutex );
    s_deferredOperations.push_back( std::move( aOperation ) );
}

void ApplyDeferredOperations()
{
    std::vector<std::function<void()>> operations;

    {
        std::lock_guard<std::mutex> lock( s_deferredMutex );
        operations = std::move( s_deferredOperations );
        s_deferredOperations.clear();
    }

    for( auto& op : operations )
    {
        op();
    }
}
