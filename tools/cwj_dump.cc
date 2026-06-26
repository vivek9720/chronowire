#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdint>
#include <vector>

#include "chronowire/analyzer.h"
#include "chronowire/result.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: cwj_dump <bundle-file>\n";
    return 2;
  }

  std::ifstream input(argv[1], std::ios::binary);
  if (!input) {
    std::cerr << "failed to open input file\n";
    return 2;
  }

  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
  auto report = chronowire::analyzeBundle(bytes.data(), bytes.size());
  if (!report) {
    std::cerr << chronowire::errorCodeName(report.error().code) << ": "
              << report.error().message << " at " << report.error().offset << "\n";
    return 1;
  }

  std::cout << chronowire::formatReport(report.value());
  return 0;
}
