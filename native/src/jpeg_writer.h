#ifndef PCD_JPEG_WRITER_H
#define PCD_JPEG_WRITER_H

#include <cstddef>
#include <cstdint>
#include <string>

// Encodes an interleaved 8-bit RGB buffer as a baseline JPEG, embedding the
// sRGB ICC profile in one or more APP2 "ICC_PROFILE" markers per the ICC spec
// (a marker can carry at most 65519 bytes of profile payload; our profile is
// ~3KB so a single marker is used in practice, but the loop supports larger
// profiles for robustness).
//
// Returns true on success. On failure, errorOut is set to a human readable
// message.
bool writeJpegFile(const std::string &outPath, int quality, const uint8_t *rgb,
                    int width, int height, std::string &errorOut);

#endif // PCD_JPEG_WRITER_H
