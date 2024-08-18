#include <QTest>
#include <QSettings>
#include <QTemporaryFile>
#include <QPromise>
#include <QSemaphore>
#include <QFuture>
#include <iostream>
#include "standardqapplicationcontext.h"
#include "appcontexttestclasses.h"
#include "qtestcase.h"

namespace mcnepp::qtdi {

using namespace qtditest;


template<> struct service_factory<BaseService> {
    using service_type = BaseService;

    explicit service_factory(int* calls = nullptr) : pCalls(calls) {

    }

    BaseService* operator()() const {
        if(pCalls) {
            ++*pCalls;
        }
        return new BaseService;
    }

    BaseService* operator()(CyclicDependency* dep, QObject* parent = nullptr) const {
        if(pCalls) {
            ++*pCalls;
        }
        return new BaseService{dep, parent};
    }
    int* pCalls;
};

///Just there in order to test whether we can use free functions as initializers, too.
void initInterface(Interface1* srv) {
    srv->init();
}

template<> struct service_traits<BaseService> : default_service_traits<BaseService> {
    using initializer_type = service_initializer<&BaseService::initContext>;
};


template<> struct service_traits<Interface1> : default_service_traits<Interface1>  {
    using initializer_type = service_initializer<initInterface>;

};



}

namespace mcnepp::qtditest {


template<typename S>  struct vector_converter {
    std::vector<S*> operator()(const QVariant& arg) const {
        auto list = detail::convertQList<S>(arg.value<QObjectList>());
        return {list.begin(), list.end()};
    }
};

template<typename S> struct ref_converter {
    S& operator()(const QVariant& arg) const {
        return dynamic_cast<S&>(*arg.value<QObject*>());
    }
};

template<typename S> class RegistrationSlot : public QObject {
public:

    explicit RegistrationSlot(const Registration<S>& registration)
    {
        m_subscription = const_cast<Registration<S>&>(registration).subscribe(this, &RegistrationSlot::setObj);
    }


    S* operator->() const {
        return m_obj.empty() ? nullptr : m_obj.back();
    }

    S* last() const {
        return m_obj.empty() ? nullptr : m_obj.back();
    }

    explicit operator bool() const {
        return !m_obj.empty();
    }




    void setObj(S* obj) {
        m_obj.push_back(obj);
    }

    bool operator ==(const RegistrationSlot& other) const {
        return m_obj == other.m_obj;
    }

    bool operator !=(const RegistrationSlot& other) const {
        return m_obj != other.m_obj;
    }

    int invocationCount() const {
        return m_obj.size();
    }

    int size() const {
        return m_obj.size();
    }

    S* operator[](std::size_t index) const {
        return m_obj[index];
    }

    Subscription& subscription() {
        return m_subscription;
    }

private:
    QList<S*> m_obj;
    Subscription m_subscription;
};

class PostProcessor : public QObject, public QApplicationContextPostProcessor {
public:
    explicit PostProcessor(QObject* parent = nullptr) : QObject(parent) {}

    // QApplicationContextServicePostProcessor interface
    void process(service_registration_handle_t handle, QObject *service, const QVariantMap& resolvedProperties) override {
        servicesMap[handle] = service;
        resolvedPropertiesMap[handle] = resolvedProperties;
    }

    QHash<service_registration_handle_t,QObject*> servicesMap;
    QHash<service_registration_handle_t,QVariantMap> resolvedPropertiesMap;
};

template<typename S> class SubscriptionThread : public QThread {
protected:
    void run() override {
        QObject context;
        auto registration = m_context->getRegistration<S>();
        registration.subscribe(&context,[this](BaseService* srv) {
            service.storeRelease(srv);
            exit();//Leave event-loop
        });

        subscribed = 1;
        QThread::run();
    }
public:
    explicit SubscriptionThread(QApplicationContext* context) :
        m_context(context) {
    }

    QAtomicPointer<BaseService> service;
    QApplicationContext* const m_context;
    QAtomicInt subscribed;
};


class ApplicationContextTest
 : public QObject {
    Q_OBJECT

public:
    explicit ApplicationContextTest(QObject* parent = nullptr) : QObject(parent),
            context(nullptr),
            configuration(nullptr) {

    }

private slots:


    void init() {
        settingsFile.reset(new QTemporaryFile);
        settingsFile->open();
        configuration.reset(new QSettings{settingsFile->fileName(), QSettings::Format::IniFormat});
        context.reset(new StandardApplicationContext{qtditest::testLogging()});
    }

    void cleanup() {
        context.reset();
        settingsFile.reset();
    }

    void testLoggingCategory() {
        QCOMPARE(&context->loggingCategory(), &qtditest::testLogging());
        StandardApplicationContext anotherContext;
        QCOMPARE(&anotherContext.loggingCategory(), &defaultLoggingCategory());
    }


    void testGlobalInstance() {
        QCOMPARE(context.get(), QApplicationContext::instance());
        QVERIFY(context->isGlobalInstance());
        StandardApplicationContext anotherContext;
        QVERIFY(!anotherContext.isGlobalInstance());
        QCOMPARE(QApplicationContext::instance(), context.get());
        context.reset();
        QVERIFY(!QApplicationContext::instance());
    }



    void testRegisterNonQObject() {
        //std::cerr is no QObject. However, this cannot be detected at compile-time, as it has virtual functions and is thus _potentially convertible_ to QObject.
        //Therefore, it should fail at runtime:
        auto reg = context->registerObject(&std::cerr);
        QVERIFY(!reg);
    }

    void testNoDependency() {
        auto reg = context->registerService<BaseService>();
        QVERIFY(reg);
        QVERIFY(!context->getRegistration<BaseService>("anotherName"));
        QCOMPARE(context->getRegistration<BaseService>(reg.registeredName()), reg);
        QVERIFY(reg.matches<BaseService>());
        QVERIFY(reg.as<BaseService>());
        QVERIFY(!reg.as<BaseService2>());
        auto asUnknown = reg.as<BaseService,ServiceScope::UNKNOWN>();
        QVERIFY(asUnknown);
        auto asPrototype = asUnknown.as<BaseService,ServiceScope::PROTOTYPE>();
        QVERIFY(!asPrototype);
        auto registrations = context->getRegistrations();
        QCOMPARE(registrations.size(), 3); //One is our BaseService, one is the QCoreApplication and one is the QApplicationContext.
        int foundBits = 0;
        for(auto& r : registrations) {
            if(r.as<QCoreApplication>()) {
                foundBits |= 1;
            }
            if(r.as<QApplicationContext>()) {
                foundBits |= 2;
            }
            if(r.as<BaseService>()) {
                foundBits |= 4;
            }
        }
        QCOMPARE(foundBits, 7);
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QVERIFY(slot);
        //The parent was not supplied to the constructor:
        QVERIFY(!slot->m_InitialParent);
        //The ApplicationContext set itself as parent after creation:
        QCOMPARE(slot->parent(), context.get());
    }

    void testInjectApplicationContextAsParent() {
        auto baseReg = context->registerService(service<BaseService>(injectIfPresent<CyclicDependency>(), injectParent()));
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};

        //The ApplicationContext was supplied as parent to the constructor:
        QCOMPARE(baseSlot->m_InitialParent, context.get());
        QCOMPARE(baseSlot->parent(), context.get());
    }

    void testInjectDelegatingApplicationContextAsParent() {
        StandardApplicationContext delegateContext{qtditest::testLogging(), context.get(), StandardApplicationContext::delegate_tag};
        auto baseReg = delegateContext.registerService(service<BaseService>(injectIfPresent<CyclicDependency>(), injectParent()));
        QCOMPARE(baseReg.applicationContext(), context.get());

        auto proxyReg = delegateContext.getRegistration<BaseService>();
        QCOMPARE(proxyReg.applicationContext(), context.get());
        QVERIFY(delegateContext.publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};

        //The ApplicationContext was supplied as parent to the constructor:
        QCOMPARE(baseSlot->m_InitialParent, context.get());
        QCOMPARE(baseSlot->parent(), context.get());
    }


    void testInjectExternalParent() {
        auto baseReg = context->registerService(service<BaseService>(injectIfPresent<CyclicDependency>(), this));
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};

        //this was supplied as parent to the constructor:
        QCOMPARE(baseSlot->m_InitialParent, this);
        QCOMPARE(baseSlot->parent(), this);
        bool destroyed = false;
        connect(baseSlot.last(), &QObject::destroyed, [&destroyed] {destroyed = true;});
        context.reset();
        //BaseService should not have been deleted by ApplicationContext's destructor:
        QVERIFY(!destroyed);
        QCOMPARE(baseSlot->parent(), this);
    }


    void testQObjectsDependency() {
        QTimer timer;
        context->registerObject(&timer);
        context->registerService<BaseService>();
        auto reg = context->registerService(service<QObjectService>(injectAll<QObject>()));
        QVERIFY(context->publish());

        RegistrationSlot<QObjectService> slot{reg};
        QVERIFY(slot.last());
        QCOMPARE(slot->m_dependencies.size(), 4); //QTimer, BaseService, QCoreApplication, QApplicationContext
        int foundBits = 0;
        for(auto obj : slot->m_dependencies) {
            if(dynamic_cast<QApplicationContext*>(obj)) {
                foundBits |= 1;
            }
            if(dynamic_cast<QCoreApplication*>(obj)) {
                foundBits |= 2;
            }
            if(dynamic_cast<QTimer*>(obj)) {
                foundBits |= 4;
            }
            if(dynamic_cast<BaseService*>(obj)) {
                foundBits |= 8;
            }
        }
        QCOMPARE(foundBits, 15);
    }

    void testQObjectProperty() {
        auto reg = context->registerService<QObjectService>("qobjects", config({{"dependency", "&context"}}));
        QVERIFY(context->publish());

        RegistrationSlot<QObjectService> slot{reg};
        QVERIFY(slot.last());
        QCOMPARE(slot->dependency(), context.get());
    }




    void testQObjectRegistration() {
        auto reg = context->registerService<BaseService>();
        QVERIFY(reg);
        auto regByName = context->getRegistration(reg.registeredName());
        QCOMPARE(regByName, reg);
        QVERIFY(regByName.matches<BaseService>());
        QVERIFY(regByName.matches<QObject>());

        auto qReg = context->getRegistration<QObject>();
        QCOMPARE(qReg.registeredServices().size(), 3); //BaseService, QCoreApplication, QApplicationContext
        QVERIFY(qReg.matches<QObject>());
        QVERIFY(context->publish());
        RegistrationSlot<QObject> slot{regByName};
        QVERIFY(slot);
    }

    void testApplicationRegisteredAsObject() {
        auto reg = context->getRegistration<QCoreApplication>();
        QVERIFY(reg.as<QObject>());

        QVERIFY(context->publish());
        RegistrationSlot<QCoreApplication> slot{reg};
        QVERIFY(slot);
        QCOMPARE(slot.last(), QCoreApplication::instance());
        auto regByName = context->getRegistration("application").as<QCoreApplication,ServiceScope::EXTERNAL>();
        QVERIFY(regByName);
        RegistrationSlot<QCoreApplication> slotByName{regByName};
        QCOMPARE(slotByName.last(), QCoreApplication::instance());
    }

    void testAsOnTemporary() {
        auto reg = context->getRegistration<QCoreApplication>().as<QObject>();
        auto appReg = context->getRegistration<QCoreApplication>("application").as<QObject>();
        QVERIFY(reg);
        QVERIFY(appReg);
        QCOMPARE(reg.registeredServices()[0], appReg);
    }


    void testApplicationContextRegisteredAsObject() {
        auto reg = context->getRegistration<QApplicationContext>();
        QVERIFY(context->publish());
        RegistrationSlot<QApplicationContext> slot{reg};
        QVERIFY(slot);
        QCOMPARE(slot.last(), context.get());
        auto regByName = context->getRegistration("context").as<QApplicationContext,ServiceScope::EXTERNAL>();
        QVERIFY(regByName);
        RegistrationSlot<QApplicationContext> slotByName{regByName};
        QCOMPARE(slotByName.last(), context.get());
    }

    void testDependOnApplicationAsParent() {
        auto reg = context->registerService(service<QTimer>(inject<QCoreApplication>()), "timer");
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QVERIFY(slot);
        QCOMPARE(slot->parent(), QCoreApplication::instance());
    }

    void testDependOnApplicationContextAsParent() {
        auto reg = context->registerService(service<QTimer>(inject<QApplicationContext>()), "timer");
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QVERIFY(slot);
        QCOMPARE(slot->parent(), context.get());
    }




    void testWithProperty() {
        auto reg = context->registerService<QTimer>("timer", config({{"interval", 4711}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }



    void testPropertyConfiguredInEnvironment() {
        auto envKey = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
        qputenv(envKey, "value from the environment");
        QCOMPARE("value from the environment", context->getConfigurationValue(envKey));
    }





    void testWithPlaceholderProperty() {
        PostProcessor postProcessor;
        configuration->setValue("timerInterval", 4711);
        context->registerObject(configuration.get());
        context->registerObject(&postProcessor);

        QCOMPARE(4711, context->getConfigurationValue("timerInterval"));
        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${timerInterval}"}}));
        QCOMPARE(reg.config().properties["interval"], "${timerInterval}");
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(postProcessor.resolvedPropertiesMap[reg.unwrap()]["interval"], 4711);

        QCOMPARE(slot->interval(), 4711);
    }

    void testWithEscapedPlaceholderProperty() {

        auto reg = context->registerService<QTimer>("", config({{"objectName", "\\${timerName}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->objectName(), "${timerName}");
    }

    void testPlaceholderPropertyUsesDefaultValue() {

        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${timerInterval:4711}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }

    void testPlaceholderPropertyIgnoresDefaultValue() {
        configuration->setValue("timerInterval", 42);
        context->registerObject(configuration.get());

        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${timerInterval:4711}"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 42);
    }


    void testWithUnbalancedPlaceholderProperty() {
        configuration->setValue("timerInterval", 4711);
        context->registerObject(configuration.get());

        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${timerInterval"}}));
        QVERIFY(!context->publish());
    }

    void testWithDollarInPlaceholderProperty() {
        configuration->setValue("timerInterval", 4711);
        context->registerObject(configuration.get());

        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${$timerInterval}"}}));
        QVERIFY(!context->publish());
    }


    void testWithEmbeddedPlaceholderProperty() {
        configuration->setValue("baseName", "theBase");
        context->registerObject(configuration.get());

        auto reg = context->registerService<BaseService>("base", config({{"objectName", "I am ${baseName}!"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};

        QCOMPARE(slot->objectName(), "I am theBase!");
    }

    void testWithEmbeddedPlaceholderPropertyAndDollarSign() {
        configuration->setValue("dollars", "one thousand");
        context->registerObject(configuration.get());

        auto reg = context->registerService<BaseService>("base", config({{"objectName", "I have $${dollars}$"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "I have $one thousand$");
    }


    void testWithTwoPlaceholders() {
        configuration->setValue("section", "BaseServices");
        configuration->setValue("baseName", "theBase");
        context->registerObject(configuration.get());

        auto reg = context->registerService<BaseService>("base", config({{"objectName", "${section}:${baseName}:yeah"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "BaseServices:theBase:yeah");
    }




    void testWithConfiguredPropertyInSection() {
        configuration->setValue("timers/interval", 4711);
        configuration->setValue("timers/single", "true");
        context->registerObject(configuration.get());
        QCOMPARE(4711, context->getConfigurationValue("timers/interval"));
        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${interval}"},
                                                                          {"singleShot", "${single}"}}).withGroup("timers"));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
        QVERIFY(slot->isSingleShot());
    }

    void testWithConfiguredPropertyInSectionWithAbsoluteAndRelativePaths() {
        configuration->setValue("timers/interval", 4711);
        configuration->setValue("timers/aTimer/single", "true");
        context->registerObject(configuration.get());
        QCOMPARE(4711, context->getConfigurationValue("timers/interval"));
        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${/timers/interval}"},
                                                                          {"singleShot", "${aTimer/single}"}}).withGroup("timers"));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
        QVERIFY(slot->isSingleShot());
    }

    void testWithConfiguredPropertyInSectionWithFallback() {
        configuration->setValue("timers/interval", 4711);
        configuration->setValue("single", "true");
        context->registerObject(configuration.get());
        auto reg = context->registerService<QTimer>("timer", config({{"interval", "${*/aTimer/interval}"},
                                                                          {"singleShot", "${*/single}"}}).withGroup("timers"));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
        QVERIFY(slot->isSingleShot());
    }

    void testWithUnresolvableProperty() {

        context->registerService<QTimer>("timer", config({{"interval", "${interval}"}}));
        QVERIFY(!context->publish());
        configuration->setValue("interval", 4711);
        context->registerObject(configuration.get());
        QVERIFY(context->publish());
    }



    void testWithInvalidProperty() {
        QVERIFY(!context->registerService<QTimer>("timer", config({{"firstName", "Max"}})));
    }

    void testWithBeanRefProperty() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", config({{"timer", "&aTimer"}}));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }


    void testEscapedBeanRef() {

        auto reg = context->registerService<BaseService>("base", config({{"objectName", "\\&another"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "&another");
    }

    void testWithEscapedBeanRefProperty() {
        auto reg = context->registerService<QTimer>("", config({{"objectName", "\\&aTimer"}}));

        QVERIFY(context->publish());
        RegistrationSlot<QTimer> baseSlot{reg};
        QCOMPARE(baseSlot->objectName(), "&aTimer");
    }


    void testInitializeWithBeanProperty() {
        QTimer timer1;
        BaseService base1;
        base1.setTimer(&timer1);
        context->registerObject(&base1, "base1");
        auto reg2 = context->registerService<BaseService>("base2", config({{"timer", "&base1.timer"}}));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot2{reg2};
        QCOMPARE(baseSlot2->timer(), &timer1);
    }

    void testInitializeWithBeanProperty2() {
        QTimer timer1;
        timer1.setInterval(4711);
        context->registerObject(&timer1, "timer1");
        auto reg2 = context->registerService<QTimer>("timer2", config({{"interval", "&timer1.interval"}}));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> timerSlot2{reg2};
        QCOMPARE(timerSlot2->interval(), 4711);
    }

    void testBindServiceRegistrationToProperty() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer);
        auto regBase = context->registerService<BaseService>("base");
        RegistrationSlot<BaseService> baseSlot{regBase};


        auto subscription = bind(regTimer, "objectName", regBase, "foo");
        QVERIFY(subscription);


        QVERIFY(context->publish());

        QCOMPARE(baseSlot->foo(), "timer");
        timer.setObjectName("another timer");
        QCOMPARE(baseSlot->foo(), "another timer");
        subscription.cancel();
        timer.setObjectName("back to timer");
        QCOMPARE(baseSlot->foo(), "another timer");
    }

    void testBindServiceRegistrationToPropertyOfServiceTemplate() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer);
        auto regBase = context->registerServiceTemplate<BaseService>("base");

        auto regDerived = context->registerService(service<DerivedService>(), regBase);
        RegistrationSlot<DerivedService> derivedSlot{regDerived};


        auto subscription = bind(regTimer, "objectName", regBase, "foo");
        QVERIFY(subscription);


        QVERIFY(context->publish());

        QCOMPARE(derivedSlot->foo(), "timer");
        timer.setObjectName("another timer");
        QCOMPARE(derivedSlot->foo(), "another timer");
        subscription.cancel();
        timer.setObjectName("back to timer");
        QCOMPARE(derivedSlot->foo(), "another timer");
    }



    void testConnectServices() {
        auto regSource = context->registerService<BaseService>();
        auto regTarget = context->registerService<QTimer>();
        void (QObject::*setter)(const QString&) = &QObject::setObjectName;//We need this temporary variable, as setObjectName has two overloads!
        auto subscription = connectServices(regSource, &BaseService::fooChanged, regTarget, setter);
        QVERIFY(subscription);
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> sourceSlot{regSource};
        RegistrationSlot<QTimer> targetSlot{regTarget};
        sourceSlot->setFoo("A new beginning");
        QCOMPARE(targetSlot->objectName(), "A new beginning");

        subscription.cancel();
        sourceSlot->setFoo("Should be ignored");
        QCOMPARE(targetSlot->objectName(), "A new beginning");

    }

    void testConnectServiceWithSelf() {
        auto regSource = context->registerService<BaseService>();
        void (QObject::*setter)(const QString&) = &QObject::setObjectName;//We need this temporary variable, as setObjectName has two overloads!
        QVERIFY(connectServices(regSource, &BaseService::fooChanged, regSource, setter));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> sourceSlot{regSource};
        sourceSlot->setFoo("A new beginning");
        QCOMPARE(sourceSlot->objectName(), "A new beginning");

    }

    void testConnectServicesWithProxy() {
        auto regSource = context->registerService<QTimer>();
        auto regTarget1 = context->registerService<BaseService>("base1");
        auto regTarget2 = context->registerService<BaseService>("base2");
        auto regProxyTarget = context->getRegistration<BaseService>();
        QVERIFY(connectServices(regSource, &QObject::objectNameChanged, regProxyTarget, &BaseService::setFoo));
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> sourceSlot{regSource};
        RegistrationSlot<BaseService> targetSlot{regProxyTarget};
        QCOMPARE(targetSlot.invocationCount(), 2);
        sourceSlot->setObjectName("A new beginning");
        QCOMPARE(targetSlot[0]->foo(), "A new beginning");
        QCOMPARE(targetSlot[1]->foo(), "A new beginning");

    }

    void testConfigurePrivatePropertyInServiceTemplate() {
        auto serviceTemplate = context->registerServiceTemplate<BaseService>("baseTemplate", config({{"foo", "${id}-foo"}}));

        auto base1 = context->registerService(service<BaseService>(), serviceTemplate, "base1", config({{".id", 4711}}));
        auto base2 = context->registerService(service<BaseService>(), serviceTemplate, "base2", config({{".id", 3141}}));
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> slot1{base1};
        RegistrationSlot<BaseService> slot2{base2};

        QCOMPARE(slot1->foo(), "4711-foo");
        QCOMPARE(slot2->foo(), "3141-foo");
    }


    void testValidatePropertyOfTemplateUponServiceRegistration() {
        //Do not validate the existence of the Q_PROPERTY "foo":
        auto srvTemplate = context->registerServiceTemplate("baseTemplate", config({{"foo", "The foo"}}));
        QVERIFY(srvTemplate);
        //Validate the existence of the Q_PROPERTY "foo" and report error:
        auto srvReg = context->registerService(service<QObjectService>(), srvTemplate);
        QVERIFY(!srvReg);
    }

    void testConfigurePrivatePropertyAsQObjectInServiceTemplate() {
        QTimer timer;
        context->registerObject(&timer, "timer");
        auto srvTemplate = context->registerServiceTemplate("baseTemplate", config({{"foo", "${id}-foo"}}));

        auto timerTemplate = context->registerService(serviceTemplate().advertiseAs<TimerAware>(), srvTemplate, "timerAware", config({{"timer", "&timer"}}));

        auto base1 = context->registerService(service<BaseService>(), timerTemplate, "base1", config({{".id", 4711}}));
        auto base2 = context->registerService(service<BaseService>(), timerTemplate, "base2", config({{".id", 3141}}));
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> slot1{base1};
        RegistrationSlot<BaseService> slot2{base2};
        auto timerReg = context->getRegistration<TimerAware>();
        QCOMPARE(timerReg.registeredServices().size(), 3);
        RegistrationSlot<TimerAware> timerSlot{timerReg};
        QVERIFY(slot1);
        QVERIFY(slot2);

        QCOMPARE(slot1->foo(), "4711-foo");
        QCOMPARE(slot1->timer(), &timer);
        QCOMPARE(slot2->foo(), "3141-foo");
        QCOMPARE(slot2->timer(), &timer);
        QCOMPARE(timerSlot.invocationCount(), 2);
    }

    void testBindServiceRegistrationToPropertyOfSelf() {


        auto regBase = context->registerService<BaseService>("base");
        RegistrationSlot<BaseService> baseSlot{regBase};


        auto subscription = bind(regBase, "objectName", regBase, "foo");
        QVERIFY(subscription);


        QVERIFY(context->publish());

        QCOMPARE(baseSlot->foo(), "base");
        baseSlot->setObjectName("another base");
        QCOMPARE(baseSlot->foo(), "another base");
        subscription.cancel();
        baseSlot->setObjectName("back to base");
        QCOMPARE(baseSlot->foo(), "another base");
    }

    void testBindServiceRegistrationToSamePropertyFails() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer);
        auto regBase = context->registerService<BaseService>("base");


        QVERIFY(bind(regTimer, "objectName", regBase, "objectName"));
        //Binding the same property twice must fail:
        QVERIFY(!bind(regTimer, "objectName", regBase, "objectName"));

    }

    void testBindServiceRegistrationToSelfFails() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer);


        QVERIFY(!bind(regTimer, "objectName", regTimer, "objectName"));


    }

    void testBindServiceRegistrationToProxyRegistration() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer);
        BaseService base;
        context->registerObject(&base, "base");
        auto regBase = context->getRegistration<BaseService>();
        QVERIFY(bind(regTimer, "objectName", regBase, "foo"));
        QVERIFY(context->publish());
        QCOMPARE(base.foo(), "timer");

        RegistrationSlot<BaseService> base2{context->registerService<BaseService>("base2")};

        QVERIFY(context->publish());

        QCOMPARE(base2->foo(), "timer");

        timer.setObjectName("another timer");
        QCOMPARE(base.foo(), "another timer");
        QCOMPARE(base2->foo(), "another timer");
    }








    void testBindServiceRegistrationToSetter() {

        BaseService base;
        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer);
        auto regBase = context->registerObject<Interface1>(&base, "base");
        auto regInterface = context->getRegistration<Interface1>();
        QVERIFY(bind(regTimer, "objectName", regInterface, &Interface1::setFoo));
        QVERIFY(context->publish());
        QCOMPARE(base.foo(), "timer");
        timer.setObjectName("another timer");
        QCOMPARE(base.foo(), "another timer");
    }

    void testBindServiceRegistrationToObjectSetter() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer).as<QObject>();
        auto regBase = context->registerService<BaseService>("base", config({{"foo", "baseFoo"}}));
        void (QObject::*setter)(const QString&) = &QObject::setObjectName;//We need this temporary variable, as setObjectName has two overloads!
        bind(regBase, "foo", regTimer, setter);
        QVERIFY(context->publish());
        QCOMPARE(timer.objectName(), "baseFoo");
        RegistrationSlot<BaseService> baseSlot{regBase};
        baseSlot->setFoo("newFoo");
        QCOMPARE(timer.objectName(), "newFoo");
    }

    void testBindParameterlessSignalToObjectSetter() {

        QTimer timer;
        timer.setObjectName("timer");
        auto regTimer = context->registerObject(&timer).as<QObject>();
        auto regBase = context->registerService<BaseService>("base", config({{"foo", "baseFoo"}}));
        void (QObject::*setter)(const QString&) = &QObject::setObjectName;//We need this temporary variable, as setObjectName has two overloads!
        bind(regBase, &BaseService::fooChanged, regTimer, setter);
        QVERIFY(context->publish());
        QCOMPARE(timer.objectName(), "baseFoo");
        RegistrationSlot<BaseService> baseSlot{regBase};
        baseSlot->setFoo("newFoo");
        QCOMPARE(timer.objectName(), "newFoo");
    }


    void testBindSignalWithParameterToObjectSetter() {

        QTimer timer;
        auto regBase1 = context->registerService<BaseService>("base1");
        auto regBase2 = context->registerService<BaseService>("base2");
        auto regBases = context->getRegistration<BaseService>();
        bind(regBase1, &BaseService::timerChanged, regBases, &BaseService::setTimer);
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot1{regBase1};
        RegistrationSlot<BaseService> baseSlot2{regBase2};
        baseSlot1->setTimer(&timer);
        QCOMPARE(baseSlot2->timer(), &timer);
    }

    void testCannotBindToSignalWithoutProperty() {

        auto regBase1 = context->registerService<BaseService>("base1");
        QVERIFY(!bind(regBase1, &BaseService::signalWithoutProperty, regBase1, &BaseService::setTimer));
    }

    void testServiceTemplate() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        auto abstractReg = context->registerServiceTemplate<BaseService>("abstractBase", config({{"timer", "&aTimer"}}));

        auto reg = context->registerService(service<DerivedService>(), abstractReg, "base");

        QVERIFY(context->publish());
        RegistrationSlot<DerivedService> derivedSlot{reg};
        RegistrationSlot<BaseService> abstractBaseSlot{abstractReg};
        QCOMPARE(derivedSlot.last(), abstractBaseSlot.last());
        QCOMPARE(derivedSlot->m_timer, &timer);
        QCOMPARE(derivedSlot->context(), context.get());
    }

    void testInvalidServiceTemplate() {
        ServiceRegistration<BaseService,ServiceScope::TEMPLATE> abstractReg;

        auto reg = context->registerService(service<DerivedService>(), abstractReg, "base");
        QVERIFY(!reg);
    }


    void testPrototypeWithTemplate() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        auto abstractReg = context->registerServiceTemplate<BaseService>("abstractBase", config({{"timer", "&aTimer"}}));

        auto protoReg = context->registerService(prototype<DerivedService>(), abstractReg, "base");

        auto depReg = context->registerService(service<DependentService>(protoReg));

        QVERIFY(context->publish());

        RegistrationSlot<DependentService> depSlot{depReg};
        QVERIFY(depSlot);
        QVERIFY(depSlot->m_dependency);
        QCOMPARE(static_cast<BaseService*>(depSlot->m_dependency)->timer(), &timer);
    }

    void testServiceTemplateWithNoDefaultConstructor() {
        BaseService base;
        auto baseReg = context->registerObject(&base);
        auto abstractReg = context->registerServiceTemplate<DependentService>("abstractDep");

        auto reg = context->registerService(service<DependentService>(baseReg), abstractReg, "dep");

        QVERIFY(context->publish());
        RegistrationSlot<DependentService> depSlot{reg};
        RegistrationSlot<DependentService> abstractBaseSlot{abstractReg};
        QCOMPARE(depSlot->m_dependency, &base);
    }


    void testAdvertiseViaServiceTemplate() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        auto abstractReg = context->registerService(serviceTemplate<BaseService>().advertiseAs<Interface1,TimerAware>(), "abstractBase", config({{"timer", "&aTimer"}}));

        auto reg = context->registerService(service<BaseService>(), abstractReg, "base");

        auto timerAwareReg = context->getRegistration<TimerAware>();

        QCOMPARE(timerAwareReg.registeredServices().size(), 2);
        QVERIFY(timerAwareReg.registeredServices().contains(abstractReg));
        QVERIFY(timerAwareReg.registeredServices().contains(reg));


        auto interfaceReg = context->getRegistration<Interface1>();

        QCOMPARE(interfaceReg.registeredServices().size(), 2);
        QVERIFY(interfaceReg.registeredServices().contains(abstractReg));
        QVERIFY(interfaceReg.registeredServices().contains(reg));
        auto depReg = context->registerService(service<DependentService>(inject<Interface1>()));
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> depSlot{depReg};
        RegistrationSlot<BaseService> baseSlot{reg};
        QVERIFY(depSlot);
        QCOMPARE(depSlot->m_dependency, baseSlot.last());
        QCOMPARE(baseSlot->timer(), &timer);
    }


    void testUseInitMethodFromServiceTemplate() {
        auto abstractReg = context->registerService(serviceTemplate<BaseService2>().advertiseAs<Interface1>(), "interface1");

        auto reg = context->registerService(service<BaseService2>(), abstractReg);

        QVERIFY(context->publish());
        RegistrationSlot<BaseService2> derivedSlot{reg};
        QCOMPARE(derivedSlot->initCalled, 1);
    }

    void testUseSecondLevelServiceTemplate() {
        BaseService2 base2;
        auto abstractInterfacReg = context->registerService(serviceTemplate<BaseService2>().advertiseAs<Interface1>(), "interface1");

        auto abstractBase = context->registerService(serviceTemplate<BaseService2>(), abstractInterfacReg);

        auto reg = context->registerService(service<BaseService2>(), abstractBase, "", config({{"reference", "&base2"}}));

        context->registerObject(&base2, "base2");

        QVERIFY(context->publish());
        RegistrationSlot<BaseService2> derivedSlot{reg};
        QCOMPARE(derivedSlot->initCalled, 1);
        QCOMPARE(derivedSlot->reference(), &base2);
    }



    void testMustNotFindServiceTemplateAsBeanRef() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerServiceTemplate<QTimer>("timer");
        auto abstractReg = context->registerService<BaseService>("base", config({{"timer", "&timer"}}));

        QVERIFY(!context->publish());
    }


    void testAutowiredPropertyByName() {
        QTimer timer;
        timer.setObjectName("timer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", config().withAutowire());

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }

    void testAutowiredPropertyByType() {
        QTimer timer;
        timer.setObjectName("IAmTheRealTimer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", config().withAutowire());

        context->registerService<BaseService2>("timer");

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }

    void testAmbiguousAutowiringByType() {
        QTimer timer1;
        context->registerObject(&timer1);
        QTimer timer2;
        context->registerObject(&timer2);

        auto reg = context->registerService<BaseService>("base", config().withAutowire());

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QVERIFY(!baseSlot->m_timer);

    }

    void testDoNotAutowireSelf() {
        auto reg = context->registerService<BaseService2>("base", config().withAutowire());

        QVERIFY(context->publish());
        RegistrationSlot<BaseService2> baseSlot{reg};
        QVERIFY(!baseSlot->m_reference);

    }

    void testDoNotAutowireQObjectSelf() {
        auto reg = context->registerService<QObjectService>("base", config().withAutowire());

        QVERIFY(context->publish());
        RegistrationSlot<QObjectService> baseSlot{reg};
        QVERIFY(!baseSlot->dependency());

    }

    void testSetPropertyToSelf() {
        auto reg = context->registerService<BaseService2>("base", config({{"reference", "&base"}}));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService2> baseSlot{reg};
        QCOMPARE(baseSlot->m_reference, baseSlot.last());

    }


    void testExplicitPropertyOverridesAutowired() {
        auto regBase = context->registerService<BaseService>("dependency");
        auto regBaseToUse = context->registerService<BaseService>("baseToUse", config({{".private", "test"}}));
        auto regCyclic = context->registerService<CyclicDependency>("cyclic", config({{"dependency", "&baseToUse"}}).withAutowire());

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{regBase};
        RegistrationSlot<BaseService> baseToUseSlot{regBaseToUse};
        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        QCOMPARE(cyclicSlot->dependency(), baseToUseSlot.last());
    }


    void testAutowiredPropertyWithWrongType() {
        QObject timer;
        timer.setObjectName("timer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", config().withAutowire());

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QVERIFY(!baseSlot->m_timer);
    }


    void testWithBeanRefWithAlias() {
        QTimer timer;
        timer.setObjectName("aTimer");
        auto timerReg = context->registerObject(&timer);
        QVERIFY(timerReg.registerAlias("theTimer"));
        auto reg = context->registerService<BaseService>("base", config({{"timer", "&theTimer"}}));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }


    void testWithMissingBeanRef() {
        context->registerService<BaseService>("base", config({{"timer", "&aTimer"}}));

        QVERIFY(!context->publish());
    }

    void testDestroyRegisteredObject() {
        std::unique_ptr<Interface1> base = std::make_unique<BaseService>();
        auto baseReg = context->registerObject(base.get());
        context->registerService(service<Interface1,BaseService>());
        auto regs = context->getRegistration<Interface1>();

        QCOMPARE(regs.registeredServices().size(), 2);
        QCOMPARE(RegistrationSlot<Interface1>{regs}.invocationCount(), 1);
        context->publish();
        QCOMPARE(RegistrationSlot<Interface1>{regs}.invocationCount(), 2);
        QVERIFY(baseReg);
        base.reset();
        QVERIFY(!baseReg);
        QCOMPARE(RegistrationSlot<Interface1>{regs}.invocationCount(), 1);
    }

    void testDestroyRegisteredServiceExternally() {
        auto reg = context->registerService(service<Interface1,BaseService>());
        RegistrationSlot<Interface1> slot{reg};
        auto regs = context->getRegistration<Interface1>();
        QCOMPARE(regs.registeredServices().size(), 1);
        QVERIFY(reg);
        context->publish();
        QVERIFY(slot.last());
        QVERIFY(slot);
        delete slot.last();
        QVERIFY(reg);
        QCOMPARE(regs.registeredServices().size(), 1);
        QVERIFY(!RegistrationSlot<Interface1>{reg}.last());
        //Publish the service again:
        context->publish();
        QVERIFY(RegistrationSlot<Interface1>{reg}.last());
    }

    void testDestroyContext() {
        auto reg = context->registerService(service<Interface1,BaseService>());

        QVERIFY(reg);
        context.reset();
        QVERIFY(!reg);
    }

    void testRegisterObjectSignalsImmediately() {
        BaseService base;
        RegistrationSlot<BaseService> baseSlot{context->registerObject(&base)};
        QVERIFY(baseSlot);
        QVERIFY(context->publish());
        QCOMPARE(baseSlot.invocationCount(), 1);
        QVERIFY(!base.parent());
    }

    void testOptionalDependency() {
        auto reg = context->registerService(service<DependentService>(injectIfPresent<Interface1>()));
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QVERIFY(!service->m_dependency);
    }

    void testOptionalDependencyWithAutowire() {
        auto reg = context->registerService(service<DependentService>(injectIfPresent<Interface1>()));
        QVERIFY(reg.autowire(&DependentService::setBase));
        RegistrationSlot<DependentService> srv{reg};
        QVERIFY(context->publish());
        QVERIFY(!srv->m_dependency);
        auto baseReg = context->registerService(service<Interface1,BaseService>());
        RegistrationSlot<Interface1> baseSlot{baseReg};
        QVERIFY(context->publish());
        QVERIFY(srv->m_dependency);
        QCOMPARE(srv->m_dependency, baseSlot.last());
    }

    void testCardinalityNDependencyWithAutowire() {
        auto reg = context->registerService(service<CardinalityNService>(injectAll<Interface1>()));
        QVERIFY(reg.autowire(&CardinalityNService::addBase));
        RegistrationSlot<CardinalityNService> srv{reg};
        QVERIFY(context->publish());
        QCOMPARE(srv->my_bases.size(), 0);
        auto baseReg1 = context->registerService(service<Interface1,BaseService>());
        RegistrationSlot<Interface1> baseSlot1{baseReg1};
        auto baseReg2 = context->registerService(service<Interface1,BaseService2>());
        RegistrationSlot<Interface1> baseSlot2{baseReg2};

        QVERIFY(context->publish());
        QCOMPARE(srv->my_bases.size(), 2);
        QVERIFY(srv->my_bases.contains(baseSlot1.last()));
        QVERIFY(srv->my_bases.contains(baseSlot2.last()));
    }
    void testInitializerWithContext() {
        auto baseReg = context->registerService<BaseService>("base with init");
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};
        QCOMPARE(baseSlot->context(), context.get());

    }

    void testInitializerWithDelegatingContext() {
        StandardApplicationContext delegateContext{qtditest::testLogging(), context.get(), StandardApplicationContext::delegate_tag};
        auto contextReg = delegateContext.getRegistration("context").as<QApplicationContext>();
        auto baseReg = delegateContext.registerService<BaseService>("base with init");
        QCOMPARE(baseReg.applicationContext(), context.get());
        QVERIFY(delegateContext.publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};
        RegistrationSlot<QApplicationContext> contextSlog{contextReg};
        QCOMPARE(contextSlog.last(), context.get());
        QCOMPARE(baseSlot->context(), context.get());

    }



    void testInitializerViaInterface() {
        auto baseReg = context->registerService(service<Interface1,BaseService2>(), "base with init");
        QVERIFY(context->publish());

        RegistrationSlot<Interface1> baseSlot{baseReg};
        QCOMPARE(dynamic_cast<BaseService2*>(baseSlot.last())->initCalled, 1);

    }

    void testInitializerViaAdvertisedInterface() {
        auto baseReg = context->registerService(service<BaseService2>().advertiseAs<Interface1>(), "base with init");
        QVERIFY(context->publish());

        RegistrationSlot<BaseService2> baseSlot{baseReg};
        QCOMPARE(baseSlot.last()->initCalled, 1);

    }






    void testAmbiuousMandatoryDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        BaseService myBase;
        context->registerObject<Interface1>(&myBase, "myBase");
        context->registerService(service<DependentService>(inject<Interface1>()));
        QVERIFY(!context->publish());
    }

    void testAmbiuousOptionalDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        BaseService myBase;
        context->registerObject<Interface1>(&myBase, "myBase");
        context->registerService(service<DependentService>(injectIfPresent<Interface1>()));
        QVERIFY(!context->publish());
    }


    void testNamedMandatoryDependency() {
        BaseService base;
        auto baseReg= context->registerObject<Interface1>(&base, "base");
        auto reg = context->registerService(service<DependentService>(inject<Interface1>("myBase")));
        QVERIFY(!context->publish());
        baseReg.registerAlias("myBase");
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
    }

    void testInjectMandatoryDependencyViaRegistration() {
        BaseService base;
        auto baseReg= context->registerObject<Interface1>(&base, "base");
        auto reg = context->registerService(service<DependentService>(baseReg));
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
    }


    void testConstructorValues() {
        BaseService base;
        auto reg = context->registerService(service<DependentService>(4711, QString{"https://web.de"}, &base), "dep");
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"https://web.de"});
    }

    void testResolveConstructorValues() {
        configuration->setValue("section/url", "https://google.de/search");
        configuration->setValue("section/term", "something");
        configuration->setValue("section/id", "4711");
        context->registerObject(configuration.get());
        BaseService base;
        auto reg = context->registerService(service<DependentService>(resolve<int>("${id}"), resolve("${url}?q=${term}"), &base), "dep", config().withGroup("section"));
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"https://google.de/search?q=something"});
    }

    void testFailResolveConstructorValues() {
        BaseService base;
        auto reg = context->registerService(service<DependentService>(4711, resolve("${url}"), &base), "dep");
        QVERIFY(reg);
        QVERIFY(!context->publish());
    }

    void testResolveConstructorValuesWithDefault() {
        BaseService base;
        auto reg = context->registerService(service<DependentService>(resolve("${id}", 4711), resolve("${url}", QString{"localhost:8080"}), &base), "dep");
        QVERIFY(reg);
        RegistrationSlot<DependentService> service{reg};

        QVERIFY(context->publish());
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"localhost:8080"});

    }

    void testResolveConstructorValuesInSectionWithFallback() {
        configuration->setValue("section/url", "https://google.de/search");
        configuration->setValue("id", "4711");
        context->registerObject(configuration.get());
        BaseService base;
        auto reg = context->registerService(service<DependentService>(resolve<int>("${*/id}"), resolve("${*/dep/url}"), &base), "dep", config().withGroup("section"));
        QVERIFY(reg);
        RegistrationSlot<DependentService> service{reg};

        QVERIFY(context->publish());
        QCOMPARE(service->m_id, 4711);
        QCOMPARE(service->m_url, QString{"https://google.de/search"});

    }

    void testResolveConstructorValuesPrecedence() {
        BaseService base;
        auto reg = context->registerService(service<DependentService>(resolve("${id:42}", 4711), resolve("${url:n/a}", QString{"localhost:8080"}), &base), "dep");
        QVERIFY(reg);
        RegistrationSlot<DependentService> service{reg};

        QVERIFY(context->publish());
        QCOMPARE(service->m_id, 42);
        QCOMPARE(service->m_url, QString{"n/a"});

    }


    void testMixConstructorValuesWithDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        auto reg = context->registerService(service<DependentService>(4711, QString{"https://web.de"}, inject<Interface1>()), "dep");
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
        auto depReg = context->registerService(service<DependentService>(injectIfPresent<Interface1>("myBase")));
        auto depReg2 = context->registerService(service<DependentService>(injectIfPresent<Interface1>("base")));

        QVERIFY(context->publish());
        RegistrationSlot<DependentService> depSlot{depReg};
        QVERIFY(!depSlot->m_dependency);
        RegistrationSlot<DependentService> depSlot2{depReg2};
        QCOMPARE(depSlot2->m_dependency, &base);

    }




    void testPrototypeDependency() {
        this->configuration->setValue("foo", "the foo");
        context->registerObject(configuration.get());
        auto regProto = context->registerPrototype<BaseService>("base", config({{"foo", "${foo}"}}));

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> protoSlot{regProto};
        QVERIFY(!protoSlot);
        auto depReg1 = context->registerService(service<DependentService>(regProto), "dependent1");
        auto depReg2 = context->registerService(service<DependentService>(regProto), "dependent2");

        auto protoDepReg = context->registerService(prototype<DependentService>(regProto), "dependent3");
        RegistrationSlot<DependentService> dependentSlot{depReg1};
        RegistrationSlot<DependentService> dependentSlot2{depReg2};
        RegistrationSlot<DependentService> protoDependentSlot{protoDepReg};
        QVERIFY(context->publish());
        QVERIFY(!protoDependentSlot);
        QCOMPARE(protoSlot.invocationCount(), 2);
        QCOMPARE(protoSlot[0]->foo(), "the foo");
        QCOMPARE(protoSlot[1]->foo(), "the foo");
        QCOMPARE(protoSlot[0]->parent(), dependentSlot.last());
        QCOMPARE(protoSlot[1]->parent(), dependentSlot2.last());
        QVERIFY(dependentSlot->m_dependency);
        QVERIFY(dependentSlot2->m_dependency);
        QCOMPARE_NE(dependentSlot->m_dependency, dependentSlot2->m_dependency);
    }

    void testPrototypeReferencedAsBean() {
        auto regProto = context->registerPrototype<BaseService>("base");
        RegistrationSlot<BaseService> protoSlot{regProto};
        auto depReg = context->registerService<CyclicDependency>("dependent", config({{"dependency", "&base"}}));
        QVERIFY(context->publish());
        RegistrationSlot<CyclicDependency> dependentSlot{depReg};
        QVERIFY(dependentSlot);
        QVERIFY(context->publish());
        QVERIFY(protoSlot);
        QCOMPARE(dependentSlot->m_dependency, protoSlot.last());
        QCOMPARE(protoSlot.last()->parent(), dependentSlot.last());

    }

    void testDeletePrototypeExternally() {
        auto regProto = context->registerPrototype<BaseService>();

        RegistrationSlot<BaseService> protoSlot{regProto};
        QVERIFY(!protoSlot);
        auto depReg1 = context->registerService(service<DependentService>(regProto), "dependent1");
        context->registerService(service<DependentService>(regProto), "dependent2");
        RegistrationSlot<DependentService> dependentSlot{depReg1};
        QVERIFY(context->publish());
        QCOMPARE(protoSlot.invocationCount(), 2);
        QVERIFY(dependentSlot->m_dependency);
        QCOMPARE(dynamic_cast<QObject*>(dependentSlot->m_dependency)->parent(), dependentSlot.last());

        delete dependentSlot->m_dependency;
        RegistrationSlot<BaseService> newProtoSlot{regProto};
        QCOMPARE(newProtoSlot.invocationCount(), 1);
    }


    void testNestedPrototypeDependency() {
        auto regBase2Proto = context->registerPrototype<BaseService2>();
        auto regBaseProto = context->registerPrototype<BaseService>();
        RegistrationSlot<BaseService> baseSlot{context->getRegistration<BaseService>()};
        RegistrationSlot<BaseService2> base2Slot{context->getRegistration<BaseService2>()};
        auto depProtoReg = context->registerService(prototype<DependentService>(regBaseProto), "dependent1");
        RegistrationSlot<DependentService> depSlot{depProtoReg};
        QVERIFY(context->publish());
        QVERIFY(!baseSlot);
        QVERIFY(!base2Slot);
        QVERIFY(!depSlot);
        auto threeReg = context->registerService(service<ServiceWithThreeArgs>(regBaseProto, depProtoReg, regBase2Proto), "three");
        RegistrationSlot<ServiceWithThreeArgs> threeSlot{threeReg};
        QVERIFY(context->publish());
        QVERIFY(threeSlot);
        QCOMPARE(threeSlot->m_base2->parent(), threeSlot.last());
        QCOMPARE(threeSlot->m_dep->parent(), threeSlot.last());
        QCOMPARE(baseSlot.invocationCount(), 2);
        if(baseSlot[0] == threeSlot->m_base) {
            QCOMPARE(baseSlot[0]->parent(), threeSlot.last());
            QCOMPARE(baseSlot[1]->parent(), threeSlot->m_dep);
        } else {
            QCOMPARE(baseSlot[0]->parent(), threeSlot->m_dep);
            QCOMPARE(baseSlot[1]->parent(), threeSlot.last());
        }
        QCOMPARE(base2Slot.invocationCount(), 1);
    }





    void testAdvertiseAs() {
        auto reg = context->registerService(service<BaseService>().advertiseAs<Interface1>());
        auto simpleReg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(reg);
        QVERIFY(simpleReg.as<Interface1>());
        QVERIFY(simpleReg.as<BaseService>());
        QVERIFY(!simpleReg.as<BaseService2>());
        QCOMPARE(reg, simpleReg);
        auto timerReg = context->registerService(service<BaseService>().advertiseAs<TimerAware>());
        QVERIFY(timerReg);
        QCOMPARE_NE(timerReg, simpleReg);
        auto failedReg = context->registerService(service<BaseService>().advertiseAs<Interface1,TimerAware>());
        //You cannot register a Service with the same implementation-type and primary interface-type, but different addtional service-types:
        QVERIFY(!failedReg);

    }

    void testAdvertiseAsNamed() {
        auto reg = context->registerService(service<BaseService>().advertiseAs<Interface1>(), "base");
        auto simpleReg = context->registerService(service<Interface1,BaseService>(), "base");
        QVERIFY(reg);
        QCOMPARE(reg, simpleReg);
        auto timerReg = context->registerService(service<BaseService>().advertiseAs<Interface1,TimerAware>(), "timeraware");
        QVERIFY(timerReg);
        QVERIFY(timerReg.as<Interface1>());
        QVERIFY(timerReg.as<BaseService>());
        QVERIFY(timerReg.as<TimerAware>());
        QVERIFY(!timerReg.as<BaseService2>());
        QCOMPARE_NE(timerReg, reg);
        auto bases = context->getRegistration<BaseService>().registeredServices();
        QCOMPARE(bases.size(), 2);
        int timerCount = 0;
        for(auto& regBase : bases) {
            if(regBase.as<TimerAware>()) {
                ++timerCount;
                QCOMPARE(regBase, timerReg);
            }
        }
        QCOMPARE(timerCount, 1);

        auto timers = context->getRegistration<TimerAware>().registeredServices();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers[0], timerReg);


    }

    void testAdvertiseAdditionalInterface() {
        auto reg = context->registerService(service<Interface1,BaseService>().advertiseAs<TimerAware>());
        auto reg2 = context->registerService(service<BaseService>().advertiseAs<Interface1,TimerAware>());
        QCOMPARE(reg, reg2);
        auto baseReg = context->getRegistration<BaseService>();
        auto ifaceReg = context->getRegistration<Interface1>();
        auto timerReg= context->getRegistration<TimerAware>();
        QCOMPARE(ifaceReg.registeredServices().size(), 1);
        QCOMPARE(timerReg.registeredServices().size(), 1);
        QCOMPARE(baseReg.registeredServices().size(), 1);
        QVERIFY(context->publish());
        RegistrationSlot<Interface1> ifaceSlot{ifaceReg};
        RegistrationSlot<TimerAware> timerSlot{timerReg};
        QVERIFY(ifaceSlot);
        QVERIFY(timerSlot);

    }

    void testAdvertiseObjectAsNotImplementedInterface() {
        BaseService2 base;
        auto failedReg = context->registerObject<Interface1,TimerAware>(&base);
    }

   void testAdvertiseObjectAs() {
        BaseService base;
        auto simpleReg = context->registerObject<Interface1>(&base);
        QVERIFY(simpleReg);
        auto failedReg = context->registerObject<Interface1,TimerAware>(&base);
        //You cannot register the same Object with the same implementation-type and primary interface-type, but different addtional service-types:
        QVERIFY(!failedReg);

    }


    void testAdvertiseObjectAsNamed() {
        BaseService base;
        auto reg = context->registerObject<Interface1>(&base, "base");
        QVERIFY(reg);
        auto simpleReg = context->registerObject<Interface1,TimerAware>(&base, "base");
        QVERIFY(!simpleReg);

    }

    void testAdvertiseObjectWithAdditionalInterface() {
        BaseService base;
        auto reg = context->registerObject<Interface1,TimerAware>(&base);
        auto baseReg = context->getRegistration<BaseService>();
        auto ifaceReg = context->getRegistration<Interface1>();
        auto timerReg= context->getRegistration<TimerAware>();
        QCOMPARE(ifaceReg.registeredServices().size(), 1);
        QCOMPARE(timerReg.registeredServices().size(), 1);
        QCOMPARE(baseReg.registeredServices().size(), 1);
        QVERIFY(context->publish());
        RegistrationSlot<Interface1> ifaceSlot{ifaceReg};
        RegistrationSlot<TimerAware> timerSlot{timerReg};
        QVERIFY(ifaceSlot);
        QVERIFY(timerSlot);

    }


    void testRegisterAlias() {
        auto reg = context->registerService(service<Interface1,BaseService>(), "base");
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        QVERIFY(reg.registerAlias("Hugo"));
        QVERIFY(reg.registerAlias("Hugo")); //Should be idempotent
        QVERIFY(reg.registerAlias("Jill"));
        QVERIFY(!reg.registerAlias("base2"));
        QVERIFY(!reg2.registerAlias("base"));
        QVERIFY(!reg2.registerAlias("Hugo"));
        QCOMPARE(context->getRegistration<Interface1>("base"), reg);
        QCOMPARE(context->getRegistration<Interface1>("Hugo"), reg);
        QCOMPARE(context->getRegistration<Interface1>("Jill"), reg);
    }


    void testRegisterTwiceDifferentImpl() {
        auto reg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(reg);
        //Same Interface, different implementation:
        auto reg2 = context->registerService(service<Interface1,BaseService2>());

        QCOMPARE_NE(reg2, reg);
        QCOMPARE(reg, context->getRegistration<Interface1>(reg.registeredName()));
        QCOMPARE(reg2, context->getRegistration<Interface1>(reg2.registeredName()));

        QVERIFY(!context->getRegistration<Interface1>(""));
    }

    void testRegisterTwiceDifferentName() {
        auto reg = context->registerService(service<Interface1,BaseService>(), "base");
        QVERIFY(reg);
        //Same Interface, same implementation, but different name:
        auto another = context->registerService(service<Interface1,BaseService>(), "alias");
        QVERIFY(another);
        QCOMPARE_NE(reg, another);
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
        auto reg = context->registerObject(&service, "base");

        QVERIFY(reg);
        QCOMPARE(reg.registeredName(), "base");
        QVERIFY(!context->registerObject(&service, "alias"));
    }

    void testRegisterAnonymousObjectTwice() {
        BaseService service;
        auto reg = context->registerObject(&service);
        QVERIFY(reg);
        auto reg4 = context->registerObject(&service);
        QCOMPARE(reg4, reg);

    }

    void testRegisterSameObjectAnonymousThenNamed() {
        BaseService service;
        auto reg = context->registerObject(&service);
        QVERIFY(reg);
        QVERIFY(!context->registerObject(&service, "base"));

    }

    void testRegisterSameObjectNamedThenAnonymous() {
        BaseService service;
        auto reg = context->registerObject(&service, "base");
        QVERIFY(reg);
        auto reg2 = context->registerObject(&service);
        QCOMPARE(reg, reg2);

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
        auto reg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(reg);
        //Same Interface, same implementation, but different properties:
        auto reg2 = context->registerService(service<Interface1,BaseService>(), "", config({{"objectName", "tester"}}));
        QCOMPARE_NE(reg2, reg);
        QVariantMap expectedProperties{{"objectName", "tester"}};
        QCOMPARE(reg2.config().properties, expectedProperties);
    }

    void testFailRegisterTwiceSameName() {
        auto reg = context->registerService(service<Interface1,BaseService>(), "base");
        QVERIFY(reg);

        //Everything is different, but the name:
        auto reg2 = context->registerService(service<DependentService>(inject<BaseService>()), "base");
        QVERIFY(!reg2);
    }



    void testFailRegisterTwice() {
        auto reg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(reg);

        //Same Interface, same implementation, same properties, same name:
        auto reg2 = context->registerService(service<Interface1,BaseService>());
        QCOMPARE(reg2, reg);
    }


    void testRegisterInvalidDependency() {
        ServiceRegistration<Interface1,ServiceScope::SINGLETON> invalidReg;
        QVERIFY(!context->registerService(service<DependentService>(invalidReg)));
    }

    void testRegisterInvalidProxyDependency() {
        ProxyRegistration<Interface1> invalidReg;
        QVERIFY(!context->registerService(service<CardinalityNService>(invalidReg)));
    }

    void testRegisterTemplateAsDependency() {
        ServiceRegistration<BaseService,ServiceScope::UNKNOWN> templateReg = context->registerServiceTemplate<BaseService>();
        QVERIFY(templateReg);
        //Using a TEMPLATE as dependency must fail at runtime:
        QVERIFY(!context->registerService(service<DependentService>(templateReg)));
    }


    void testServiceRegistrationEquality() {
        ServiceRegistration<Interface1> reg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(reg);
        ServiceRegistration<Interface1> anotherReg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(anotherReg);
        QCOMPARE(reg, anotherReg);

        QCOMPARE_NE(reg, ServiceRegistration<Interface1>());
    }



    void testInvalidServiceRegistrationEquality() {
        ServiceRegistration<Interface1> invalidReg;
        QVERIFY(!invalidReg);
        QCOMPARE(invalidReg.registeredName(), QString{});

        ServiceRegistration<Interface1> anotherInvalidReg;
        //Two invalid registrations are never equal:
        QCOMPARE_NE(anotherInvalidReg, invalidReg);
    }



    void testDependencyWithRequiredName() {
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1");
        auto reg = context->registerService(service<DependentService>(inject<Interface1>("base2")));
        QVERIFY(!context->publish());
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, base2.last());

    }

    void testPublishPartialDependencyWithRequiredName() {
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1");
        RegistrationSlot<Interface1> slot1{reg1};
        auto reg = context->registerService(service<DependentService>(inject<Interface1>("base2")));
        RegistrationSlot<DependentService> srvSlot{reg};
        QVERIFY(!context->publish(true));
        QVERIFY(slot1);
        QVERIFY(!srvSlot);
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        QVERIFY(context->publish());
        RegistrationSlot<Interface1> slot2{reg2};
        QVERIFY(slot2);
        QCOMPARE(srvSlot->m_dependency, slot2.last());

    }

    void testPublishPartialWithBeanRef() {
        auto timerReg1 = context->registerService(service<QTimer>(), "timer1");
        RegistrationSlot<QTimer> timerSlot1{timerReg1};

        auto reg = context->registerService(service<BaseService>(), "srv", config({{"timer", "&timer2"}}));
        RegistrationSlot<BaseService> slot1{reg};
        QVERIFY(!context->publish(true));
        QVERIFY(timerSlot1);
        QVERIFY(!slot1);
        auto timerReg2 = context->registerService(service<QTimer>(), "timer2");
        RegistrationSlot<QTimer> timerSlot2{timerReg2};
        QVERIFY(context->publish());
        QVERIFY(timerSlot2);
        QVERIFY(slot1);
        QCOMPARE(slot1->timer(), timerSlot2.last());

    }

    void testPublishPartialWithConfig() {
        context->registerObject(configuration.get());
        auto reg = context->registerService(service<BaseService>(), "srv", config({{"foo", "${foo}"}}));
        QVERIFY(!context->publish(true));
        RegistrationSlot<BaseService> slot1{reg};
        QVERIFY(!slot1);
        configuration->setValue("foo", "Hello, world");
        QVERIFY(context->publish());
        QVERIFY(slot1);
        QCOMPARE(slot1->foo(), "Hello, world");

    }

    void testDependencyWithRequiredRegisteredName() {
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1");
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        auto reg = context->registerService(service<DependentService>(reg2));

        QVERIFY(context->publish());
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, base2.last());

    }



    void testCardinalityNService() {
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1");
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        auto reg = context->registerService(service<CardinalityNService>(injectAll<Interface1>()));
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        QCOMPARE(regs.registeredServices().size(), 2);
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);

        QCOMPARE(service->my_bases.size(), 2);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QVERIFY(service->my_bases.contains(base1.last()));
        QVERIFY(service->my_bases.contains(base2.last()));

    }

    void testInjectAllViaRegistration() {
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1");
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        auto regs = context->getRegistration<Interface1>();

        auto reg = context->registerService(service<CardinalityNService>(regs));
        QVERIFY(context->publish());
        QCOMPARE(regs.registeredServices().size(), 2);
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);

        QCOMPARE(service->my_bases.size(), 2);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QVERIFY(service->my_bases.contains(base1.last()));
        QVERIFY(service->my_bases.contains(base2.last()));

    }

    void testCardinalityNServiceWithRequiredName() {
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1");
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2");
        auto reg = context->registerService(service<CardinalityNService>(injectAll<Interface1>("base2")));
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);
        QCOMPARE(service->my_bases.size(), 1);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QCOMPARE(service->my_bases[0], services.last());

    }

    void testCancelSubscription() {
        auto reg = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> services{reg};
        context->registerService(service<Interface1,BaseService>(), "base1");
        context->publish();
        QCOMPARE(1, services.size());
        BaseService2 base2;
        context->registerObject<Interface1>(&base2);
        QCOMPARE(2, services.size());
        services.subscription().cancel();
        BaseService2 base3;
        context->registerObject<Interface1>(&base3);
        QCOMPARE(2, services.size());
    }

    void testCancelAutowireSubscription() {
        auto reg = context->registerService<CardinalityNService>(service<CardinalityNService>(injectAll<Interface1>()));
        auto subscription = reg.autowire(&CardinalityNService::addBase);
        RegistrationSlot<CardinalityNService> slot{reg};
        context->publish();
        QCOMPARE(slot->my_bases.size(), 0);
        context->registerService(service<Interface1,BaseService>(), "base1");

        context->publish();

        QCOMPARE(slot->my_bases.size(), 1);
        BaseService2 base2;
        context->registerObject<Interface1>(&base2);
        QCOMPARE(slot->my_bases.size(), 2);
        subscription.cancel();
        BaseService2 base3;
        context->registerObject<Interface1>(&base3);
        QCOMPARE(slot->my_bases.size(), 2);
    }


    void testPostProcessor() {
        auto processReg = context->registerService<PostProcessor>();
        configuration->setValue("foo", "Harry");
        context->registerObject(configuration.get());
        auto reg1 = context->registerService(service<Interface1,BaseService>(), "base1", config({{"foo", "${foo}"}}));
        auto reg2 = context->registerService(service<Interface1,BaseService2>(), "base2", config({{".store", "for later use"}}));
        QVERIFY(context->publish());
        RegistrationSlot<PostProcessor> processSlot{processReg};
        QCOMPARE(processSlot->servicesMap.size(), 2);
        QVERIFY(dynamic_cast<BaseService*>(processSlot->servicesMap[reg1.unwrap()]));
        QVERIFY(dynamic_cast<BaseService2*>(processSlot->servicesMap[reg2.unwrap()]));
        QCOMPARE(processSlot->resolvedPropertiesMap[reg1.unwrap()]["foo"], "Harry");
        QCOMPARE(processSlot->resolvedPropertiesMap[reg2.unwrap()][".store"], "for later use");
    }



    void testCardinalityNServiceEmpty() {
        auto reg = context->registerService(service<CardinalityNService>(injectAll<Interface1>()));
        QVERIFY(context->publish());
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE(service->my_bases.size(), 0);
    }



    void testUseViaImplType() {
        context->registerService(service<Interface1,BaseService>());
        context->registerService(service<DependentService>(inject<BaseService>()));
        QVERIFY(context->publish());
    }


    void testRegisterWithExplicitServiceFactory() {
        int calledFactory = 0;
        auto baseReg = context->registerService(serviceFactory(service_factory<BaseService>{&calledFactory}).advertiseAs<Interface1>());
        QVERIFY(context->publish());
        QCOMPARE(calledFactory, 1);
    }

    void testRegisterWithAnonymousServiceFactory() {
        int calledFactory = 0;
        auto baseFactory = [&calledFactory] { ++calledFactory; return new BaseService{}; };
        auto baseReg = context->registerService(serviceFactory<decltype(baseFactory),BaseService>(baseFactory).advertiseAs<Interface1>());
        QVERIFY(context->publish());
        QCOMPARE(calledFactory, 1);
        auto depFactory = [&calledFactory](Interface1* dep) { ++calledFactory; return new DependentService{dep}; };
        auto depReg = context->registerService(serviceFactory<decltype(depFactory),DependentService>(depFactory, baseReg));
        QVERIFY(context->publish());
        QCOMPARE(calledFactory, 2);
    }

    void testRegisterByServiceType() {
        auto reg = context->registerService(service<Interface1,BaseService>());
        QVERIFY(reg);
        QVERIFY(reg.matches<Interface1>());
        QVERIFY(reg.matches<BaseService>());
        QVERIFY(reg.as<Interface1>());
        QVERIFY(reg.as<BaseService>());
        QVERIFY(!reg.as<BaseService2>());
        QVERIFY(context->publish());
    }



    void testMissingDependency() {
        auto reg = context->registerService(service<DependentService>(inject<Interface1>()));
        QVERIFY(reg);
        QVERIFY(!context->publish());
        context->registerService(service<Interface1,BaseService>());
        QVERIFY(context->publish());
    }

    void testCyclicDependency() {
        auto reg1 = context->registerService(service<BaseService>(inject<CyclicDependency>()));
        QVERIFY(reg1);



        auto reg2 = context->registerService(service<CyclicDependency>(inject<BaseService>()));
        QVERIFY(!reg2);

    }

    void testWorkaroundCyclicDependencyWithBeanRef() {
        auto regBase = context->registerService(service<BaseService>(inject<CyclicDependency>()), "base");
        QVERIFY(regBase);



        auto regCyclic = context->registerService<CyclicDependency>( "cyclic", config({{"dependency", "&base"}}));
        QVERIFY(regCyclic);

        QVERIFY(context->publish());

        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        RegistrationSlot<BaseService> baseSlot{regBase};

        QVERIFY(cyclicSlot);
        QCOMPARE(cyclicSlot.last(), baseSlot->dependency());
        QCOMPARE(baseSlot.last(), cyclicSlot->dependency());

    }

    void testWorkaroundCyclicDependencyWithAutowiring() {
        auto regBase = context->registerService(service<BaseService>(inject<CyclicDependency>()), "dependency");
        QVERIFY(regBase);



        auto regCyclic = context->registerService<CyclicDependency>( "cyclic", config().withAutowire());
        QVERIFY(regCyclic);

        QVERIFY(context->publish());

        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        RegistrationSlot<BaseService> baseSlot{regBase};

        QVERIFY(cyclicSlot);
        QCOMPARE(cyclicSlot.last(), baseSlot->dependency());
        QCOMPARE(baseSlot.last(), cyclicSlot->dependency());

    }


    void testKeepOrderOfRegistrations() {
        context->registerService(service<Interface1,BaseService>(), "base1");
        context->registerService(service<Interface1,BaseService>(inject<CyclicDependency>()), "base2");
        context->registerService(service<Interface1,BaseService>(), "base3");
        auto regCard = context->registerService(service<CardinalityNService>(injectAll<Interface1>()));
        auto regCyclic = context->registerService(service<CyclicDependency>(inject<BaseService>("base3")));
        RegistrationSlot<CardinalityNService> slotCard{regCard};
        QVERIFY(context->publish());
        QCOMPARE(slotCard->my_bases.size(), 3);
        QCOMPARE(static_cast<BaseService*>(slotCard->my_bases[0])->objectName(), "base1");
        QCOMPARE(static_cast<BaseService*>(slotCard->my_bases[1])->objectName(), "base2");
        QCOMPARE(static_cast<BaseService*>(slotCard->my_bases[2])->objectName(), "base3");
    }



    void testPublishAdditionalServices() {

        unsigned contextPublished = context->published();
        unsigned contextPending = context->pendingPublication();
        connect(context.get(), &QApplicationContext::publishedChanged, this, [this,&contextPublished] {contextPublished = context->published();});
        connect(context.get(), &QApplicationContext::pendingPublicationChanged, this, [this,&contextPending] {contextPending = context->pendingPublication();});
        auto baseReg = context->getRegistration<Interface1>();
        context->registerService(service<Interface1,BaseService>(), "base");
        QCOMPARE(contextPending, 1);
        RegistrationSlot<Interface1> baseSlot{baseReg};
        auto regDep = context->registerService(service<DependentService>(inject<Interface1>()));
        RegistrationSlot<DependentService> depSlot{regDep};
        QCOMPARE(contextPending, 2);
        QCOMPARE(contextPublished, 2); //The QCoreApplication and the QApplicationContext.
        QVERIFY(context->publish());
        QCOMPARE(contextPending, 0);
        QCOMPARE(contextPublished, 4);

        QVERIFY(baseSlot);
        QVERIFY(depSlot);
        QCOMPARE(baseSlot.invocationCount(), 1);

        auto anotherBaseReg = context->registerService(service<Interface1,BaseService2>(), "anotherBase");
        QCOMPARE(contextPending, 1);
        QCOMPARE(contextPublished, 4);

        RegistrationSlot<Interface1> anotherBaseSlot{anotherBaseReg};
        auto regCard = context->registerService(service<CardinalityNService>(injectAll<Interface1>()));
        QCOMPARE(contextPending, 2);
        QCOMPARE(contextPublished, 4);


        RegistrationSlot<CardinalityNService> cardSlot{regCard};
        QVERIFY(context->publish());
        QCOMPARE(contextPending, 0);
        QCOMPARE(contextPublished, 6);
        QVERIFY(cardSlot);
        QCOMPARE(cardSlot->my_bases.size(), 2);
        QCOMPARE(baseSlot.invocationCount(), 2);
        QCOMPARE(baseSlot.last(), anotherBaseSlot.last());

    }

    void testPublishThenSubscribeInThread() {
        auto registration = context->registerService<BaseService>();
        RegistrationSlot<BaseService> slot{registration};
        context->publish();
        SubscriptionThread<BaseService> thread{context.get()};
        thread.start();
        bool hasSubscribed = QTest::qWaitFor([&thread] { return thread.subscribed;}, 1000);
        QVERIFY(hasSubscribed);
        QVERIFY(thread.wait(1000));
        QVERIFY(thread.service);
        QCOMPARE(thread.service, slot.last());
    }



    void testSubscribeInThreadThenPublish() {
        auto registration = context->registerService<BaseService>();
        RegistrationSlot<BaseService> slot{registration};
        SubscriptionThread<BaseService> thread{context.get()};
        thread.start();
        bool hasSubscribed = QTest::qWaitFor([&thread] { return thread.subscribed;}, 1000);
        QVERIFY(hasSubscribed);
        context->publish();
        QVERIFY(thread.wait(1000));
        QVERIFY(thread.service);
        QCOMPARE(thread.service, slot.last());
    }


    void testPublishInThreadFails() {
        auto registration = context->registerService<BaseService>();
        RegistrationSlot<BaseService> slot{registration};

        QAtomicInt success{-1};
        QThread* thread = QThread::create([this,&success] {
            success = context->publish();
        });
        thread->start();
        bool hasSubscribed = QTest::qWaitFor([&success] { return success != -1;}, 1000);
        QVERIFY(hasSubscribed);
        QVERIFY(!success);
        QVERIFY(!slot);
        QVERIFY(thread->wait(1000));
        delete thread;
    }


    void testGetRegistrationInThread() {
        QMutex mutex;
        ProxyRegistration<BaseService> reg;
        QThread* thread = QThread::create([this,&reg,&mutex] {
            QMutexLocker locker{&mutex};
            reg = context->getRegistration<BaseService>();
        });
        thread->start();
        bool hasSetParent = QTest::qWaitFor([&reg,&mutex] {QMutexLocker locker{&mutex}; return reg.isValid();}, 1000);
        QVERIFY(hasSetParent);
        QCOMPARE(reg.unwrap()->thread(), QThread::currentThread());
        QVERIFY(thread->wait(1000));
        delete thread;
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
        auto dependent2Reg = context->registerService(service<DependentServiceLevel2>(inject<DependentService>()), "dependent2");
        dependent2Reg.subscribe(this, published);
        auto dependentReg = context->registerService(service<DependentService>(baseReg), "dependent");
        dependentReg.subscribe(this, published);
        auto threeReg = context->registerService(service<ServiceWithThreeArgs>(baseReg, dependentReg, base2Reg), "three");
        threeReg.subscribe(this, published);
        auto fourReg = context->registerService(service<ServiceWithFourArgs>(inject<BaseService,ref_converter<BaseService>>(),
                                                                             inject<DependentService,ref_converter<DependentService>>(),
                                                                             inject<BaseService2,ref_converter<BaseService2>>(),
                                                                             inject<ServiceWithThreeArgs,ref_converter<ServiceWithThreeArgs>>()), "four");
        fourReg.subscribe(this, published);
        auto fiveReg = context->registerService(service<ServiceWithFiveArgs>(baseReg, dependentReg, base2Reg, threeReg, fourReg), "five");
        fiveReg.subscribe(this, published);
        auto sixReg = context->registerService(service<ServiceWithSixArgs>(QString{"Hello"}, base2Reg, injectAll<ServiceWithFiveArgs,vector_converter<ServiceWithFiveArgs>>(), threeReg, fourReg, resolve("${pi}", 3.14159)), "six");
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

        auto serviceHandles = context->getRegistrations();
        QCOMPARE(serviceHandles.size(), 10); // We have 8 registered services, plus the QCoreApplication and the QApplicationContex that are registered by default!

        //1. BaseService must be initialized before BaseService2 (because the order of registration shall be kept, barring other restrictions).
        //2. DependentService must be initialized after both BaseService.
        //3. DependentService must be initialized before DependentServiceLevel2.
        //4. ServiceWithThreeArgs must be initialized after BaseService, BaseService2 and DependentService
        QVERIFY(publishedInOrder.indexOf(base.last()) < publishedInOrder.indexOf(base2.last()));
        QVERIFY(publishedInOrder.indexOf(dependent.last()) < publishedInOrder.indexOf(dependent2.last()));
        QVERIFY(publishedInOrder.indexOf(base.last()) < publishedInOrder.indexOf(three.last()));
        QVERIFY(publishedInOrder.indexOf(dependent.last()) < publishedInOrder.indexOf(three.last()));
        QVERIFY(publishedInOrder.indexOf(base2.last()) < publishedInOrder.indexOf(three.last()));
        QVERIFY(publishedInOrder.indexOf(three.last()) < publishedInOrder.indexOf(four.last()));
        QVERIFY(publishedInOrder.indexOf(four.last()) < publishedInOrder.indexOf(five.last()));
        QVERIFY(publishedInOrder.indexOf(five.last()) < publishedInOrder.indexOf(six.last()));
        context.reset();

        QCOMPARE(destroyedInOrder.size(), 8);

        //We cannot say anything about the destruction-order of the Services that have no dependencies:
        //BaseService and BaseService2
        //However, what we can say is:
        //1. DependentService must be destroyed before both BaseService.
        //2. DependentService must be destroyed after DependentServiceLevel2.
        //3. ServiceWithThreeArgs must be destroyed before BaseService, BaseService2 and DependentService
        //4. BaseService2 must destroyed before BaseService (because the order of registration shall be kept, barring other restrictions).

        QVERIFY(destroyedInOrder.indexOf(dependent.last()) > destroyedInOrder.indexOf(dependent2.last()));
        QVERIFY(destroyedInOrder.indexOf(base.last()) > destroyedInOrder.indexOf(three.last()));
        QVERIFY(destroyedInOrder.indexOf(dependent.last()) > destroyedInOrder.indexOf(three.last()));
        QVERIFY(destroyedInOrder.indexOf(base2.last()) > destroyedInOrder.indexOf(three.last()));
        QVERIFY(destroyedInOrder.indexOf(three.last()) > destroyedInOrder.indexOf(four.last()));
        QVERIFY(destroyedInOrder.indexOf(four.last()) > destroyedInOrder.indexOf(five.last()));
        QVERIFY(destroyedInOrder.indexOf(five.last()) > destroyedInOrder.indexOf(six.last()));
        QVERIFY(destroyedInOrder.indexOf(base2.last()) < destroyedInOrder.indexOf(base.last()));
    }

private:
    std::unique_ptr<QApplicationContext> context;
    std::unique_ptr<QTemporaryFile> settingsFile;
    std::unique_ptr<QSettings> configuration;
};

} //mcnepp::qtdi

#include "testqapplicationcontext.moc"


QTEST_MAIN(mcnepp::qtdi::ApplicationContextTest)
