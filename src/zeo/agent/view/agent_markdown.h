#ifndef AGENT_MARKDOWN_H
#define AGENT_MARKDOWN_H

#include <wx/string.h>

namespace AgentMarkdown
{
    /**
     * Convert Markdown text to HTML suitable for wxHtmlWindow.
     * Supports code blocks, tables, lists, headings, blockquotes,
     * and inline formatting (bold, italic, code, links).
     */
    wxString ToHtml( const wxString& aMarkdown );

    /**
     * Process inline markdown formatting only (bold, italic, code, links).
     * Useful for processing table cells or other inline content.
     */
    wxString ProcessInline( const wxString& aText );

    /**
     * Wrap long lines at a maximum character width, preferring to break
     * at natural break points (spaces, commas, etc.).
     */
    wxString WrapLongLines( const wxString& aText, int aMaxChars = 60 );
}

#endif // AGENT_MARKDOWN_H
