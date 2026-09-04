#include <lumora/configuration/ApplicationConfiguration.hpp>
#include <lumora/configuration/ConfigurationStore.hpp>

#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using lumora::configuration::ApplicationConfiguration;
using lumora::configuration::ConfigurationStore;

[[nodiscard]] std::filesystem::path pathFromQString(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const auto encoded = path.toUtf8();
    return std::filesystem::path(
        std::string(encoded.constData(), static_cast<std::size_t>(encoded.size())));
#endif
}

[[nodiscard]] QByteArray readAll(const std::filesystem::path& path) {
#ifdef _WIN32
    QFile file(QString::fromStdWString(path.native()));
#else
    QFile file(QString::fromUtf8(path.native()));
#endif
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void writeAll(const std::filesystem::path& path, const QByteArray& contents) {
#ifdef _WIN32
    QFile file(QString::fromStdWString(path.native()));
#else
    QFile file(QString::fromUtf8(path.native()));
#endif
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(contents), contents.size());
    file.close();
}

[[nodiscard]] QByteArray validConfigurationJson() {
    return R"json({
        "schemaVersion": 1,
        "application": {},
        "cameraProfiles": {},
        "processing": {},
        "presets": {},
        "capture": {},
        "ui": {}
    })json";
}

TEST(ConfigurationStore, MissingFileReturnsValidatedDefaults) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    ConfigurationStore store(pathFromQString(temp.path()) / "config.json");

    auto loaded = store.load();

    ASSERT_TRUE(loaded.hasValue());
    EXPECT_EQ(loaded.value().schemaVersion, ApplicationConfiguration::CurrentSchemaVersion);
    EXPECT_TRUE(loaded.value().usedDefaults);
    EXPECT_FALSE(loaded.value().loadWarning.has_value());
    EXPECT_FALSE(loaded.value().preservedInvalidFile.has_value());
}

TEST(ConfigurationStore, ValidConfigurationRoundTripsIncludingUnicode) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = pathFromQString(temp.path()) / u8"إعدادات" / "config.json";
    ConfigurationStore store(path);
    ApplicationConfiguration configuration;
    configuration.application.insert("operatorLabel", QString::fromUtf8("اختبار الأشعة"));
    configuration.processing.insert("preset", "Standard");

    const auto saved = store.save(configuration);
    ASSERT_TRUE(saved.hasValue()) << saved.error().diagnosticDetail;
    const auto loaded = store.load();

    ASSERT_TRUE(loaded.hasValue());
    EXPECT_FALSE(loaded.value().usedDefaults);
    EXPECT_EQ(loaded.value().application, configuration.application);
    EXPECT_EQ(loaded.value().processing, configuration.processing);
}

class InvalidConfigurationTest : public ::testing::TestWithParam<std::pair<const char*, QByteArray>> {};

TEST_P(InvalidConfigurationTest, PreservesInvalidInputAndReturnsDefaultsWithWarning) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = pathFromQString(temp.path()) / "config.json";
    const auto& [description, contents] = GetParam();
    SCOPED_TRACE(description);
    writeAll(path, contents);
    ConfigurationStore store(path);

    auto loaded = store.load();

    ASSERT_TRUE(loaded.hasValue());
    EXPECT_TRUE(loaded.value().usedDefaults);
    ASSERT_TRUE(loaded.value().loadWarning.has_value());
    EXPECT_EQ(loaded.value().loadWarning->category, lumora::core::ErrorCategory::Configuration);
    ASSERT_TRUE(loaded.value().preservedInvalidFile.has_value());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(*loaded.value().preservedInvalidFile));
    EXPECT_EQ(readAll(*loaded.value().preservedInvalidFile), contents);
}

INSTANTIATE_TEST_SUITE_P(
    FailureMatrix,
    InvalidConfigurationTest,
    ::testing::Values(
        std::pair{"truncated JSON", QByteArray{"{\"schemaVersion\":"}},
        std::pair{"wrong root type", QByteArray{"[]"}},
        std::pair{"missing schema", QByteArray{"{\"application\":{}}"}},
        std::pair{"future schema", QByteArray{"{\"schemaVersion\":2}"}},
        InvalidConfigurationTest::ParamType{
            "non-object section",
            QByteArray{R"json({"schemaVersion":1,"application":[],"cameraProfiles":{},"processing":{},"presets":{},"capture":{},"ui":{}})json"}}),
    [](const ::testing::TestParamInfo<InvalidConfigurationTest::ParamType>& info) {
        return std::string{"Case"} + std::to_string(info.index);
    });

TEST(ConfigurationStore, UnavailableParentReturnsTypedFailure) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto blockingFile = pathFromQString(temp.path()) / "not-a-directory";
    writeAll(blockingFile, "occupied");
    ConfigurationStore store(blockingFile / "config.json");

    const auto saved = store.save(ApplicationConfiguration{});

    ASSERT_FALSE(saved.hasValue());
    EXPECT_EQ(saved.error().category, lumora::core::ErrorCategory::Configuration);
    EXPECT_EQ(saved.error().code, "configuration_directory_unavailable");
}

TEST(ConfigurationStore, ReplaceFailureDoesNotDamageExistingTarget) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto target = pathFromQString(temp.path()) / "config.json";
    ASSERT_TRUE(std::filesystem::create_directory(target));
    writeAll(target / "marker", "do-not-remove");
    ConfigurationStore store(target);

    const auto saved = store.save(ApplicationConfiguration{});

    ASSERT_FALSE(saved.hasValue());
    EXPECT_EQ(saved.error().code, "configuration_replace_failed");
    EXPECT_EQ(readAll(target / "marker"), "do-not-remove");
}

TEST(ConfigurationStore, ValidationFailureLeavesPriorValidContentUntouched) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = pathFromQString(temp.path()) / "config.json";
    writeAll(path, validConfigurationJson());
    const auto original = readAll(path);
    ApplicationConfiguration invalid;
    invalid.schemaVersion = ApplicationConfiguration::CurrentSchemaVersion + 1;
    ConfigurationStore store(path);

    const auto saved = store.save(invalid);

    ASSERT_FALSE(saved.hasValue());
    EXPECT_EQ(saved.error().code, "configuration_future_schema");
    EXPECT_EQ(readAll(path), original);
}

}  // namespace
