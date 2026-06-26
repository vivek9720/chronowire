#include "chronowire/checksum.h"

namespace chronowire {

std::uint32_t checksum32(const std::uint8_t* data, std::size_t size) {
  std::uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
    hash = (hash << 5) | (hash >> 27);
  }
  return hash;
}

std::uint32_t checksum32(const std::vector<std::uint8_t>& data) {
  return checksum32(data.data(), data.size());
}

std::uint32_t checksum32(const std::string& text) {
  return checksum32(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

}  // namespace chronowire
