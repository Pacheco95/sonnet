#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <span>

namespace sonnet::assets {

// Every JSON and CBOR document the engine reads arrives as bytes from core::readFile or a bundle,
// and goes through these rather than nlohmann's own entry points. Given std::byte, nlohmann reads
// through std::char_traits<std::byte>, which libc++ stopped defining in LLVM 19 (std::byte is not
// a character type), so the Android NDK's libc++ fails to compile it; libstdc++ and Apple's
// libc++ still accept it. These hand nlohmann char and unsigned char instead.

// A JSON text. Invalid input gives a discarded value (`is_discarded()`), never an exception.
[[nodiscard]] nlohmann::json parseJson(std::span<const std::byte> text);

// A CBOR document, strict: trailing bytes are an error. Invalid input gives a discarded value.
[[nodiscard]] nlohmann::json parseCbor(std::span<const std::byte> bytes);

} // namespace sonnet::assets
