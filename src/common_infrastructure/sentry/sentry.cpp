// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sentry.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

namespace sentry {

static Optional<fmt::UuidArray> DeviceId(Atomic<u64>& seed) {
    PathArena path_arena {PageAllocator::Instance()};
    auto const path = KnownDirectoryWithSubdirectories(path_arena,
                                                       KnownDirectoryType::UserData,
                                                       Array {"Floe"_s},
                                                       "device_id",
                                                       {.create = true});

    auto file = TRY_OR(OpenFile(path, FileMode::ReadWrite), {
        g_log.Error(k_main_log_module, "Failed to create device_id file: {}, {}", path, error);
        return k_nullopt;
    });

    TRY_OR(file.Lock(FileLockType::Exclusive), {
        g_log.Error(k_main_log_module, "Failed to lock device_id file: {}, {}", path, error);
        return k_nullopt;
    });
    DEFER { auto _ = file.Unlock(); };

    auto const size = TRY_OR(file.FileSize(), {
        g_log.Error(k_main_log_module, "Failed to get size of device_id file: {}, {}", path, error);
        return k_nullopt;
    });

    if (size == fmt::k_uuid_size) {
        fmt::UuidArray uuid {};
        TRY_OR(file.Read(uuid.data, uuid.size), {
            g_log.Error(k_main_log_module, "Failed to read device_id file: {}, {}", path, error);
            return k_nullopt;
        });

        bool valid = true;
        for (auto c : uuid) {
            if (!IsHexDigit(c)) {
                valid = false;
                break;
            }
        }
        if (valid) return uuid;
    }

    // File is invalid or empty, let's recreate it
    auto const uuid = detail::Uuid(seed);

    TRY_OR(file.Seek(0, File::SeekOrigin::Start),
           { g_log.Error(k_main_log_module, "Failed to seek device_id file: {}, {}", path, error); });

    TRY_OR(file.Truncate(0),
           { g_log.Error(k_main_log_module, "Failed to truncate device_id file: {}, {}", path, error); });

    TRY_OR(file.Write(uuid),
           { g_log.Error(k_main_log_module, "Failed to write device_id file: {}, {}", path, error); });

    auto _ = file.Flush();

    return uuid;
}

// We only support the format: https://<public_key>@<host>/<project_id>
static Optional<SentryDsn> ParseSentryDsn(String dsn) {
    auto read_until = [](String& s, char c) {
        auto const i = Find(s, c);
        if (!i) return String {};
        auto const result = s.SubSpan(0, *i);
        s.RemovePrefix(*i + 1);
        return result;
    };

    SentryDsn result {.dsn = dsn};

    // Skip https://
    if (!StartsWithSpan(dsn, "https://"_s)) return k_nullopt;
    if (dsn.size < 8) return k_nullopt;
    dsn.RemovePrefix(8);

    // Get public key (everything before @)
    auto key = read_until(dsn, '@');
    if (key.size == 0) return k_nullopt;
    result.public_key = key;

    // Get host (everything before last /)
    auto last_slash = Find(dsn, '/');
    if (!last_slash) return k_nullopt;

    result.host = dsn.SubSpan(0, *last_slash);
    dsn.RemovePrefix(*last_slash + 1);

    // Remaining part is project_id
    if (dsn.size == 0) return k_nullopt;
    result.project_id = dsn;

    return result;
}

// not thread-safe, dsn must be static, tags are cloned
static void InitSentry(Sentry& sentry, SentryDsn dsn, Span<Tag const> tags) {
    ASSERT(sentry.dsn.dsn.size == 0);
    sentry.dsn = dsn;
    sentry.device_id = DeviceId(sentry.seed);

    // clone the tags
    ASSERT(sentry.tags.size == 0);
    sentry.tags = sentry.arena.Clone(tags, CloneType::Deep);

    // this data is common to every event we want to send, so let's cache it as a JSON blob
    {
        DynamicArray<char> event_context_json {sentry.arena};
        event_context_json.Reserve(1024);
        json::WriteContext json_writer {
            .out = dyn::WriterFor(event_context_json),
            .add_whitespace = false,
        };

        auto _ = json::WriteObjectBegin(json_writer);

        // user
        if (sentry.device_id) {
            auto _ = json::WriteKeyObjectBegin(json_writer, "user");
            auto _ = json::WriteKeyValue(json_writer, "id", *sentry.device_id);
            auto _ = json::WriteObjectEnd(json_writer);
        }

        // contexts
        {
            auto _ = json::WriteKeyObjectBegin(json_writer, "contexts");

            // device
            {
                auto const system = GetSystemStats();
                auto _ = json::WriteKeyObjectBegin(json_writer, "device");
                auto _ = json::WriteKeyValue(json_writer, "name", "desktop");
                auto _ = json::WriteKeyValue(json_writer, "arch", system.Arch());
                auto _ = json::WriteKeyValue(json_writer, "cpu_description", system.cpu_name);
                auto _ = json::WriteKeyValue(json_writer, "processor_count", system.num_logical_cpus);
                auto _ = json::WriteKeyValue(json_writer, "processor_frequency", system.frequency_mhz);
                auto _ = json::WriteObjectEnd(json_writer);
            }

            // os
            {
                auto const os = GetOsInfo();
                auto _ = json::WriteKeyObjectBegin(json_writer, "os");
                auto _ = json::WriteKeyValue(json_writer, "name", os.name);
                if (os.version.size) auto _ = json::WriteKeyValue(json_writer, "version", os.version);
                if (os.build.size) auto _ = json::WriteKeyValue(json_writer, "build", os.build);
                if (os.kernel_version.size)
                    auto _ = json::WriteKeyValue(json_writer, "kernel_version", os.kernel_version);
                if (os.pretty_name.size)
                    auto _ = json::WriteKeyValue(json_writer, "pretty_name", os.pretty_name);
                if (os.distribution_name.size)
                    auto _ = json::WriteKeyValue(json_writer, "distribution_name", os.distribution_name);
                if (os.distribution_version.size)
                    auto _ =
                        json::WriteKeyValue(json_writer, "distribution_version", os.distribution_version);
                if (os.distribution_pretty_name.size)
                    auto _ = json::WriteKeyValue(json_writer,
                                                 "distribution_pretty_name",
                                                 os.distribution_pretty_name);
                auto _ = json::WriteObjectEnd(json_writer);
            }

            auto _ = json::WriteObjectEnd(json_writer);
        }
        auto _ = json::WriteObjectEnd(json_writer);

        // we want to be able to print the context directly into an existing JSON event so we remove the
        // object braces
        dyn::Pop(event_context_json);
        dyn::Remove(event_context_json, 0);

        sentry.event_context_json = event_context_json.ToOwnedSpan();
    }
}

alignas(Sentry) u8 g_sentry_storage[sizeof(Sentry)];
Atomic<Sentry*> g_instance {nullptr};

Sentry* InitGlobalSentry(String dsn, Span<Tag const> tags) {
    auto existing = g_instance.Load(LoadMemoryOrder::Relaxed);
    if (existing) return existing;

    auto parsed_dsn = TRY_OPT_OR(ParseSentryDsn(dsn), {
        g_log.Error(k_main_log_module, "Failed to parse Sentry DSN: {}", dsn);
        return nullptr;
    });

    auto sentry = PLACEMENT_NEW(g_sentry_storage) Sentry {};
    InitSentry(*sentry, parsed_dsn, tags);

    g_instance.Store(sentry, StoreMemoryOrder::Relaxed);
    return sentry;
}

TEST_CASE(TestSentry) {
    SUBCASE("Parse DSN") {
        auto o = ParseSentryDsn("https://publickey@host.com/123");
        REQUIRE(o.HasValue());
        CHECK_EQ(o->dsn, "https://publickey@host.com/123"_s);
        CHECK_EQ(o->host, "host.com"_s);
        CHECK_EQ(o->project_id, "123"_s);
        CHECK_EQ(o->public_key, "publickey"_s);

        CHECK(!ParseSentryDsn("https://host.com/123"));
        CHECK(!ParseSentryDsn("https://publickey@host.com"));
        CHECK(!ParseSentryDsn("  "));
        CHECK(!ParseSentryDsn(""));
    }

    SUBCASE("Basics") {
        auto sentry = InitGlobalSentry("https://publickey@host.com/123", {});
        REQUIRE(sentry);
        CHECK(sentry->device_id);

        // build an envelope
        DynamicArray<char> envelope {tester.scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        TRY(EnvelopeAddHeader(*sentry, writer));
        TRY(EnvelopeAddSessionUpdate(*sentry, writer, SessionUpdateType::Start));
        TRY(EnvelopeAddEvent(*sentry,
                             writer,
                             {
                                 .level = ErrorEvent::Level::Info,
                                 .message = "Test event message"_s,
                                 .tags = ArrayT<Tag>({
                                     {"tag1", "value1"},
                                     {"tag2", "value2"},
                                 }),
                             }));
        TRY(EnvelopeAddSessionUpdate(*sentry, writer, SessionUpdateType::EndedNormally));

        CHECK(envelope.size > 0);
    }

    return k_success;
}

} // namespace sentry

TEST_REGISTRATION(RegisterSentryTests) { REGISTER_TEST(sentry::TestSentry); }
