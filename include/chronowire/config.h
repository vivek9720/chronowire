#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "chronowire/result.h"

namespace chronowire {

enum class ExprKind {
  kNumber,
  kString,
  kIdentifier,
  kBinary,
};

struct Expression {
  ExprKind kind = ExprKind::kIdentifier;
  std::string text;
  double number = 0.0;
  char op = 0;
  std::unique_ptr<Expression> left;
  std::unique_ptr<Expression> right;

  Expression() = default;
  Expression(const Expression& other);
  Expression& operator=(const Expression& other);
  Expression(Expression&&) noexcept = default;
  Expression& operator=(Expression&&) noexcept = default;
};

struct ConfigEntry {
  std::string key;
  Expression value;
  std::vector<ConfigEntry> children;
  bool is_block = false;
};

struct IncludeRecord {
  std::string target;
  std::size_t source_offset = 0;
};

struct ConfigDocument {
  std::vector<ConfigEntry> entries;
  std::vector<IncludeRecord> includes;
};

Result<ConfigDocument> parseConfig(std::string_view source);
std::string expressionToString(const Expression& expression);
std::size_t countConfigNodes(const ConfigDocument& document);

}  // namespace chronowire
