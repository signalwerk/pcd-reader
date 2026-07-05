#ifndef PCD_TIFF_BYTES_H
#define PCD_TIFF_BYTES_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Little-endian byte-packing helpers for hand-rolled TIFF IFD structures.
// Shared between tiff_writer.cpp (a full TIFF file) and jpeg_writer.cpp (an
// Exif APP1 segment, which is itself a miniature TIFF file per the Exif spec).
namespace tiffbytes {

inline void appendU8(std::vector<uint8_t> &out, uint8_t v) { out.push_back(v); }

inline void appendU16LE(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

inline void appendU32LE(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

inline void appendBytes(std::vector<uint8_t> &out, const uint8_t *data, size_t len) {
  out.insert(out.end(), data, data + len);
}

inline void padToEven(std::vector<uint8_t> &out) {
  if (out.size() % 2 != 0) out.push_back(0);
}

// A single 12-byte IFD entry: tag, field type, value count, and either the
// value itself (if it fits in 4 bytes) or an offset to it (offsets are
// relative to whatever the containing TIFF structure's byte 0 is - the file
// start for a full TIFF, or the start of the embedded mini-TIFF for Exif).
inline void writeIfdEntry(std::vector<uint8_t> &out, uint16_t tag, uint16_t type,
                           uint32_t count, uint32_t valueOrOffset) {
  appendU16LE(out, tag);
  appendU16LE(out, type);
  appendU32LE(out, count);
  appendU32LE(out, valueOrOffset);
}

// TIFF field types.
constexpr uint16_t kTypeAscii = 2;
constexpr uint16_t kTypeShort = 3;
constexpr uint16_t kTypeLong = 4;
constexpr uint16_t kTypeRational = 5;
constexpr uint16_t kTypeUndefined = 7;

} // namespace tiffbytes

#endif // PCD_TIFF_BYTES_H
