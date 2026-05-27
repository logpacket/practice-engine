// PlatformKey.h - backend-neutral key enum.
//
// Stage 1 (§6.d): only the two values that the §6.d smoke test exercises
// (Unknown / Escape). Stage 3 input subsystem will expand to the full ~120-key
// set when there is an actual gameplay use case. YAGNI per Architecture.md §3.8.3.

#pragma once

#include <Core/Types.h>

namespace pe {

enum class EKey : uint16 {
    Unknown = 0,
    Escape  = 1,
};

struct FKeyEvent {
    EKey  key;
    bool  pressed;
    uint8 modifiers;  // reserved; Stage 3 fills bitmask
};

}  // namespace pe
