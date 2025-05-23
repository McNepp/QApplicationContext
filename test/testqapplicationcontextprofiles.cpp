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
        QFileInfo info{configuration->fileName()};

        qInfo().nospace() << info.path() << " " << info.fileName() << " " << info.baseName();

        QFileInfo profileInto{info.path(), info.baseName()+"-test."+info.suffix()};
        qInfo().nospace() << profileInto.path() << " " << profileInto.fileName() << " " << profileInto.baseName();

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

    void testCannotSetEmptyProfiles() {
        StandardApplicationContext* ctx = static_cast<StandardApplicationContext*>(context.get());
        ctx->setActiveProfiles(Profiles{});
        QCOMPARE(context->activeProfiles(), Profiles{"default"});
    }

    void testChangeActiveProfilesAfterPublish() {
        StandardApplicationContext* ctx = static_cast<StandardApplicationContext*>(context.get());

        Profiles activeProfiles = context->activeProfiles();
        connect(ctx, &StandardApplicationContext::activeProfilesChanged, this, [&activeProfiles](const Profiles& profiles) {
            activeProfiles = profiles;
        });
        configuration->setValue("qtdi/activeProfiles", QStringList{"unit-test"});
        context->registerService<BaseService>();
        QVERIFY(context->publish());
        //The only published Service does not depend on any profile. Thus, we can change the active profiles:
        ctx->setActiveProfiles(Profiles{"integration-test"});
        QCOMPARE(activeProfiles, Profiles{"integration-test"});
        //The only published Service does not depend on any profile. Thus, registering the QSettings will add another active profiles:
        context->registerObject(configuration.get());
        Profiles expectedActiveProfiles{"unit-test", "integration-test"};
        QCOMPARE(activeProfiles, expectedActiveProfiles);
    }

    void testCannotChangeActiveProfilesAfterPublish() {
        StandardApplicationContext* ctx = static_cast<StandardApplicationContext*>(context.get());
        Profiles activeProfiles = context->activeProfiles();
        connect(ctx, &StandardApplicationContext::activeProfilesChanged, this, [&activeProfiles](const Profiles& profiles) {
            activeProfiles = profiles;
        });
        configuration->setValue("qtdi/activeProfiles", QStringList{"unit-test"});
        auto reg = context->registerService(service<BaseService>(), "base", Condition::Profile == "default");
        QVERIFY(context->publish());
        //The only published Service depends on a profile. Thus, we cannot change the active profiles:
        ctx->setActiveProfiles(Profiles{"integration-test"});
        QCOMPARE(activeProfiles, Profiles{"default"});
        //The only published Service depends on a profile. Thus, registering the QSettings will not add another active profiles:
        context->registerObject(configuration.get());
        QCOMPARE(activeProfiles, Profiles{"default"});
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

    void testProfileSpecificPropertiesInFile() {
        configuration->setValue("timer/interval", 5000);
        configuration->setValue("timer/singleShot", true);
        configuration -> setValue("qtdi/activeProfiles", "test");
        configuration->setValue("qtdi/enableProfileSpecificSettings", true);
        QFileInfo info{settingsFile->fileName()};
        QString withProfile = QDir{info.path()}.filePath(info.completeBaseName()+"-test."+info.suffix());
        auto removeFile = [](QFile* file) { file->remove(); delete file;};
        std::unique_ptr<QFile,decltype(removeFile)> profileSpecific{new QFile{withProfile}, removeFile};

        QVERIFY(profileSpecific->open(QIODeviceBase::WriteOnly | QIODeviceBase::Truncate));
        profileSpecific->write("[timer]\n");
        profileSpecific->write("interval=4711");
        profileSpecific->close();
        context->registerObject(configuration.get());

        auto timerReg = context->registerService(service<QTimer>() << withAutowire, "timer");
        RegistrationSlot<QTimer> timerSlot{timerReg, this};
        QVERIFY(context->publish());
        QCOMPARE(timerSlot->interval(), 4711);
        QVERIFY(timerSlot->isSingleShot());
    }

    void testProfileSpecificPropertiesNative() {
        QSettings settings{QSettings::NativeFormat, QSettings::UserScope, "mcnepp", "qtditest"};
        settings.setValue("timer/interval", 5000);
        settings.setValue("timer/singleShot", true);
        settings.setValue("qtdi/activeProfiles", "test");
        settings.setValue("qtdi/enableProfileSpecificSettings", true);

        {
            QSettings profileSpecific{QSettings::NativeFormat, QSettings::UserScope, "mcnepp", "qtditest-test"};
            profileSpecific.setValue("timer/interval", 4711);
        } //QSettings goes out of scope but leaves persistent configuration-entries in place.

        context->registerObject(&settings);

        auto timerReg = context->registerService(service<QTimer>() << withAutowire, "timer");
        RegistrationSlot<QTimer> timerSlot{timerReg, this};
        QVERIFY(context->publish());
        QCOMPARE(timerSlot->interval(), 4711);
        QVERIFY(timerSlot->isSingleShot());
    }

    void testProfileSpecificPropertyAsCondition() {
        QSettings settings{QSettings::NativeFormat, QSettings::UserScope, "mcnepp", "qtditest"};
        settings.setValue("qtdi/activeProfiles", "test");
        {
            QSettings profileSpecific{QSettings::NativeFormat, QSettings::UserScope, "mcnepp", "qtditest-test"};
            profileSpecific.setValue("timer/singleShot", true);
        } //QSettings goes out of scope but leaves persistent configuration-entries in place.

        context->registerObject(&settings);

        auto timerReg = context->registerService(service<QTimer>() << propValue("singleShot", "${timer/singleShot}"), "timer", Condition::Config["${timer/singleShot}"] == true);
        RegistrationSlot<QTimer> timerSlot{timerReg, this};
        QVERIFY(context->publish());
        QVERIFY(timerSlot);
        QVERIFY(timerSlot->isSingleShot());
    }

    void testProfileSpecificPropertiesAutoRefresh() {
        QSettings settings{QSettings::NativeFormat, QSettings::UserScope, "mcnepp", "qtditest"};
        settings.setValue("timer/interval", 5000);
        settings.setValue("qtdi/enableAutoRefresh", true);
        settings.setValue("qtdi/autoRefreshMillis", 100);
        settings.setValue("qtdi/activeProfiles", "test");
        settings.setValue("qtdi/enableProfileSpecificSettings", true);

        QSettings profileSpecific{QSettings::NativeFormat, QSettings::UserScope, "mcnepp", "qtditest-test"};
        profileSpecific.setValue("timer/interval", 4711);

        context->registerObject(&settings);

        auto timerReg = context->registerService(service<QTimer>() << autoRefresh("interval", "${timer/interval}"), "timer");
        RegistrationSlot<QTimer> timerSlot{timerReg, this};
        QVERIFY(context->publish());
        QCOMPARE(timerSlot->interval(), 4711);

        profileSpecific.setValue("timer/interval", 1812);
        profileSpecific.sync();
        QVERIFY(QTest::qWaitFor([&timerSlot] { return timerSlot->interval() == 1812;}, 1000));
        settings.setValue("qtdi/enableAutoRefresh", false);
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
        auto defaultBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-default"), "base", Condition::Profile == "default");
        QVERIFY(defaultBaseReg);
        // We deliberately supply a duplicate Profile here. It should be silently pruned:
        auto testBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base", Condition::Profile & Profiles{"test", "test"});
        QVERIFY(testBaseReg);
        auto testBaseReg2 = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base", Condition::Profile & Profiles{"test", "test"});
        QVERIFY(testBaseReg2);
        QCOMPARE(testBaseReg, testBaseReg2);

        auto testOverlappingProfileReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test-default"), "base", Condition::Profile & Profiles{"test", "default"});
        QVERIFY(!testOverlappingProfileReg);

        auto testOverlappingNegatedProfileReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test-default"), "base", Condition::Profile != "mock");
        QVERIFY(!testOverlappingNegatedProfileReg);

    }


    void testRegisterServiceForDifferentProfiles() {

        auto commonBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-common"), "base");
        QVERIFY(!commonBaseReg.registeredCondition().hasProfiles());

        auto defaultBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-default"), "base-with-profile", Condition::Profile == "default");
        QVERIFY(defaultBaseReg);
        QVERIFY(defaultBaseReg.registeredCondition().hasProfiles());
        auto testBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base-with-profile", Condition::Profile == "test");
        QVERIFY(testBaseReg);
        QVERIFY(testBaseReg.registeredCondition().hasProfiles());
        QCOMPARE_NE(defaultBaseReg, testBaseReg);

        auto byName = context->getRegistration("base-with-profile");
        QCOMPARE(byName, defaultBaseReg);

        auto byType = context->getRegistration<BaseService>().registeredServices();

        QCOMPARE(byType.size(), 3);
        QVERIFY(byType.contains(commonBaseReg));
        QVERIFY(byType.contains(defaultBaseReg));
        QVERIFY(byType.contains(testBaseReg));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test"});
        context->registerObject(configuration.get());

        byName = context->getRegistration("base-with-profile");
        QCOMPARE(byName, testBaseReg);


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

    void testRegisterServiceForProfileNotIn() {

        auto commonBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-common"), "base");

        auto defaultBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-default"), "base-with-profile", Condition::Profile != "test");
        QVERIFY(defaultBaseReg);

        auto testBaseReg = context->registerService(service<BaseService>() << propValue("foo", "foo-test"), "base-with-profile", Condition::Profile != "default");
        QVERIFY(testBaseReg);
        QCOMPARE_NE(defaultBaseReg, testBaseReg);

        auto byName = context->getRegistration("base-with-profile");
        QCOMPARE(byName, defaultBaseReg);

        auto byType = context->getRegistration<BaseService>().registeredServices();

        QCOMPARE(byType.size(), 3);
        QVERIFY(byType.contains(commonBaseReg));
        QVERIFY(byType.contains(defaultBaseReg));
        QVERIFY(byType.contains(testBaseReg));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test"});
        context->registerObject(configuration.get());

        byName = context->getRegistration("base-with-profile");
        QCOMPARE(byName, testBaseReg);


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
        auto defaultReg = context->registerService(service<Interface1,BaseService>(), "", Condition::Profile == "default");
        QVERIFY(defaultReg);
        auto testReg = context->registerService(service<Interface1,BaseService>(), "", Condition::Profile == "test");
        QVERIFY(testReg);
        QCOMPARE_NE(defaultReg, testReg);
        auto mockReg = context->registerService(service<BaseService>().advertiseAs<Interface1,TimerAware>(), "", Condition::Profile == "mock");
        QVERIFY(mockReg);
        QCOMPARE_NE(defaultReg, mockReg);
        QCOMPARE_NE(testReg, mockReg);
    }

    void testProfileSpecificDependency() {

        context->registerService(service<Interface1,BaseService>(), "base-with-profile", Condition::Profile == "default");
        context->registerService(service<Interface1,BaseService2>(), "base-with-profile", Condition::Profile == "test");

        auto dependentReg = context->registerService(service<DependentService>(inject<Interface1>()));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test"});
        context->registerObject(configuration.get());

        QVERIFY(context->publish());

        RegistrationSlot<DependentService> dependentSlot{dependentReg, this};
        QVERIFY(dynamic_cast<BaseService2*>(dependentSlot->m_dependency));
    }

    void testProfileSpecificDependencies() {
        auto baseReg = context->registerService(service<Interface1,BaseService>(), "base");
        context->registerService(service<Interface1,BaseService>(), "base-with-profile", Condition::Profile == "default");
        auto testReg = context->registerService(service<Interface1,BaseService2>(), "base-with-profile", Condition::Profile == "test");

        auto dependentReg = context->registerService(service<CardinalityNService>(injectAll<Interface1>()));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test"});
        context->registerObject(configuration.get());

        QVERIFY(context->publish());

        RegistrationSlot<Interface1> baseSlot{baseReg, this};
        RegistrationSlot<Interface1> testSlot{testReg, this};
        RegistrationSlot<CardinalityNService> dependentSlot{dependentReg, this};
        QVERIFY(dependentSlot);
        QCOMPARE(dependentSlot->my_bases.size(), 2);
        QVERIFY(dependentSlot->my_bases.contains(baseSlot.last()));
        QVERIFY(dependentSlot->my_bases.contains(testSlot.last()));
    }

    void testRegistrationNever() {
        Condition always = Condition::always();
        auto alwaysReg = context->registerService(service<Interface1,BaseService>(), "base", always);
        QVERIFY(alwaysReg);
        auto neverReg = context->registerService(service<Interface1,BaseService2>(), "base", !always);
        QVERIFY(neverReg);

        RegistrationSlot<Interface1> alwaysSlot{alwaysReg, this};
        RegistrationSlot<Interface1> neverSlot{neverReg, this};
        QVERIFY(context->publish());
        QVERIFY(alwaysSlot);
        QVERIFY(!neverSlot);
    }


    void testAmbiguousRegistrationAtPublish() {

        QVERIFY(context->registerService(service<Interface1,BaseService>(), "base-with-profile", Condition::Profile == "default"));
        QVERIFY(context->registerService(service<Interface1,BaseService2>(), "base-with-profile", Condition::Profile == "test"));


        configuration->setValue("qtdi/activeProfiles", QStringList{"test", "default"});
        context->registerObject(configuration.get());
        //Two services with the same name have been registered for two active profiles.
        //That is ambiguous:
        QVERIFY(!context->getRegistration("base-with-profile"));

        QVERIFY(!context->publish());
    }

    void testAmbiguousAlias() {

        auto defaultReg = context->registerService(service<Interface1,BaseService>(), "base-with-profile-default", Condition::Profile == "default");
        QVERIFY(context->registerService(service<Interface1,BaseService2>(), "base-with-profile-test", Condition::Profile == "test"));

        configuration->setValue("qtdi/activeProfiles", QStringList{"test", "default"});
        context->registerObject(configuration.get());

        QVERIFY(!defaultReg.registerAlias("base-with-profile-test"));
    }

    void testAmbiguousProfileSpecificDependency() {

        context->registerService(service<Interface1,BaseService>(), "base-with-profile", Condition::Profile == "default");
        context->registerService(service<Interface1,BaseService2>(), "base-with-profile", Condition::Profile == "test");
        context->registerService(service<Interface1,BaseService2>(), "base-with-two-profiles", Condition::Profile & Profiles{"test", "default"});

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
