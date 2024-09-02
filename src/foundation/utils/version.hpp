// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/optional.hpp"
#include "foundation/utils/format.hpp"

// WARNING: the packed form of a SemanticVersion does not contain the beta version.
PUBLIC constexpr u32 PackVersionIntoU32(u16 maj, u8 min, u8 patch) {
    return ((u32)maj << 16) | ((u32)min << 8) | ((u32)patch);
}
PUBLIC constexpr u16 ExtractMajorFromPackedVersion(u32 packed) { return (packed & 0xffff0000) >> 16; }
PUBLIC constexpr u8 ExtractMinorFromPackedVersion(u32 packed) { return (packed & 0x0000ff00) >> 8; }
PUBLIC constexpr u8 ExtractPatchFromPackedVersion(u32 packed) { return packed & 0x000000ff; }

// WARNING: doesn't exactly follow the semantic versioning spec
struct Version {
    constexpr Version() = default;
    constexpr Version(u8 mj, u8 mn, u8 p, u8 b = {}) : major(mj), minor(mn), patch(p), beta(b) {}
    constexpr Version(u32 packed_version) {
        major = CheckedCast<u8>(ExtractMajorFromPackedVersion(packed_version));
        minor = ExtractMinorFromPackedVersion(packed_version);
        patch = ExtractPatchFromPackedVersion(packed_version);
    }

    u8& operator[](usize const index) const {
        ASSERT(index < k_num_version_subdivisions);
        return ((u8*)&major)[index];
    }

    DynamicArray<char> ToString(Allocator& a = Malloc::Instance()) const {
        DynamicArray<char> result {a};
        fmt::Assign(result, "{}.{}.{}", major, minor, patch);
        if (beta) fmt::Append(result, "-Beta{}", beta);
        return result;
    }

    bool IsEmpty() const { return !major && !minor && !patch; }
    u32 Packed() const { return PackVersionIntoU32(major, minor, patch); }

    friend bool operator==(Version const& a, Version const& b) {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch && a.beta == b.beta;
    }
    friend bool operator<(Version const& a, Version const& b) {
        if (a.major < b.major) return true;
        if (a.major > b.major) return false;
        if (a.minor < b.minor) return true;
        if (a.minor > b.minor) return false;
        if (a.patch < b.patch) return true;
        if (a.patch > b.patch) return false;
        if (a.beta && !b.beta) return true;
        if (a.beta && b.beta) return a.beta < b.beta;
        return false;
    }
    friend bool operator!=(Version const& lhs, Version const& rhs) { return !(lhs == rhs); }
    friend bool operator>(Version const& lhs, Version const& rhs) { return rhs < lhs; }
    friend bool operator<=(Version const& lhs, Version const& rhs) { return !(lhs > rhs); }
    friend bool operator>=(Version const& lhs, Version const& rhs) { return !(lhs < rhs); }

    static constexpr usize k_num_version_subdivisions = 3;
    u8 major {}, minor {}, patch {};
    u8 beta {};
};

PUBLIC Optional<Version> ParseVersionString(String str) {
    if (Count(str, '.') != 2) return {};

    auto const first_dot = Find(str, '.').Value();
    auto const second_dot = Find(str, '.', first_dot + 1).Value();
    if (second_dot == str.size - 1) return {};

    auto major_text = str.SubSpan(0, first_dot);
    auto minor_text = str.SubSpan(first_dot + 1, second_dot - first_dot - 1);
    auto patch_text = str.SubSpan(second_dot + 1);
    Optional<String> beta_text {};
    constexpr String k_patch_divider = "-Beta"_s;
    auto const patch_dash = FindSpan(patch_text, k_patch_divider);
    if (patch_dash) {
        beta_text = patch_text.SubSpan(*patch_dash + k_patch_divider.size);
        patch_text = patch_text.SubSpan(0, *patch_dash);
    }

    major_text = WhitespaceStripped(major_text);
    minor_text = WhitespaceStripped(minor_text);
    patch_text = WhitespaceStripped(patch_text);

    Version result {};
    usize num_chars_read = 0;
    if (auto n = ParseInt(major_text, ParseIntBase::Decimal, &num_chars_read);
        n.HasValue() && num_chars_read == major_text.size)
        result.major = (u8)n.Value();
    else
        return k_nullopt;

    if (auto n = ParseInt(minor_text, ParseIntBase::Decimal, &num_chars_read);
        n.HasValue() && num_chars_read == minor_text.size)
        result.minor = (u8)n.Value();
    else
        return k_nullopt;

    if (auto n = ParseInt(patch_text, ParseIntBase::Decimal, &num_chars_read);
        n.HasValue() && num_chars_read == patch_text.size)
        result.patch = (u8)n.Value();
    else
        return k_nullopt;

    if (beta_text) {
        if (auto n = ParseInt(*beta_text, ParseIntBase::Decimal, &num_chars_read);
            n.HasValue() && num_chars_read == beta_text->size)
            result.beta = (u8)n.Value();
        else
            return k_nullopt;
    }

    return result;
}
