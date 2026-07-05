// Minimal baseline-plus TIFF 6.0 writer: little-endian, single strip,
// 8-bit RGB, Adobe Deflate ("ZIP") compression with a horizontal
// differencing predictor, and an embedded ICC profile.
//
// pcdtojpeg (the vendored decoder) only ever produced JPEG output; TIFF
// support is new code written for this project, not derived from the vendor
// library. It reuses the same embedded sRGB ICC profile as jpeg_writer.cpp
// (see icc_srgb.h for that profile's license).

#include "tiff_writer.h"
#include "icc_srgb.h"

#include <cstdio>
#include <vector>

#include <zlib.h>

namespace {

void appendU8(std::vector<uint8_t> &out, uint8_t v) { out.push_back(v); }

void appendU16LE(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void appendU32LE(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void appendBytes(std::vector<uint8_t> &out, const uint8_t *data, size_t len) {
  out.insert(out.end(), data, data + len);
}

void padToEven(std::vector<uint8_t> &out) {
  if (out.size() % 2 != 0) out.push_back(0);
}

// A single 12-byte IFD entry: tag, field type, value count, and either the
// value itself (if it fits in 4 bytes) or an offset to it (patched below).
void writeIfdEntry(std::vector<uint8_t> &out, uint16_t tag, uint16_t type,
                    uint32_t count, uint32_t valueOrOffset) {
  appendU16LE(out, tag);
  appendU16LE(out, type);
  appendU32LE(out, count);
  appendU32LE(out, valueOrOffset);
}

// TIFF field types we use.
constexpr uint16_t kTypeAscii = 2;
constexpr uint16_t kTypeShort = 3;
constexpr uint16_t kTypeLong = 4;
constexpr uint16_t kTypeRational = 5;
constexpr uint16_t kTypeUndefined = 7;

// Applies the TIFF "horizontal differencing" predictor in place, per row,
// across interleaved RGB samples (stride 3). Must run before compression;
// readers reverse it (prefix sum) after decompression, per the Predictor tag.
void applyHorizontalPredictor(std::vector<uint8_t> &rgb, int width, int height) {
  const int spp = 3;
  for (int y = 0; y < height; y++) {
    uint8_t *row = rgb.data() + static_cast<size_t>(y) * width * spp;
    for (int x = width - 1; x >= 1; x--) {
      for (int c = 0; c < spp; c++) {
        row[x * spp + c] = static_cast<uint8_t>(row[x * spp + c] - row[(x - 1) * spp + c]);
      }
    }
  }
}

bool deflateBuffer(const std::vector<uint8_t> &src, std::vector<uint8_t> &dst) {
  uLongf destLen = compressBound(static_cast<uLong>(src.size()));
  dst.resize(destLen);
  const int rc = compress2(dst.data(), &destLen, src.data(), static_cast<uLong>(src.size()),
                            Z_BEST_SPEED + 3 /* level 6: balanced ratio/speed */);
  if (rc != Z_OK) return false;
  dst.resize(destLen);
  return true;
}

} // namespace

bool writeTiffFile(const std::string &outPath, const uint8_t *rgb, int width, int height,
                    std::string &errorOut) {
  const size_t pixelBytes = static_cast<size_t>(width) * height * 3;

  std::vector<uint8_t> predicted(rgb, rgb + pixelBytes);
  applyHorizontalPredictor(predicted, width, height);

  std::vector<uint8_t> compressed;
  if (!deflateBuffer(predicted, compressed)) {
    errorOut = "zlib compression of TIFF strip data failed";
    return false;
  }
  predicted.clear();
  predicted.shrink_to_fit();

  const std::string software = "pcd-reader (browser-based PCD converter)";

  std::vector<uint8_t> out;
  out.reserve(8 + compressed.size() + kIccSRGBProfileLen + 128);

  // --- Header ---
  appendU8(out, 'I');
  appendU8(out, 'I');
  appendU16LE(out, 42);
  const size_t ifdOffsetFieldPos = out.size();
  appendU32LE(out, 0); // patched once we know the IFD's offset

  // --- Strip data ---
  const size_t stripOffset = out.size();
  appendBytes(out, compressed.data(), compressed.size());
  padToEven(out);

  // --- BitsPerSample: 8,8,8 ---
  const size_t bitsPerSampleOffset = out.size();
  appendU16LE(out, 8);
  appendU16LE(out, 8);
  appendU16LE(out, 8);
  padToEven(out);

  // --- XResolution / YResolution (nominal 300 dpi; PCD carries no reliable
  // physical scan resolution to source this from) ---
  const size_t xResOffset = out.size();
  appendU32LE(out, 300);
  appendU32LE(out, 1);
  const size_t yResOffset = out.size();
  appendU32LE(out, 300);
  appendU32LE(out, 1);

  // --- Software string ---
  const size_t softwareOffset = out.size();
  const size_t softwareLen = software.size() + 1; // ASCII count includes the NUL
  appendBytes(out, reinterpret_cast<const uint8_t *>(software.c_str()), softwareLen);
  padToEven(out);

  // --- ICC profile ---
  const size_t iccOffset = out.size();
  appendBytes(out, kIccSRGBProfile, kIccSRGBProfileLen);
  padToEven(out);

  // --- IFD ---
  const size_t ifdStart = out.size();
  {
    const uint32_t v = static_cast<uint32_t>(ifdStart);
    out[ifdOffsetFieldPos + 0] = static_cast<uint8_t>(v & 0xff);
    out[ifdOffsetFieldPos + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
    out[ifdOffsetFieldPos + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
    out[ifdOffsetFieldPos + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
  }

  constexpr uint16_t kNumEntries = 16;
  appendU16LE(out, kNumEntries);

  writeIfdEntry(out, 256, kTypeLong, 1, static_cast<uint32_t>(width));           // ImageWidth
  writeIfdEntry(out, 257, kTypeLong, 1, static_cast<uint32_t>(height));          // ImageLength
  writeIfdEntry(out, 258, kTypeShort, 3, static_cast<uint32_t>(bitsPerSampleOffset)); // BitsPerSample
  writeIfdEntry(out, 259, kTypeShort, 1, 8);                                     // Compression = Adobe Deflate ("ZIP")
  writeIfdEntry(out, 262, kTypeShort, 1, 2);                                     // PhotometricInterpretation = RGB
  writeIfdEntry(out, 273, kTypeLong, 1, static_cast<uint32_t>(stripOffset));     // StripOffsets
  writeIfdEntry(out, 277, kTypeShort, 1, 3);                                    // SamplesPerPixel
  writeIfdEntry(out, 278, kTypeLong, 1, static_cast<uint32_t>(height));          // RowsPerStrip (single strip)
  writeIfdEntry(out, 279, kTypeLong, 1, static_cast<uint32_t>(compressed.size())); // StripByteCounts
  writeIfdEntry(out, 282, kTypeRational, 1, static_cast<uint32_t>(xResOffset));  // XResolution
  writeIfdEntry(out, 283, kTypeRational, 1, static_cast<uint32_t>(yResOffset));  // YResolution
  writeIfdEntry(out, 284, kTypeShort, 1, 1);                                    // PlanarConfiguration = chunky
  writeIfdEntry(out, 296, kTypeShort, 1, 2);                                    // ResolutionUnit = inch
  writeIfdEntry(out, 305, kTypeAscii, static_cast<uint32_t>(softwareLen),
                static_cast<uint32_t>(softwareOffset));                          // Software
  writeIfdEntry(out, 317, kTypeShort, 1, 2);                                    // Predictor = horizontal differencing
  writeIfdEntry(out, 34675, kTypeUndefined, static_cast<uint32_t>(kIccSRGBProfileLen),
                static_cast<uint32_t>(iccOffset));                              // ICCProfile

  appendU32LE(out, 0); // next IFD offset: none

  FILE *f = fopen(outPath.c_str(), "wb");
  if (!f) {
    errorOut = "Could not open output file for writing: " + outPath;
    return false;
  }
  const size_t written = fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  if (written != out.size()) {
    errorOut = "Short write while saving TIFF output";
    return false;
  }
  return true;
}
