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

#ifndef FILE_ATTACH_H
#define FILE_ATTACH_H

#include <string>
#include <vector>
#include <wx/string.h>
#include <wx/window.h>
#include <nlohmann/json.hpp>

/**
 * A user-attached file (image or document) ready for display and API submission.
 */
struct FILE_ATTACHMENT
{
    std::string base64_data;
    std::string media_type;
    std::string filename;
};


namespace FileAttach
{

/**
 * Maximum image dimension (width or height) for API submission.
 * Matches the Claude API limit used by the screenshot tool.
 */
static const int MAX_IMAGE_DIMENSION = 1568;

/**
 * Maximum raw file size for non-image attachments (32 MB).
 * Matches the Anthropic API request size limit for PDF documents.
 */
static const size_t MAX_FILE_SIZE = 32UL * 1024 * 1024;

/**
 * Check if a media type is an image type supported by the API.
 */
bool IsImageMediaType( const std::string& aMediaType );

/**
 * Load an image file from disk, resize to fit API limits, and encode as PNG base64.
 *
 * @param aPath Absolute path to the image file
 * @param aResult Output attachment (populated on success)
 * @return true if the image was loaded and encoded successfully
 */
bool LoadImageFromFile( const wxString& aPath, FILE_ATTACHMENT& aResult );

/**
 * Load a non-image file from disk and encode as base64.
 * Sets media_type based on file extension (e.g. .pdf -> application/pdf).
 * Enforces MAX_FILE_SIZE limit.
 *
 * @param aPath Absolute path to the file
 * @param aResult Output attachment (populated on success)
 * @return true if the file was loaded and encoded successfully
 */
bool LoadFileFromDisk( const wxString& aPath, FILE_ATTACHMENT& aResult );

/**
 * Parse attachments from a JS submit message JSON payload.
 * Expects an "attachments" array with objects containing "base64", "media_type", "filename".
 *
 * @param aMsg The parsed JSON message from the input webview
 * @return Vector of parsed attachments (empty if none)
 */
std::vector<FILE_ATTACHMENT> ParseAttachmentsFromJson( const nlohmann::json& aMsg );

/**
 * Build inline HTML for attachment thumbnails inside a user chat bubble.
 *
 * @param aAttachments The attachments to render
 * @return HTML string (empty if no attachments)
 */
wxString BuildAttachmentBubbleHtml( const std::vector<FILE_ATTACHMENT>& aAttachments );

/**
 * Build a combined user chat bubble from a history message with attachment content blocks.
 * Handles both live base64 data and "__stripped__" placeholders for images and documents.
 *
 * @param aContentArray The "content" JSON array from a user message
 * @return HTML string for the complete user bubble (empty if no renderable content)
 */
wxString BuildHistoryBubbleHtml( const nlohmann::json& aContentArray );

/**
 * Show a modal image preview dialog from base64-encoded image data.
 *
 * @param aParent Parent window for the dialog
 * @param aBase64 Raw base64-encoded image data (no data URI prefix)
 */
void ShowPreviewDialog( wxWindow* aParent, const wxString& aBase64 );

/**
 * Open a non-image file in the system default application.
 * Writes base64-decoded data to a temp file, then launches the default viewer.
 *
 * @param aBase64 Raw base64-encoded file data
 * @param aFilename Original filename (used for temp file extension)
 */
void OpenFilePreview( const wxString& aBase64, const wxString& aFilename );

} // namespace FileAttach

#endif // FILE_ATTACH_H
