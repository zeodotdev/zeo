#include "agent_html_template.h"
#include "agent_template_embedded.h"

wxString GetAgentHtmlTemplate()
{
    return wxString::FromUTF8( AGENT_HTML_TEMPLATE_RAW );
}
