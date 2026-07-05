#ifndef PCD_JPEG_WRITER_H
#define PCD_JPEG_WRITER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "image_metadata.h"

// Encodes an interleaved 8-bit RGB buffer as a baseline JPEG, embedding the
// sRGB ICC profile in one or more APP2 "ICC_PROFILE" markers per the ICC spec
// (a marker can carry at most 65519 bytes of profile payload; our profile is
// ~3KB so a single marker is used in practice, but the loop supports larger
// profiles for robustness). Non-empty fields of `metadata` are written into
// an Exif APP1 segment ahead of the ICC profile; if every field is empty, no
// Exif segment is written at all.
//
// Returns true on success. On failure, errorOut is set to a human readable
// message.
bool writeJpegFile(const std::string &outPath, int quality, const uint8_t *rgb, int width,
                    int height, const ImageMetadata &metadata, std::string &errorOut);

#endif // PCD_JPEG_WRITER_H
