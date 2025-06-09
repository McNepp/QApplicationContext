#include "placeholderresolver.h"
#include "applicationcontextimplbase.h"
#include <QSettings>
#include <QTemporaryFile>
#include <QTest>


namespace mcnepp::qtdi::detail {

class MockConfigurationResolver : public ApplicationContextImplBase<QApplicationContext> {
public:
    explicit MockConfigurationResolver() :
        ApplicationContextImplBase<QApplicationContext>{} {

    }


    QVariant getConfigurationValue(const QString &key, bool searchParentSections) const override
    {
        QString searchKey = key;
        do {
            lookupKeys.push_back(searchKey);
            if(QVariant resolved = base_t::getConfigurationValue(searchKey, false); resolved.isValid()) {
                return resolved;
            }
        } while(searchParentSections && removeLastConfigPath(searchKey));
        return {};
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
        configResolver = std::make_unique<MockConfigurationResolver>();
        configResolver->registerObject(settings.get());
    }

    void testResolveLiteral() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, world!", configResolver.get());
        QVERIFY(resolver);
        QVERIFY(!resolver->hasPlaceholders());
        QCOMPARE(resolver->resolve(), "Hello, world!");
        QVERIFY(configResolver->lookupKeys.isEmpty());
    }

    void testResolveSimplePlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit}", configResolver.get());
        QVERIFY(resolver);
        QVERIFY(resolver->hasPlaceholders());
        settings->setValue("sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testResolvePlaceholderInSection() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${test/sayit}", configResolver.get());
        QVERIFY(resolver);
        settings->setValue("test/sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"test/sayit"});
    }

    void testResolvePlaceholderInConfigSection() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit}", configResolver.get());
        QVERIFY(resolver);
        settings->setValue("test/sayit", "Hello, world!");
        QCOMPARE(resolver->resolve("test"), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"test/sayit"});
    }


    void testResolvePlaceholderInSectionRecursive() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${*/tests/test/sayit}", configResolver.get());
        QVERIFY(resolver);
        settings->setValue("sayit", "Hello, world!");
        QCOMPARE(resolver->resolve(), "Hello, world!");
        QStringList expected{"tests/test/sayit", "tests/sayit", "sayit"};
        QCOMPARE(configResolver->lookupKeys, expected);
    }

    void testResolveEmbeddedPlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, ${sayit}!", configResolver.get());
        QVERIFY(resolver);
        settings->setValue("sayit", "world");
        QCOMPARE(resolver->resolve(), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testResolveFromPrivateProperty() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, ${sayit}!", configResolver.get());
        QVERIFY(resolver);
        QVariantMap cfg;
        cfg.insert("sayit", "world");
        QCOMPARE(resolver->resolve("", cfg), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }


    void testResolveRecursiveFromPrivateProperty() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, ${sayit}!", configResolver.get());
        QVERIFY(resolver);

        settings->setValue("text", "world");
        QVariantMap cfg;
        cfg.insert("sayit", "${text}");
        QCOMPARE(resolver->resolve("", cfg), "Hello, world!");
        QStringList expected{"sayit", "text"};
        QCOMPARE(configResolver->lookupKeys, expected);
        QCOMPARE(cfg["sayit"], "world");
    }

    void testResolveGroup() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("Hello, ${sayit}!", configResolver.get());
        settings->setValue("sub/sayit", "world");
        QVariantMap cfg;
        cfg.insert("group", "sub");
        QCOMPARE(resolver->resolve("${group}", cfg), "Hello, world!");
    }


    void testResolveDefaultValue() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit:Hello, world!}", configResolver.get());
        QVERIFY(resolver);
        QCOMPARE(resolver->resolve(), "Hello, world!");
        QCOMPARE(configResolver->lookupKeys, QStringList{"sayit"});
    }

    void testEscapeDollar() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("price: ${amount}\\$", configResolver.get());
        QVERIFY(resolver);
        settings->setValue("amount", 42);
        QCOMPARE(resolver->resolve(), "price: 42$");
    }

    void testEscapeOpeningBracket() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("$\\{placeholder}", configResolver.get());
        QVERIFY(resolver);
        QVERIFY(!resolver->hasPlaceholders());
        QCOMPARE(resolver->resolve(), "${placeholder}");
    }


    void testUnbalanced() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${sayit", configResolver.get());
        QVERIFY(!resolver);
    }

    void testInvalidDollarInPlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${A dollar$}", configResolver.get());
        QVERIFY(!resolver);
    }

    void testInvalidWildcardInPlaceholder() {
        PlaceholderResolver* resolver = PlaceholderResolver::parse("${*A dollar}", configResolver.get());
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
