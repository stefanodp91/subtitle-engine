#include "ebml.hpp"

namespace subtitle_engine {

EbmlReader::EbmlReader(FILE* file) : file_(file) {
  fseeko(file_, 0, SEEK_END);
  size_ = ftello(file_);
  fseeko(file_, 0, SEEK_SET);
}

bool EbmlReader::seek(int64_t to) {
  if (to < 0 || to > size_ || fseeko(file_, to, SEEK_SET) != 0) return false;
  pos_ = to;
  return true;
}

bool EbmlReader::readByte(uint8_t* b) {
  int c = fgetc(file_);
  if (c == EOF) return false;
  *b = static_cast<uint8_t>(c);
  ++pos_;
  return true;
}

bool EbmlReader::readId(uint32_t* id) {
  uint8_t first;
  if (!readByte(&first) || first == 0) return false;
  int length = 1;
  while (!(first & (0x80 >> (length - 1)))) ++length;
  if (length > 4) return false;
  uint32_t value = first;
  for (int i = 1; i < length; ++i) {
    uint8_t b;
    if (!readByte(&b)) return false;
    value = (value << 8) | b;
  }
  *id = value;
  return true;
}

bool EbmlReader::readSize(uint64_t* size, bool* unknown) {
  uint8_t first;
  if (!readByte(&first) || first == 0) return false;
  int length = 1;
  while (!(first & (0x80 >> (length - 1)))) ++length;
  uint64_t value = first & (0xFF >> length);
  bool allOnes = value == (0xFFu >> length);
  for (int i = 1; i < length; ++i) {
    uint8_t b;
    if (!readByte(&b)) return false;
    value = (value << 8) | b;
    allOnes = allOnes && b == 0xFF;
  }
  *unknown = allOnes;
  *size = value;
  return true;
}

bool EbmlReader::readBytes(uint64_t count, std::string* out) {
  if (pos_ + static_cast<int64_t>(count) > size_) return false;
  out->resize(count);
  if (count && fread(&(*out)[0], 1, count, file_) != count) return false;
  pos_ += static_cast<int64_t>(count);
  return true;
}

bool EbmlReader::readUInt(uint64_t size, uint64_t* value) {
  if (size > 8) return false;
  std::string bytes;
  if (!readBytes(size, &bytes)) return false;
  uint64_t v = 0;
  for (unsigned char c : bytes) v = (v << 8) | c;
  *value = v;
  return true;
}

}  // namespace subtitle_engine
