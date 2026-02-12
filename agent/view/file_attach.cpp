#include "file_attach.h"

#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/base64.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/mimetype.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/statbmp.h>

#include <algorithm>
#include <cmath>


bool FileAttach::IsImageMediaType( const std::string& aMediaType )
{
    return aMediaType == "image/jpeg" || aMediaType == "image/png"
           || aMediaType == "image/gif" || aMediaType == "image/webp";
}


bool FileAttach::LoadImageFromFile( const wxString& aPath, FILE_ATTACHMENT& aResult,
                                    wxString* aError )
{
    wxImage image;
    if( !image.LoadFile( aPath ) )
    {
        if( aError )
        {
            wxFileName fn( aPath );
            *aError = wxString::Format( "Could not load image: %s", fn.GetFullName() );
        }
        return false;
    }

    // Cap at ~12MP to keep history file sizes reasonable
    int w = image.GetWidth();
    int h = image.GetHeight();
    long pixels = (long) w * h;

    if( pixels > MAX_IMAGE_MEGAPIXELS )
    {
        double scale = std::sqrt( (double) MAX_IMAGE_MEGAPIXELS / pixels );
        image.Rescale( (int)( w * scale ), (int)( h * scale ), wxIMAGE_QUALITY_HIGH );
        w = image.GetWidth();
        h = image.GetHeight();
    }

    // Resize to fit API limits
    if( w > MAX_IMAGE_DIMENSION || h > MAX_IMAGE_DIMENSION )
    {
        double scale = std::min( (double) MAX_IMAGE_DIMENSION / w,
                                 (double) MAX_IMAGE_DIMENSION / h );
        image.Rescale( (int)( w * scale ), (int)( h * scale ), wxIMAGE_QUALITY_HIGH );
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


bool FileAttach::LoadFileFromDisk( const wxString& aPath, FILE_ATTACHMENT& aResult,
                                   wxString* aError )
{
    wxFileName fn( aPath );

    wxFile file( aPath, wxFile::read );
    if( !file.IsOpened() )
    {
        if( aError )
            *aError = wxString::Format( "Could not open file: %s", fn.GetFullName() );
        return false;
    }

    wxFileOffset fileSize = file.Length();
    if( fileSize <= 0 )
    {
        if( aError )
            *aError = wxString::Format( "File is empty: %s", fn.GetFullName() );
        return false;
    }

    if( (size_t) fileSize > MAX_FILE_SIZE )
    {
        if( aError )
        {
            *aError = wxString::Format( "File too large (max 32 MB): %s", fn.GetFullName() );
        }
        wxLogWarning( "File too large to attach (%zu bytes, max %zu): %s",
                      (size_t) fileSize, MAX_FILE_SIZE, aPath );
        return false;
    }

    wxMemoryBuffer buf( (size_t) fileSize );
    if( file.Read( buf.GetData(), (size_t) fileSize ) != fileSize )
    {
        if( aError )
            *aError = wxString::Format( "Failed to read file: %s", fn.GetFullName() );
        return false;
    }

    buf.SetDataLen( (size_t) fileSize );

    aResult.base64_data = wxBase64Encode( buf ).ToStdString();
    aResult.filename = fn.GetFullName().ToStdString();

    wxString ext = fn.GetExt().Lower();
    if( ext == "pdf" )
        aResult.media_type = "application/pdf";
    else
        aResult.media_type = "application/octet-stream";

    return true;
}


std::vector<FILE_ATTACHMENT> FileAttach::ParseAttachmentsFromJson( const nlohmann::json& aMsg )
{
    std::vector<FILE_ATTACHMENT> result;

    if( !aMsg.contains( "attachments" ) || !aMsg["attachments"].is_array() )
        return result;

    for( const auto& att : aMsg["attachments"] )
    {
        FILE_ATTACHMENT fa;
        fa.base64_data = att.value( "base64", "" );
        fa.media_type = att.value( "media_type", "" );
        fa.filename = att.value( "filename", "" );

        if( fa.base64_data.empty() )
            continue;

        // Resize images that exceed API limits
        if( IsImageMediaType( fa.media_type ) )
        {
            wxMemoryBuffer buf = wxBase64Decode( wxString::FromUTF8( fa.base64_data ) );

            if( buf.GetDataLen() > 0 )
            {
                wxMemoryInputStream stream( buf.GetData(), buf.GetDataLen() );
                wxImage image( stream );

                if( image.IsOk() )
                {
                    int w = image.GetWidth();
                    int h = image.GetHeight();
                    long pixels = (long) w * h;
                    bool needsReencode = false;

                    // Cap at ~12MP
                    if( pixels > MAX_IMAGE_MEGAPIXELS )
                    {
                        double scale = std::sqrt( (double) MAX_IMAGE_MEGAPIXELS / pixels );
                        image.Rescale( (int)( w * scale ), (int)( h * scale ),
                                       wxIMAGE_QUALITY_HIGH );
                        w = image.GetWidth();
                        h = image.GetHeight();
                        needsReencode = true;
                    }

                    if( w > MAX_IMAGE_DIMENSION || h > MAX_IMAGE_DIMENSION )
                    {
                        double scale = std::min( (double) MAX_IMAGE_DIMENSION / w,
                                                 (double) MAX_IMAGE_DIMENSION / h );
                        image.Rescale( (int)( w * scale ), (int)( h * scale ),
                                       wxIMAGE_QUALITY_HIGH );
                        needsReencode = true;
                    }

                    if( needsReencode )
                    {

                        wxMemoryOutputStream memStream;
                        image.SaveFile( memStream, wxBITMAP_TYPE_PNG );

                        size_t dataLen = memStream.GetLength();
                        wxMemoryBuffer outBuf( dataLen );
                        memStream.CopyTo( outBuf.GetData(), dataLen );
                        outBuf.SetDataLen( dataLen );

                        fa.base64_data = wxBase64Encode( outBuf ).ToStdString();
                        fa.media_type = "image/png";
                    }
                }
            }
        }

        result.push_back( std::move( fa ) );
    }

    return result;
}


static wxString EscapeHtml( const wxString& aStr )
{
    wxString s = aStr;
    s.Replace( "&", "&amp;" );
    s.Replace( "<", "&lt;" );
    s.Replace( ">", "&gt;" );
    s.Replace( "\"", "&quot;" );
    return s;
}


static wxString BuildDocumentIconHtml( const wxString& aFilename,
                                       const wxString& aBase64 = wxEmptyString )
{
    wxString escapedName = EscapeHtml( aFilename );
    wxString cursorStyle = aBase64.IsEmpty() ? "default" : "pointer";
    wxString dataAttrs;

    if( !aBase64.IsEmpty() )
    {
        dataAttrs = wxString::Format( " data-file-base64=\"%s\" data-file-name=\"%s\"",
                                       aBase64, escapedName );
    }

    return wxString::Format(
        "<div class=\"doc-preview\" style=\"display:flex; align-items:center; gap:6px; "
        "padding:6px 10px; background:#333; border-radius:6px; margin:4px 0; color:#ccc; "
        "font-size:12px; width:fit-content; cursor:%s;\"%s>"
        "<svg width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
        "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/>"
        "<polyline points=\"14 2 14 8 20 8\"/><line x1=\"16\" y1=\"13\" x2=\"8\" y2=\"13\"/>"
        "<line x1=\"16\" y1=\"17\" x2=\"8\" y2=\"17\"/><polyline points=\"10 9 9 9 8 9\"/>"
        "</svg>"
        "<span>%s</span></div>",
        cursorStyle, dataAttrs, escapedName );
}


wxString FileAttach::BuildAttachmentBubbleHtml(
        const std::vector<FILE_ATTACHMENT>& aAttachments )
{
    wxString html;

    for( const auto& att : aAttachments )
    {
        if( IsImageMediaType( att.media_type ) )
        {
            html += wxString::Format(
                "<img src=\"data:%s;base64,%s\" style=\"max-width:100%%; "
                "border-radius:6px; margin:4px 0; display:block;\" />",
                wxString::FromUTF8( att.media_type ),
                wxString::FromUTF8( att.base64_data ) );
        }
        else
        {
            html += BuildDocumentIconHtml( wxString::FromUTF8( att.filename ),
                                           wxString::FromUTF8( att.base64_data ) );
        }
    }

    return html;
}


wxString FileAttach::BuildHistoryBubbleHtml( const nlohmann::json& aContentArray )
{
    wxString attachHtml;
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
                attachHtml += wxString::Format(
                    "<img src=\"data:%s;base64,%s\" style=\"max-width:100%%; "
                    "border-radius:6px; margin:4px 0; display:block;\" />",
                    wxString::FromUTF8( mediaType ),
                    wxString::FromUTF8( data ) );
            }
            else
            {
                attachHtml += "<i style=\"color:#808080;\">"
                              "(Image from previous session)</i><br>";
            }
        }
        else if( bt == "document" && block.contains( "source" ) )
        {
            std::string data = block["source"].value( "data", "" );

            if( data != "__stripped__" && !data.empty() )
            {
                attachHtml += BuildDocumentIconHtml( "PDF document",
                                                     wxString::FromUTF8( data ) );
            }
            else
            {
                attachHtml += "<i style=\"color:#808080;\">"
                              "(PDF from previous session)</i><br>";
            }
        }
        else if( bt == "text" )
        {
            std::string raw = block.value( "text", "" );

            // Strip <project_context>...</project_context> injected into first message
            static const std::string CTX_PREFIX = "<project_context>\n";
            static const std::string CTX_SUFFIX = "\n</project_context>\n\n";

            if( raw.compare( 0, CTX_PREFIX.size(), CTX_PREFIX ) == 0 )
            {
                size_t end = raw.find( CTX_SUFFIX );
                if( end != std::string::npos )
                    raw = raw.substr( end + CTX_SUFFIX.size() );
            }

            wxString blockText = wxString::FromUTF8( raw );

            if( !textContent.IsEmpty() && !blockText.IsEmpty() )
                textContent += "\n";

            textContent += blockText;
        }
    }

    if( attachHtml.IsEmpty() && textContent.IsEmpty() )
        return wxString();

    wxString escapedText = textContent;
    escapedText.Replace( "&", "&amp;" );
    escapedText.Replace( "<", "&lt;" );
    escapedText.Replace( ">", "&gt;" );
    escapedText.Replace( "\n", "<br>" );

    wxString bubbleContent = attachHtml + escapedText;

    return wxString::Format(
        "<div class=\"flex justify-end my-3\"><div class=\"bg-bg-tertiary "
        "py-2 px-3.5 rounded-lg max-w-[80%%] whitespace-pre-wrap\">%s"
        "</div></div>",
        bubbleContent );
}


void FileAttach::ShowPreviewDialog( wxWindow* aParent, const wxString& aBase64 )
{
    wxMemoryBuffer buf = wxBase64Decode( aBase64 );

    if( buf.GetDataLen() == 0 )
        return;

    wxMemoryInputStream stream( buf.GetData(), buf.GetDataLen() );
    wxImage image( stream );

    if( !image.IsOk() )
        return;

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


void FileAttach::OpenFilePreview( const wxString& aBase64, const wxString& aFilename )
{
    wxMemoryBuffer buf = wxBase64Decode( aBase64 );

    if( buf.GetDataLen() == 0 )
        return;

    // Write to temp file preserving the original extension
    wxFileName fn( aFilename );
    wxString ext = fn.GetExt();

    wxString tempDir = wxStandardPaths::Get().GetTempDir();
    wxString tempPath = wxFileName( tempDir, "zeo_preview_" + fn.GetName(), ext ).GetFullPath();

    wxFile file( tempPath, wxFile::write );
    if( !file.IsOpened() )
        return;

    file.Write( buf.GetData(), buf.GetDataLen() );
    file.Close();

    wxLaunchDefaultApplication( tempPath );
}
