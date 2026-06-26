#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chronowire {

std::uint32_t checksum32(const std::uint8_t* data, std::size_t size);
std::uint32_t checksum32(const std::vector<std::uint8_t>& data);
std::uint32_t checksum32(const std::string& text);

}  // namespace chronowire
