/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
