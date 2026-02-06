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

#ifndef IMAGE_ATTACH_H
#define IMAGE_ATTACH_H

#include <string>
#include <vector>
#include <wx/string.h>
#include <wx/window.h>
#include <nlohmann/json.hpp>

/**
 * A user-attached image ready for display and API submission.
 */
struct IMAGE_ATTACHMENT
{
    std::string base64_data;
    std::string media_type;
    std::string filename;
};


namespace ImageAttach
{

/**
 * Maximum image dimension (width or height) for API submission.
 * Matches the Claude API limit used by the screenshot tool.
 */
static const int MAX_IMAGE_DIMENSION = 1568;

/**
 * Load an image file from disk, resize to fit API limits, and encode as PNG base64.
 *
 * @param aPath Absolute path to the image file
 * @param aResult Output attachment (populated on success)
 * @return true if the image was loaded and encoded successfully
 */
bool LoadImageFromFile( const wxString& aPath, IMAGE_ATTACHMENT& aResult );

/**
 * Parse image attachments from a JS submit message JSON payload.
 * Expects an "attachments" array with objects containing "base64", "media_type", "filename".
 *
 * @param aMsg The parsed JSON message from the input webview
 * @return Vector of parsed attachments (empty if none)
 */
std::vector<IMAGE_ATTACHMENT> ParseAttachmentsFromJson( const nlohmann::json& aMsg );

/**
 * Build inline HTML for attachment thumbnail images inside a user chat bubble.
 *
 * @param aAttachments The attachments to render
 * @return HTML string with <img> tags (empty if no attachments)
 */
wxString BuildAttachmentBubbleHtml( const std::vector<IMAGE_ATTACHMENT>& aAttachments );

/**
 * Build a combined user chat bubble from a history message with image content blocks.
 * Handles both live base64 data and "__stripped__" placeholders.
 *
 * @param aContentArray The "content" JSON array from a user message
 * @return HTML string for the complete user bubble (empty if no renderable content)
 */
wxString BuildHistoryImageBubbleHtml( const nlohmann::json& aContentArray );

/**
 * Show a modal image preview dialog from base64-encoded image data.
 *
 * @param aParent Parent window for the dialog
 * @param aBase64 Raw base64-encoded image data (no data URI prefix)
 */
void ShowPreviewDialog( wxWindow* aParent, const wxString& aBase64 );

} // namespace ImageAttach

#endif // IMAGE_ATTACH_H
