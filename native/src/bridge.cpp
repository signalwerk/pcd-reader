// WebAssembly entry point. Bridges the vendored pcdDecode PhotoCD decoder
// (vendor/pcdtojpeg/src/pcdDecode.{h,cpp}) to our own JPEG/TIFF encoders.
//
// The calling convention favors Emscripten's virtual filesystem (MEMFS):
// the JS side writes the uploaded .pcd bytes to `inPath`, calls pcd_convert,
// then reads the encoded image back from `outPath`. This mirrors how the
// original pcdtojpeg CLI works (it's a file-in/file-out tool) and avoids
// juggling raw pointers for potentially 50MB+ decoded buffers across the
// JS/WASM boundary.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <emscripten/emscripten.h>

#include "pcdDecode.h"

#include "jpeg_writer.h"
#include "tiff_writer.h"

namespace {

enum PcdOutputFormat {
  kFormatJpeg = 0,
  kFormatTiff = 1,
};

void copyToBuffer(const std::string &s, char *buf, int bufLen) {
  if (!buf || bufLen <= 0) return;
  const size_t n = std::min(s.size(), static_cast<size_t>(bufLen - 1));
  std::memcpy(buf, s.data(), n);
  buf[n] = '\0';
}

} // namespace

extern "C" {

// Returns 0 on success, -1 on decode failure, -2 on encode failure,
// -3 on invalid arguments. messageBuf always receives a human-readable
// string: the decoder's warning/error text on decode paths, or the encoder's
// error text if encoding fails.
EMSCRIPTEN_KEEPALIVE
int pcd_convert(const char *inPath, const char *outPath, int format, int resolution,
                 int whiteBalance, int monochrome, int jpegQuality, int *outWidth,
                 int *outHeight, char *messageBuf, int messageBufLen) {
  if (!inPath || !outPath || !messageBuf) return -3;
  if (resolution < 0) resolution = 0;
  if (resolution > k64Base) resolution = k64Base;

  // 64Base is stored as a separate Overview Pac (IPE) file alongside the base
  // Image Pac on the original disc; this app only ever has the single .pcd
  // file the user selected, so we never have that companion file to pass in.
  // Clamp the request here rather than asking pcdDecode::parseFile for
  // k64Base with ipe_file=NULL: its internal parseICFile() indexes into the
  // (absent) filename unconditionally whenever the requested resolution
  // reaches k64Base, which is only safe because we sidestep it entirely.
  const bool wanted64Base = resolution >= k64Base;
  if (wanted64Base) resolution = k16Base;

  pcdDecode decoder;
  decoder.setInterpolation(kUpResLumaIterpolate); // falls back automatically in the GPL build
  decoder.setColorSpace(kPCDsRGBColorSpace);
  decoder.setWhiteBalance(whiteBalance == 1 ? kPCDD50White : kPCDD65White);
  decoder.setIsMonoChrome(monochrome != 0);

  const bool parsed = decoder.parseFile(inPath, nullptr, static_cast<unsigned int>(resolution));
  const std::string decoderMessage = decoder.getErrorString();
  if (wanted64Base && decoderMessage.empty()) {
    copyToBuffer(
        "64Base (Photo CD's highest resolution) needs a separate Overview Pac file that a "
        "standalone .pcd doesn't include; decoded at 16Base instead.",
        messageBuf, messageBufLen);
  } else {
    copyToBuffer(decoderMessage, messageBuf, messageBufLen);
  }
  if (!parsed) {
    return -1;
  }

  decoder.postParse();

  const size_t width = decoder.getWidth();
  const size_t height = decoder.getHeight();
  if (width == 0 || height == 0) {
    copyToBuffer("Decoded image has zero dimensions", messageBuf, messageBufLen);
    return -1;
  }

  std::string rgb;
  rgb.resize(width * height * 3);
  uint8_t *pixels = reinterpret_cast<uint8_t *>(&rgb[0]);
  // Interleaved RGB: offset each channel pointer by 1 byte with stride 3.
  decoder.populateUInt8Buffers(pixels, pixels + 1, pixels + 2, nullptr, 3);

  if (outWidth) *outWidth = static_cast<int>(width);
  if (outHeight) *outHeight = static_cast<int>(height);

  std::string encodeError;
  bool encodeOk = false;
  if (format == kFormatTiff) {
    encodeOk = writeTiffFile(outPath, pixels, static_cast<int>(width), static_cast<int>(height),
                              encodeError);
  } else {
    const int quality = jpegQuality > 0 && jpegQuality <= 100 ? jpegQuality : 92;
    encodeOk = writeJpegFile(outPath, quality, pixels, static_cast<int>(width),
                              static_cast<int>(height), encodeError);
  }

  if (!encodeOk) {
    copyToBuffer(encodeError, messageBuf, messageBufLen);
    return -2;
  }

  return 0;
}

} // extern "C"
