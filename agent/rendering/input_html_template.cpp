#include "input_html_template.h"
#include "input_template_embedded.h"

wxString GetInputHtmlTemplate()
{
    return wxString::FromUTF8( INPUT_HTML_TEMPLATE_RAW );
}
