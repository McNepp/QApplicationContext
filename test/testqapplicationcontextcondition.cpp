#include <QTest>
#include <QSettings>
#include <QTemporaryFile>
#include "appcontexttestclasses.h"
#include "registrationslot.h"
#include "standardapplicationcontext.h"
#include "qtestcase.h"


namespace mcnepp::qtdi {


using namespace mcnepp::qtditest;





class ApplicationContextConditionTest
 : public QObject {
    Q_OBJECT

public:
    explicit ApplicationContextConditionTest(QObject* parent = nullptr) : QObject(parent),
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
        context->registerObject(configuration.get());
    }

    void cleanup() {
        context.reset();
        settingsFile.reset();
    }

    void testConditionAlways() {
        Condition cond = Condition::always();
        QVERIFY(cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::Profile == "test"));
        QVERIFY(cond.overlaps(Condition::Config["test"].exists()));
        QCOMPARE(cond, Condition::always());
        QCOMPARE_NE(cond, Condition::Profile == "test");
        QCOMPARE_NE(cond, Condition::Config["test"].exists());
        QVERIFY(cond.matches(context.get()));
        Condition never = !cond;
        QVERIFY(!never.matches(context.get()));
        QVERIFY(!never.hasProfiles());
        QCOMPARE(cond, !never);
    }

    void testConditionForProfileIn() {
        Profiles expectedProfiles{"test", "default"};
        Condition cond = Condition::Profile & expectedProfiles;
        QVERIFY(!cond.isAlways());
        QVERIFY(cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QVERIFY(cond.overlaps(Condition::Profile == "test"));
        QVERIFY(cond.overlaps(Condition::Profile != "mock"));
        QVERIFY(!cond.overlaps(Condition::Profile == "mock"));
        QCOMPARE(cond, Condition::Profile & expectedProfiles);
        QCOMPARE_NE(cond, Condition::Profile == "test");
        QVERIFY(cond.matches(context.get()));
        static_cast<StandardApplicationContext*>(context.get())->setActiveProfiles({"mock"});
        QVERIFY(!cond.matches(context.get()));
    }


    void testConditionForProfileNotIn() {
        Condition cond = Condition::Profile ^ Profiles{"default", "whatever"};
        QVERIFY(!cond.isAlways());
        QVERIFY(cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QVERIFY(cond.overlaps(Condition::Profile != "default"));
        QVERIFY(cond.overlaps(Condition::Profile == "mock"));
        QVERIFY(!cond.overlaps(Condition::Profile != "mock"));
        QCOMPARE_NE(cond, Condition::Profile == "default");
        QVERIFY(!cond.matches(context.get()));
        static_cast<StandardApplicationContext*>(context.get())->setActiveProfiles({"mock"});
        QVERIFY(cond.matches(context.get()));
    }

    void testConditionForConfigEntryExists() {
        Condition cond = Condition::Config["${test}"].exists();
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QVERIFY(cond.overlaps(Condition::Config["${test}"].exists()));
        QVERIFY(!cond.overlaps(!Condition::Config["${test}"]));
        QVERIFY(!cond.overlaps(Condition::Config["${mock}"].exists()));
        QVERIFY(!cond.overlaps(Condition::Profile == "test"));
        QCOMPARE(cond, Condition::Config["${test}"].exists());
        QCOMPARE_NE(cond, Condition::Config["${mock}"].exists());
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", true);
        QVERIFY(cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, !Condition::Config["${test}"]);
        QCOMPARE(cond, !inverse);

    }

    void testConditionForConfigEntryNotExists() {
        Condition cond = !Condition::Config["${test}"];
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QVERIFY(cond.overlaps(!Condition::Config["${test}"]));
        QVERIFY(!cond.overlaps(Condition::Config["${test}"].exists()));
        QVERIFY(!cond.overlaps(Condition::Config["${mock}"].exists()));
        QVERIFY(!cond.overlaps(Condition::Profile == "test"));
        QCOMPARE(cond, !Condition::Config["${test}"]);
        QCOMPARE_NE(cond, !Condition::Config["${mock}"]);
        QVERIFY(cond.matches(context.get()));
        configuration->setValue("test", true);
        QVERIFY(!cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"].exists());
        QCOMPARE(cond, !inverse);

    }

    void testConditionForConfigEntryEquals() {
        Condition cond = Condition::Config["${test}"] == 42;
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QVERIFY(cond.overlaps(Condition::Config["${test}"] == 42));
        QVERIFY(!cond.overlaps(!Condition::Config["${test}"]));
        QVERIFY(!cond.overlaps(Condition::Config["${mock}"].exists()));
        QVERIFY(!cond.overlaps(Condition::Profile == "test"));
        QCOMPARE(cond, Condition::Config["${test}"] == 42);
        QCOMPARE_NE(cond, Condition::Config["${test}"] == 5);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", true);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 42);
        QVERIFY(cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"] != 42);
        QCOMPARE(cond, !inverse);

    }

    void testConditionForConfigEntryLessThan() {
        Condition cond = Condition::Config["${test}"] < 42;
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QCOMPARE(cond, Condition::Config["${test}"] < 42);
        QCOMPARE_NE(cond, Condition::Config["${test}"] == 42);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 42);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 41);
        QVERIFY(cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"] >= 42);
        QCOMPARE(cond, !inverse);

    }

    void testConditionForConfigEntryLessThanOrEqual() {
        Condition cond = Condition::Config["${test}"] <= 42;
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QCOMPARE(cond, Condition::Config["${test}"] <= 42);
        QCOMPARE_NE(cond, Condition::Config["${test}"] == 42);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 43);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 42);
        QVERIFY(cond.matches(context.get()));
        configuration->setValue("test", 41);
        QVERIFY(cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"] > 42);
        QCOMPARE(cond, !inverse);

    }

    void testConditionForConfigEntryGreaterThan() {
        Condition cond = Condition::Config["${test}"] > 42;
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QCOMPARE(cond, Condition::Config["${test}"] > 42);
        QCOMPARE_NE(cond, Condition::Config["${test}"] == 42);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 42);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 43);
        QVERIFY(cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"] <= 42);
        QCOMPARE(cond, !inverse);

    }

    void testConditionForConfigEntryGreaterThanOrEqual() {
        Condition cond = Condition::Config["${test}"] >= 42;
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QCOMPARE(cond, Condition::Config["${test}"] >= 42);
        QCOMPARE_NE(cond, Condition::Config["${test}"] == 42);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 41);
        QVERIFY(!cond.matches(context.get()));
        configuration->setValue("test", 42);
        QVERIFY(cond.matches(context.get()));
        configuration->setValue("test", 43);
        QVERIFY(cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"] < 42);
        QCOMPARE(cond, !inverse);

    }


    void testConditionForConfigEntryNotEquals() {
        Condition cond = Condition::Config["${test}"] != 42;
        QVERIFY(!cond.isAlways());
        QVERIFY(!cond.hasProfiles());
        QVERIFY(cond.overlaps(Condition::always()));
        QVERIFY(cond.overlaps(Condition::Config["${test}"] != 42));
        QVERIFY(!cond.overlaps(!Condition::Config["${test}"]));
        QVERIFY(!cond.overlaps(Condition::Config["${mock}"].exists()));
        QVERIFY(!cond.overlaps(Condition::Profile == "test"));
        QCOMPARE(cond, Condition::Config["${test}"] != 42);
        QCOMPARE_NE(cond, Condition::Config["${test}"] != 5);
        QVERIFY(cond.matches(context.get()));
        configuration->setValue("test", true);
        QVERIFY(cond.matches(context.get()));
        configuration->setValue("test", 42);
        QVERIFY(!cond.matches(context.get()));
        Condition inverse = !cond;
        QCOMPARE(inverse, Condition::Config["${test}"] == 42);
        QCOMPARE(cond, !inverse);
    }

    void testRegisterServiceForConfigExists() {
        configuration->setValue("timer/singleShot", true);
        auto reg1 = context->registerService(service<QTimer>() << propValue("interval", 4711), "timer", Condition::Config["${timer/singleShot}"].exists());

        QVERIFY(reg1);

        auto reg2 = context->registerService(service<QTimer>() << propValue("interval", 53), "timer", Condition::Config["${timer/interval}"].exists());
        QVERIFY(reg2);

        RegistrationSlot<QTimer> slot1{reg1, this};
        RegistrationSlot<QTimer> slot2{reg2, this};
        QVERIFY(context->publish());

        QVERIFY(slot1);
        QVERIFY(!slot2);
        QCOMPARE(slot1->interval(), 4711);
    }

    void testRegisterServiceForConfigNotExists() {
        configuration->setValue("timer/interval", 1);
        auto reg1 = context->registerService(service<QTimer>() << propValue("interval", 4711), "timer", !Condition::Config["${timer/singleShot}"]);

        QVERIFY(reg1);

        auto reg2 = context->registerService(service<QTimer>() << propValue("interval", 53), "timer", !Condition::Config["${timer/interval}"]);
        QVERIFY(reg2);

        RegistrationSlot<QTimer> slot1{reg1, this};
        RegistrationSlot<QTimer> slot2{reg2, this};
        QVERIFY(context->publish());

        QVERIFY(slot1);
        QVERIFY(!slot2);
        QCOMPARE(slot1->interval(), 4711);
    }

    void testRegisterServiceForConfigEquals() {
        configuration->setValue("timer/interval", 4711);
        auto reg1 = context->registerService(service<QTimer>() << propValue("singleShot", true), "timer", Condition::Config["${timer/interval}"] == 4711);

        QVERIFY(reg1);

        auto reg2 = context->registerService(service<QTimer>() << propValue("singleShot", false), "timer", Condition::Config["${timer/interval}"] == 53);
        QVERIFY(reg2);

        RegistrationSlot<QTimer> slot1{reg1, this};
        RegistrationSlot<QTimer> slot2{reg2, this};
        QVERIFY(context->publish());

        QVERIFY(slot1);
        QVERIFY(!slot2);
        QVERIFY(slot1->isSingleShot());
    }

    void testRegisterServiceForConfigNotEquals() {
        configuration->setValue("timer/interval", 4711);
        auto reg1 = context->registerService(service<QTimer>() << propValue("singleShot", true), "timer", Condition::Config["${timer/interval}"] != 53);

        QVERIFY(reg1);

        auto reg2 = context->registerService(service<QTimer>() << propValue("singleShot", false), "timer", Condition::Config["${timer/interval}"] != 4711);
        QVERIFY(reg2);

        RegistrationSlot<QTimer> slot1{reg1, this};
        RegistrationSlot<QTimer> slot2{reg2, this};
        QVERIFY(context->publish());

        QVERIFY(slot1);
        QVERIFY(!slot2);
        QVERIFY(slot1->isSingleShot());
    }

    void testRegisterServiceForConditionNotEqualsAbsent() {
        auto reg1 = context->registerService(service<QTimer>(), "timer", Condition::Config["${timer/singleShot}"] != true);

        QVERIFY(reg1);

        RegistrationSlot<QTimer> slot1{reg1, this};
        QVERIFY(context->publish());

        QVERIFY(slot1);
    }

    void testRegisterServiceForConditionMatches() {
        configuration->setValue("base/foo", "http://mcnepp.com");
        auto reg1 = context->registerService(service<Interface1,BaseService>() << propValue("foo", "${base/foo}"), "base", Condition::Config["${base/foo}"].matches("http://.*"));

        QVERIFY(reg1);

        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base", Condition::Config["${base/foo}"].matches("file://.*"));
        QVERIFY(reg2);

        RegistrationSlot<Interface1> slot1{reg1, this};
        RegistrationSlot<Interface1> slot2{reg2, this};
        QVERIFY(context->publish());

        QVERIFY(slot1);
        QVERIFY(!slot2);
        QCOMPARE(slot1->foo(), "http://mcnepp.com");
    }

    void testCannotRegisterServiceForOverlappingCondition() {
        configuration->setValue("timer/singleShot", true);
        auto reg1 = context->registerService(service<QTimer>(), "timer");

        QVERIFY(reg1);

        auto reg2 = context->registerService(service<QTimer>(), "timer", Condition::Config["${timer/singleShot}"] == true);
        QVERIFY(!reg2);
    }





private:
    std::unique_ptr<QApplicationContext> context;
    std::unique_ptr<QTemporaryFile> settingsFile;
    std::unique_ptr<QSettings> configuration;
};

} //mcnepp::qtdi

#include "testqapplicationcontextcondition.moc"


QTEST_MAIN(mcnepp::qtdi::ApplicationContextConditionTest)
