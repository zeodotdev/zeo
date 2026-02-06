#ifndef INPUT_HTML_TEMPLATE_H
#define INPUT_HTML_TEMPLATE_H

#include <wx/string.h>

/**
 * @brief Returns the HTML template for the agent input box with markdown syntax highlighting.
 *
 * Uses a textarea + pre overlay pattern for real-time syntax highlighting of
 * markdown tokens and @{tag} context references.
 *
 * @return wxString containing the complete HTML document for the input webview.
 */
wxString GetInputHtmlTemplate();

#endif // INPUT_HTML_TEMPLATE_H
