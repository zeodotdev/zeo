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

#include "agent_markdown.h"
#include <wx/arrstr.h>

namespace AgentMarkdown
{

wxString WrapLongLines( const wxString& aText, int aMaxChars )
{
    wxString result;
    int lineLen = 0;

    for( size_t i = 0; i < aText.length(); i++ )
    {
        wxChar ch = aText[i];

        if( ch == '\n' || ( aText.Mid( i, 4 ) == "<br>" ) )
        {
            // Reset line length on explicit breaks
            lineLen = 0;
            if( ch == '\n' )
            {
                result += "<br>";
                continue;
            }
        }

        result += ch;
        lineLen++;

        // Insert break at max length, preferring to break after certain characters
        if( lineLen >= aMaxChars )
        {
            // Look for a good break point (space, comma, colon, etc.)
            bool breakInserted = false;
            for( int j = result.length() - 1; j >= (int)result.length() - 20 && j >= 0; j-- )
            {
                wxChar c = result[j];
                if( c == ' ' || c == ',' || c == ':' || c == ';' || c == '{' || c == '}' || c == '[' || c == ']' )
                {
                    result.insert( j + 1, "<br>" );
                    breakInserted = true;
                    break;
                }
            }
            if( !breakInserted )
            {
                result += "<br>";
            }
            lineLen = 0;
        }
    }

    return result;
}

wxString ProcessInline( const wxString& aText )
{
    wxString processed = aText;

    // Inline code `code` -> monospace
    wxString temp;
    bool inInlineCode = false;
    wxString codeContent;
    for( size_t j = 0; j < processed.length(); j++ )
    {
        if( processed[j] == '`' )
        {
            if( !inInlineCode )
            {
                inInlineCode = true;
                codeContent.clear();
            }
            else
            {
                temp += "<code>" + codeContent + "</code>";
                inInlineCode = false;
            }
        }
        else if( inInlineCode )
        {
            codeContent += processed[j];
        }
        else
        {
            temp += processed[j];
        }
    }
    if( inInlineCode )
        temp += "`" + codeContent; // Unclosed backtick
    processed = temp;

    // Bold **text** - use wxString positions consistently (handles Unicode correctly)
    int boldIterations = 0;
    while( boldIterations < 100 )
    {
        boldIterations++;
        int start = processed.Find( "**" );
        if( start == wxNOT_FOUND ) break;

        // Find closing ** after the opening one
        wxString afterStart = processed.Mid( start + 2 );
        int endOffset = afterStart.Find( "**" );
        if( endOffset == wxNOT_FOUND ) break;

        int end = start + 2 + endOffset;
        wxString before = processed.Left( start );
        wxString bold = processed.Mid( start + 2, end - start - 2 );
        wxString after = processed.Mid( end + 2 );
        processed = before + "<strong>" + bold + "</strong>" + after;
    }

    // Italic *text* (but not **)
    temp.clear();
    bool inItalic = false;
    for( size_t j = 0; j < processed.length(); j++ )
    {
        if( processed[j] == '*' && ( j + 1 >= processed.length() || processed[j+1] != '*' ) &&
            ( j == 0 || processed[j-1] != '*' ) )
        {
            if( !inItalic )
            {
                temp += "<em>";
                inItalic = true;
            }
            else
            {
                temp += "</em>";
                inItalic = false;
            }
        }
        else
        {
            temp += processed[j];
        }
    }
    if( inItalic )
        temp += "</em>"; // Auto-close
    processed = temp;

    // Links [text](url) - use wxString positions consistently
    int linkIterations = 0;
    while( linkIterations < 100 )
    {
        linkIterations++;
        int bracketStart = processed.Find( "[" );
        if( bracketStart == wxNOT_FOUND ) break;

        int bracketEnd = processed.Find( "](" );
        if( bracketEnd == wxNOT_FOUND || bracketEnd < bracketStart ) break;

        // Find closing ) after ](
        wxString afterBracket = processed.Mid( bracketEnd + 2 );
        int parenEndOffset = afterBracket.Find( ")" );
        if( parenEndOffset == wxNOT_FOUND ) break;

        int parenEnd = bracketEnd + 2 + parenEndOffset;

        wxString before = processed.Left( bracketStart );
        wxString linkText = processed.Mid( bracketStart + 1, bracketEnd - bracketStart - 1 );
        wxString url = processed.Mid( bracketEnd + 2, parenEnd - bracketEnd - 2 );
        wxString after = processed.Mid( parenEnd + 1 );

        processed = before + "<a href=\"" + url + "\">" + linkText + "</a>" + after;
    }

    return processed;
}

wxString ToHtml( const wxString& aMarkdown )
{
    wxString result;
    wxArrayString lines;

    // Split into lines
    wxString current;
    for( size_t i = 0; i < aMarkdown.length(); i++ )
    {
        if( aMarkdown[i] == '\n' )
        {
            lines.Add( current );
            current.clear();
        }
        else
        {
            current += aMarkdown[i];
        }
    }
    if( !current.empty() )
        lines.Add( current );

    bool inCodeBlock = false;
    bool inList = false;
    bool inTable = false;
    wxString codeBlockContent;
    wxString codeBlockLanguage;

    for( size_t i = 0; i < lines.GetCount(); i++ )
    {
        wxString line = lines[i];
        wxString trimmed = line;
        trimmed.Trim( false ).Trim( true );

        // Code blocks (```)
        if( trimmed.StartsWith( "```" ) )
        {
            if( !inCodeBlock )
            {
                inCodeBlock = true;
                codeBlockContent.clear();
                // Extract language identifier (e.g., "cpp" from "```cpp")
                codeBlockLanguage = trimmed.Mid( 3 ).Trim( false ).Trim( true );
                // Close any open list
                if( inList )
                {
                    result += "</ul>";
                    inList = false;
                }
            }
            else
            {
                // End code block - render with semantic HTML for highlight.js
                codeBlockContent.Replace( "&", "&amp;" );
                codeBlockContent.Replace( "<", "&lt;" );
                codeBlockContent.Replace( ">", "&gt;" );

                // Build language class for highlight.js
                wxString langClass;
                if( !codeBlockLanguage.IsEmpty() )
                {
                    langClass = " class=\"language-" + codeBlockLanguage + "\"";
                }

                result += "<pre class=\"code-block\"><code" + langClass + ">" + codeBlockContent + "</code></pre>";
                inCodeBlock = false;
                codeBlockLanguage.clear();
            }
            continue;
        }

        if( inCodeBlock )
        {
            if( !codeBlockContent.empty() )
                codeBlockContent += "\n";
            codeBlockContent += line;
            continue;
        }

        // Tables (lines starting with |)
        if( trimmed.StartsWith( "|" ) && trimmed.EndsWith( "|" ) )
        {
            // Check if this is a separator line (|---|---|)
            bool isSeparator = true;
            for( size_t j = 0; j < trimmed.length(); j++ )
            {
                wxChar c = trimmed[j];
                if( c != '|' && c != '-' && c != ':' && c != ' ' )
                {
                    isSeparator = false;
                    break;
                }
            }

            if( isSeparator )
            {
                // Mark that we've seen separator - next rows are data rows
                // The header row was already added, so just continue
                continue;
            }

            if( !inTable )
            {
                if( inList ) { result += "</ul>"; inList = false; }
                result += "<table>";
                inTable = true;
            }

            // Parse table row - collect cells first
            wxArrayString cells;
            wxString cell;
            bool inCell = false;
            for( size_t j = 0; j < trimmed.length(); j++ )
            {
                if( trimmed[j] == '|' )
                {
                    if( inCell )
                    {
                        cell.Trim( false ).Trim( true );
                        cells.Add( cell );
                        cell.clear();
                    }
                    inCell = true;
                }
                else if( inCell )
                {
                    cell += trimmed[j];
                }
            }

            // Check if next line is separator (this is header row)
            bool isHeader = false;
            if( i + 1 < lines.GetCount() )
            {
                wxString nextLine = lines[i + 1];
                nextLine.Trim( false ).Trim( true );
                if( nextLine.StartsWith( "|" ) )
                {
                    isHeader = true;
                    for( size_t k = 0; k < nextLine.length(); k++ )
                    {
                        wxChar c = nextLine[k];
                        if( c != '|' && c != '-' && c != ':' && c != ' ' )
                        {
                            isHeader = false;
                            break;
                        }
                    }
                }
            }

            // Render row
            result += "<tr>";
            for( size_t c = 0; c < cells.GetCount(); c++ )
            {
                wxString cellContent = ProcessInline( cells[c] );
                if( isHeader )
                {
                    result += "<th>" + cellContent + "</th>";
                }
                else
                {
                    result += "<td>" + cellContent + "</td>";
                }
            }
            result += "</tr>";
            continue;
        }
        else if( inTable )
        {
            result += "</table><br>";
            inTable = false;
        }

        // Close list if we hit a non-list line
        if( inList && !trimmed.StartsWith( "-" ) && !trimmed.StartsWith( "*" ) &&
            !trimmed.StartsWith( "1." ) && !trimmed.StartsWith( "2." ) && !trimmed.StartsWith( "3." ) &&
            !trimmed.IsEmpty() )
        {
            result += "</ul>";
            inList = false;
        }

        // Empty lines
        if( trimmed.IsEmpty() )
        {
            if( inList )
            {
                result += "</ul>";
                inList = false;
            }
            result += "<br>";
            continue;
        }

        // Headings (semantic HTML5 tags)
        if( trimmed.StartsWith( "######" ) )
        {
            result += "<h6>" + ProcessInline( trimmed.Mid( 6 ).Trim( false ) ) + "</h6>";
            continue;
        }
        if( trimmed.StartsWith( "#####" ) )
        {
            result += "<h5>" + ProcessInline( trimmed.Mid( 5 ).Trim( false ) ) + "</h5>";
            continue;
        }
        if( trimmed.StartsWith( "####" ) )
        {
            result += "<h4>" + ProcessInline( trimmed.Mid( 4 ).Trim( false ) ) + "</h4>";
            continue;
        }
        if( trimmed.StartsWith( "###" ) )
        {
            result += "<h3>" + ProcessInline( trimmed.Mid( 3 ).Trim( false ) ) + "</h3>";
            continue;
        }
        if( trimmed.StartsWith( "##" ) )
        {
            result += "<h2>" + ProcessInline( trimmed.Mid( 2 ).Trim( false ) ) + "</h2>";
            continue;
        }
        if( trimmed.StartsWith( "#" ) )
        {
            result += "<h1>" + ProcessInline( trimmed.Mid( 1 ).Trim( false ) ) + "</h1>";
            continue;
        }

        // Blockquotes
        if( trimmed.StartsWith( ">" ) )
        {
            wxString quote = ProcessInline( trimmed.Mid( 1 ).Trim( false ) );
            result += "<blockquote>" + quote + "</blockquote>";
            continue;
        }

        // Unordered lists
        if( trimmed.StartsWith( "- " ) || trimmed.StartsWith( "* " ) )
        {
            if( !inList )
            {
                result += "<ul>";
                inList = true;
            }
            result += "<li>" + ProcessInline( trimmed.Mid( 2 ) ) + "</li>";
            continue;
        }

        // Ordered lists (simple check for 1. 2. 3. etc)
        if( trimmed.length() > 2 && trimmed[1] == '.' && trimmed[0] >= '0' && trimmed[0] <= '9' )
        {
            if( !inList )
            {
                result += "<ul>";
                inList = true;
            }
            result += "<li>" + ProcessInline( trimmed.Mid( 2 ).Trim( false ) ) + "</li>";
            continue;
        }

        // Horizontal rule
        if( trimmed == "---" || trimmed == "***" || trimmed == "___" )
        {
            result += "<hr>";
            continue;
        }

        // Regular paragraph - process inline formatting
        result += ProcessInline( trimmed ) + "<br>";
    }

    // Close any open elements
    if( inList )
        result += "</ul>";
    if( inTable )
        result += "</table>";
    if( inCodeBlock )
    {
        codeBlockContent.Replace( "&", "&amp;" );
        codeBlockContent.Replace( "<", "&lt;" );
        codeBlockContent.Replace( ">", "&gt;" );

        // Build language class for highlight.js
        wxString langClass;
        if( !codeBlockLanguage.IsEmpty() )
        {
            langClass = " class=\"language-" + codeBlockLanguage + "\"";
        }

        result += "<pre class=\"code-block\"><code" + langClass + ">" + codeBlockContent + "</code></pre>";
    }

    return result;
}

} // namespace AgentMarkdown
