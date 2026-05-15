#ifndef CC_EVENTS_H
#define CC_EVENTS_H

#include <wx/event.h>

// Raw events from the Claude Code subprocess
wxDECLARE_EVENT( EVT_CC_LINE, wxThreadEvent );   ///< NDJSON line from stdout
wxDECLARE_EVENT( EVT_CC_EXIT, wxThreadEvent );    ///< Process exited (int payload = exit code)
wxDECLARE_EVENT( EVT_CC_ERROR, wxThreadEvent );   ///< stderr output (string payload)

#endif // CC_EVENTS_H
