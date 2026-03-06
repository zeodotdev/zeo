#ifndef SVG_RASTERIZER_H
#define SVG_RASTERIZER_H

#include <string>

namespace SvgRasterizer
{

/**
 * Rasterize an SVG file to a PNG file using nanosvg.
 * Scales the image so the longest side is at most aMaxDim pixels.
 *
 * @param aSvgPath   Path to the input SVG file.
 * @param aPngPath   Path for the output PNG file.
 * @param aMaxDim    Maximum dimension in pixels (default 4096).
 * @return true on success.
 */
bool RasterizeSvgToPng( const std::string& aSvgPath, const std::string& aPngPath,
                        int aMaxDim = 4096 );

} // namespace SvgRasterizer

#endif // SVG_RASTERIZER_H
