#ifndef PCD_TIFF_WRITER_H
#define PCD_TIFF_WRITER_H

#include <cstdint>
#include <string>

// Encodes an interleaved 8-bit RGB buffer as a baseline-plus TIFF: single
// strip, Adobe/"ZIP" deflate compression (TIFF Compression tag = 8) with a
// horizontal differencing predictor (Predictor tag = 2) for a better
// compression ratio, and the sRGB ICC profile embedded in the standard
// ICCProfile tag (34675) for accurate color reproduction.
//
// Returns true on success. On failure, errorOut is set to a human readable
// message.
bool writeTiffFile(const std::string &outPath, const uint8_t *rgb, int width,
                    int height, std::string &errorOut);

#endif // PCD_TIFF_WRITER_H
