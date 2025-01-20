#include "sentry.hpp"

#include "tests/framework.hpp"

namespace sentry {

TEST_CASE(TestSentry) {
    SUBCASE("Parse DSN") {
        auto o = detail::ParseSentryDsn("https://publickey@host.com/123");
        REQUIRE(o.HasValue());
        CHECK_EQ(o->dsn, "https://publickey@host.com/123"_s);
        CHECK_EQ(o->host, "host.com"_s);
        CHECK_EQ(o->project_id, "123"_s);
        CHECK_EQ(o->public_key, "publickey"_s);

        CHECK(!detail::ParseSentryDsn("https://host.com/123"));
        CHECK(!detail::ParseSentryDsn("https://publickey@host.com"));
        CHECK(!detail::ParseSentryDsn("  "));
        CHECK(!detail::ParseSentryDsn(""));
    }

    SUBCASE("Basics") {
        Sentry sentry {};
        REQUIRE(InitSentry(sentry, "https://publickey@host.com/123"));
        CHECK(sentry.device_id);

        // build an envelope
        DynamicArray<char> envelope {tester.scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        TRY(EnvelopeAddHeader(sentry, writer));
        TRY(EnvelopeAddSessionUpdate(sentry, writer, {.status = Sentry::Session::Status::Ok}));
        TRY(EnvelopeAddEvent(sentry,
                             writer,
                             {
                                 .level = Sentry::Event::Level::Info,
                                 .message = "Test event message"_s,
                                 .tags = ArrayT<Sentry::Event::Tag>({
                                     {"tag1", "value1"},
                                     {"tag2", "value2"},
                                 }),
                             }));
        TRY(EnvelopeAddSessionUpdate(sentry, writer, {.status = Sentry::Session::Status::Exited}));

        CHECK(envelope.size > 0);
    }

    return k_success;
}

} // namespace sentry

TEST_REGISTRATION(RegisterSentryTests) { REGISTER_TEST(sentry::TestSentry); }
