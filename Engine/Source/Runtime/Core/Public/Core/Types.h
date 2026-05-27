// Types.h - integer / float aliases and engine string typedefs.
//
// FString/FStringView are placeholders aliased to std::string/std::string_view in Stage 1.
// In Stage 3, if the ABI strict guards are promoted (Architecture.md §9 Stage 3),
// these aliases may be swapped for engine-owned containers.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace pe {

using int8  = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8  = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// Platform-natural sizes (typically 64-bit on 64-bit platforms, 32-bit on 32-bit).
// Use these for indexing stdlib containers or any byte-count expressed in the
// platform's "register-sized integer" sense.
using usize = std::size_t;
using isize = std::ptrdiff_t;

using float32 = float;
using float64 = double;

using FString     = std::string;
using FStringView = std::string_view;

}  // namespace pe
