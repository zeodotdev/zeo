#include "vcs_html_template.h"

// This header is generated at build time from vcs_template.html by embed_html.cmake.
// It defines: const char* VCS_HTML_TEMPLATE_RAW
#include "vcs_html_template_embedded.h"

wxString GetVcsHtmlTemplate()
{
    return wxString::FromUTF8( VCS_HTML_TEMPLATE_RAW );
}
