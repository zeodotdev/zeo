/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "unified_html_template.h"
#include "unified_template_embedded.h"

wxString GetUnifiedHtmlTemplate()
{
    return wxString::FromUTF8( UNIFIED_HTML_TEMPLATE_RAW, UNIFIED_HTML_TEMPLATE_RAW_LEN );
}
