// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/container/dynamic_array.hpp"
#include "foundation/container/hash_table.hpp"
#include "foundation/universal_defs.hpp"

// Widen/narrow code from stb.h:
// https://github.com/nothings/stb/blob/ae721c50eaf761660b4f90cc590453cdb0c2acd0/deprecated/stb.h#L1010

PUBLIC constexpr usize MaxWidenedStringSize(String utf8_str) { return utf8_str.size; }

PUBLIC constexpr Optional<usize> WidenToBuffer(wchar_t* out, String utf8_str) {
    auto const text_encoding_error = nullopt;

    auto str = (unsigned char const*)utf8_str.data;
    u32 c;
    auto const end = str + utf8_str.size;
    usize out_size = 0;
    while (str < end) {
        if (!(*str & 0x80)) {
            out[out_size++] = (wchar_t)*str;
            str++;
        } else if ((*str & 0xe0) == 0xc0) {
            if (*str < 0xc2) return text_encoding_error;
            c = ((u32)*str++ & 0x1f) << 6;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            out[out_size++] = (wchar_t)(c + (*str & 0x3f));
            str++;
        } else if ((*str & 0xf0) == 0xe0) {
            if (*str == 0xe0 && (str[1] < 0xa0 || str[1] > 0xbf)) return text_encoding_error;
            if (*str == 0xed && str[1] > 0x9f) return text_encoding_error; // str[1] < 0x80 is checked below
            c = ((u32)*str++ & 0x0f) << 12;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += ((u32)*str++ & 0x3f) << 6;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            out[out_size++] = (wchar_t)(c + (*str & 0x3f));
            str++;
        } else if ((*str & 0xf8) == 0xf0) {
            if (*str > 0xf4) return text_encoding_error;
            if (*str == 0xf0 && (str[1] < 0x90 || str[1] > 0xbf)) return text_encoding_error;
            if (*str == 0xf4 && str[1] > 0x8f) return text_encoding_error; // str[1] < 0x80 is checked below
            c = ((u32)*str++ & 0x07) << 18;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += ((u32)*str++ & 0x3f) << 12;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += ((u32)*str++ & 0x3f) << 6;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += (*str++ & 0x3f);
            // utf-8 encodings of values used in surrogate pairs are invalid
            if ((c & 0xFFFFF800) == 0xD800) return text_encoding_error;
            if (c >= 0x10000) {
                c -= 0x10000;
                out[out_size++] = (wchar_t)(0xD800 | (0x3ff & (c >> 10)));
                out[out_size++] = (wchar_t)(0xDC00 | (0x3ff & (c)));
            }
        } else {
            return text_encoding_error;
        }
    }
    return out_size;
}

PUBLIC constexpr usize MaxNarrowedStringSize(WString wstr) { return wstr.size * 3; }

PUBLIC constexpr Optional<usize> NarrowToBuffer(char* out, WString wstr) {
    auto const text_encoding_error = nullopt;
    auto str = wstr.data;
    auto const end = wstr.data + wstr.size;
    usize out_size = 0;
    while (str < end) {
        if (*str < 0x80) {
            out[out_size++] = (char)*str;
            str++;
        } else if (*str < 0x800) {
            out[out_size++] = (char)(0xc0 + (*str >> 6));
            out[out_size++] = (char)(0x80 + (*str & 0x3f));
            str += 1;
        } else if (*str >= 0xd800 && *str < 0xdc00) {
            u32 const c = (((u32)str[0] - 0xd800) << 10) + (((u32)str[1]) - 0xdc00) + 0x10000;
            out[out_size++] = (char)(0xf0 + (c >> 18));
            out[out_size++] = (char)(0x80 + ((c >> 12) & 0x3f));
            out[out_size++] = (char)(0x80 + ((c >> 6) & 0x3f));
            out[out_size++] = (char)(0x80 + ((c) & 0x3f));
            str += 2;
        } else if (*str >= 0xdc00 && *str < 0xe000) {
            return text_encoding_error;
        } else {
            out[out_size++] = (char)(0xe0 + (*str >> 12));
            out[out_size++] = (char)(0x80 + ((*str >> 6) & 0x3f));
            out[out_size++] = (char)(0x80 + ((*str) & 0x3f));
            str += 1;
        }
    }
    return out_size;
}

PUBLIC constexpr bool WidenAppend(DynamicArray<wchar_t>& out, String utf8_str) {
    auto const reserved = out.Reserve(out.size + MaxWidenedStringSize(utf8_str));
    ASSERT(reserved);
    auto const size = TRY_OPT(WidenToBuffer(out.data + out.size, utf8_str));
    out.ResizeWithoutCtorDtor(out.size + size);
    return true;
}

PUBLIC constexpr bool NarrowAppend(DynamicArray<char>& out, WString wstr) {
    auto const reserved = out.Reserve(out.size + MaxNarrowedStringSize(wstr));
    ASSERT(reserved);
    auto const size = TRY_OPT(NarrowToBuffer(out.data + out.size, wstr));
    out.ResizeWithoutCtorDtor(out.size + size);
    return true;
}

PUBLIC constexpr Optional<Span<wchar_t>> Widen(Allocator& a, String utf8_str) {
    Span<wchar_t> const result = a.AllocateExactSizeUninitialised<wchar_t>(MaxWidenedStringSize(utf8_str));
    auto const size = TRY_OPT(WidenToBuffer(result.data, utf8_str));
    return a.ResizeType(result, size, size);
}

PUBLIC constexpr Optional<MutableString> Narrow(Allocator& a, WString wstr) {
    DynamicArray<char> result {a};
    if (!NarrowAppend(result, wstr)) return nullopt;
    return result.ToOwnedSpan();
}

// do not Free() the result.
PUBLIC constexpr Optional<Span<wchar_t>> WidenAllocNullTerm(ArenaAllocator& allocator, String utf8_str) {
    DynamicArray<wchar_t> buffer {allocator};
    if (!WidenAppend(buffer, utf8_str)) return nullopt;
    dyn::Append(buffer, L'\0');
    auto result = buffer.ToOwnedSpan();
    result.RemoveSuffix(1);
    return result;
}

// do not Free() the result.
PUBLIC constexpr Optional<MutableString> NarrowAllocNullTerm(ArenaAllocator& allocator, WString wstr) {
    DynamicArray<char> buffer {allocator};
    if (!NarrowAppend(buffer, wstr)) return nullopt;
    dyn::Append(buffer, '\0');
    auto result = buffer.ToOwnedSpan();
    result.RemoveSuffix(1);
    return result;
}

constexpr u32 k_invalid_unicode_codepoint = 0xFFFD;

// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
// Convert UTF-8 to 32-bit character, process single character input.
// A nearly-branchless UTF-8 decoder, based on work of Christopher Wellons
// (https://github.com/skeeto/branchless-utf8). We handle UTF-8 decoding error by skipping forward.
PUBLIC usize Utf8CharacterToUtf32(u32* out_char,
                                  char const* in_text,
                                  char const* in_text_end,
                                  u32 max_codepoint = 0x10FFFF) {
    constexpr u8 const k_lengths[32] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0};
    constexpr u32 const k_masks[] = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
    constexpr u32 const k_mins[] = {0x400000, 0, 0x80, 0x800, 0x10000};
    constexpr u32 const k_shiftc[] = {0, 18, 12, 6, 0};
    constexpr u32 const k_shifte[] = {0, 6, 4, 2, 0};
    auto const len = k_lengths[*(unsigned char const*)in_text >> 3];
    usize wanted = len + !len;

    if (in_text_end == nullptr)
        in_text_end = in_text + wanted; // Max length, nulls will be taken into account.

    // Copy at most 'len' bytes, stop copying at 0 or past in_text_end. Branch predictor does a good job here,
    // so it is fast even with excessive branching.
    unsigned char s[4];
    s[0] = in_text + 0 < in_text_end ? (unsigned char)in_text[0] : 0;
    s[1] = in_text + 1 < in_text_end ? (unsigned char)in_text[1] : 0;
    s[2] = in_text + 2 < in_text_end ? (unsigned char)in_text[2] : 0;
    s[3] = in_text + 3 < in_text_end ? (unsigned char)in_text[3] : 0;

    // Assume a four-byte character and load four bytes. Unused bits are shifted out.
    *out_char = (u32)(s[0] & k_masks[len]) << 18;
    *out_char |= (u32)(s[1] & 0x3f) << 12;
    *out_char |= (u32)(s[2] & 0x3f) << 6;
    *out_char |= (u32)(s[3] & 0x3f) << 0;
    *out_char >>= k_shiftc[len];

    // Accumulate the various error conditions.
    int e = 0;
    e = (*out_char < k_mins[len]) << 6; // non-canonical encoding
    e |= ((*out_char >> 11) == 0x1b) << 7; // surrogate half?
    e |= (*out_char > max_codepoint) << 8; // out of range?
    e |= (s[1] & 0xc0) >> 2;
    e |= (s[2] & 0xc0) >> 4;
    e |= (s[3]) >> 6;
    e ^= 0x2a; // top two bits of each tail byte correct?
    e >>= k_shifte[len];

    if (e) {
        // No bytes are consumed when *in_text == 0 || in_text == in_text_end.
        // One byte is consumed in case of invalid first byte of in_text.
        // All available bytes (at most `len` bytes) are consumed on incomplete/invalid second to last bytes.
        // Invalid or incomplete input may consume less bytes than wanted, therefore every byte has to be
        // inspected in s.
        wanted = Min<usize>(wanted, !!s[0] + !!s[1] + !!s[2] + !!s[3]);
        *out_char = k_invalid_unicode_codepoint;
    }

    return wanted;
}

PUBLIC constexpr String SplitWithIterator(String whole, Optional<usize>& cursor, char token) {
    if (!cursor) return {};
    auto const cursor_val = *cursor;

    auto pos = Find(whole, token, cursor_val);
    String result;
    if (!pos) {
        result = whole.SubSpan(cursor_val);
        cursor = {};
    } else {
        result = whole.SubSpan(cursor_val, *pos - cursor_val);
        cursor = *pos + 1;
    }
    return result;
}

PUBLIC DynamicArray<String> Split(String str, char token, Allocator& allocator) {
    DynamicArray<String> result {allocator};
    Optional<usize> cursor {0uz};
    while (cursor)
        dyn::Append(result, SplitWithIterator(str, cursor, token));
    return result;
}

// Supports * and ? wildcards
// https://research.swtch.com/glob
PUBLIC constexpr bool MatchWildcard(String wildcard, String haystack) {
    usize px = 0;
    usize nx = 0;
    usize next_px = 0;
    usize next_nx = 0;
    while (px < wildcard.size || nx < haystack.size) {
        if (px < wildcard.size) {
            auto c = wildcard[px];
            switch (c) {
                case '?': {
                    if (nx < haystack.size) {
                        px++;
                        nx++;
                        continue;
                    }
                    break;
                }
                case '*': {
                    next_px = px;
                    next_nx = nx + 1;
                    px++;
                    continue;
                }
                default: {
                    if (nx < haystack.size && haystack[nx] == c) {
                        px++;
                        nx++;
                        continue;
                    }
                    break;
                }
            }
        }
        if (0 < next_nx && next_nx <= haystack.size) {
            px = next_px;
            nx = next_nx;
            continue;
        }
        return false;
    }

    return true;
}

template <usize k_size>
PUBLIC void CopyStringIntoBufferWithNullTerm(char (&destination)[k_size], String source) {
    auto const size = Min(k_size - 1, source.size);
    CopyMemory(destination, source.data, size);
    destination[size] = '\0';
}

PUBLIC constexpr void CopyStringIntoBufferWithNullTerm(Span<char> buffer, String source) {
    if (!buffer.size) return;

    auto const size = Min(buffer.size - 1, source.size);
    for (auto const i : Range(size))
        buffer[i] = source[i];
    buffer[size] = 0;
}

PUBLIC constexpr const char* IncrementUTF8Characters(void const* str, int num_to_inc) {
    auto s = (u8 const*)str;
    int num_codepoints = 0;
    while (*s && num_codepoints < num_to_inc) {
        if ((*s & 0b11111000) == 0b11110000)
            s += 4;
        else if ((*s & 0b11110000) == 0b11100000)
            s += 3;
        else if ((*s & 0b11100000) == 0b11000000)
            s += 2;
        else
            s += 1;

        num_codepoints++;
    }
    return (char const*)s;
}

PUBLIC constexpr bool NullTermStringsEqual(char const* a, char const* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

PUBLIC constexpr bool NullTermStringStartsWith(char const* str, char const* prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        ++str;
        ++prefix;
    }
    return true;
}

template <typename Type>
requires(CharacterType<Type>)
PUBLIC constexpr Span<Type> FromNullTerminated(Type* null_term_data) {
    usize size = 0;
    auto ptr = null_term_data;
    while (*ptr++)
        ++size;

    return Span {null_term_data, size};
}

PUBLIC constexpr usize NullTerminatedSize(char const* str) {
    usize size = 0;
    while (*str++)
        ++size;
    return size;
}

PUBLIC char* NullTerminated(String str, ArenaAllocator& a) {
    auto result = a.AllocateExactSizeUninitialised<char>(str.size + 1);
    CopyMemory(result.data, str.data, str.size);
    result[str.size] = '\0';
    return result.data;
}

PUBLIC wchar_t* NullTerminated(WString str, ArenaAllocator& a) {
    auto result = a.AllocateExactSizeUninitialised<wchar_t>(str.size + 1);
    CopyMemory(result.data, str.data, str.size * sizeof(wchar_t));
    result[str.size] = L'\0';
    return result.data;
}

#define ANSI_COLOUR_SET_FOREGROUND_RED     "\x1b[31m"
#define ANSI_COLOUR_SET_FOREGROUND_GREEN   "\x1b[32m"
#define ANSI_COLOUR_SET_FOREGROUND_YELLOW  "\x1b[33m"
#define ANSI_COLOUR_SET_FOREGROUND_BLUE    "\x1b[34m"
#define ANSI_COLOUR_RESET                  "\x1b[0m"
#define ANSI_COLOUR_FOREGROUND_RED(str)    ANSI_COLOUR_SET_FOREGROUND_RED str ANSI_COLOUR_RESET
#define ANSI_COLOUR_FOREGROUND_GREEN(str)  ANSI_COLOUR_SET_FOREGROUND_GREEN str ANSI_COLOUR_RESET
#define ANSI_COLOUR_FOREGROUND_YELLOW(str) ANSI_COLOUR_SET_FOREGROUND_YELLOW str ANSI_COLOUR_RESET
#define ANSI_COLOUR_FOREGROUND_BLUE(str)   ANSI_COLOUR_SET_FOREGROUND_BLUE str ANSI_COLOUR_RESET

PUBLIC constexpr char ToUppercaseAscii(char c) {
    if (c >= 'a' && c <= 'z') return c + ('A' - 'a');
    return c;
}

PUBLIC constexpr char ToLowercaseAscii(char c) {
    if (c >= 'A' && c <= 'Z') return c - ('A' - 'a');
    return c;
}

PUBLIC constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }
PUBLIC constexpr bool IsHexDigit(char c) {
    return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
PUBLIC constexpr bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
PUBLIC constexpr bool IsAlphanum(char c) { return IsAlpha(c) || IsDigit(c); }
PUBLIC constexpr bool IsEndOfLine(char c) { return c == '\n' || c == '\r'; }
PUBLIC constexpr bool IsSpacing(char c) { return c == ' ' || c == '\t'; }
PUBLIC constexpr bool IsWhitespace(char c) { return IsSpacing(c) || IsEndOfLine(c); }
PUBLIC constexpr bool IsPrintableAscii(char c) { return c >= 32 && c <= 126; }

// https://en.wikipedia.org/wiki/Whitespace_character
PUBLIC constexpr bool IsSpaceU32(u32 c) {
    switch (c) {
        case 9: return true;
        case 10: return true;
        case 11: return true;
        case 12: return true;
        case 13: return true;
        case 32: return true;
        case 133: return true;
        case 160: return true;
        case 5760: return true;
        case 8192: return true;
        case 8193: return true;
        case 8194: return true;
        case 8195: return true;
        case 8196: return true;
        case 8197: return true;
        case 8198: return true;
        case 8199: return true;
        case 8200: return true;
        case 8201: return true;
        case 8202: return true;
        case 8232: return true;
        case 8233: return true;
        case 8239: return true;
    }
    return false;
}

enum class ParseIntBase { Decimal, Hexadecimal };
PUBLIC constexpr Optional<s64> ParseInt(String str, ParseIntBase base, usize* num_chars_read = nullptr) {
    auto const end = str.data + str.size;
    auto pos = str.data;
    while (pos != end && IsWhitespace(*pos))
        ++pos;
    if (pos == end) return nullopt;

    s64 result = 0;
    bool is_negative = false;
    if (str[0] == '-') {
        is_negative = true;
        ++pos;
    } else if (str[0] == '+') {
        ++pos;
    }
    bool has_digits = false;
    switch (base) {
        case ParseIntBase::Decimal: {
            while (pos != end) {
                if (!IsDigit(*pos)) break;
                result = result * 10 + (*pos - '0');
                ++pos;
                has_digits = true;
            }
            break;
        }
        case ParseIntBase::Hexadecimal: {
            while (pos != end) {
                if (!IsHexDigit(*pos)) break;
                if (IsDigit(*pos))
                    result = result * 16 + (*pos - '0');
                else
                    result = result * 16 + (ToLowercaseAscii(*pos) - 'a' + 10);
                ++pos;
                has_digits = true;
            }
            break;
        }
    }
    if (!has_digits) return nullopt;
    if (num_chars_read) *num_chars_read = (usize)(pos - str.data);
    return is_negative ? -result : result;
}

Optional<double> ParseFloat(String str, usize* num_chars_read = nullptr);

PUBLIC constexpr bool ContainsCaseInsensitiveAscii(String str, String other) {
    if (other.size == 0) return true;
    if (other.size > str.size) return false;

    for (auto const i : Range(str.size - other.size + 1)) {
        bool matched = true;
        for (auto const j : Range(other.size)) {
            if (ToUppercaseAscii(str[i + j]) != ToUppercaseAscii(other[j])) {
                matched = false;
                break;
            }
        }

        if (matched) return true;
    }

    return false;
}

PUBLIC constexpr bool IsEqualToCaseInsensitiveAscii(String str, String other) {
    if (str.size != other.size) return false;
    for (auto const i : Range(str.size))
        if (ToUppercaseAscii(str[i]) != ToUppercaseAscii(other[i])) return false;
    return true;
}

// Same as strcmp
PUBLIC constexpr int CompareAscii(String str, String other) {
    auto const lhs_sz = str.size;
    auto const rhs_sz = other.size;
    auto n = Min(lhs_sz, rhs_sz);
    auto s1 = str.data;
    auto s2 = other.data;
    while (n && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        if (lhs_sz < rhs_sz) return -1;
        if (lhs_sz > rhs_sz) return 1;
        return 0;
    }

    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

// Same as strcasecmp
PUBLIC constexpr int CompareCaseInsensitiveAscii(String str, String other) {
    auto const lhs_size = str.size;
    auto const rhs_size = other.size;

    auto n = Min(lhs_size, rhs_size);
    auto s1 = str.data;
    auto s2 = other.data;
    while (n && *s1 && (ToUppercaseAscii(*s1) == ToUppercaseAscii(*s2))) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        if (lhs_size < rhs_size) return -1;
        if (lhs_size > rhs_size) return 1;
        return 0;
    }

    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

PUBLIC constexpr bool StartsWithCaseInsensitiveAscii(String str, String other) {
    if (other.size > str.size) return false;
    return IsEqualToCaseInsensitiveAscii(str.SubSpan(0, other.size), other);
}

PUBLIC constexpr bool EndsWithCaseInsensitive(String str, String other) {
    if (other.size > str.size) return false;
    return IsEqualToCaseInsensitiveAscii(str.SubSpan(str.size - other.size), other);
}

PUBLIC constexpr usize CountWhitespaceAtEnd(String str) {
    usize n = 0;
    for (usize i = str.size - 1; i != usize(-1); --i)
        if (IsWhitespace(str[i]))
            ++n;
        else
            break;
    return n;
}

PUBLIC constexpr usize CountWhitespaceAtStart(String str) {
    usize n = 0;
    for (auto const i : Range(str.size))
        if (IsWhitespace(str[i]))
            ++n;
        else
            break;
    return n;
}

template <typename T>
concept StringConstOrNot = Same<T, String> || Same<T, MutableString>;

[[nodiscard]] PUBLIC constexpr auto WhitespaceStrippedStart(StringConstOrNot auto str) {
    return str.SubSpan(CountWhitespaceAtStart(str));
}

[[nodiscard]] PUBLIC constexpr auto WhitespaceStrippedEnd(StringConstOrNot auto str) {
    return str.SubSpan(0, str.size - CountWhitespaceAtEnd(str));
}

[[nodiscard]] PUBLIC constexpr auto WhitespaceStripped(StringConstOrNot auto str) {
    auto const offset = CountWhitespaceAtStart(str);
    return str.SubSpan(offset, str.size - (CountWhitespaceAtEnd(str) + offset));
}

[[nodiscard]] PUBLIC constexpr auto TrimStartIfMatches(StringConstOrNot auto str, String possible_prefix) {
    if (StartsWithSpan(str, possible_prefix)) return str.SubSpan(possible_prefix.size);
    return str;
}

[[nodiscard]] PUBLIC constexpr auto TrimEndIfMatches(StringConstOrNot auto str, String possible_suffix) {
    if (EndsWithSpan(str, possible_suffix)) return str.SubSpan(0, str.size - possible_suffix.size);
    return str;
}

[[nodiscard]] PUBLIC constexpr auto TrimStartIfMatches(StringConstOrNot auto str, char possible_prefix) {
    if (str.size == 0) return str;
    if (str[0] == possible_prefix) return str.SubSpan(1);
    return str;
}

[[nodiscard]] PUBLIC constexpr auto TrimEndIfMatches(StringConstOrNot auto str, char possible_suffix) {
    if (str.size == 0) return str;
    auto const last_index = str.size - 1;
    if (str[last_index] == possible_suffix) return str.SubSpan(0, last_index);
    return str;
}

// IMPROVE: use an arena rather than static size array
PUBLIC DynamicArrayInline<String, 50> Args(int argc, char** argv, bool include_program_name) {
    DynamicArrayInline<String, 50> result;
    for (int i = include_program_name ? 0 : 1; i < argc; i++)
        dyn::Append(result, FromNullTerminated(argv[i]));
    return result;
}

PUBLIC HashTable<String, String> ParseCommandLineArgs(ArenaAllocator& arena, Span<String const> args) {
    DynamicHashTable<String, String> result {arena};
    enum class ArgType { Short, Long, None };
    auto const check_arg = [](String arg) {
        if (arg[0] == '-' && IsAlphanum(arg[1])) return ArgType::Short;
        if (arg.size > 2) {
            if (arg[0] == '-' && arg[1] == '-') return ArgType::Long;
            if (arg[0] == '-' && IsAlphanum(arg[1]) && arg[2] == '=') return ArgType::Short;
        }
        return ArgType::None;
    };
    auto const prefix_size = [](ArgType type) -> usize {
        switch (type) {
            case ArgType::Short: return 1;
            case ArgType::Long: return 2;
            case ArgType::None: return 0;
        }
        return 0;
    };
    struct KeyVal {
        String key;
        String value;
    };
    auto const try_get_combined_key_val = [](String arg) {
        if (auto const opt_index = Find(arg, '='))
            return KeyVal {arg.SubSpan(0, *opt_index), arg.SubSpan(*opt_index + 1)};
        return KeyVal {arg, ""_s};
    };

    for (usize i = 0; i < args.size;) {
        if (auto const type = check_arg(args[i]); type != ArgType::None) {
            auto const arg = args[i].SubSpan(prefix_size(type));
            auto const [key, value] = try_get_combined_key_val(arg);

            bool added_next = false;
            if (i != args.size - 1) {
                auto const& next = args[i + 1];
                if (auto const next_type = check_arg(next); next_type == ArgType::None) {
                    result.Insert(arg, next);
                    i += 2;
                    added_next = true;
                }
            }

            if (!added_next) {
                result.Insert(key, value);
                i += 1;
            }
        } else {
            // positional arguments aren't supported
            i += 1;
        }
    }

    return result.ToOwnedTable();
}
