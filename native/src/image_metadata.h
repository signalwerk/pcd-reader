#ifndef PCD_IMAGE_METADATA_H
#define PCD_IMAGE_METADATA_H

#include <string>

class pcdDecode;

// PCD-sourced metadata mapped onto the handful of standard TIFF/Exif IFD0
// tags it fits cleanly. The decoder exposes a much larger set of fields (see
// PCDMetaDataDictionary in pcdDecode.h) - anything without a natural home in
// a standard tag is folded into `description` as free text instead of being
// dropped.
//
// Fields are empty when the source .pcd doesn't carry that piece of
// information; writers should omit the corresponding tag entirely in that
// case rather than write a placeholder value.
struct ImageMetadata {
  std::string dateTime;    // "YYYY:MM:DD HH:MM:SS", from the disc's scan time
  std::string make;        // scanner vendor
  std::string model;       // scanner product
  std::string copyright;   // only set when the disc flags copyright restrictions
  std::string description; // free-text: medium, film type, photofinisher, etc.
};

// decoder must have had parseFile() (successfully) and postParse() called.
ImageMetadata extractImageMetadata(pcdDecode &decoder);

#endif // PCD_IMAGE_METADATA_H
