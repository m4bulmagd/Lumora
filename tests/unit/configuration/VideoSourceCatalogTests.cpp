#include <lumora/configuration/VideoSourceCatalog.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {
using namespace lumora::configuration;
VideoSourceDefinition source() {
    return {"b870204a-b16e-4122-97ac-6de753521cc7", "Loading dock", "rtsp://camera.local:554/live"};
}
TEST(VideoSourceCatalog, MissingCatalogLoadsEmptyAndSourcesRoundTrip) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    VideoSourceCatalog store(temp.filePath("nested/sources.json"));
    auto missing = store.load();
    ASSERT_TRUE(missing.hasValue());
    EXPECT_TRUE(missing.value().empty());
    ASSERT_TRUE(store.save({source()}).hasValue());
    const auto loaded = store.load();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value(), std::vector<VideoSourceDefinition>{source()});
}
TEST(VideoSourceCatalog, RejectsCredentialsAndUnsupportedAddressesWithoutEchoingThem) {
    for (const auto* address : {"http://camera/live", "file:///tmp/movie.mp4", "rtsp://",
             "rtsp://admin:private-password@camera/live", "rtsp://camera:0/live", "rtsp://camera/live#secret"}) {
        auto candidate = source();
        candidate.url = address;
        auto result = validateVideoSources({candidate});
        ASSERT_FALSE(result.hasValue()) << address;
        EXPECT_EQ(result.error().diagnosticDetail.find("private-password"), std::string::npos);
        EXPECT_EQ(result.error().operatorSummary.find("private-password"), std::string::npos);
    }
    auto secure = source();
    secure.url = "rtsps://[::1]:322/live?channel=2";
    EXPECT_TRUE(validateVideoSources({secure}).hasValue());
}
TEST(VideoSourceCatalog, RejectsAmbiguousIdentityAndUnboundedCatalogs) {
    EXPECT_FALSE(validateVideoSources({source(), source()}).hasValue());
    auto invalid = source();
    invalid.id = "camera";
    EXPECT_FALSE(validateVideoSources({invalid}).hasValue());
    invalid = source();
    invalid.name = " ";
    EXPECT_FALSE(validateVideoSources({invalid}).hasValue());
    invalid.name = std::string(129, 'x');
    EXPECT_FALSE(validateVideoSources({invalid}).hasValue());
    EXPECT_FALSE(validateVideoSources(std::vector<VideoSourceDefinition>(33, source())).hasValue());
}
TEST(VideoSourceCatalog, RejectsCredentialQueryParametersButAllowsChannelSelection) {
    for (const auto* query : {"user=operator&password=secret", "ToKeN=secret", "%70assword=secret", "api_key=secret"}) {
        auto candidate = source();
        candidate.url += std::string("?") + query;
        const auto valid = validateVideoSources({candidate});
        ASSERT_FALSE(valid.hasValue());
        EXPECT_EQ(valid.error().operatorSummary.find("secret"), std::string::npos);
    }
    auto channel = source();
    channel.url += "?channel=1&subtype=0";
    EXPECT_TRUE(validateVideoSources({channel}).hasValue());
}
TEST(VideoSourceCatalog, InvalidSaveLeavesPreviousCatalogUntouched) {
    QTemporaryDir temp;
    VideoSourceCatalog store(temp.filePath("sources.json"));
    ASSERT_TRUE(store.save({source()}).hasValue());
    auto invalid = source();
    invalid.url = "rtsp://user:secret@camera/live";
    EXPECT_FALSE(store.save({invalid}).hasValue());
    const auto loaded = store.load();
    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value(), std::vector<VideoSourceDefinition>{source()});
}
TEST(VideoSourceCatalog, MalformedOrFutureCatalogIsPreservedOnRead) {
    QTemporaryDir temp;
    const auto path = temp.filePath("sources.json");
    for (const QByteArray& original : {QByteArray("{broken"),
             QByteArray(R"({"schemaVersion":99,"sources":[]})"),
             QByteArray(R"({"schemaVersion":1,"sources":[{}]})")}) {
        QFile output(path);
        ASSERT_TRUE(output.open(QIODevice::WriteOnly));
        ASSERT_EQ(output.write(original), original.size());
        output.close();
        EXPECT_FALSE(VideoSourceCatalog(path).load().hasValue());
        QFile input(path);
        ASSERT_TRUE(input.open(QIODevice::ReadOnly));
        EXPECT_EQ(input.readAll(), original);
    }
}
}
