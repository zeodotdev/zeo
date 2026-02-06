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

#include "image_attach.h"

#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/base64.h>
#include <wx/filename.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/statbmp.h>

#include <algorithm>


bool ImageAttach::LoadImageFromFile( const wxString& aPath, IMAGE_ATTACHMENT& aResult )
{
    wxImage image;
    if( !image.LoadFile( aPath ) )
        return false;

    // Resize to fit API limits
    int w = image.GetWidth();
    int h = image.GetHeight();

    if( w > MAX_IMAGE_DIMENSION || h > MAX_IMAGE_DIMENSION )
    {
        double scale = std::min( (double) MAX_IMAGE_DIMENSION / w,
                                 (double) MAX_IMAGE_DIMENSION / h );
        image.Rescale( (int) ( w * scale ), (int) ( h * scale ), wxIMAGE_QUALITY_HIGH );
    }

    // Encode as PNG base64
    wxMemoryOutputStream memStream;
    image.SaveFile( memStream, wxBITMAP_TYPE_PNG );

    size_t dataLen = memStream.GetLength();
    wxMemoryBuffer buf( dataLen );
    memStream.CopyTo( buf.GetData(), dataLen );
    buf.SetDataLen( dataLen );

    aResult.base64_data = wxBase64Encode( buf ).ToStdString();
    aResult.media_type = "image/png";

    wxFileName fn( aPath );
    aResult.filename = fn.GetFullName().ToStdString();

    return true;
}


std::vector<IMAGE_ATTACHMENT> ImageAttach::ParseAttachmentsFromJson( const nlohmann::json& aMsg )
{
    std::vector<IMAGE_ATTACHMENT> result;

    if( !aMsg.contains( "attachments" ) || !aMsg["attachments"].is_array() )
        return result;

    for( const auto& att : aMsg["attachments"] )
    {
        IMAGE_ATTACHMENT ia;
        ia.base64_data = att.value( "base64", "" );
        ia.media_type = att.value( "media_type", "" );
        ia.filename = att.value( "filename", "" );

        if( !ia.base64_data.empty() )
            result.push_back( std::move( ia ) );
    }

    return result;
}


wxString ImageAttach::BuildAttachmentBubbleHtml(
        const std::vector<IMAGE_ATTACHMENT>& aAttachments )
{
    wxString html;

    for( const auto& att : aAttachments )
    {
        html += wxString::Format(
            "<img src=\"data:%s;base64,%s\" style=\"max-width:200px; max-height:150px; "
            "border-radius:6px; margin:4px 0; display:block;\" />",
            wxString::FromUTF8( att.media_type ),
            wxString::FromUTF8( att.base64_data ) );
    }

    return html;
}


wxString ImageAttach::BuildHistoryImageBubbleHtml( const nlohmann::json& aContentArray )
{
    wxString imageHtml;
    wxString textContent;

    for( const auto& block : aContentArray )
    {
        std::string bt = block.value( "type", "" );

        if( bt == "image" && block.contains( "source" ) )
        {
            std::string data = block["source"].value( "data", "" );
            std::string mediaType = block["source"].value( "media_type", "image/png" );

            if( data != "__stripped__" && !data.empty() )
            {
                imageHtml += wxString::Format(
                    "<img src=\"data:%s;base64,%s\" style=\"max-width:200px; "
                    "max-height:150px; border-radius:6px; margin:4px 0; "
                    "display:block;\" />",
                    wxString::FromUTF8( mediaType ),
                    wxString::FromUTF8( data ) );
            }
            else
            {
                imageHtml += "<i style=\"color:#808080;\">"
                             "(Image from previous session)</i><br>";
            }
        }
        else if( bt == "text" )
        {
            wxString blockText = wxString::FromUTF8( block.value( "text", "" ) );

            if( !textContent.IsEmpty() && !blockText.IsEmpty() )
                textContent += "\n";

            textContent += blockText;
        }
    }

    if( imageHtml.IsEmpty() && textContent.IsEmpty() )
        return wxString();

    wxString escapedText = textContent;
    escapedText.Replace( "&", "&amp;" );
    escapedText.Replace( "<", "&lt;" );
    escapedText.Replace( ">", "&gt;" );
    escapedText.Replace( "\n", "<br>" );

    wxString bubbleContent = imageHtml + escapedText;

    return wxString::Format(
        "<div class=\"flex justify-end my-1.5\"><div class=\"bg-bg-tertiary "
        "py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s"
        "</div></div>",
        bubbleContent );
}


void ImageAttach::ShowPreviewDialog( wxWindow* aParent, const wxString& aBase64 )
{
    wxMemoryBuffer buf = wxBase64Decode( aBase64 );

    if( buf.GetDataLen() == 0 )
        return;

    wxMemoryInputStream stream( buf.GetData(), buf.GetDataLen() );
    wxImage image( stream );

    if( !image.IsOk() )
        return;

    int maxDim = 600;
    int w = image.GetWidth();
    int h = image.GetHeight();

    if( w > maxDim || h > maxDim )
    {
        double scale = std::min( (double) maxDim / w, (double) maxDim / h );
        image.Rescale( (int)( w * scale ), (int)( h * scale ), wxIMAGE_QUALITY_HIGH );
    }

    wxDialog dlg( aParent, wxID_ANY, "Image Preview",
                  wxDefaultPosition, wxDefaultSize,
                  wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER );
    dlg.SetBackgroundColour( wxColour( 28, 28, 28 ) );

    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    wxStaticBitmap* bmp = new wxStaticBitmap( &dlg, wxID_ANY, wxBitmap( image ) );
    sizer->Add( bmp, 1, wxALL | wxALIGN_CENTER, 10 );
    dlg.SetSizerAndFit( sizer );
    dlg.CentreOnParent();
    dlg.ShowModal();
}
