#include "image_metadata.h"

#include <cstdio>
#include <ctime>

#include "pcdDecode.h"

namespace {

std::string getMetadataValue(pcdDecode &decoder, unsigned int select) {
  char description[kPCDMaxStringLength];
  char value[kPCDMaxStringLength];
  decoder.getMetadata(select, description, value);
  return std::string(value);
}

// getMetadata() (and the resolutions it draws from) uses "-" or "Error" as
// its own placeholders for "not present on this disc" - see pcdDecode.cpp.
// It also has its own guard against exotic photofinisher character sets, but
// that guard trusts a charset byte that isn't always reliable: some discs
// (e.g. Kodak's own reference disc) leave the field as raw 0xFF filler that
// slips past it. Since every field this module reads is meant to hold plain
// text (names, dates, human-readable labels), treat anything containing a
// non-printable-ASCII byte as unavailable too, rather than pass it through
// into a TIFF/Exif tag whose type is defined as printable ASCII.
bool isUnavailable(const std::string &s) {
  if (s.empty() || s == "-" || s == "Error") return true;
  for (unsigned char c : s) {
    if (c < 0x20 || c > 0x7e) return true;
  }
  return false;
}

// EXIF/TIFF DateTime fields use "YYYY:MM:DD HH:MM:SS" (fixed 19 chars + NUL).
std::string formatExifDateTime(long unixSeconds) {
  const time_t t = static_cast<time_t>(unixSeconds);
  const struct tm *utc = gmtime(&t);
  if (!utc) return std::string();
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d:%02d:%02d %02d:%02d:%02d", utc->tm_year + 1900,
           utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min, utc->tm_sec);
  return std::string(buf);
}

void appendField(std::string &out, const char *label, const std::string &value) {
  if (isUnavailable(value)) return;
  if (!out.empty()) out += "; ";
  out += label;
  out += ": ";
  out += value;
}

} // namespace

ImageMetadata extractImageMetadata(pcdDecode &decoder) {
  ImageMetadata meta;

  // digitisationTime() doesn't itself flag "unavailable" (it echoes the raw
  // field, sentinel and all) - so gate it on getMetadata()'s check for the
  // same underlying field before trusting it.
  const std::string scanningTime = getMetadataValue(decoder, kimageScanningTime);
  if (!isUnavailable(scanningTime)) {
    meta.dateTime = formatExifDateTime(decoder.digitisationTime());
  }

  const std::string vendor = getMetadataValue(decoder, kscannerVendorIdentity);
  if (!isUnavailable(vendor)) meta.make = vendor;

  const std::string product = getMetadataValue(decoder, kscannerProductIdentity);
  if (!isUnavailable(product)) meta.model = product;

  // "Copyright restrictions not specified" is not itself a copyright claim -
  // only assert the Copyright tag when the disc actually flags restrictions.
  const std::string copyrightStatus = getMetadataValue(decoder, kcopyrightStatus);
  if (copyrightStatus.rfind("Copyright restrictions apply", 0) == 0) {
    meta.copyright = copyrightStatus;
  }

  appendField(meta.description, "Medium", getMetadataValue(decoder, kimageMedium));
  appendField(meta.description, "Film", getMetadataValue(decoder, ksbaFilm));
  appendField(meta.description, "Product", getMetadataValue(decoder, kproductType));
  appendField(meta.description, "Photofinisher", getMetadataValue(decoder, kphotoFinisherName));
  appendField(meta.description, "Equipment manufacturer",
              getMetadataValue(decoder, kpiwEquipmentManufacturer));

  return meta;
}
