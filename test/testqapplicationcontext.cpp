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

    explicit RegistrationSlot(ServiceRegistration<S>* registration) : m_obj(nullptr),
        m_invocationCount(0) {
        registration->subscribe(this, &RegistrationSlot::setObj);
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
        config = new QSettings{settingsFile->fileName(), QSettings::Format::IniFormat};
        context = new StandardApplicationContext(config);
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
        QCOMPARE(reg->service_type(), typeid(BaseService));
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QVERIFY(slot());
    }

    void testWithProperty() {
        auto reg = context->registerService<QTimer>("timer", {{"interval", 4711}});
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }


    void testWithPlaceholderProperty() {
        config->setValue("timerInterval", 4711);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", {{"interval", "${timerInterval}"}});
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
    }

    void testWithUnbalancedPlaceholderProperty() {
        config->setValue("timerInterval", 4711);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", {{"interval", "${timerInterval"}});
        QVERIFY(!context->publish());
    }

    void testWithDollarInPlaceholderProperty() {
        config->setValue("timerInterval", 4711);
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", {{"interval", "${$timerInterval}"}});
        QVERIFY(!context->publish());
    }


    void testWithEmbeddedPlaceholderProperty() {
        config->setValue("baseName", "theBase");
        context->registerObject(config);

        auto reg = context->registerService<BaseService>("base", {{"objectName", "I am ${baseName}!"}});
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};

        QCOMPARE(slot->objectName(), "I am theBase!");
    }

    void testWithEmbeddedPlaceholderPropertyAndDollarSign() {
        config->setValue("dollars", "one thousand");
        context->registerObject(config);

        auto reg = context->registerService<BaseService>("base", {{"objectName", "I have $${dollars}$"}});
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "I have $one thousand$");
    }


    void testWithTwoPlaceholders() {
        config->setValue("section", "BaseServices");
        config->setValue("baseName", "theBase");
        context->registerObject(config);

        auto reg = context->registerService<BaseService>("base", {{"objectName", "${section}:${baseName}:yeah"}});
        QVERIFY(context->publish());
        RegistrationSlot<BaseService> slot{reg};
        QCOMPARE(slot->objectName(), "BaseServices:theBase:yeah");
    }




    void testWithConfiguredPropertyInSubConfig() {
        config->setValue("timers/interval", 4711);
        config->setValue("timers/single", "true");
        context->registerObject(config);

        auto reg = context->registerService<QTimer>("timer", {{"interval", "${timers/interval}"},
                                                                              {"singleShot", "${timers/single}"}});
        QVERIFY(context->publish());
        RegistrationSlot<QTimer> slot{reg};
        QCOMPARE(slot->interval(), 4711);
        QVERIFY(slot->isSingleShot());
    }

    void testWithUnresolvableProperty() {

        context->registerService<QTimer>("timer", {{"interval", "${timer.interval}"}});
        QVERIFY(!context->publish());
    }



    void testWithInvalidProperty() {
        context->registerService<QTimer>("timer", {{"firstName", "Max"}});
        QVERIFY(!context->publish());
    }

    void testWithBeanRefProperty() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", {{"timer", "&aTimer"}});

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }

    void testWithBeanRefNestedProperty() {
        QTimer timer1;
        timer1.setInterval(4711);
        auto reg2 = context->registerService<QTimer>("timer2", {{"interval", "&base.timer.interval"}});
        context->registerObject(&timer1, "timer1");
        context->registerService<BaseService>("base", {{"timer", "&timer1"}});

        QVERIFY(context->publish());
        RegistrationSlot<QTimer> timerSlot2{reg2};
        QCOMPARE(timerSlot2->interval(), 4711);
    }

    void testAutowiredPropertyByName() {
        QTimer timer;
        timer.setObjectName("timer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", {}, true);

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }

    void testAutowiredPropertyByType() {
        QTimer timer;
        timer.setObjectName("IAmTheRealTimer");
        context->registerObject(&timer);
        auto reg = context->registerService<BaseService>("base", {}, true);
        context->registerService<BaseService2>("timer");

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }


    void testExplicitPropertyOverridesAutowired() {
        auto regBase = context->registerService<BaseService>("dependency");
        auto regBaseToUse = context->registerService<BaseService>("baseToUse");
        auto regCyclic = context->registerService<CyclicDependency>("cyclic", {{"dependency", "&baseToUse"}}, true);

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
        auto reg = context->registerService<BaseService>("base", {}, true);

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QVERIFY(!baseSlot->m_timer);
    }


    void testWithBeanRefWithAlias() {
        QTimer timer;
        timer.setObjectName("aTimer");
        context->registerObject(&timer);
        context->registerObject(&timer, "theTimer");
        auto reg = context->registerService<BaseService>("base", {{"timer", "&theTimer"}});

        QVERIFY(context->publish());
        RegistrationSlot<BaseService> baseSlot{reg};
        QCOMPARE(baseSlot->m_timer, &timer);
    }


    void testWithMissingBeanRef() {
        context->registerService<BaseService>("base", {{"timer", "&aTimer"}});

        QVERIFY(!context->publish());
    }


    void testRegisterObjectSignalsImmediately() {
        BaseService base;
        RegistrationSlot<BaseService> baseSlot{context->registerObject(&base)};
        QVERIFY(baseSlot());
        QVERIFY(context->publish());
        QCOMPARE(baseSlot.invocationCount(), 1);
    }

    void testOptionalDependency() {
        auto reg = context->registerService<DependentService,Dependency<Interface1,Cardinality::OPTIONAL>>();
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QVERIFY(!service->m_dependency);
    }

    void testOptionalDependencyWithAutowire() {
        auto reg = context->registerService<DependentService,Dependency<Interface1,Cardinality::OPTIONAL>>();
        QVERIFY(reg->autowire(&DependentService::setBase));
        QVERIFY(!reg->autowire(&DependentService::setBase)); //Should report false on the second time.
        RegistrationSlot<DependentService> service{reg};
        QVERIFY(context->publish());
        QVERIFY(!service->m_dependency);
        auto baseReg = context->registerService<Service<Interface1,BaseService>>();
        RegistrationSlot<Interface1> baseSlot{baseReg};
        QVERIFY(context->publish());
        QVERIFY(service->m_dependency);
        QCOMPARE(service->m_dependency, baseSlot());
    }

    void testCardinalityNDependencyWithAutowire() {
        auto reg = context->registerService<CardinalityNService,Dependency<Interface1,Cardinality::N>>();
        QVERIFY(reg->autowire(&CardinalityNService::addBase));
        QVERIFY(!reg->autowire(&CardinalityNService::addBase)); //Should report false on the second time.
        RegistrationSlot<CardinalityNService> service{reg};
        QVERIFY(context->publish());
        QCOMPARE(service->my_bases.size(), 0);
        auto baseReg1 = context->registerService<Service<Interface1,BaseService>>();
        RegistrationSlot<Interface1> baseSlot1{baseReg1};
        auto baseReg2 = context->registerService<Service<Interface1,BaseService2>>();
        RegistrationSlot<Interface1> baseSlot2{baseReg2};

        QVERIFY(context->publish());
        QCOMPARE(service->my_bases.size(), 2);
        QVERIFY(service->my_bases.contains(baseSlot1()));
        QVERIFY(service->my_bases.contains(baseSlot2()));
    }


    void testInitMethod() {
        auto baseReg = context->registerService<BaseService>("base", {}, false, "init");
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};
        QVERIFY(baseSlot->wasInitialized());
    }

    void testInitMethodWithContext() {
        auto baseReg = context->registerService<BaseService>("base", {}, false, "initContext");
        QVERIFY(context->publish());

        RegistrationSlot<BaseService> baseSlot{baseReg};
        QCOMPARE(baseSlot->context(), context);
    }



    void testAmbiuousMandatoryDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        BaseService myBase;
        context->registerObject<Interface1>(&myBase, "myBase");
        context->registerService<DependentService,Interface1>();
        QVERIFY(!context->publish());
    }

    void testAmbiuousOptionalDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        BaseService myBase;
        context->registerObject<Interface1>(&myBase, "myBase");
        context->registerService<DependentService,Dependency<Interface1,Cardinality::OPTIONAL>>();
        QVERIFY(!context->publish());
    }


    void testNamedMandatoryDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        auto reg = context->registerService<DependentService>("", service_config{}, Dependency<Interface1>{"myBase"});
        QVERIFY(!context->publish());
        context->registerObject<Interface1>(&base, "myBase");
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        QCOMPARE(service->m_dependency, &base);
    }

    void testNamedOptionalDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        auto depReg = context->registerService<DependentService>("", service_config{}, Dependency<Interface1,Cardinality::OPTIONAL>{"myBase"});
        auto depReg2 = context->registerService<DependentService>("", service_config{}, Dependency<Interface1,Cardinality::OPTIONAL>{"base"});

        QVERIFY(context->publish());
        RegistrationSlot<DependentService> depSlot{depReg};
        QVERIFY(!depSlot->m_dependency);
        RegistrationSlot<DependentService> depSlot2{depReg2};
        QCOMPARE(depSlot2->m_dependency, &base);

    }



    void testPrivateCopyDependency() {
        auto depReg = context->registerService<DependentService,Dependency<BaseService,Cardinality::PRIVATE_COPY>>("dependent");
        auto threeReg = context->registerService<ServiceWithThreeArgs,BaseService,Dependency<DependentService,Cardinality::PRIVATE_COPY>,BaseService2>("three");
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

    void testInvalidPrivateCopyDependency() {
        BaseService base;
        context->registerObject<Interface1>(&base, "base");
        context->registerService<DependentService,Dependency<Interface1,Cardinality::PRIVATE_COPY>>("dependent");
        QVERIFY(!context->publish());
    }

    void testAutoDependency() {
        auto reg = context->registerService<DependentService,BaseService>();
        QVERIFY(reg);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        RegistrationSlot<BaseService> baseSlot{context->getRegistration<BaseService>()};
        QVERIFY(baseSlot());
        QCOMPARE(service->m_dependency, baseSlot());
    }

    void testPrefersExplicitOverAutoDependency() {
        BaseService base;
        auto reg = context->registerService<DependentService,BaseService>();
        QVERIFY(reg);
        context->registerObject(&base);
        QVERIFY(context->publish());
        RegistrationSlot<DependentService> service{reg};
        RegistrationSlot<BaseService> baseSlot{context->getRegistration<BaseService>()};
        QCOMPARE(baseSlot(), &base);
        QCOMPARE(service->m_dependency, &base);
    }







    void testRegisterTwiceDifferentImpl() {
        auto reg = context->registerService<Service<Interface1,BaseService>>();
        QVERIFY(reg);
        //Same Interface, different implementation:
        auto reg2 = context->registerService<Service<Interface1,BaseService2>>();
        QCOMPARE_NE(reg2->unwrap(), reg->unwrap());
    }

    void testRegisterTwiceDifferentName() {
        auto reg = context->registerService<Service<Interface1,BaseService>>("base");
        QVERIFY(reg);
        //Same Interface, same implementation, but different name:
        auto reg4 = context->registerService<Service<Interface1,BaseService>>("alias");
        QCOMPARE(reg4->unwrap(), reg->unwrap());
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
        QCOMPARE_NE(reg4->unwrap(), reg->unwrap());
    }

    void testRegisterSameObjectMultipleTimesWithDifferentNames() {
        BaseService service;
        service.setObjectName("base");
        auto reg = context->registerObject(&service);

        QVERIFY(reg);
        auto reg4 = context->registerObject(&service, "alias");
        QCOMPARE(reg4->unwrap(), reg->unwrap());
        auto reg5 = context->registerObject(&service, "anotherAlias");
        QCOMPARE(reg5->unwrap(), reg->unwrap());
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
        QCOMPARE(reg4->unwrap(), reg->unwrap());

    }

    void testRegisterDifferentObjectsOfSameType() {
        BaseService service1;
        BaseService service2;
        auto reg1 = context->registerObject(&service1);
        auto reg2 = context->registerObject(&service2);
        QVERIFY(reg1);
        QVERIFY(reg2);
        QCOMPARE_NE(reg1->unwrap(), reg2->unwrap());

    }


    void testRegisterTwiceDifferentProperties() {
        auto reg = context->registerService<Service<Interface1,BaseService>>();
        QVERIFY(reg);
        //Same Interface, same implementation, but different properties:
        auto reg3 = context->registerService<Service<Interface1,BaseService>>("", {{"objectName", "tester"}});
        QCOMPARE_NE(reg3, reg);
    }

    void testFailRegisterTwiceSameName() {
        auto reg = context->registerService<Service<Interface1,BaseService>>("base");
        QVERIFY(reg);

        //Everything is different, but the name:
        auto reg5 = context->registerService<DependentService,BaseService>("base");
        QCOMPARE(reg5->unwrap(), reg->unwrap());
    }



    void testFailRegisterTwice() {
        auto reg = context->registerService<Service<Interface1,BaseService>>();
        QVERIFY(reg);

        //Same Interface, same implementation, same properties, same name:
        auto reg5 = context->registerService<Service<Interface1,BaseService>>();
        QCOMPARE(reg5->unwrap(), reg->unwrap());
    }









    void testCardinalityNService() {
        auto reg1 = context->registerService<Service<Interface1,BaseService>>("base1");
        auto reg2 = context->registerService<Service<Interface1,BaseService2>>("base2");
        auto reg = context->registerService<CardinalityNService,Dependency<Interface1,Cardinality::N>>();
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);
        QCOMPARE(service->my_bases.size(), 2);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QCOMPARE(regs->getPublishedObjects().size(), 2);
        QVERIFY(regs->getPublishedServices().contains(service->my_bases[0]));
        QVERIFY(regs->getPublishedServices().contains(service->my_bases[1]));
        QVERIFY(service->my_bases.contains(base1()));
        QVERIFY(service->my_bases.contains(base2()));

    }

    void testCardinalityNServiceWithRequiredName() {
        auto reg1 = context->registerService<Service<Interface1,BaseService>>("base1");
        auto reg2 = context->registerService<Service<Interface1,BaseService2>>("base2");
        auto reg = context->registerService<CardinalityNService>("", service_config{}, Dependency<Interface1,Cardinality::N>{"base2"});
        QVERIFY(context->publish());
        auto regs = context->getRegistration<Interface1>();
        RegistrationSlot<Interface1> base1{reg1};
        RegistrationSlot<Interface1> base2{reg2};
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE_NE(base1, base2);
        QCOMPARE(service->my_bases.size(), 1);

        RegistrationSlot<Interface1> services{regs};
        QCOMPARE(services.invocationCount(), 2);
        QCOMPARE(regs->getPublishedObjects().size(), 2);
        QCOMPARE(service->my_bases[0], services());

    }


    void testPostProcessor() {
        auto processReg = context->registerService<PostProcessor>();
        auto reg1 = context->registerService<Service<Interface1,BaseService>>("base1", {{".store", true}});
        auto reg2 = context->registerService<Service<Interface1,BaseService2>>("base2");
        auto reg = context->registerService<CardinalityNService,Dependency<Interface1,Cardinality::N>>("card", {{".store", true}});
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
        QCOMPARE(regs->getPublishedObjects().size(), 2);
        QCOMPARE(processSlot->processedObjects.size(), 2);
        QVERIFY(processSlot->processedObjects.contains(dynamic_cast<QObject*>(base1())));
        QVERIFY(!processSlot->processedObjects.contains(dynamic_cast<QObject*>(base2())));
        QVERIFY(processSlot->processedObjects.contains(service()));

    }



    void testCardinalityNServiceEmpty() {
        auto reg = context->registerService<CardinalityNService,Dependency<Interface1,Cardinality::N>>();
        QVERIFY(context->publish());
        RegistrationSlot<CardinalityNService> service{reg};
        QCOMPARE(service->my_bases.size(), 0);
    }



    void testUseViaImplType() {
        context->registerService<Service<Interface1,BaseService>>();
        context->registerService<DependentService,BaseService>();
        QVERIFY(context->publish());
    }




    void testRegisterByServiceType() {
        auto reg = context->registerService<Service<Interface1,BaseService>>();
        QVERIFY(reg);
        QCOMPARE(reg->service_type(), typeid(Interface1));
        QVERIFY(context->publish());
    }



    void testMissingDependency() {
        auto reg = context->registerService<DependentService,Interface1>();
        QVERIFY(reg);
        QVERIFY(!context->publish());
    }

    void testCyclicDependency() {
        auto reg1 = context->registerService<BaseService,CyclicDependency>();
        QVERIFY(reg1);



        auto reg2 = context->registerService<CyclicDependency,BaseService>();
        QVERIFY(!reg2);

    }

    void testWorkaroundCyclicDependencyWithBeanRef() {
        auto regBase = context->registerService<BaseService,CyclicDependency>("base");
        QVERIFY(regBase);



        auto regCyclic = context->registerService<CyclicDependency>("cyclic", {{"dependency", "&base"}});
        QVERIFY(regCyclic);

        QVERIFY(context->publish());

        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        RegistrationSlot<BaseService> baseSlot{regBase};

        QVERIFY(cyclicSlot());
        QCOMPARE(cyclicSlot(), baseSlot->dependency());
        QCOMPARE(baseSlot(), cyclicSlot->dependency());

    }

    void testWorkaroundCyclicDependencyWithAutowiring() {
        auto regBase = context->registerService<BaseService,CyclicDependency>("dependency");
        QVERIFY(regBase);



        auto regCyclic = context->registerService<CyclicDependency>("cyclic", {}, true);
        QVERIFY(regCyclic);

        QVERIFY(context->publish());

        RegistrationSlot<CyclicDependency> cyclicSlot{regCyclic};
        RegistrationSlot<BaseService> baseSlot{regBase};

        QVERIFY(cyclicSlot());
        QCOMPARE(cyclicSlot(), baseSlot->dependency());
        QCOMPARE(baseSlot(), cyclicSlot->dependency());

    }





    void testPublishAdditionalServices() {

        bool contextPublished = context->published();
        connect(context, &QApplicationContext::publishedChanged, this, [&contextPublished] (bool p) {contextPublished = p;});
        context->registerService<Service<Interface1,BaseService>>("base");
        RegistrationSlot<Interface1> baseSlot{context->getRegistration<Interface1>()};
        auto regDep = context->registerService<DependentService,Interface1>();
        RegistrationSlot<DependentService> depSlot{regDep};

        QVERIFY(!contextPublished);
        QVERIFY(context->publish());
        QVERIFY(contextPublished);
        QVERIFY(baseSlot());
        QVERIFY(depSlot());
        QCOMPARE(baseSlot.invocationCount(), 1);
        QCOMPARE(baseSlot(), baseSlot());

        auto anotherBaseReg = context->registerService<Service<Interface1,BaseService2>>("anotherBase");
        QVERIFY(!contextPublished);
        RegistrationSlot<Interface1> anotherBaseSlot{anotherBaseReg};
        auto regCard = context->registerService<CardinalityNService,Dependency<Interface1,Cardinality::N>>();
        QVERIFY(!contextPublished);

        RegistrationSlot<CardinalityNService> cardSlot{regCard};
        QVERIFY(context->publish());
        QVERIFY(contextPublished);
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
        baseReg->subscribe(this, published);
        auto base2Reg = context->registerService<BaseService2>("base2");
        base2Reg->subscribe(this, published);
        auto dependent2Reg = context->registerService<DependentServiceLevel2,DependentService>("dependent2");
        dependent2Reg->subscribe(this, published);
        auto dependentReg = context->registerService<DependentService,BaseService>("dependent");
        dependentReg->subscribe(this, published);
        auto threeReg = context->registerService<ServiceWithThreeArgs,BaseService,DependentService,BaseService2>("three");
        threeReg->subscribe(this, published);
        auto fourReg = context->registerService<ServiceWithFourArgs,BaseService,DependentService,BaseService2,ServiceWithThreeArgs>("four");
        fourReg->subscribe(this, published);
        auto fiveReg = context->registerService<ServiceWithFiveArgs,BaseService,DependentService,BaseService2,ServiceWithThreeArgs,ServiceWithFourArgs>("five");
        fiveReg->subscribe(this, published);


        QVERIFY(context->publish());

        RegistrationSlot<BaseService> base{baseReg};
        RegistrationSlot<BaseService2> base2{base2Reg};
        RegistrationSlot<DependentService> dependent{dependentReg};
        RegistrationSlot<DependentServiceLevel2> dependent2{dependent2Reg};
        RegistrationSlot<ServiceWithThreeArgs> three{threeReg};
        RegistrationSlot<ServiceWithFourArgs> four{fourReg};
        RegistrationSlot<ServiceWithFiveArgs> five{fiveReg};


        QCOMPARE(publishedInOrder.size(), 7);

        //We cannot say anything about the initialization-order of the 3 Services that have no dependencies:
        //BaseService, BaseService2, and IConfiguration.
        //However, what we can say is:
        //1. DependentService must be initialized after both BaseService.
        //2. DependentService must be initialized before DependentServiceLevel2.
        //3. ServiceWithThreeArgs must be initialized after BaseService, BaseService2 and DependentService
        QVERIFY(publishedInOrder.indexOf(dependent()) < publishedInOrder.indexOf(dependent2()));
        QVERIFY(publishedInOrder.indexOf(base()) < publishedInOrder.indexOf(three()));
        QVERIFY(publishedInOrder.indexOf(dependent()) < publishedInOrder.indexOf(three()));
        QVERIFY(publishedInOrder.indexOf(base2()) < publishedInOrder.indexOf(three()));
        QVERIFY(publishedInOrder.indexOf(three()) < publishedInOrder.indexOf(four()));
        QVERIFY(publishedInOrder.indexOf(four()) < publishedInOrder.indexOf(five()));
        delete context;
        context = nullptr;

        QCOMPARE(destroyedInOrder.size(), 7);

        //We cannot say anything about the destruction-order of the 3 Services that have no dependencies:
        //BaseService, BaseService2, and IConfiguration.
        //However, what we can say is:
        //1. DependentService must be destroyed before both BaseService.
        //2. DependentService must be destroyed after DependentServiceLevel2.
        //3. ServiceWithThreeArgs must be destroyed before BaseService, BaseService2 and DependentService

        QVERIFY(destroyedInOrder.indexOf(dependent()) > destroyedInOrder.indexOf(dependent2()));
        QVERIFY(destroyedInOrder.indexOf(base()) > destroyedInOrder.indexOf(three()));
        QVERIFY(destroyedInOrder.indexOf(dependent()) > destroyedInOrder.indexOf(three()));
        QVERIFY(destroyedInOrder.indexOf(base2()) > destroyedInOrder.indexOf(three()));
        QVERIFY(destroyedInOrder.indexOf(three()) > destroyedInOrder.indexOf(four()));
        QVERIFY(destroyedInOrder.indexOf(four()) > destroyedInOrder.indexOf(five()));
    }

private:
    QApplicationContext* context;
    QTemporaryFile* settingsFile;
    QSettings* config;
};

} //mcnepp::qtdi

#include "testqapplicationcontext.moc"


QTEST_MAIN(mcnepp::qtdi::ApplicationContextTest)
