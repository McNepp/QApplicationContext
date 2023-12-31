#include <QTest>
#include <QSettings>
#include <QTemporaryFile>
#include "standardqapplicationcontext.h"
#include "appcontexttestclasses.h"
#include "qtestcase.h"

namespace mcnepp::qtdi {

using namespace qtditest;

template<> struct service_factory<BaseService> {
    BaseService* operator()() const {
        return new BaseService;
    }

    BaseService* operator()(CyclicDependency* dep) const {
        return new BaseService{dep};
    }

};
}

namespace mcnepp::qtditest {

template<typename S> class RegistrationSlot : public QObject {
public:

    explicit RegistrationSlot(ServiceRegistration<S> registration) : m_obj(nullptr),
        m_invocationCount(0) {
        registration.subscribe(this, &RegistrationSlot::setObj);
    }


    S* operator->() const {
        return m_obj;
    }

    S* operator()() const {
        return m_obj;
    }


    void setObj(S* obj) {
        m_obj = obj;
        ++m_invocationCount;
    }

    bool operator ==(const RegistrationSlot& other) const {
        return m_obj == other.m_obj;
    }

    bool operator !=(const RegistrationSlot& other) const {
        return m_obj != other.m_obj;
    }

    int invocationCount() const {
        return m_invocationCount;
    }

private:
    S* m_obj;
    int m_invocationCount;
};

class PostProcessor : public QObject, public QApplicationContextPostProcessor {
public:
    explicit PostProcessor(QObject* parent = nullptr) : QObject(parent) {}

    // QApplicationContextServicePostProcessor interface
    void process(QApplicationContext *appContext, QObject *service, const QVariantMap& additionalInfos) override {
        if(additionalInfos.contains(".store")) {
            processedObjects.push_back(service);
        }
    }

    QObjectList processedObjects;
};

class ApplicationContextTest
 : public QObject {
    Q_OBJECT

public:
    explicit ApplicationContextTest(QObject* parent = nullptr) : QObject(parent),
            context(nullptr),
            config(nullptr) {

    }

private slots:


    void init() {
        settingsFile = new QTemporaryFile;
        settingsFile->open();
        config = new QSettings{settingsFile->fileName(), QSettings::Format::IniFormat};
        context = new StandardApplicationContext;
    }

    void cleanup() {
        delete context;
        delete config;
        delete settingsFile;
    }




    void testNoDependency() {
        bool baseHasFactory = detail::has_service_factory<BaseService>;
        QVERIFY(baseHasFactory);
        auto reg = context->registerService<BaseService>();
        QVERIFY(reg);
        QVERIFY(!context->getRegistration<BaseService>("anotherName"));
        QCOMPARE(context->getRegistration<BaseService>(reg.registeredName()), reg);
        QCOMPARE(reg.unwrap()->service_type(), typeid(BaseService));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QVERIFY(slot());
    }

    void testWithProperty() {
        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", 4711}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }


    void testWithPlaceholderProperty() {
        config->setValue("timerInterval", 4711);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", "${timerInterval}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }

    void testWithEscapedPlaceholderProperty() {

        auto reg = context->registerService<QTimer>("", make_config({{"objectName", "\\${timerName}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->objectName(), "${timerName}");
    }

    void testPlaceholderPropertyUsesDefaultValue() {

        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", "${timerInterval:4711}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }

    void testPlaceholderPropertyIgnoresDefaultValue() {
        config->setValue("timerInterval", 42);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", "${timerInterval:4711}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 42);
    }


    void testWithUnbalancedPlaceholderProperty() {
        config->setValue("timerInterval", 4711);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", "${timerInterval"}}));
        QVERIFY(!context->publish());
    }

    void testWithDollarInPlaceholderProperty() {
        config->setValue("timerInterval", 4711);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", "${$timerInterval}"}}));
        QVERIFY(!context->publish());
    }


    void testWithEmbeddedPlaceholderProperty() {
        config->setValue("baseName", "theBase");
        context->registerObject(config);

        auto reg = context->registerService<BaseService>("base", make_config({{"objectName", "I am ${baseName}!"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};

        QCOMPARE(slot->objectName(), "I am theBase!");
    }

    void testWithEmbeddedPlaceholderPropertyAndDollarSign() {
        config->setValue("dollars", "one thousand");
        context->registerObject(config);

        auto reg = context->registerService<BaseService>("base", make_config({{"objectName", "I have $${dollars}$"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "I have $one thousand$");
    }


    void testWithTwoPlaceholders() {
        config->setValue("section", "BaseServices");
        config->setValue("baseName", "theBase");
        context->registerObject(config);

        auto reg = context->registerService<BaseService>("base", make_config({{"objectName", "${section}:${baseName}:yeah"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "BaseServices:theBase:yeah");
    }




    void testWithConfiguredPropertyInSubConfig() {
        config->setValue("timers/interval", 4711);
        config->setValue("timers/single", "true");
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", make_config({{"interval", "${interval}"},
                                                                          {"singleShot", "${single}"}}, "timers"));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
        QVERIFY(slot->isSingleShot());
    }

    void testWithUnresolvableProperty() {

        context->registerService<QTimer>("timer", make_config({{"interval", "${interval}"}}));
        QVERIFY(!context->publish());
        config->setValue("interval", 4711);
        context->registerObject(config);
        QVERIFY(context->publish());
    }



    void testWithInvalidProperty() {
        QVERIFY(!context->registerService<QTimer>("timer", make_config({{"firstName", "Max"}})));
    }

    void testWithBeanRefProperty() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", make_config({{"timer", "&aTimer"}}));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }

    void testWithEscapedBeanRefProperty() {
        auto reg = context->registerService<QTimer>("", make_config({{"objectName", "\\&aTimer"}}));

        QVERIFY(context->publish());
        RegistrationSlot<QTimer> baseSlot{reg};
        QCOMPARE(baseSlot->objectName(), "&aTimer");
    }


    void testBindToBeanProperty() {
        QTimer timer1;
        BaseService base1;
        base1.setTimer(&timer1);
        context->registerObject(&base1, "base1");
        auto reg2 = context->registerService<BaseService>("base2", make_config({{"timer", "&base1.timer"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot2{reg2};
        QCOMPARE(baseSlot2->timer(), &timer1);

        QTimer timer2;
        base1.setTimer(&timer2);

        QCOMPARE(baseSlot2->timer(), &timer2);
    }

    void testBindToBindableBeanProperty() {
        QTimer timer1;
        timer1.setInterval(4711);
        context->registerObject(&timer1, "timer1");
        auto reg2 = context->registerService<QTimer>("timer2", make_config({{"interval", "&timer1.interval"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> timerSlot2{reg2};
        QCOMPARE(timerSlot2->interval(), 4711);

        //Modify property "interval" of the timer1 (which resides in the BaseService):
        timer1.setInterval(1908);
        //The property "interval" of the timer2 has been bound to "base.timer.interval", thus should change:
        QCOMPARE(timerSlot2->interval(), 1908);
    }






    void testAutowiredPropertyByName() {
        QTimer timer;
        timer.setObjectName("timer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", make_config({}, "", true));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }

    void testAutowiredPropertyByType() {
        QTimer timer;
        timer.setObjectName("IAmTheRealTimer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", make_config({}, "", true));

        context->registerService<BaseService2>("timer");

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }


    void testExplicitPropertyOverridesAutowired() {
        auto regBase = context->registerService<BaseService>("dependency");
        auto regBaseToUse = context->registerService<BaseService>("baseToUse");
        auto regCyclic = context->registerService<CyclicDependency>("cyclic", make_config({{"dependency", "&baseToUse"}}, "", true));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{regBase};
        RegistrationSlot<BaseService> baseToUseSlot{regBaseToUse};
        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        QCOMPARE(cyclicSlot->dependency(), baseToUseSlot());
    }


    void testAutowiredPropertyWithWrongType() {
        QObject timer;
        timer.setObjectName("timer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", make_config({}, "", true));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QVERIFY(!baseSlot->m_timer);
    }


    void testWithBeanRefWithAlias() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        context->registerObject(&timer, "theTimer");
        auto reg = context->registerService<BaseService>("base", make_config({{"timer", "&theTimer"}}));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }


    void testWithMissingBeanRef() {
        context->registerService<BaseService>("base", service_config{{{"timer", "&aTimer"}}});

        QVERIFY(!context->publish());
    }

    void testDestroyRegisteredObject() {
        std::unique_ptr<Interface1> base = std::make_unique<BaseService>();
        auto baseReg = context->registerObject(base.get());
        context->registerService(Service<Interface1,BaseService>{});
        auto regs = context->getRegistration<Interface1>();

        QCOMPARE(regs.publishedObjects().size(), 1);
        QVERIFY(baseReg);
        base.reset();
        QVERIFY(!baseReg);
        QCOMPARE(regs.publishedObjects().size(), 0);
    }

    void testDestroyRegisteredServiceExternally() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        RegistrationSlot<Interface1> slot{reg};

        QVERIFY(reg);
        context->publish();
        QCOMPARE(reg.publishedObjects().size(), 1);
        QVERIFY(slot());
        delete slot();
        QVERIFY(reg);
        QCOMPARE(reg.publishedObjects().size(), 0);
    }

    void testDestroyContext() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});

        QVERIFY(reg);
        delete context;
        context = nullptr;
        QVERIFY(!reg);
    }

    void testRegisterObjectSignalsImmediately() {
        BaseService base;
        RegistrationSlot<BaseService> baseSlot{context->registerObject(&base)};
        QVERIFY(baseSlot());
        QVERIFY(context->publish());
        QCOMPARE(baseSlot.invocationCount(), 1);
    }

    void testOptionalDependency() {
        auto reg = context->registerService(Service<DependentService>{injectIfPresent<Interface1>()});
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QVERIFY(!service->m_dependency);
    }

    void testOptionalDependencyWithAutowire() {
        auto reg = context->registerService(Service<DependentService>{injectIfPresent<Interface1>()});
        QVERIFY(reg.autowire(&DependentService::setBase));
        RegistrationSlot<DependentService> service{reg};
        QVERIFY(context->publish());
        QVERIFY(!service->m_dependency);
        auto baseReg = context->registerService(Service<Interface1,BaseService>{});
        RegistrationSlot<Interface1> baseSlot{baseReg};
        QVERIFY(context->publish());
        QVERIFY(service->m_dependency);
        QCOMPARE(service->m_dependency, baseSlot());
    }

    void testCardinalityNDependencyWithAutowire() {
        auto reg = context->registerService(Service<CardinalityNService>{injectAll<Interface1>()});
        QVERIFY(reg.autowire(&CardinalityNService::addBase));
        RegistrationSlot<CardinalityNService> service{reg};
        QVERIFY(context->publish());
        QCOMPARE(service->my_bases.size(), 0);
        auto baseReg1 = context->registerService(Service<Interface1,BaseService>{});
        RegistrationSlot<Interface1> baseSlot1{baseReg1};
        auto baseReg2 = context->registerService(Service<Interface1,BaseService2>{});
        RegistrationSlot<Interface1> baseSlot2{baseReg2};

        QVERIFY(context->publish());
        QCOMPARE(service->my_bases.size(), 2);
        QVERIFY(service->my_bases.contains(baseSlot1()));
        QVERIFY(service->my_bases.contains(baseSlot2()));
    }


    void testInitMethod() {
        auto baseReg = context->registerService<BaseService>("base", make_config({}, "", false, "init"));
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};
        QVERIFY(baseSlot->wasInitialized());
    }

    void testInitMethodWithContext() {
        auto baseReg = context->registerService<BaseService>("base", make_config({}, "", false, "initContext"));
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};
        QCOMPARE(baseSlot->context(), context);
    }

    void testNonExistingInitMethod() {
        QVERIFY(!context->registerService<BaseService>("base", make_config({}, "", false, "start")));
    }



    void testAmbiuousMandatoryDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        BaseService myBase;
        context->registerObject<Interface1>(&myBase, "myBase");
        context->registerService(Service<DependentService>{inject<Interface1>()});
        QVERIFY(!context->publish());
    }

    void testAmbiuousOptionalDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        BaseService myBase;
        context->registerObject<Interface1>(&myBase, "myBase");
        context->registerService(Service<DependentService>{injectIfPresent<Interface1>()});
        QVERIFY(!context->publish());
    }


    void testNamedMandatoryDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        auto reg = context->registerService(Service<DependentService>{inject<Interface1>("myBase")});
        QVERIFY(!context->publish());
        context->registerObject<Interface1>(&base, "myBase");
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
    }


    void testConstructorValues() {
        BaseService base;
        auto reg = context->registerService(Service<DependentService>{4711, QString{"https://web.de"}, &base}, "dep");
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"https://web.de"});
    }

    void testResolveConstructorValues() {
        config->setValue("section/url", "https://google.de/search");
        config->setValue("section/term", "something");
        config->setValue("section/id", "4711");
        context->registerObject(config);
        BaseService base;
        auto reg = context->registerService(Service<DependentService>{resolve<int>("${id}"), resolve("${url}?q=${term}"), &base}, "dep", make_config({}, "section"));
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"https://google.de/search?q=something"});
    }

    void testFailResolveConstructorValues() {
        BaseService base;
        auto reg = context->registerService(Service<DependentService>{4711, resolve("${url}"), &base}, "dep");
        QVERIFY(reg);
        QVERIFY(!context->publish());
    }

    void testResolveConstructorValuesWithDefault() {
        BaseService base;
        auto reg = context->registerService(Service<DependentService>{resolve("${id}", 4711), resolve("${url}", QString{"localhost:8080"}), &base}, "dep");
        QVERIFY(reg);
        RegistrationSlot<DependentService> service{reg};

        QVERIFY(context->publish());
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"localhost:8080"});

    }

    void testResolveConstructorValuesPrecedence() {
        BaseService base;
        auto reg = context->registerService(Service<DependentService>{resolve("${id:42}", 4711), resolve("${url:n/a}", QString{"localhost:8080"}), &base}, "dep");
        QVERIFY(reg);
        RegistrationSlot<DependentService> service{reg};

        QVERIFY(context->publish());
        QCOMPARE(service->m_id, 42);
        QCOMPARE(service->m_url, QString{"n/a"});

    }


    void testMixConstructorValuesWithDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        auto reg = context->registerService(Service<DependentService>{4711, QString{"https://web.de"}, inject<Interface1>()}, "dep");
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"https://web.de"});
    }
    void testNamedOptionalDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        auto depReg = context->registerService(Service<DependentService>{injectIfPresent<Interface1>("myBase")});
        auto depReg2 = context->registerService(Service<DependentService>{injectIfPresent<Interface1>("base")});

        QVERIFY(context->publish());
        RegistrationSlot<DependentService> depSlot{depReg};
        QVERIFY(!depSlot->m_dependency);
        RegistrationSlot<DependentService> depSlot2{depReg2};
        QCOMPARE(depSlot2->m_dependency, &base);

    }



    void testPrivateCopyDependency() {
        auto depReg = context->registerService(Service<DependentService>{injectPrivateCopy<BaseService>()}, "dependent");
        auto threeReg = context->registerService(Service<ServiceWithThreeArgs>{inject<BaseService>(), injectPrivateCopy<DependentService>(), inject<BaseService2>()}, "three");
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> dependentSlot{depReg};
        RegistrationSlot<BaseService> baseSlot{context->getRegistration<BaseService>()};
        RegistrationSlot<ServiceWithThreeArgs> threeSlot{threeReg};
        QVERIFY(dependentSlot->m_dependency);
        QVERIFY(baseSlot());
        QVERIFY(threeSlot());
        QCOMPARE_NE(dependentSlot->m_dependency, baseSlot());
        QCOMPARE_NE(threeSlot->m_dep, dependentSlot());
        QCOMPARE(baseSlot.invocationCount(), 1);
        QCOMPARE(dependentSlot.invocationCount(), 1);
    }

    void testPrivateCopyDependencyWithRequiredName() {
        context->registerService(Service<Interface1,BaseService>{}, "base1");
        auto depReg = context->registerService(Service<DependentService>{injectPrivateCopy<Interface1>("base2")}, "dependent");
        QVERIFY(!context->publish());
        context->registerService(Service<Interface1,BaseService2>{}, "base2");
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> dependentSlot{depReg};
        RegistrationSlot<Interface1> baseSlot{context->getRegistration<Interface1>()};
        QVERIFY(dependentSlot->m_dependency);
        QVERIFY(baseSlot());
        QCOMPARE_NE(dependentSlot->m_dependency, baseSlot());
        QVERIFY(dynamic_cast<BaseService2*>(dependentSlot->m_dependency));
    }

    void testInvalidPrivateCopyDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        context->registerService(Service<DependentService>{injectPrivateCopy<Interface1>()}, "dependent");
        QVERIFY(!context->publish());
    }

    void testAutoDependency() {
        auto reg = context->registerService(Service<DependentService>{inject<BaseService>()});
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        RegistrationSlot<BaseService> baseSlot{context->getRegistration<BaseService>()};
        QVERIFY(baseSlot());
        QCOMPARE(service->m_dependency, baseSlot());
    }

    void testPrefersExplicitOverAutoDependency() {
        BaseService base;
        auto reg = context->registerService(Service<DependentService>{inject<BaseService>()});
        QVERIFY(reg);
        context->registerObject(&base);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        RegistrationSlot<BaseService> baseSlot{context->getRegistration<BaseService>()};
        QCOMPARE(baseSlot(), &base);
        QCOMPARE(service->m_dependency, &base);
    }







    void testRegisterTwiceDifferentImpl() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(reg);
        //Same Interface, different implementation:
        auto reg2 = context->registerService(Service<Interface1,BaseService2>{});
        QCOMPARE_NE(reg2, reg);
    }

    void testRegisterTwiceDifferentName() {
        auto reg = context->registerService(Service<Interface1,BaseService>{}, "base");
        QVERIFY(reg);
        //Same Interface, same implementation, but different name:
        auto reg4 = context->registerService(Service<Interface1,BaseService>{}, "alias");
        QCOMPARE(reg4, reg);
        QVERIFY(context->publish());
        RegistrationSlot<Interface1> services{context->getRegistration<Interface1>()};
        QCOMPARE(services.invocationCount(), 1);
    }

    void testRegisterSameObjectTwiceWithDifferentInterfaces() {
        BaseService service;
        service.setObjectName("base");
        auto reg = context->registerObject(&service);
        QVERIFY(reg);
        auto reg4 = context->registerObject<Interface1>(&service, "alias");
        QCOMPARE_NE(reg4, reg);
    }

    void testRegisterSameObjectMultipleTimesWithDifferentNames() {
        BaseService service;
        service.setObjectName("base");
        auto reg = context->registerObject(&service);

        QVERIFY(reg);
        auto reg4 = context->registerObject(&service, "alias");
        QCOMPARE(reg4, reg);
        auto reg5 = context->registerObject(&service, "anotherAlias");
        QCOMPARE(reg5, reg);
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot, RegistrationSlot<BaseService>{reg5});
        QCOMPARE(baseSlot, RegistrationSlot<BaseService>{reg4});

    }

    void testRegisterAnonymousObjectTwice() {
        BaseService service;
        auto reg = context->registerObject<BaseService>(&service);
        QVERIFY(reg);
        auto reg4 = context->registerObject(&service);
        QCOMPARE(reg4, reg);

    }

    void testRegisterDifferentObjectsOfSameType() {
        BaseService service1;
        BaseService service2;
        auto reg1 = context->registerObject(&service1);
        auto reg2 = context->registerObject(&service2);
        QVERIFY(reg1);
        QVERIFY(reg2);
        QCOMPARE_NE(reg1, reg2);

    }


    void testRegisterTwiceDifferentProperties() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(reg);
        //Same Interface, same implementation, but different properties:
        auto reg2 = context->registerService(Service<Interface1,BaseService>{}, "", make_config({{"objectName", "tester"}}));
        QCOMPARE_NE(reg2, reg);
    }

    void testFailRegisterTwiceSameName() {
        auto reg = context->registerService(Service<Interface1,BaseService>{}, "base");
        QVERIFY(reg);

        //Everything is different, but the name:
        auto reg2 = context->registerService(Service<DependentService>{inject<BaseService>()}, "base");
        QVERIFY(!reg2);
    }



    void testFailRegisterTwice() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(reg);

        //Same Interface, same implementation, same properties, same name:
        auto reg2 = context->registerService(Service<Interface1,BaseService>{});
        QCOMPARE(reg2, reg);
    }



    void testServiceRegistrationEquality() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(reg);
        auto anotherReg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(anotherReg);
        QCOMPARE(reg, anotherReg);

        QCOMPARE_NE(reg, ServiceRegistration<Interface1>{});
    }

    void testServiceRegistrationMove() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(reg);
        auto wrapped = reg.unwrap();
        auto anotherReg = std::move(reg);
        QVERIFY(anotherReg);
        QVERIFY(!reg);
        QCOMPARE(anotherReg.unwrap(), wrapped);
    }

    void testInvalidServiceRegistrationEquality() {
        ServiceRegistration<Interface1> invalidReg;
        QVERIFY(!invalidReg);
        QCOMPARE(invalidReg.publishedObjects().size(), 0);
        QCOMPARE(invalidReg.registeredName(), QString{});
        qCInfo(loggingCategory()) << invalidReg;

        ServiceRegistration<Interface1> anotherInvalidReg;
        //Two invalid registrations are never equal:
        QCOMPARE_NE(anotherInvalidReg, invalidReg);
    }



    void testDependencyWithRequiredName() {
        auto reg1 = context->registerService(Service<Interface1,BaseService>{}, "base1");
        auto reg = context->registerService(Service<DependentService>{inject<Interface1>("base2")});
        QVERIFY(!context->publish());
        auto reg2 = context->registerService(Service<Interface1,BaseService2>{}, "base2");
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, base2());

    }



    void testCardinalityNService() {
        auto reg1 = context->registerService(Service<Interface1,BaseService>{}, "base1");
        auto reg2 = context->registerService(Service<Interface1,BaseService2>{}, "base2");
        auto reg = context->registerService(Service<CardinalityNService>{injectAll<Interface1>()});
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);
        QCOMPARE(regs.publishedObjects().size(), 2);
        QCOMPARE(service->my_bases.size(), 2);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QCOMPARE(regs.publishedObjects().size(), 2);
        QVERIFY(regs.publishedObjects().contains(service->my_bases[0]));
        QVERIFY(regs.publishedObjects().contains(service->my_bases[1]));
        QVERIFY(service->my_bases.contains(base1()));
        QVERIFY(service->my_bases.contains(base2()));

    }

    void testCardinalityNServiceWithRequiredName() {
        auto reg1 = context->registerService(Service<Interface1,BaseService>{}, "base1");
        auto reg2 = context->registerService(Service<Interface1,BaseService2>{}, "base2");
        auto reg = context->registerService(Service<CardinalityNService>{injectAll<Interface1>("base2")});
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);
        QCOMPARE(service->my_bases.size(), 1);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QCOMPARE(regs.publishedObjects().size(), 2);
        QCOMPARE(service->my_bases[0], services());

    }


    void testPostProcessor() {
        auto processReg = context->registerService<PostProcessor>();
        auto reg1 = context->registerService(Service<Interface1,BaseService>{}, "base1", service_config{{{".store", true}}});
        auto reg2 = context->registerService(Service<Interface1,BaseService2>{}, "base2");
        auto reg = context->registerService(Service<CardinalityNService>{injectAll<Interface1>()}, "card", make_config({{".store", true}}));
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        RegistrationSlot<PostProcessor> processSlot{processReg};
        QCOMPARE_NE(base1, base2);
        QCOMPARE(service->my_bases.size(), 2);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QCOMPARE(regs.publishedObjects().size(), 2);
        QCOMPARE(processSlot->processedObjects.size(), 2);
        QVERIFY(processSlot->processedObjects.contains(dynamic_cast<QObject*>(base1())));
        QVERIFY(!processSlot->processedObjects.contains(dynamic_cast<QObject*>(base2())));
        QVERIFY(processSlot->processedObjects.contains(service()));

    }



    void testCardinalityNServiceEmpty() {
        auto reg = context->registerService(Service<CardinalityNService>{injectAll<Interface1>()});
        QVERIFY(context->publish());
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE(service->my_bases.size(), 0);
    }



    void testUseViaImplType() {
        context->registerService(Service<Interface1,BaseService>{});
        context->registerService(Service<DependentService>{inject<BaseService>()});
        QVERIFY(context->publish());
    }




    void testRegisterByServiceType() {
        auto reg = context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(reg);
        QCOMPARE(reg.unwrap()->service_type(), typeid(Interface1));
        QVERIFY(context->publish());
    }



    void testMissingDependency() {
        auto reg = context->registerService(Service<DependentService>{inject<Interface1>()});
        QVERIFY(reg);
        QVERIFY(!context->publish());
        context->registerService(Service<Interface1,BaseService>{});
        QVERIFY(context->publish());
    }

    void testCyclicDependency() {
        auto reg1 = context->registerService(Service<BaseService>{inject<CyclicDependency>()});
        QVERIFY(reg1);



        auto reg2 = context->registerService(Service<CyclicDependency>{inject<BaseService>()});
        QVERIFY(!reg2);

    }

    void testWorkaroundCyclicDependencyWithBeanRef() {
        auto regBase = context->registerService(Service<BaseService>{inject<CyclicDependency>()}, "base");
        QVERIFY(regBase);



        auto regCyclic = context->registerService<CyclicDependency>( "cyclic", make_config({{"dependency", "&base"}}));
        QVERIFY(regCyclic);

        QVERIFY(context->publish());

        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        RegistrationSlot<BaseService> baseSlot{regBase};

        QVERIFY(cyclicSlot());
        QCOMPARE(cyclicSlot(), baseSlot->dependency());
        QCOMPARE(baseSlot(), cyclicSlot->dependency());

    }

    void testWorkaroundCyclicDependencyWithAutowiring() {
        auto regBase = context->registerService(Service<BaseService>{inject<CyclicDependency>()}, "dependency");
        QVERIFY(regBase);



        auto regCyclic = context->registerService<CyclicDependency>( "cyclic", make_config({}, "", true));
        QVERIFY(regCyclic);

        QVERIFY(context->publish());

        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        RegistrationSlot<BaseService> baseSlot{regBase};

        QVERIFY(cyclicSlot());
        QCOMPARE(cyclicSlot(), baseSlot->dependency());
        QCOMPARE(baseSlot(), cyclicSlot->dependency());

    }





    void testPublishAdditionalServices() {

        unsigned contextPublished = context->published();
        unsigned contextPending = context->pendingPublication();
        connect(context, &QApplicationContext::publishedChanged, this, [this,&contextPublished] {contextPublished = context->published();});
        connect(context, &QApplicationContext::pendingPublicationChanged, this, [this,&contextPending] {contextPending = context->pendingPublication();});
        auto baseReg = context->getRegistration<Interface1>();
        context->registerService(Service<Interface1,BaseService>{}, "base");
        QCOMPARE(contextPending, 1);
        RegistrationSlot<Interface1> baseSlot{baseReg};
        auto regDep = context->registerService(Service<DependentService>{inject<Interface1>()});
        RegistrationSlot<DependentService> depSlot{regDep};
        QCOMPARE(contextPending, 2);
        QCOMPARE(contextPublished, 0);
        QVERIFY(context->publish());
        QCOMPARE(contextPending, 0);
        QCOMPARE(contextPublished, 2);

        QVERIFY(baseSlot());
        QVERIFY(depSlot());
        QCOMPARE(baseSlot.invocationCount(), 1);
        QCOMPARE(baseSlot(), baseSlot());

        auto anotherBaseReg = context->registerService(Service<Interface1,BaseService2>{}, "anotherBase");
        QCOMPARE(contextPending, 1);
        QCOMPARE(contextPublished, 2);

        RegistrationSlot<Interface1> anotherBaseSlot{anotherBaseReg};
        auto regCard = context->registerService(Service<CardinalityNService>{injectAll<Interface1>()});
        QCOMPARE(contextPending, 2);
        QCOMPARE(contextPublished, 2);


        RegistrationSlot<CardinalityNService> cardSlot{regCard};
        QVERIFY(context->publish());
        QCOMPARE(contextPending, 0);
        QCOMPARE(contextPublished, 4);
        QVERIFY(cardSlot());
        QCOMPARE(cardSlot->my_bases.size(), 2);
        QCOMPARE(baseSlot.invocationCount(), 2);
        QCOMPARE(baseSlot(), anotherBaseSlot());

    }



    void testPublishAll() {
        QObjectList destroyedInOrder;
        QObjectList publishedInOrder;
        auto destroyHandler = [&destroyedInOrder](QObject* service) {destroyedInOrder.push_back(service);};
        auto published = [this,&publishedInOrder,destroyHandler](QObject* service) {
            publishedInOrder.push_back(service);
            connect(service, &QObject::destroyed, this, destroyHandler);
         };

        auto baseReg = context->registerService<BaseService>("base");
        baseReg.subscribe(this, published);
        auto base2Reg = context->registerService<BaseService2>("base2");
        base2Reg.subscribe(this, published);
        auto dependent2Reg = context->registerService(Service<DependentServiceLevel2>{inject<DependentService>()}, "dependent2");
        dependent2Reg.subscribe(this, published);
        auto dependentReg = context->registerService(Service<DependentService>{inject<BaseService>()}, "dependent");
        dependentReg.subscribe(this, published);
        auto threeReg = context->registerService(Service<ServiceWithThreeArgs>{inject<BaseService>(), inject<DependentService>(), inject<BaseService2>()}, "three");
        threeReg.subscribe(this, published);
        auto fourReg = context->registerService(Service<ServiceWithFourArgs>{inject<BaseService>(), inject<DependentService>(), inject<BaseService2>(), inject<ServiceWithThreeArgs>()}, "four");
        fourReg.subscribe(this, published);
        auto fiveReg = context->registerService(Service<ServiceWithFiveArgs>{inject<BaseService>(), inject<DependentService>(), inject<BaseService2>(), inject<ServiceWithThreeArgs>(), inject<ServiceWithFourArgs>()}, "five");
        fiveReg.subscribe(this, published);
        auto sixReg = context->registerService(Service<ServiceWithSixArgs>{QString{"Hello"}, inject<BaseService2>(), injectAll<ServiceWithFiveArgs>(), inject<ServiceWithThreeArgs>(), inject<ServiceWithFourArgs>(), resolve("${pi}", 3.14159)}, "six");
        sixReg.subscribe(this, published);


        QVERIFY(context->publish());

        RegistrationSlot<BaseService> base{baseReg};
        RegistrationSlot<BaseService2> base2{base2Reg};
        RegistrationSlot<DependentService> dependent{dependentReg};
        RegistrationSlot<DependentServiceLevel2> dependent2{dependent2Reg};
        RegistrationSlot<ServiceWithThreeArgs> three{threeReg};
        RegistrationSlot<ServiceWithFourArgs> four{fourReg};
        RegistrationSlot<ServiceWithFiveArgs> five{fiveReg};
        RegistrationSlot<ServiceWithSixArgs> six{sixReg};


        QCOMPARE(publishedInOrder.size(), 8);

        //1. BaseService must be initialized before BaseService2 (because the order of registration shall be kept, barring other restrictions).
        //2. DependentService must be initialized after both BaseService.
        //3. DependentService must be initialized before DependentServiceLevel2.
        //4. ServiceWithThreeArgs must be initialized after BaseService, BaseService2 and DependentService
        QVERIFY(publishedInOrder.indexOf(base()) < publishedInOrder.indexOf(base2()));
        QVERIFY(publishedInOrder.indexOf(dependent()) < publishedInOrder.indexOf(dependent2()));
        QVERIFY(publishedInOrder.indexOf(base()) < publishedInOrder.indexOf(three()));
        QVERIFY(publishedInOrder.indexOf(dependent()) < publishedInOrder.indexOf(three()));
        QVERIFY(publishedInOrder.indexOf(base2()) < publishedInOrder.indexOf(three()));
        QVERIFY(publishedInOrder.indexOf(three()) < publishedInOrder.indexOf(four()));
        QVERIFY(publishedInOrder.indexOf(four()) < publishedInOrder.indexOf(five()));
        QVERIFY(publishedInOrder.indexOf(five()) < publishedInOrder.indexOf(six()));
        delete context;
        context = nullptr;

        QCOMPARE(destroyedInOrder.size(), 8);

        //We cannot say anything about the destruction-order of the Services that have no dependencies:
        //BaseService and BaseService2
        //However, what we can say is:
        //1. DependentService must be destroyed before both BaseService.
        //2. DependentService must be destroyed after DependentServiceLevel2.
        //3. ServiceWithThreeArgs must be destroyed before BaseService, BaseService2 and DependentService
        //4. BaseService2 must destroyed before BaseService (because the order of registration shall be kept, barring other restrictions).

        QVERIFY(destroyedInOrder.indexOf(dependent()) > destroyedInOrder.indexOf(dependent2()));
        QVERIFY(destroyedInOrder.indexOf(base()) > destroyedInOrder.indexOf(three()));
        QVERIFY(destroyedInOrder.indexOf(dependent()) > destroyedInOrder.indexOf(three()));
        QVERIFY(destroyedInOrder.indexOf(base2()) > destroyedInOrder.indexOf(three()));
        QVERIFY(destroyedInOrder.indexOf(three()) > destroyedInOrder.indexOf(four()));
        QVERIFY(destroyedInOrder.indexOf(four()) > destroyedInOrder.indexOf(five()));
        QVERIFY(destroyedInOrder.indexOf(five()) > destroyedInOrder.indexOf(six()));
        QVERIFY(destroyedInOrder.indexOf(base2()) < destroyedInOrder.indexOf(base()));
    }

private:
    QApplicationContext* context;
    QTemporaryFile* settingsFile;
    QSettings* config;
};

} //mcnepp::qtdi

#include "testqapplicationcontext.moc"


QTEST_MAIN(mcnepp::qtdi::ApplicationContextTest)
