#include "placeholderresolver.h"
#include <QSettings>
#include <QTemporaryFile>
#include <QTest>


namespace mcnepp::qtdi::detail {

class MockConfigurationResolver : public QConfigurationResolver {
public:
    explicit MockConfigurationResolver(QSettings* settings) :
        m_settings{settings} {

    }

private:
    QSettings* m_settings;

    // QConfigurationResolver interface
public:
    QVariant getConfigurationValue(const QString &key, bool searchParentSections) const override
    {
        lookupKeys.push_back(key);
        QVariant result = m_settings->value(key);
        if(!result.isValid() && searchParentSections) {
            if(QString k = key; removeLastPath(k)) {
                return getConfigurationValue(k, searchParentSections);
            }
        }
        return result;
    }
    QConfigurationWatcher *watchConfigValue(const QString &) override
    {
        return nullptr;
    }
    bool autoRefreshEnabled() const override
    {
        return false;
    }

    mutable QStringList lookupKeys;
};


class TestPlaceholderResolver : public QObject {
    Q_OBJECT

private slots:

    void init() {
        tempFile = std::make_unique<QTemporaryFile>();
        tempFile->open();
        settings = std::make_unique<QSettings>(tempFile->fileName(), QSettings::IniFormat);
        configResolver = std::make_unique<MockConfigurationResolver>(settings.get());
    }

    void testResolveLiteral() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, world!", this);
        QVERIFY(resolver);
        QVERIFY(!resolver->hasPlaceholders());
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "Hello, world!");
        QVERIFY(configResolver->lookupKeys.isEmpty());
    }

    void testResolveSimplePlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit}", this);
        QVERIFY(resolver);
        QVERIFY(resolver->hasPlaceholders());
        settings->setValue("sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testResolvePlaceholderInSection() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${test/sayit}", this);
        QVERIFY(resolver);
        settings->setValue("test/sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"test/sayit"});
    }

    void testResolvePlaceholderInConfigSection() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit}", this);
        QVERIFY(resolver);
        settings->setValue("test/sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(configResolver.get(), config().withGroup("test")), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"test/sayit"});
    }


    void testResolvePlaceholderInSectionRecursive() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${*/tests/test/sayit}", this);
        QVERIFY(resolver);
        settings->setValue("sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(configResolver.get(), config()), "Hello, world!");
        QStringList expected{"tests/test/sayit", "tests/sayit", "sayit"};
        QCOMPARE(configResolver->lookupKeys, expected);
    }

    void testResolveEmbeddedPlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, ${sayit}!", this);
        QVERIFY(resolver);
        settings->setValue("sayit", "world");
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testResolveFromPrivateProperty() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, ${sayit}!", this);
        QVERIFY(resolver);
        QCOMPARE(resolver->resolve(configResolver.get(), config({{".sayit", "world"}})), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testResolveDefaultValue() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit:Hello, world!}", this);
        QVERIFY(resolver);
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testEscapeDollar() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("price: ${amount}\\$", this);
        QVERIFY(resolver);
        settings->setValue("amount", 42);
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "price: 42$");
    }

    void testEscapeOpeningBracket() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("$\\{placeholder}", this);
        QVERIFY(resolver);
        QVERIFY(!resolver->hasPlaceholders());
        QCOMPARE(resolver->resolve(configResolver.get(), service_config{}), "${placeholder}");
    }


    void testUnbalanced() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit", this);
        QVERIFY(!resolver);
    }

    void testInvalidDollarInPlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${A dollar$}", this);
        QVERIFY(!resolver);
    }

    void testInvalidWildcardInPlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${*A dollar}", this);
        QVERIFY(!resolver);
    }



private:
    std::unique_ptr<QTemporaryFile> tempFile;
    std::unique_ptr<QSettings> settings;
    std::unique_ptr<MockConfigurationResolver> configResolver;

};
}

#include "testplaceholderresolver.moc"


QTEST_MAIN(mcnepp::qtdi::detail::TestPlaceholderResolver)
