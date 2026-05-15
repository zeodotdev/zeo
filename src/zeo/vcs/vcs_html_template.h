#ifndef VCS_HTML_TEMPLATE_H
#define VCS_HTML_TEMPLATE_H

#include <wx/string.h>

/**
 * Returns the full HTML/CSS/JS content for the VCS web UI.
 * The content is embedded at build time from vcs_template.html.
 */
wxString GetVcsHtmlTemplate();

#endif // VCS_HTML_TEMPLATE_H
