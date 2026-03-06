/**
 * Self-contained SVG rasterizer using vcpkg's nanosvg.
 *
 * KiCad's thirdparty ships a modified nanosvg with different struct layouts
 * (missing gradient fields in NSVGshape). To avoid linker conflicts, we rename
 * all nanosvg symbols with a zeo_ prefix before pulling in the vcpkg
 * header-only implementations.
 */

// Rename all public nanosvg symbols to avoid conflicts with KiCad's thirdparty nanosvg
#define NSVGimage              zeo_NSVGimage
#define NSVGshape              zeo_NSVGshape
#define NSVGpath               zeo_NSVGpath
#define NSVGpaint              zeo_NSVGpaint
#define NSVGgradient           zeo_NSVGgradient
#define NSVGgradientStop       zeo_NSVGgradientStop
#define NSVGrasterizer         zeo_NSVGrasterizer
#define nsvgParseFromFile      zeo_nsvgParseFromFile
#define nsvgParse              zeo_nsvgParse
#define nsvgDuplicatePath      zeo_nsvgDuplicatePath
#define nsvgDelete             zeo_nsvgDelete
#define nsvgCreateRasterizer   zeo_nsvgCreateRasterizer
#define nsvgRasterize          zeo_nsvgRasterize
#define nsvgRasterizeXY        zeo_nsvgRasterizeXY
#define nsvgDeleteRasterizer   zeo_nsvgDeleteRasterizer

// Also rename internal symbols that may leak
#define nsvg__parseXML         zeo_nsvg__parseXML
#define nsvg__parseColor       zeo_nsvg__parseColor
#define nsvg__parseColorHex    zeo_nsvg__parseColorHex
#define nsvg__parseColorName   zeo_nsvg__parseColorName
#define nsvg__parseColorRGB    zeo_nsvg__parseColorRGB

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION

// Prevent re-definition of the include guard from KiCad's thirdparty nanosvg.h
#undef NANOSVG_H
#undef NANOSVGRAST_H

#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

// Undo renames for the rest of this file (use zeo_ prefixed names explicitly)
#undef NSVGimage
#undef NSVGshape
#undef NSVGpath
#undef NSVGpaint
#undef NSVGgradient
#undef NSVGgradientStop
#undef NSVGrasterizer
#undef nsvgParseFromFile
#undef nsvgParse
#undef nsvgDuplicatePath
#undef nsvgDelete
#undef nsvgCreateRasterizer
#undef nsvgRasterize
#undef nsvgRasterizeXY
#undef nsvgDeleteRasterizer
#undef nsvg__parseXML
#undef nsvg__parseColor
#undef nsvg__parseColorHex
#undef nsvg__parseColorName
#undef nsvg__parseColorRGB

#include "svg_rasterizer.h"

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <wx/image.h>
#include <wx/log.h>


bool SvgRasterizer::RasterizeSvgToPng( const std::string& aSvgPath,
                                        const std::string& aPngPath, int aMaxDim )
{
    zeo_NSVGimage* image = zeo_nsvgParseFromFile( aSvgPath.c_str(), "px", 96.0f );

    if( !image )
    {
        wxLogError( "SvgRasterizer: Failed to parse SVG: %s", aSvgPath.c_str() );
        return false;
    }

    if( image->width <= 0 || image->height <= 0 )
    {
        wxLogError( "SvgRasterizer: SVG has invalid dimensions: %.0fx%.0f",
                    image->width, image->height );
        zeo_nsvgDelete( image );
        return false;
    }

    // Scale so longest side fits in aMaxDim
    float scale = static_cast<float>( aMaxDim ) / std::max( image->width, image->height );
    int   w = static_cast<int>( image->width * scale );
    int   h = static_cast<int>( image->height * scale );

    if( w <= 0 || h <= 0 )
    {
        zeo_nsvgDelete( image );
        return false;
    }

    // Rasterize to RGBA buffer
    std::vector<unsigned char> pixels( w * h * 4 );

    zeo_NSVGrasterizer* rast = zeo_nsvgCreateRasterizer();

    if( !rast )
    {
        zeo_nsvgDelete( image );
        return false;
    }

    zeo_nsvgRasterize( rast, image, 0, 0, scale, pixels.data(), w, h, w * 4 );
    zeo_nsvgDeleteRasterizer( rast );
    zeo_nsvgDelete( image );

    // Convert RGBA to wxImage (RGB + alpha channel)
    wxImage wxImg( w, h );
    wxImg.InitAlpha();

    unsigned char* rgb = wxImg.GetData();
    unsigned char* alpha = wxImg.GetAlpha();

    for( int i = 0; i < w * h; ++i )
    {
        rgb[i * 3 + 0] = pixels[i * 4 + 0];
        rgb[i * 3 + 1] = pixels[i * 4 + 1];
        rgb[i * 3 + 2] = pixels[i * 4 + 2];
        alpha[i] = pixels[i * 4 + 3];
    }

    if( !wxImg.SaveFile( wxString::FromUTF8( aPngPath ), wxBITMAP_TYPE_PNG ) )
    {
        wxLogError( "SvgRasterizer: Failed to save PNG: %s", aPngPath.c_str() );
        return false;
    }

    wxLogInfo( "SvgRasterizer: Rasterized %dx%d PNG from SVG", w, h );
    return true;
}
