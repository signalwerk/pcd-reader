// JPEG encoding with embedded ICC profile.
//
// The compression setup and the technique of injecting an ICC profile via a
// raw APP2 marker (instead of pulling in a full CMM) are adapted from
// pcdtojpeg's main.cpp (vendor/pcdtojpeg/src/main.cpp, write_JPEG_file),
// (C) Copyright 2009-2013 Sandy McGuffog and Contributors, GPLv2+. This file
// extends that approach to (a) write into an arbitrary output path from a
// standalone encoder (not the vendor CLI's main()), and (b) split the profile
// across multiple APP2 markers per the ICC spec so profiles larger than one
// marker's payload are still valid.
//
// The embedded profile itself, `kIccSRGBProfile` (icc_srgb.h), is
// (c) Copyright 1998 Hewlett-Packard Company, and is used here under HP's
// sRGB Profile Licensing Agreement:
//
//   To anyone who acknowledges that the file "sRGB Color Space Profile.icm"
//   is provided "AS IS" WITH NO EXPRESS OR IMPLIED WARRANTY: permission to
//   use, copy and distribute this file for any purpose is hereby granted
//   without fee, provided that the file is not changed including the HP
//   copyright notice tag, and that the name of Hewlett-Packard Company not
//   be used in advertising or publicity pertaining to distribution of the
//   software without specific, written prior permission. Hewlett-Packard
//   Company makes no representations about the suitability of this software
//   for any purpose.

#include "jpeg_writer.h"
#include "icc_srgb.h"
#include "tiff_bytes.h"

#include <algorithm>
#include <cstdio>
#include <vector>

extern "C" {
#include <jpeglib.h>
}

using namespace tiffbytes;

namespace {

// Maximum ICC payload bytes per APP2 marker: a JFIF marker's 2-byte length
// field (which includes itself) tops out at 65535, so the data portion is at
// most 65533 bytes; 14 of those are the "ICC_PROFILE\0" + sequence header.
constexpr size_t kMaxICCBytesPerMarker = 65533 - 14;

// Builds an Exif APP1 segment: "Exif\0\0" followed by a miniature TIFF file
// (its own header + IFD0) per the Exif spec, so offsets inside it are
// relative to the first byte after "Exif\0\0", not to the JPEG file. Returns
// an empty vector if there's nothing in `metadata` to write.
std::vector<uint8_t> buildExifSegment(const ImageMetadata &metadata) {
  std::vector<uint8_t> out;
  if (metadata.description.empty() && metadata.make.empty() && metadata.model.empty() &&
      metadata.dateTime.empty() && metadata.copyright.empty()) {
    return out;
  }

  static const char kExifHeader[6] = {'E', 'x', 'i', 'f', '\0', '\0'};
  appendBytes(out, reinterpret_cast<const uint8_t *>(kExifHeader), sizeof(kExifHeader));
  const size_t tiffStart = out.size();

  appendU8(out, 'I');
  appendU8(out, 'I');
  appendU16LE(out, 42);
  const size_t ifdOffsetFieldPos = out.size();
  appendU32LE(out, 0); // patched below once we know the IFD's offset

  size_t descOffset = 0, descLen = 0;
  size_t makeOffset = 0, makeLen = 0;
  size_t modelOffset = 0, modelLen = 0;
  size_t dateOffset = 0, dateLen = 0;
  size_t copyrightOffset = 0, copyrightLen = 0;

  auto writeAsciiField = [&out, tiffStart](const std::string &s, size_t &offset, size_t &len) {
    offset = out.size() - tiffStart;
    len = s.size() + 1; // ASCII count includes the NUL
    appendBytes(out, reinterpret_cast<const uint8_t *>(s.c_str()), len);
    padToEven(out);
  };

  if (!metadata.description.empty()) writeAsciiField(metadata.description, descOffset, descLen);
  if (!metadata.make.empty()) writeAsciiField(metadata.make, makeOffset, makeLen);
  if (!metadata.model.empty()) writeAsciiField(metadata.model, modelOffset, modelLen);
  if (!metadata.dateTime.empty()) writeAsciiField(metadata.dateTime, dateOffset, dateLen);
  if (!metadata.copyright.empty()) writeAsciiField(metadata.copyright, copyrightOffset, copyrightLen);

  const size_t ifdStart = out.size();
  {
    const uint32_t v = static_cast<uint32_t>(ifdStart - tiffStart);
    out[ifdOffsetFieldPos + 0] = static_cast<uint8_t>(v & 0xff);
    out[ifdOffsetFieldPos + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
    out[ifdOffsetFieldPos + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
    out[ifdOffsetFieldPos + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
  }

  const uint16_t numEntries = (!metadata.description.empty()) + (!metadata.make.empty()) +
                               (!metadata.model.empty()) + (!metadata.dateTime.empty()) +
                               (!metadata.copyright.empty());
  appendU16LE(out, numEntries);

  if (!metadata.description.empty())
    writeIfdEntry(out, 270, kTypeAscii, static_cast<uint32_t>(descLen),
                  static_cast<uint32_t>(descOffset)); // ImageDescription
  if (!metadata.make.empty())
    writeIfdEntry(out, 271, kTypeAscii, static_cast<uint32_t>(makeLen),
                  static_cast<uint32_t>(makeOffset)); // Make
  if (!metadata.model.empty())
    writeIfdEntry(out, 272, kTypeAscii, static_cast<uint32_t>(modelLen),
                  static_cast<uint32_t>(modelOffset)); // Model
  if (!metadata.dateTime.empty())
    writeIfdEntry(out, 306, kTypeAscii, static_cast<uint32_t>(dateLen),
                  static_cast<uint32_t>(dateOffset)); // DateTime
  if (!metadata.copyright.empty())
    writeIfdEntry(out, 33432, kTypeAscii, static_cast<uint32_t>(copyrightLen),
                  static_cast<uint32_t>(copyrightOffset)); // Copyright

  appendU32LE(out, 0); // next IFD offset: none

  return out;
}

void writeIccProfileMarkers(jpeg_compress_struct *cinfo, const uint8_t *profile,
                             size_t profileLen) {
  const int numMarkers =
      static_cast<int>((profileLen + kMaxICCBytesPerMarker - 1) / kMaxICCBytesPerMarker);
  if (numMarkers == 0) return;

  std::vector<uint8_t> chunk;
  size_t offset = 0;
  for (int seq = 1; seq <= numMarkers; seq++) {
    const size_t thisLen = std::min(kMaxICCBytesPerMarker, profileLen - offset);
    chunk.clear();
    chunk.reserve(14 + thisLen);
    static const char kTag[12] = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', '\0'};
    chunk.insert(chunk.end(), kTag, kTag + 12);
    chunk.push_back(static_cast<uint8_t>(seq));
    chunk.push_back(static_cast<uint8_t>(numMarkers));
    chunk.insert(chunk.end(), profile + offset, profile + offset + thisLen);

    jpeg_write_marker(cinfo, JPEG_APP0 + 2, chunk.data(), static_cast<unsigned int>(chunk.size()));
    offset += thisLen;
  }
}

} // namespace

bool writeJpegFile(const std::string &outPath, int quality, const uint8_t *rgb, int width,
                    int height, const ImageMetadata &metadata, std::string &errorOut) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  FILE *outfile = fopen(outPath.c_str(), "wb");
  if (!outfile) {
    errorOut = "Could not open output file for writing: " + outPath;
    return false;
  }

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, outfile);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  // Keep chroma at full resolution (4:4:4) - PCD images are already
  // color-managed and modest in size; avoid introducing chroma subsampling
  // artifacts on top of the PhotoCD decode.
  cinfo.comp_info[0].h_samp_factor = 1;
  cinfo.comp_info[0].v_samp_factor = 1;
  cinfo.comp_info[1].h_samp_factor = 1;
  cinfo.comp_info[1].v_samp_factor = 1;
  cinfo.comp_info[2].h_samp_factor = 1;
  cinfo.comp_info[2].v_samp_factor = 1;

  jpeg_set_quality(&cinfo, quality, TRUE);

  jpeg_start_compress(&cinfo, TRUE);

  const std::vector<uint8_t> exifSegment = buildExifSegment(metadata);
  if (!exifSegment.empty()) {
    jpeg_write_marker(&cinfo, JPEG_APP0 + 1, exifSegment.data(),
                       static_cast<unsigned int>(exifSegment.size()));
  }

  writeIccProfileMarkers(&cinfo, kIccSRGBProfile, kIccSRGBProfileLen);

  const int rowStride = width * 3;
  JSAMPROW rowPointer[1];
  while (cinfo.next_scanline < cinfo.image_height) {
    rowPointer[0] = const_cast<JSAMPROW>(&rgb[cinfo.next_scanline * rowStride]);
    jpeg_write_scanlines(&cinfo, rowPointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  fclose(outfile);
  jpeg_destroy_compress(&cinfo);

  return true;
}
