#include "chronowire/config.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace chronowire {
namespace {

enum class TokenKind {
  kEnd,
  kIdentifier,
  kNumber,
  kString,
  kLBrace,
  kRBrace,
  kLParen,
  kRParen,
  kEquals,
  kSemicolon,
  kPlus,
  kMinus,
  kStar,
  kSlash,
  kDot,
  kComma,
};

struct Token {
  TokenKind kind = TokenKind::kEnd;
  std::string text;
  std::size_t offset = 0;
};

class Lexer {
 public:
  explicit Lexer(std::string_view source) : source_(source) {}

  Result<Token> next() {
    skipTrivia();
    if (offset_ >= source_.size()) {
      return Result<Token>::success(Token{TokenKind::kEnd, "", offset_});
    }

    const std::size_t start = offset_;
    const char ch = source_[offset_++];
    switch (ch) {
      case '{':
        return Result<Token>::success(Token{TokenKind::kLBrace, "{", start});
      case '}':
        return Result<Token>::success(Token{TokenKind::kRBrace, "}", start});
      case '(':
        return Result<Token>::success(Token{TokenKind::kLParen, "(", start});
      case ')':
        return Result<Token>::success(Token{TokenKind::kRParen, ")", start});
      case '=':
        return Result<Token>::success(Token{TokenKind::kEquals, "=", start});
      case ';':
        return Result<Token>::success(Token{TokenKind::kSemicolon, ";", start});
      case '+':
        return Result<Token>::success(Token{TokenKind::kPlus, "+", start});
      case '-':
        if (offset_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_]))) {
          --offset_;
          return readNumber();
        }
        return Result<Token>::success(Token{TokenKind::kMinus, "-", start});
      case '*':
        return Result<Token>::success(Token{TokenKind::kStar, "*", start});
      case '/':
        return Result<Token>::success(Token{TokenKind::kSlash, "/", start});
      case '.':
        return Result<Token>::success(Token{TokenKind::kDot, ".", start});
      case ',':
        return Result<Token>::success(Token{TokenKind::kComma, ",", start});
      case '"':
        return readString(start);
      default:
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
          --offset_;
          return readIdentifier();
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
          --offset_;
          return readNumber();
        }
        return Result<Token>::failure(
            makeError(ErrorCode::kInvalidSyntax, "unexpected character in config", start));
    }
  }

 private:
  void skipTrivia() {
    while (offset_ < source_.size()) {
      const char ch = source_[offset_];
      if (std::isspace(static_cast<unsigned char>(ch))) {
        ++offset_;
        continue;
      }
      if (ch == '#') {
        while (offset_ < source_.size() && source_[offset_] != '\n') {
          ++offset_;
        }
        continue;
      }
      if (ch == '/' && offset_ + 1 < source_.size() && source_[offset_ + 1] == '/') {
        offset_ += 2;
        while (offset_ < source_.size() && source_[offset_] != '\n') {
          ++offset_;
        }
        continue;
      }
      break;
    }
  }

  Result<Token> readIdentifier() {
    const std::size_t start = offset_;
    while (offset_ < source_.size()) {
      const char ch = source_[offset_];
      if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') {
        break;
      }
      ++offset_;
    }
    return Result<Token>::success(
        Token{TokenKind::kIdentifier, std::string(source_.substr(start, offset_ - start)), start});
  }

  Result<Token> readNumber() {
    const std::size_t start = offset_;
    if (source_[offset_] == '-') {
      ++offset_;
    }
    while (offset_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_]))) {
      ++offset_;
    }
    if (offset_ < source_.size() && source_[offset_] == '.') {
      ++offset_;
      while (offset_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[offset_]))) {
        ++offset_;
      }
    }
    return Result<Token>::success(
        Token{TokenKind::kNumber, std::string(source_.substr(start, offset_ - start)), start});
  }

  Result<Token> readString(std::size_t start) {
    std::string output;
    while (offset_ < source_.size()) {
      const char ch = source_[offset_++];
      if (ch == '"') {
        return Result<Token>::success(Token{TokenKind::kString, output, start});
      }
      if (ch == '\\') {
        if (offset_ >= source_.size()) {
          return Result<Token>::failure(
              makeError(ErrorCode::kUnexpectedEof, "unterminated escape sequence", start));
        }
        const char escaped = source_[offset_++];
        switch (escaped) {
          case 'n':
            output.push_back('\n');
            break;
          case 'r':
            output.push_back('\r');
            break;
          case 't':
            output.push_back('\t');
            break;
          case '\\':
          case '"':
            output.push_back(escaped);
            break;
          default:
            output.push_back(escaped);
            break;
        }
      } else {
        output.push_back(ch);
      }
    }
    return Result<Token>::failure(
        makeError(ErrorCode::kUnexpectedEof, "unterminated quoted string", start));
  }

  std::string_view source_;
  std::size_t offset_ = 0;
};

class Parser {
 public:
  explicit Parser(std::string_view source) : lexer_(source) {}

  Result<ConfigDocument> parse() {
    auto first = lexer_.next();
    if (!first) {
      return Result<ConfigDocument>::failure(first.error());
    }
    current_ = first.value();

    ConfigDocument document;
    while (current_.kind != TokenKind::kEnd) {
      auto entry = parseStatement(document);
      if (!entry) {
        return Result<ConfigDocument>::failure(entry.error());
      }
      if (entry.value().key.empty() && !entry.value().is_block) {
        continue;
      }
      document.entries.push_back(entry.takeValue());
    }
    return Result<ConfigDocument>::success(std::move(document));
  }

 private:
  Result<ConfigEntry> parseStatement(ConfigDocument& document) {
    if (current_.kind != TokenKind::kIdentifier) {
      return Result<ConfigEntry>::failure(
          makeError(ErrorCode::kInvalidSyntax, "expected config statement", current_.offset));
    }

    const Token key = current_;
    auto advanced = advance();
    if (!advanced) {
      return Result<ConfigEntry>::failure(advanced.error());
    }

    if (key.text == "include") {
      if (current_.kind != TokenKind::kString) {
        return Result<ConfigEntry>::failure(
            makeError(ErrorCode::kInvalidSyntax, "include expects quoted target",
                      current_.offset));
      }
      document.includes.push_back(IncludeRecord{current_.text, current_.offset});
      auto next = advance();
      if (!next) {
        return Result<ConfigEntry>::failure(next.error());
      }
      auto semi = expect(TokenKind::kSemicolon, "include expects ';'");
      if (!semi) {
        return Result<ConfigEntry>::failure(semi.error());
      }
      return Result<ConfigEntry>::success(ConfigEntry{});
    }

    std::string full_key = key.text;
    while (current_.kind == TokenKind::kDot) {
      auto dot = advance();
      if (!dot) {
        return Result<ConfigEntry>::failure(dot.error());
      }
      if (current_.kind != TokenKind::kIdentifier) {
        return Result<ConfigEntry>::failure(
            makeError(ErrorCode::kInvalidSyntax, "expected identifier after '.'",
                      current_.offset));
      }
      full_key += ".";
      full_key += current_.text;
      auto name = advance();
      if (!name) {
        return Result<ConfigEntry>::failure(name.error());
      }
    }

    if (current_.kind == TokenKind::kLBrace) {
      ConfigEntry block;
      block.key = std::move(full_key);
      block.is_block = true;
      auto open = advance();
      if (!open) {
        return Result<ConfigEntry>::failure(open.error());
      }
      while (current_.kind != TokenKind::kRBrace) {
        if (current_.kind == TokenKind::kEnd) {
          return Result<ConfigEntry>::failure(
              makeError(ErrorCode::kUnexpectedEof, "unterminated config block", key.offset));
        }
        auto child = parseStatement(document);
        if (!child) {
          return Result<ConfigEntry>::failure(child.error());
        }
        if (!child.value().key.empty() || child.value().is_block) {
          block.children.push_back(child.takeValue());
        }
      }
      auto close = advance();
      if (!close) {
        return Result<ConfigEntry>::failure(close.error());
      }
      return Result<ConfigEntry>::success(std::move(block));
    }

    auto eq = expect(TokenKind::kEquals, "assignment expects '='");
    if (!eq) {
      return Result<ConfigEntry>::failure(eq.error());
    }
    auto expression = parseExpression();
    if (!expression) {
      return Result<ConfigEntry>::failure(expression.error());
    }
    auto semi = expect(TokenKind::kSemicolon, "assignment expects ';'");
    if (!semi) {
      return Result<ConfigEntry>::failure(semi.error());
    }
    ConfigEntry entry;
    entry.key = std::move(full_key);
    entry.value = expression.takeValue();
    return Result<ConfigEntry>::success(std::move(entry));
  }

  Result<Expression> parseExpression() { return parseAdditive(); }

  Result<Expression> parseAdditive() {
    auto left = parseMultiplicative();
    if (!left) {
      return left;
    }
    while (current_.kind == TokenKind::kPlus || current_.kind == TokenKind::kMinus) {
      const char op = current_.kind == TokenKind::kPlus ? '+' : '-';
      auto next = advance();
      if (!next) {
        return Result<Expression>::failure(next.error());
      }
      auto right = parseMultiplicative();
      if (!right) {
        return right;
      }
      Expression combined;
      combined.kind = ExprKind::kBinary;
      combined.op = op;
      combined.left.reset(new Expression(left.takeValue()));
      combined.right.reset(new Expression(right.takeValue()));
      combined.text = expressionToString(*combined.left) + op + expressionToString(*combined.right);
      left = Result<Expression>::success(std::move(combined));
    }
    return left;
  }

  Result<Expression> parseMultiplicative() {
    auto left = parsePrimary();
    if (!left) {
      return left;
    }
    while (current_.kind == TokenKind::kStar || current_.kind == TokenKind::kSlash) {
      const char op = current_.kind == TokenKind::kStar ? '*' : '/';
      auto next = advance();
      if (!next) {
        return Result<Expression>::failure(next.error());
      }
      auto right = parsePrimary();
      if (!right) {
        return right;
      }
      Expression combined;
      combined.kind = ExprKind::kBinary;
      combined.op = op;
      combined.left.reset(new Expression(left.takeValue()));
      combined.right.reset(new Expression(right.takeValue()));
      combined.text = expressionToString(*combined.left) + op + expressionToString(*combined.right);
      left = Result<Expression>::success(std::move(combined));
    }
    return left;
  }

  Result<Expression> parsePrimary() {
    if (current_.kind == TokenKind::kNumber) {
      Expression expression;
      expression.kind = ExprKind::kNumber;
      expression.text = current_.text;
      expression.number = std::strtod(current_.text.c_str(), nullptr);
      auto next = advance();
      if (!next) {
        return Result<Expression>::failure(next.error());
      }
      return Result<Expression>::success(std::move(expression));
    }
    if (current_.kind == TokenKind::kString) {
      Expression expression;
      expression.kind = ExprKind::kString;
      expression.text = current_.text;
      auto next = advance();
      if (!next) {
        return Result<Expression>::failure(next.error());
      }
      return Result<Expression>::success(std::move(expression));
    }
    if (current_.kind == TokenKind::kIdentifier) {
      Expression expression;
      expression.kind = ExprKind::kIdentifier;
      expression.text = current_.text;
      auto next = advance();
      if (!next) {
        return Result<Expression>::failure(next.error());
      }
      return Result<Expression>::success(std::move(expression));
    }
    if (current_.kind == TokenKind::kLParen) {
      auto open = advance();
      if (!open) {
        return Result<Expression>::failure(open.error());
      }
      auto expression = parseExpression();
      if (!expression) {
        return expression;
      }
      auto close = expect(TokenKind::kRParen, "expression expects ')'");
      if (!close) {
        return Result<Expression>::failure(close.error());
      }
      return expression;
    }
    return Result<Expression>::failure(
        makeError(ErrorCode::kInvalidSyntax, "expected expression", current_.offset));
  }

  Result<void> expect(TokenKind kind, const char* message) {
    if (current_.kind != kind) {
      return Result<void>::failure(makeError(ErrorCode::kInvalidSyntax, message, current_.offset));
    }
    return advance();
  }

  Result<void> advance() {
    auto next = lexer_.next();
    if (!next) {
      return Result<void>::failure(next.error());
    }
    current_ = next.value();
    return Result<void>::success();
  }

  Lexer lexer_;
  Token current_;
};

std::size_t countEntryNodes(const ConfigEntry& entry) {
  std::size_t total = 1;
  for (const auto& child : entry.children) {
    total += countEntryNodes(child);
  }
  return total;
}

}  // namespace

Expression::Expression(const Expression& other)
    : kind(other.kind), text(other.text), number(other.number), op(other.op) {
  if (other.left) {
    left.reset(new Expression(*other.left));
  }
  if (other.right) {
    right.reset(new Expression(*other.right));
  }
}

Expression& Expression::operator=(const Expression& other) {
  if (this == &other) {
    return *this;
  }
  kind = other.kind;
  text = other.text;
  number = other.number;
  op = other.op;
  left.reset(other.left ? new Expression(*other.left) : nullptr);
  right.reset(other.right ? new Expression(*other.right) : nullptr);
  return *this;
}

Result<ConfigDocument> parseConfig(std::string_view source) {
  Parser parser(source);
  return parser.parse();
}

std::string expressionToString(const Expression& expression) {
  switch (expression.kind) {
    case ExprKind::kNumber:
      return expression.text;
    case ExprKind::kString:
      return "\"" + expression.text + "\"";
    case ExprKind::kIdentifier:
      return expression.text;
    case ExprKind::kBinary:
      if (expression.left && expression.right) {
        return "(" + expressionToString(*expression.left) + expression.op +
               expressionToString(*expression.right) + ")";
      }
      return expression.text;
  }
  return expression.text;
}

std::size_t countConfigNodes(const ConfigDocument& document) {
  std::size_t total = document.includes.size();
  for (const auto& entry : document.entries) {
    total += countEntryNodes(entry);
  }
  return total;
}

}  // namespace chronowire
