#include <cstddef>
#include <cstdint>
#include <string>

#include "chronowire/config.h"
#include "chronowire/schema.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr && size != 0) {
    return 0;
  }

  if (size > 131072) {
    return 0;
  }

  std::string text;
  if (data != nullptr && size != 0) {
    text.assign(reinterpret_cast<const char*>(data), size);
  }
  auto parsed = chronowire::parseConfig(text);
  if (parsed) {
    volatile std::size_t nodes = chronowire::countConfigNodes(parsed.value());
    (void)nodes;
    for (const auto& entry : parsed.value().entries) {
      volatile std::size_t length = chronowire::expressionToString(entry.value).size() +
                                    chronowire::expressionNodeCount(entry.value);
      (void)length;
    }
  }

  return 0;
}
