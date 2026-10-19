#include "diagnostics/LogCapture.h"

#include <QLoggingCategory>

#include <catch2/catch_test_macros.hpp>

using namespace pf;

Q_LOGGING_CATEGORY(lcTest, "prismforge.test")

TEST_CASE("Log messages are captured with level and category", "[log]")
{
    static bool installed = false;
    if (!installed) {
        LogCapture::install();
        installed = true;
    }
    LogCapture::instance().clear();

    qCInfo(lcTest) << "hello from the test";
    qCWarning(lcTest) << "something odd";

    const auto entries = LogCapture::instance().entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].type == QtInfoMsg);
    CHECK(entries[0].category == "prismforge.test");
    CHECK(entries[0].message.contains("hello from the test"));
    CHECK(entries[1].type == QtWarningMsg);
    CHECK(entries[1].sequence > entries[0].sequence);

    LogCapture::instance().clear();
    CHECK(LogCapture::instance().entries().isEmpty());
}
