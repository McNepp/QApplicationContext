#include <QTest>
#include <QSettings>
#include <QTemporaryFile>
#include "appcontexttestclasses.h"
#include "registrationslot.h"
#include "standardapplicationcontext.h"
#include "qtestcase.h"


namespace mcnepp::qtdi {


using namespace mcnepp::qtditest;







class ApplicationContextProfilesTest
 : public QObject {
    Q_OBJECT

public:
    explicit ApplicationContextProfilesTest(QObject* parent = nullptr) : QObject(parent),
            context(nullptr),
            configuration(nullptr) {

    }

    static void initMain() {
        qputenv("QTEST_FUNCTION_TIMEOUT", "10000");
    }


private slots:


    void init() {
        settingsFile.reset(new QTemporaryFile);
        settingsFile->setAutoRemove(true);
        settingsFile->open();
        configuration.reset(new QSettings{settingsFile->fileName(), QSettings::Format::IniFormat});
        context.reset(new StandardApplicationContext{qtditest::testLogging()});
    }

    void cleanup() {
        context.reset();
        settingsFile.reset();
    }

    void testConfigureActiveProfiles() {

        QCOMPARE(context->activeProfiles(), Profiles{"default"});

        Profiles activeProfiles{"unit-test", "integration-test"};

        configuration->setValue("qtdi/enableAutoRefresh", "true"); //Not relevant for Profiles.
        configuration->setValue("qtdi/activeProfiles", QStringList{activeProfiles.begin(), activeProfiles.end()});
        context->registerObject(configuration.get());
        QCOMPARE(context->activeProfiles(), activeProfiles);

        QSettings tempConfig{QSettings::Scope::UserScope, "mcnepp", "qtditest"};
        tempConfig.setValue("qtdi/activeProfiles", "unit-test, mock");
        context->registerObject(&tempConfig);
        activeProfiles << "mock";
        QCOMPARE(context->activeProfiles(), activeProfiles);
    }

    void testConfigureActiveProfilesWithIniFile() {

        QCOMPARE(context->activeProfiles(), Profiles{"default"});

        QTemporaryFile tempFile;
        QVERIFY(tempFile.open());
        tempFile.write("[qtdi]\n");
        //Mind the space after the comma:
        tempFile.write("activeProfiles=unit-test, mock\n");
        tempFile.flush();
        QSettings tempConfig{tempFile.fileName(), QSettings::Format::IniFormat};
        context->registerObject(&tempConfig);
        Profiles expected{"unit-test", "mock"};
        QCOMPARE(context->activeProfiles(), expected);
    }

    void testConfigureActiveProfilesViaEnvironment() {
        auto cleanup = [](QString* str) {
              qputenv("QTDI_ACTIVE_PROFILES", str->toUtf8());
        };
        QString oldEnv = qEnvironmentVariable("QTDI_ACTIVE_PROFILES");
        std::unique_ptr<QString,decltype(cleanup)> ptr{&oldEnv, cleanup};
        //Mind the space after the comma:
        qputenv("QTDI_ACTIVE_PROFILES", "unit-test, mock,unit-test");

        StandardApplicationContext tempContext;
        Profiles expected{"unit-test", "mock"};
        QCOMPARE(tempContext.activeProfiles(), expected);
    }

    void testCannotRegisterServiceForOverlappingProfiles() {
        configuration->setValue("qtdi/activeProfiles", QStringList{"test", "default"});
        context->registerObject(configuration.get());
        auto defaultBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-default"), "base", {"default"});
        QVERIFY(defaultBaseReg);
        QCOMPARE(defaultBaseReg.registeredProfiles(), Profiles{"default"});
        // We deliberately supply a duplicate profile here. It should be silently pruned:
        auto testBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base", {"test", "test"});
        QVERIFY(testBaseReg);
        QCOMPARE(testBaseReg.registeredProfiles(), Profiles{"test"});
        auto testBaseReg2 = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base", {"test", "test"});
        QVERIFY(testBaseReg2);
        QCOMPARE(testBaseReg, testBaseReg2);

        auto testDefaultBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test-default"), "base", {"test", "default"});
        QVERIFY(!testDefaultBaseReg);
    }


    void testRegisterServiceForDifferentProfiles() {

        auto commonBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-common"), "base");

        auto defaultBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-default"), "base-with-profile", {"default"});
        QVERIFY(defaultBaseReg);
        QCOMPARE(defaultBaseReg.registeredProfiles(), Profiles{"default"});
        auto testBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base-with-profile", {"test"});
        QVERIFY(testBaseReg);
        QCOMPARE(testBaseReg.registeredProfiles(), Profiles{"test"});
        QCOMPARE_NE(defaultBaseReg, testBaseReg);

        auto byName = context->getRegistration("base-with-profile");
        QCOMPARE(byName, defaultBaseReg);

        auto byType = context->getRegistration<BaseService>().registeredServices();

        QCOMPARE(byType.size(), 2);
        QVERIFY(byType.contains(commonBaseReg));
        QVERIFY(byType.contains(defaultBaseReg));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test"});
        context->registerObject(configuration.get());

        byName = context->getRegistration("base-with-profile");
        QCOMPARE(byName, testBaseReg);

        byType = context->getRegistration<BaseService>().registeredServices();

        QCOMPARE(byType.size(), 2);
        QVERIFY(byType.contains(commonBaseReg));
        QVERIFY(byType.contains(testBaseReg));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> commonBaseSlot{commonBaseReg, this};
        QVERIFY(commonBaseSlot.last());
        QCOMPARE(commonBaseSlot->foo(), "foo-common");
        RegistrationSlot<BaseService> defaultBaseSlot{defaultBaseReg, this};
        QVERIFY(!defaultBaseSlot.last());
        RegistrationSlot<BaseService> testBaseSlot{testBaseReg, this};
        QVERIFY(testBaseSlot.last());
        QCOMPARE(testBaseSlot->foo(), "foo-test");
        QCOMPARE(testBaseSlot->objectName(), "base-with-profile");
    }

    void testRegisterAnonymousProfileSpecific() {
        auto defaultReg = context->registerService(service<Interface1,BaseService>(), "", {"default"});
        QVERIFY(defaultReg);
        auto testReg = context->registerService(service<Interface1,BaseService>(), "", {"test"});
        QVERIFY(testReg);
        QCOMPARE_NE(defaultReg, testReg);
        auto mockReg = context->registerService(service<BaseService>().advertiseAs<Interface1,TimerAware>(), "", {"mock"});
        QVERIFY(mockReg);
        QCOMPARE_NE(defaultReg, mockReg);
        QCOMPARE_NE(testReg, mockReg);
    }

    void testProfileSpecificDependency() {

        context->registerService(service<Interface1,BaseService>(), "base-with-profile", {"default"});
        context->registerService(service<Interface1,BaseService2>(), "base-with-profile", {"test"});

        auto dependentReg = context->registerService(service<DependentService>(inject<Interface1>()));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test"});
        context->registerObject(configuration.get());

        QVERIFY(context->publish());

        RegistrationSlot<DependentService> dependentSlot{dependentReg, this};
        QVERIFY(dynamic_cast<BaseService2*>(dependentSlot->m_dependency));
    }

    void testAmbiguousRegistrationAtPublish() {

        QVERIFY(context->registerService(service<Interface1,BaseService>(), "base-with-profile", {"default"}));
        QVERIFY(context->registerService(service<Interface1,BaseService2>(), "base-with-profile", {"test"}));


        configuration->setValue("qtdi/activeProfiles", QStringList{"test", "default"});
        context->registerObject(configuration.get());
        //Two services with the same name have been registered for two active profiles.
        //That is ambiguous:
        QVERIFY(!context->getRegistration("base-with-profile"));

        QVERIFY(!context->publish());
    }

    void testAmbiguousAlias() {

        auto defaultReg = context->registerService(service<Interface1,BaseService>(), "base-with-profile-default", {"default"});
        QVERIFY(context->registerService(service<Interface1,BaseService2>(), "base-with-profile-test", {"test"}));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test", "default"});
        context->registerObject(configuration.get());

        QVERIFY(!defaultReg.registerAlias("base-with-profile-test"));
    }

    void testAmbiguousProfileSpecificDependency() {

        context->registerService(service<Interface1,BaseService>(), "base-with-profile", {"default"});
        context->registerService(service<Interface1,BaseService2>(), "base-with-profile", {"test"});
        context->registerService(service<Interface1,BaseService2>(), "base-with-two-profiles", {"test", "default"});

        auto dependentReg = context->registerService(service<DependentService>(inject<Interface1>()));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test", "default"});
        context->registerObject(configuration.get());
        //Two services with the same name have been registered for two active profiles.
        //That is ambiguous:
        QVERIFY(!context->getRegistration("base-with-profile"));

        //One service "base-with-two-profiles" has been registered for two active pofiles.
        //That is Ok:
        QVERIFY(context->getRegistration("base-with-two-profiles"));

        QVERIFY(!context->publish());
    }



private:
    std::unique_ptr<QApplicationContext> context;
    std::unique_ptr<QTemporaryFile> settingsFile;
    std::unique_ptr<QSettings> configuration;
};

} //mcnepp::qtdi

#include "testqapplicationcontextprofiles.moc"


QTEST_MAIN(mcnepp::qtdi::ApplicationContextProfilesTest)
