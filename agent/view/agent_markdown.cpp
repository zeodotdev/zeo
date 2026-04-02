#include "agent_markdown.h"
#include <wx/arrstr.h>
#include <wctype.h>

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

        // Escape HTML entities in link text and URL
        wxString safeUrl = url;
        safeUrl.Replace( "&", "&amp;" );
        safeUrl.Replace( "\"", "&quot;" );
        wxString safeLinkText = linkText;
        safeLinkText.Replace( "&", "&amp;" );
        safeLinkText.Replace( "<", "&lt;" );
        safeLinkText.Replace( ">", "&gt;" );
        processed = before + "<a href=\"" + safeUrl + "\">" + safeLinkText + "</a>" + after;
    }

    // Component reference designator badges (R1, C3, U1, D4, Q2, L1, etc.)
    // Matches word-boundary patterns like R1, R12, C100, U3A — but not inside
    // HTML tags, code blocks, or URLs (which already have <a>/<code> wrappers).
    {
        wxString result;
        size_t i = 0;
        bool insideTag = false;

        while( i < processed.length() )
        {
            wxChar ch = processed[i];

            // Skip HTML tags entirely
            if( ch == '<' )
            {
                insideTag = true;
                result += ch;
                i++;
                continue;
            }

            if( insideTag )
            {
                result += ch;
                if( ch == '>' )
                    insideTag = false;
                i++;
                continue;
            }

            // Check for reference designator pattern at word boundary
            // Prefixes: R, C, U, D, Q, L, J, P, F, T, SW, BT, TP, FB, RN, IC, LED
            bool isWordStart = ( i == 0 || !iswalnum( processed[i - 1] ) );

            if( isWordStart && iswupper( ch ) )
            {
                // Collect the alpha prefix
                size_t prefixStart = i;
                size_t j = i;

                while( j < processed.length() && iswupper( processed[j] ) )
                    j++;

                // Must have at least 1 digit after the prefix
                if( j < processed.length() && iswdigit( processed[j] ) )
                {
                    size_t digitStart = j;

                    while( j < processed.length() && iswdigit( processed[j] ) )
                        j++;

                    // Optional alpha suffix (e.g., U3A, Q1B)
                    if( j < processed.length() && iswupper( processed[j] )
                        && ( j + 1 >= processed.length() || !iswalnum( processed[j + 1] ) ) )
                        j++;

                    // Must end at a word boundary
                    bool isWordEnd = ( j >= processed.length() || !iswalnum( processed[j] ) );

                    wxString prefix = processed.Mid( prefixStart, digitStart - prefixStart );

                    // Only match known EDA prefixes
                    static const wxString knownPrefixes[] = {
                        "R", "C", "U", "D", "Q", "L", "J", "P", "F", "T",
                        "SW", "BT", "TP", "FB", "RN", "IC", "LED", "M", "K",
                        "Y", "X", "H", "FL"
                    };

                    bool isKnownPrefix = false;

                    for( const auto& kp : knownPrefixes )
                    {
                        if( prefix == kp )
                        {
                            isKnownPrefix = true;
                            break;
                        }
                    }

                    if( isKnownPrefix && isWordEnd )
                    {
                        wxString ref = processed.Mid( prefixStart, j - prefixStart );
                        result += "<a href=\"agent:select:" + ref
                                + "\" class=\"ref-badge\">" + ref + "</a>";
                        i = j;
                        continue;
                    }
                }

                // Not a valid ref — output the character normally
            }

            result += ch;
            i++;
        }

        processed = result;
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
    bool inOrderedList = false;
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
                    result += inOrderedList ? "</ol>" : "</ul>";
                    inList = false;
                    inOrderedList = false;
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
                if( inList ) { result += inOrderedList ? "</ol>" : "</ul>"; inList = false; inOrderedList = false; }
                result += "<div class=\"table-scroll-wrapper\"><table>";
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
            result += "</table></div><br>";
            inTable = false;
        }

        // Detect if this line is a list item
        bool isUnorderedItem = trimmed.StartsWith( "- " ) || trimmed.StartsWith( "* " );
        bool isOrderedItem = false;

        if( !isUnorderedItem )
        {
            // Match N. where N is one or more digits followed by ". "
            size_t dotPos = trimmed.find( '.' );

            if( dotPos != wxString::npos && dotPos > 0 && dotPos < 10
                && dotPos + 1 < trimmed.length() && trimmed[dotPos + 1] == ' ' )
            {
                isOrderedItem = true;

                for( size_t c = 0; c < dotPos; c++ )
                {
                    if( trimmed[c] < '0' || trimmed[c] > '9' )
                    {
                        isOrderedItem = false;
                        break;
                    }
                }
            }
        }

        // Close list if we hit a non-list line
        if( inList && !isUnorderedItem && !isOrderedItem && !trimmed.IsEmpty() )
        {
            result += inOrderedList ? "</ol>" : "</ul>";
            inList = false;
            inOrderedList = false;
        }

        // Empty lines
        if( trimmed.IsEmpty() )
        {
            if( inList )
            {
                result += inOrderedList ? "</ol>" : "</ul>";
                inList = false;
                inOrderedList = false;
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
        if( isUnorderedItem )
        {
            if( !inList || inOrderedList )
            {
                if( inList )
                    result += "</ol>";

                result += "<ul>";
                inList = true;
                inOrderedList = false;
            }

            result += "<li>" + ProcessInline( trimmed.Mid( 2 ) ) + "</li>";
            continue;
        }

        // Ordered lists (N. where N is one or more digits)
        if( isOrderedItem )
        {
            if( !inList || !inOrderedList )
            {
                if( inList )
                    result += "</ul>";

                result += "<ol>";
                inList = true;
                inOrderedList = true;
            }

            size_t dotPos = trimmed.find( '.' );
            result += "<li>" + ProcessInline( trimmed.Mid( dotPos + 1 ).Trim( false ) ) + "</li>";
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
        result += inOrderedList ? "</ol>" : "</ul>";
    if( inTable )
        result += "</table></div>";
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
