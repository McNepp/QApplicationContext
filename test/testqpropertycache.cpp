#include <QTest>
#include <QTemporaryFile>
#include "appcontexttestclasses.h"
#include "qpropertycache.h"
#include "qtestcase.h"


namespace mcnepp::qtdi::detail {


using namespace mcnepp::qtditest;







class QPropertyCacheTest
 : public QObject {
    Q_OBJECT

public:
    explicit QPropertyCacheTest(QObject* parent = nullptr) : QObject(parent),
            propertyCache(nullptr) {

    }


private slots:


    void init() {
        propertyCache.reset(new detail::QPropertyCache);
    }

    void cleanup() {
        propertyCache.reset();
    }


    void testQSSetterEquivalenceForMember() {
        q_setter_t fooSetter1 = adaptSetter<BaseService,QString>(&BaseService::setFoo);
        q_setter_t fooSetter2 = adaptSetter<BaseService,QString>(&BaseService::setFoo);
        QCOMPARE(fooSetter1, fooSetter2);
    }

    void testQSSetterEquivalenceForFunctionPointer() {
        void (*setFoo)(BaseService*,const QString&) = [](BaseService* srv, const QString& foo) { srv->setFoo(foo);};
        q_setter_t fooSetter1 = adaptSetter<BaseService,QString>(setFoo);
        q_setter_t fooSetter2 = adaptSetter<BaseService,QString>(setFoo);
        QCOMPARE(fooSetter1, fooSetter2);
    }

    void testQSSetterHash() {
        q_setter_t fooSetter1;
        q_setter_t fooSetter2;
        QCOMPARE(hashCode(fooSetter1), hashCode(fooSetter2));
        fooSetter2 = adaptSetter<BaseService,QString>(&BaseService::setFoo);
        QCOMPARE_NE(hashCode(fooSetter1), hashCode(fooSetter2));
        fooSetter1 = adaptSetter<BaseService,QString>(&BaseService::setFoo);
        QCOMPARE(hashCode(fooSetter1), hashCode(fooSetter2));
    }


    void testFindNameBySetter() {
        BaseService base;
        q_setter_t fooSetter = adaptSetter<BaseService,QString>(&BaseService::setFoo);

        QMetaProperty prop = propertyCache->tryToFindPropertyBySetter(&base, fooSetter, "Hello, world");
        QCOMPARE(prop.name(), "foo");
        QCOMPARE(base.foo(), "Hello, world");
    }

    void testSetProperty() {
        BaseService base;
        property_descriptor descr{"", adaptSetter<BaseService,QString>(&BaseService::setFoo)};

        propertyCache->setProperty(descr, &base, "Hello, world");
        QCOMPARE(descr.name, "foo");
        QCOMPARE(base.foo(), "Hello, world");
    }

    void testCannotFindNameBySetterIfValueDoesNotChange() {
        BaseService base;
        q_setter_t fooSetter = adaptSetter<BaseService,QString>(&BaseService::setFoo);

        QMetaProperty prop = propertyCache->tryToFindPropertyBySetter(&base, fooSetter, base.foo());
        QVERIFY(!prop.isValid());
    }

    void testNameInCache() {
        testFindNameBySetter();
        BaseService base;
        q_setter_t fooSetter = adaptSetter<BaseService,QString>(&BaseService::setFoo);

        QMetaProperty prop = propertyCache->tryToFindPropertyBySetter(&base, fooSetter, base.foo());
        QCOMPARE(prop.name(), "foo");
    }


    void testFindNameWithBinding() {
        BaseService2 base;
        q_setter_t fooSetter = adaptSetter<BaseService2,QString>(&BaseService2::setFoo);

        QMetaProperty prop = propertyCache->tryToFindPropertyBySetter(&base, fooSetter, "Hello, world");
        QCOMPARE(prop.name(), "foo");
        QCOMPARE(base.foo(), "Hello, world");
    }

    void testCannotFindNameWithBindingIfValueDoesNotChange() {
        BaseService2 base;
        q_setter_t fooSetter = adaptSetter<BaseService2,QString>(&BaseService2::setFoo);

        QMetaProperty prop = propertyCache->tryToFindPropertyBySetter(&base, fooSetter, base.foo());
        QVERIFY(!prop.isValid());
    }



private:
    std::unique_ptr<QPropertyCache> propertyCache;
};

} //mcnepp::qtdi

#include "testqpropertycache.moc"


QTEST_MAIN(mcnepp::qtdi::detail::QPropertyCacheTest)
