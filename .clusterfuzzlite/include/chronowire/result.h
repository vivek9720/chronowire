#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace chronowire {

enum class ErrorCode {
  kUnexpectedEof,
  kInvalidMagic,
  kUnsupportedVersion,
  kMalformedVarint,
  kLimitExceeded,
  kChecksumMismatch,
  kInvalidSyntax,
  kInvalidState,
  kUnknownSection,
};

struct Error {
  ErrorCode code = ErrorCode::kInvalidState;
  std::string message;
  std::size_t offset = 0;
};

inline Error makeError(ErrorCode code, std::string message, std::size_t offset = 0) {
  return Error{code, std::move(message), offset};
}

template <typename T>
class Result {
 public:
  static Result success(T value) {
    Result result;
    result.ok_ = true;
    result.value_ = std::move(value);
    return result;
  }

  static Result failure(Error error) {
    Result result;
    result.ok_ = false;
    result.error_ = std::move(error);
    return result;
  }

  bool ok() const { return ok_; }
  explicit operator bool() const { return ok_; }

  const T& value() const { return value_; }
  T& value() { return value_; }
  T takeValue() { return std::move(value_); }

  const Error& error() const { return error_; }

 private:
  bool ok_ = false;
  T value_{};
  Error error_{};
};

template <>
class Result<void> {
 public:
  static Result success() {
    Result result;
    result.ok_ = true;
    return result;
  }

  static Result failure(Error error) {
    Result result;
    result.ok_ = false;
    result.error_ = std::move(error);
    return result;
  }

  bool ok() const { return ok_; }
  explicit operator bool() const { return ok_; }
  const Error& error() const { return error_; }

 private:
  bool ok_ = false;
  Error error_{};
};

const char* errorCodeName(ErrorCode code);

}  // namespace chronowire
