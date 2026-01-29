#ifndef AGENT_HTML_TEMPLATE_H
#define AGENT_HTML_TEMPLATE_H

#include <wx/string.h>

/**
 * @brief Returns the HTML5 template with CSS stylesheet for the agent chat window.
 *
 * This template provides:
 * - Modern HTML5 structure with CSS3 styling
 * - Dark theme matching VS Code color scheme
 * - Semantic HTML classes for all content types
 * - CSS flex-direction: column-reverse for natural auto-scroll
 * - JavaScript message passing for interactivity
 *
 * @return wxString containing the HTML template (DOCTYPE through <div class="content-wrapper">)
 */
wxString GetAgentHtmlTemplate();

#endif // AGENT_HTML_TEMPLATE_H
