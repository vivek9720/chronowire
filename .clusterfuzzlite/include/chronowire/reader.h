#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chronowire/result.h"

namespace chronowire {

class ByteReader {
 public:
  ByteReader(const std::uint8_t* data, std::size_t size);
  explicit ByteReader(const std::vector<std::uint8_t>& data);

  std::size_t offset() const { return offset_; }
  std::size_t remaining() const;
  bool empty() const { return remaining() == 0; }
  bool startsWith(const char* magic, std::size_t size) const;

  Result<std::uint8_t> readU8();
  Result<std::uint16_t> readLe16();
  Result<std::uint32_t> readLe32();
  Result<std::uint64_t> readLe64();
  Result<std::uint64_t> readVarint(std::uint64_t max_value);
  Result<std::vector<std::uint8_t>> readBytes(std::size_t count);
  Result<std::string> readString(std::size_t count);
  Result<void> skip(std::size_t count);

 private:
  Result<void> require(std::size_t count) const;

  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t offset_ = 0;
};

std::vector<std::uint8_t> bytesFromString(const std::string& text);
std::string stringFromBytes(const std::vector<std::uint8_t>& bytes);

}  // namespace chronowire
