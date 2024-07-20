// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stb_sprintf.h>

#include "foundation/container/dynamic_array.hpp"
#include "foundation/container/span.hpp"
#include "foundation/error/error_code.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/string.hpp"
#include "foundation/utils/writer.hpp"

namespace fmt {

/*

IMPROVE: use the type-system and structs to configure format options instead of character codes. e.g.:
    fmt::Format("Float is {}", fmt::Arg(5.92013f, {.float_precision = 2}));
    fmt::Format("Int is {}", fmt::Arg(2340931, .{.hex = true, .show_plus = true}));
Instead of:
    fmt::Format("{.2}", 5.92013f);
    fmt::Format("{x+}", 2340931);
It's more verbose but I think it could be a system that is more more extensible and easier to understand. We
can put whatever we want in the Args options struct rather than having to use character codes.


Simple string formatting that covers the cases that we need for now. Uses python-like curly braces for
formatting.

Cool feature: you can wrap an argument to the format function with DumpStruct to dump the struct using
clang's __builtin_dump_struct, useful for debugging. e.g. Format("{}", DumpStruct(my_object));

Format (..., "{}: {}", val, "hi");

Inside the {} you can have various things as shown in the table below.
- <number> means any integer.

type                 | code       | description
-------------------------------------------------------------------------------------------------------------
any                  | <number>   | THIS MUST BE FIRST. The minimum number of characters for the value. This
                                  | is padded with spaces unless this number starts with a 0. e.g. 4 (padded
                                  | with spaces) or 04 (padded with 0).
f32 or f64           | g          | Auto-float (uses scientific notation if needed)
f32 or f64           | .<number>  | float precision e.g. {.2}
integer              | x          | Hexadecimal
integer              | +          | Show a + if the number is positive
ErrorCode            | d          | Print the debug info of the error

*/

struct FormatOptions {
    bool auto_float_format = false;
    bool hex = false;
    bool show_plus = false;
    String float_precision {};
    bool is_string_literal {};
    bool error_debug_info = !PRODUCTION_BUILD;
    u64 required_width {};
    char padding_character {};
};

PUBLIC constexpr ErrorCodeOr<void>
PadToRequiredWidthIfNeeded(Writer writer, FormatOptions options, usize size) {
    if (options.required_width && size < options.required_width)
        TRY(writer.WriteCharRepeated(options.padding_character, options.required_width - size));
    return k_success;
}

template <Scalar Type>
constexpr auto ScalarToInt(Type x) {
    if constexpr (Pointer<Type>)
        return (uintptr)x;
    else if constexpr (Enum<Type>)
        return (UnderlyingType<Type>)x;
    else
        return x;
}

template <typename T>
concept TypeWithCustomValueToString =
    requires(T value, Writer writer, FormatOptions options) { CustomValueToString(writer, value, options); };

struct IntToStringOptions {
    enum class Base { Decimal, Hexadecimal };
    Base base = Base::Decimal;
    bool include_sign = false;
};

template <Integral IntType>
PUBLIC constexpr usize IntToString(IntType num, char* buffer, IntToStringOptions options = {}) {
    if (num == 0) {
        buffer[0] = '0';
        return 1;
    }

    IntType base {};
    switch (options.base) {
        case IntToStringOptions::Base::Decimal: base = 10; break;
        case IntToStringOptions::Base::Hexadecimal: base = 16; break;
    }

    bool const is_negative = SignedInt<IntType> && num < 0;
    if (is_negative) num = -num; // Make the number positive for conversion

    // Start with the end of the buffer
    char* ptr = buffer;

    if (num == 0) {
        *ptr++ = '0';
    } else {
        do {
            *ptr++ = "0123456789abcdef"[num % base];
            num /= base;
        } while (num != 0);
    }

    if constexpr (SignedInt<IntType>) {
        if (is_negative)
            *ptr++ = '-';
        else if (options.include_sign)
            *ptr++ = '+';
    }

    *ptr = '\0';

    // Reverse the string
    auto length = (usize)(ptr - buffer);
    char* start = buffer;
    char* end = buffer + length - 1;
    while (start < end) {
        char const temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }

    return length;
}

template <Integral IntType>
PUBLIC constexpr DynamicArrayInline<char, 32> IntToString(IntType num, IntToStringOptions options = {}) {
    DynamicArrayInline<char, 32> result;
    result.size = IntToString(num, result.data, options);
    return result;
}

template <typename T>
struct DumpStructWrapper {
    T const& value;
};
template <typename T>
constexpr DumpStructWrapper<T> DumpStruct(T const& x) {
    return {x};
}

struct DumpStructContext {
    Writer writer;
    usize pos {};
    char buffer[4000];
};

static constexpr void DumpStructSprintf(DumpStructContext& ctx, char const* format, auto... args) {
    auto const size_remaining = (int)(ArraySize(ctx.buffer) - ctx.pos);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wdouble-promotion"
    auto const n = stbsp_snprintf(ctx.buffer + ctx.pos, size_remaining, format, args...);
#pragma clang diagnostic pop

    ASSERT(n >= 0);

    auto const num_written = Min(size_remaining, n);
    ctx.pos += (usize)num_written;
}

template <typename T>
PUBLIC ErrorCodeOr<void> ValueToString(Writer writer, T const& value, FormatOptions options) {
    using Type = RemoveCV<T>;

    if constexpr (IsSpecializationOf<Type, Optional>) {
        if (value.HasValue())
            return ValueToString(writer, *value, options);
        else
            TRY(writer.WriteChars("nullopt"_s));
        return k_success;
    }

    else if constexpr (FloatingPoint<Type>) {
        u32 format_buf_size = 0;
        char format_buf[8];
        format_buf[format_buf_size++] = '%';
        for (auto c : options.float_precision)
            format_buf[format_buf_size++] = c;
        format_buf[format_buf_size++] = options.auto_float_format ? 'g' : 'f';
        format_buf[format_buf_size++] = '\0';

        char buffer[32];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdouble-promotion"
        auto const num_written = stbsp_snprintf(buffer, (int)ArraySize(buffer), format_buf, value);
#pragma clang diagnostic pop
        if (num_written < 0) Panic("invalid snprintf format");
        TRY(PadToRequiredWidthIfNeeded(writer, options, (usize)num_written));
        TRY(writer.WriteChars(String {buffer, (usize)num_written}));
        return k_success;
    }

    else if constexpr (Same<Type, char>) {
        TRY(PadToRequiredWidthIfNeeded(writer, options, 1));
        TRY(writer.WriteChar(value));
        return k_success;
    }

    else if constexpr (Same<Type, bool>) {
        String const str = value ? "true"_s : "false"_s;
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Same<Type, SourceLocation>) {
        auto const file = FromNullTerminated(value.file);
        auto const file_line_separator = ":"_s;
        auto const line = IntToString(value.line);
        auto const function = FromNullTerminated(value.function);
        auto const line_function_separator = ": "_s;
        auto const size =
            file.size + file_line_separator.size + line.size + line_function_separator.size + function.size;
        TRY(PadToRequiredWidthIfNeeded(writer, options, size));
        for (auto const str : Array {file, file_line_separator, line, line_function_separator, function})
            TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Convertible<Type, String> || ConstructibleWithArgs<String, Type>) {
        String const str {value};
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Convertible<Type, char const*>) {
        auto const str = FromNullTerminated(value);
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Integral<Type> || Enum<Type> || Convertible<AddConst<Type>, void const*>) {
        char buffer[32] {};
        auto const size = IntToString(ScalarToInt(value),
                                      buffer,
                                      {
                                          .base = options.hex ? IntToStringOptions::Base::Hexadecimal
                                                              : IntToStringOptions::Base::Decimal,
                                          .include_sign = options.show_plus,
                                      });
        TRY(PadToRequiredWidthIfNeeded(writer, options, size));
        TRY(writer.WriteChars(String {buffer, size}));
        return k_success;
    }

    else if constexpr (IsSpecializationOf<Type, Span>) {
        // IMPROVE: support width option
        ASSERT(options.required_width == 0);
        options.required_width = 0;
        TRY(writer.WriteChar('{'));
        for (auto const& i : value) {
            TRY(ValueToString(writer, i, options));
            if (&i != &Last(value)) TRY(writer.WriteChars(", "_s));
        }
        TRY(writer.WriteChar('}'));
        return k_success;
    }

    else if constexpr (Same<Type, ErrorCode>) {
        // IMPROVE: support width option
        ASSERT(options.required_width == 0);
        if (value.category == nullptr) return writer.WriteChars("success");
        TRY(writer.WriteChars(FromNullTerminated(value.category->category_id)));
        TRY(writer.WriteChar('['));
        TRY(writer.WriteChars(IntToString(value.code)));
        TRY(writer.WriteChar(']'));
        if (value.category->message) {
            TRY(writer.WriteChars(": "));
            TRY(value.category->message(writer, value));
        }
        if (options.error_debug_info) {
            TRY(writer.WriteChar('\n'));

            auto file = FromNullTerminated(value.source_location.file);
            if (file.size) {
                constexpr auto k_find_last_slash = [](String str) {
                    for (usize i = str.size - 1; i != usize(-1); --i) {
                        auto const c = str[i];
                        if constexpr (IS_WINDOWS) {
                            if (c == '\\' || c == '/') return i;
                        } else if (c == '/') {
                            return i;
                        }
                    }
                    return usize(-1);
                };
                auto const last_slash = k_find_last_slash(file);
                ASSERT(last_slash != usize(-1));
                file = file.SubSpan(last_slash + 1);
            }

            TRY(writer.WriteChars(file));
            TRY(writer.WriteChar(':'));
            TRY(writer.WriteChars(IntToString(value.source_location.line)));

            TRY(writer.WriteChars(": "));
            TRY(writer.WriteChars(FromNullTerminated(value.source_location.function)));
            if (value.extra_debug_info) {
                TRY(writer.WriteChars(", "));
                TRY(writer.WriteChars(FromNullTerminated(value.extra_debug_info)));
            }
        }
        return k_success;
    }

    else if constexpr (IsSpecializationOf<Type, DumpStructWrapper>) {
        DumpStructContext ctx {.writer = writer};
        __builtin_dump_struct(&value.value, DumpStructSprintf, ctx);
        if (ctx.pos != ArraySize(ctx.buffer)) --ctx.pos;
        TRY(writer.WriteChars(String {ctx.buffer, ctx.pos}));
        return k_success;
    }

    else if constexpr (TypeWithCustomValueToString<Type>)
        return CustomValueToString(writer, value, options);

    else
        static_assert(
            false,
            "Unsupported type for formatting, either add it to the function or define CustomValueToString function for it");

    PanicIfReached();
    return k_success;
}

namespace details {

struct BraceSectionResult {
    char const* new_p;
    bool should_format_type;
    FormatOptions options;
};

// This is primarily done in a separate function in order to reduce the size of the template function
static ErrorCodeOr<BraceSectionResult> ParseBraceSection(Writer writer, char const* p, char const* end) {
    ASSERT(*p == '{');
    for (; p < end; ++p) {
        ++p;
        if (p == end) Panic("Mismatched {}");
        if (*p == '{') {
            TRY(writer.WriteChar('{'));
            return BraceSectionResult {.new_p = p, .should_format_type = false};
        }

        auto const start = p;
        while (*p != '}') {
            if (p == end) Panic("Mismatched {}");
            ++p;
        }
        String brace_contents {start, (usize)(p - start)};
        FormatOptions options {};
        options.padding_character = ' ';

        if (brace_contents.size) {
            if (StartsWith(brace_contents, '0')) options.padding_character = '0';

            usize num_chars_read = 0;
            if (auto o = ParseInt(brace_contents, ParseIntBase::Decimal, &num_chars_read); o.HasValue()) {
                options.required_width = (u64)Max<s64>(o.Value(), 0);
                brace_contents.RemovePrefix(num_chars_read);
            }

            for (usize i = 0; i < brace_contents.size; ++i) {
                switch (brace_contents[i]) {
                    case 'g': options.auto_float_format = true; break;
                    case 'x': options.hex = true; break;
                    case '+': options.show_plus = true; break;
                    case '.': {
                        usize num_numbers = 1;
                        for (auto c : brace_contents.SubSpan(i + 1))
                            if (c >= '0' && c <= '9')
                                ++num_numbers;
                            else
                                break;
                        if (num_numbers == 1) Panic("Expected numbers after . in format string");

                        options.float_precision = brace_contents.SubSpan(i, num_numbers);
                        i += num_numbers;
                        break;
                    }
                    case 'd': options.error_debug_info = true; break;
                    default: Panic("Unknown options inside {}");
                }
            }
        }

        BraceSectionResult result {};
        result.new_p = p;
        result.should_format_type = true;
        result.options = options;
        return result;
    }
    PanicIfReached();
    return {};
}

// 'format' is advanced to after the next brace-section
template <typename Arg>
static ErrorCodeOr<void> FindAndWriteNextValue(Writer writer, String& format, Arg const& arg) {
    auto const starting_size = format.size;
    auto const end = format.end();
    for (auto p = format.begin(); p != end; ++p) {
        if (*p == '{') {
            auto brace_result = TRY(ParseBraceSection(writer, p, end));
            p = brace_result.new_p;
            if (!brace_result.should_format_type) continue;

            TRY(ValueToString(writer, arg, brace_result.options));

            ASSERT(p >= format.begin());
            format.RemovePrefix((usize)(p - format.begin() + 1));
            break;
        } else if (*p == '}') {
            ++p;
            if (p == end || *p != '}') Panic("Mismatched }");
            TRY(writer.WriteChar('}'));
        } else {
            TRY(writer.WriteChar(*p));
        }
    }

    if (starting_size == format.size) Panic("More args than {}");

    return k_success;
}

static constexpr ErrorCodeOr<void> WriteRemaining(Writer writer, String format) {
    auto const end = format.end();
    for (auto p = format.begin(); p != end; ++p) {
        if (*p == '{') {
            ++p;
            if (p == end || *p != '{') Panic("More {} than args");
        } else if (*p == '}') {
            ++p;
            if (p == end || *p != '}') Panic("Mismatched {}");
        }
        TRY(writer.WriteChar(*p));
    }

    return k_success;
}

} // namespace details

// Writer
//=================================================
template <typename... Args>
PUBLIC ErrorCodeOr<void> FormatToWriter(Writer writer, String format, Args const&... args) {
    ErrorCodeOr<void> result = k_success;
    bool const succeeded =
        ((result = details::FindAndWriteNextValue(writer, format, args), result.Succeeded()) && ...);
    if (!succeeded) return result;
    return details::WriteRemaining(writer, format);
}

template <typename... Args>
PUBLIC inline ErrorCodeOr<void> AppendLine(Writer writer, String format, Args const&... args) {
    TRY(FormatToWriter(writer, format, args...));
    TRY(writer.WriteChar('\n'));
    return k_success;
}

template <typename Arg>
PUBLIC inline ErrorCodeOr<void> AppendRaw(Writer writer, Arg const& arg) {
    return ValueToString(writer, arg, FormatOptions {});
}

template <typename Arg>
PUBLIC inline ErrorCodeOr<void> AppendLineRaw(Writer writer, Arg const& arg) {
    TRY(ValueToString(writer, arg, FormatOptions {}));
    TRY(writer.WriteChar('\n'));
    return k_success;
}

// Char buffer
// We tend to ignore writer errors here because we don't handle memory allocation failure anyways
//=================================================
template <dyn::DynArray DynCharArray, typename... Args>
PUBLIC inline void Append(DynCharArray& output, String format, Args const&... args) {
    output.Reserve(output.size + format.size * 2);
    auto result = FormatToWriter(dyn::WriterFor(output), format, args...);
    if (result.HasError()) __builtin_debugtrap();
}

template <dyn::DynArray DynCharArray, typename... Args>
PUBLIC inline void Assign(DynCharArray& output, String format, Args const&... args) {
    dyn::Clear(output);
    output.Reserve(format.size * 2);
    auto const outcome = FormatToWriter(dyn::WriterFor(output), format, args...);
    if (outcome.HasError()) __builtin_debugtrap();
}

template <usize k_size, typename... Args>
PUBLIC inline DynamicArrayInline<char, k_size> FormatInline(String format, Args const&... args) {
    DynamicArrayInline<char, k_size> result;
    auto const outcome = FormatToWriter(dyn::WriterFor(result), format, args...);
    if (outcome.HasError()) __builtin_debugtrap();
    return result;
}

template <typename... Args>
PUBLIC inline MutableString Format(Allocator& a, String format, Args const&... args) {
    DynamicArray<char> result {a};
    result.Reserve(format.size * 2);
    auto const outcome = FormatToWriter(dyn::WriterFor(result), format, args...);
    if (outcome.HasError()) __builtin_debugtrap();
    return result.ToOwnedSpan();
}

//=================================================
struct StringReplacement {
    String find;
    String replacement;
};

PUBLIC MutableString FormatStringReplace(Allocator& a,
                                         String text,
                                         Span<StringReplacement const> replacements) {
    if (replacements.size == 0) return a.Clone(text);

    DynamicArray<char> result {a};
    result.Reserve(text.size * 2 / 3);

    while (text.size) {
        bool found = false;
        for (auto r : replacements) {
            if (StartsWithSpan(text, r.find)) {
                dyn::AppendSpan(result, r.replacement);
                text.RemovePrefix(r.find.size);
                found = true;
            }
        }
        if (!found) {
            dyn::Append(result, text[0]);
            text.RemovePrefix(1);
        }
    }

    return result.ToOwnedSpan();
}

} // namespace fmt
