#include "chronowire/reader.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace chronowire {

ByteReader::ByteReader(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size) {}

ByteReader::ByteReader(const std::vector<std::uint8_t>& data)
    : data_(data.data()), size_(data.size()) {}

std::size_t ByteReader::remaining() const {
  return offset_ <= size_ ? size_ - offset_ : 0;
}

bool ByteReader::startsWith(const char* magic, std::size_t size) const {
  return remaining() >= size &&
         std::memcmp(data_ + offset_, magic, size) == 0;
}

Result<void> ByteReader::require(std::size_t count) const {
  if (count > remaining()) {
    return Result<void>::failure(
        makeError(ErrorCode::kUnexpectedEof, "not enough bytes", offset_));
  }
  return Result<void>::success();
}

Result<std::uint8_t> ByteReader::readU8() {
  auto ready = require(1);
  if (!ready) {
    return Result<std::uint8_t>::failure(ready.error());
  }
  return Result<std::uint8_t>::success(data_[offset_++]);
}

Result<std::uint16_t> ByteReader::readLe16() {
  auto ready = require(2);
  if (!ready) {
    return Result<std::uint16_t>::failure(ready.error());
  }
  std::uint16_t value = static_cast<std::uint16_t>(data_[offset_]) |
                        (static_cast<std::uint16_t>(data_[offset_ + 1]) << 8);
  offset_ += 2;
  return Result<std::uint16_t>::success(value);
}

Result<std::uint32_t> ByteReader::readLe32() {
  auto ready = require(4);
  if (!ready) {
    return Result<std::uint32_t>::failure(ready.error());
  }
  std::uint32_t value = static_cast<std::uint32_t>(data_[offset_]) |
                        (static_cast<std::uint32_t>(data_[offset_ + 1]) << 8) |
                        (static_cast<std::uint32_t>(data_[offset_ + 2]) << 16) |
                        (static_cast<std::uint32_t>(data_[offset_ + 3]) << 24);
  offset_ += 4;
  return Result<std::uint32_t>::success(value);
}

Result<std::uint64_t> ByteReader::readLe64() {
  auto ready = require(8);
  if (!ready) {
    return Result<std::uint64_t>::failure(ready.error());
  }
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(data_[offset_ + i]) << (8 * i);
  }
  offset_ += 8;
  return Result<std::uint64_t>::success(value);
}

Result<std::uint64_t> ByteReader::readVarint(std::uint64_t max_value) {
  std::uint64_t value = 0;
  unsigned shift = 0;
  const std::size_t start = offset_;

  for (int byte_count = 0; byte_count < 10; ++byte_count) {
    auto next = readU8();
    if (!next) {
      return Result<std::uint64_t>::failure(next.error());
    }

    const std::uint8_t byte = next.value();
    if (shift >= 64 && (byte & 0x7f) != 0) {
      return Result<std::uint64_t>::failure(
          makeError(ErrorCode::kMalformedVarint, "varint overflows uint64", start));
    }
    value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      if (value > max_value) {
        return Result<std::uint64_t>::failure(
            makeError(ErrorCode::kLimitExceeded, "varint exceeds configured limit", start));
      }
      return Result<std::uint64_t>::success(value);
    }
    shift += 7;
  }

  return Result<std::uint64_t>::failure(
      makeError(ErrorCode::kMalformedVarint, "varint uses too many bytes", start));
}

Result<std::vector<std::uint8_t>> ByteReader::readBytes(std::size_t count) {
  auto ready = require(count);
  if (!ready) {
    return Result<std::vector<std::uint8_t>>::failure(ready.error());
  }
  std::vector<std::uint8_t> bytes(data_ + offset_, data_ + offset_ + count);
  offset_ += count;
  return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<std::string> ByteReader::readString(std::size_t count) {
  auto bytes = readBytes(count);
  if (!bytes) {
    return Result<std::string>::failure(bytes.error());
  }
  return Result<std::string>::success(stringFromBytes(bytes.value()));
}

Result<void> ByteReader::skip(std::size_t count) {
  auto ready = require(count);
  if (!ready) {
    return ready;
  }
  offset_ += count;
  return Result<void>::success();
}

std::vector<std::uint8_t> bytesFromString(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string stringFromBytes(const std::vector<std::uint8_t>& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

}  // namespace chronowire
