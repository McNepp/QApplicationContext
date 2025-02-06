#pragma once
#include "qapplicationcontext.h"
#include <QTimer>
#include <QObject>

namespace mcnepp::qtditest {

using namespace qtdi;

///
/// \brief A type for testing properties with types that have no built-in support for QVariant.
///
struct Address {

    QString value;
};


inline bool operator==(const Address& a1,const Address& a2) {
    return a1.value == a2.value;
}

Q_DECLARE_LOGGING_CATEGORY(testLogging)

class Interface1 {
public:

    virtual ~Interface1() = default;

    virtual QString foo() const = 0;

    virtual void setFoo(const QString&) = 0;

    virtual void init() = 0;
};

class TimerAware {
protected:

    ~TimerAware() = default;
public:

    virtual QTimer* timer() const = 0;

};



class CyclicDependency;



class BaseService : public QObject, public Interface1, public TimerAware
{
    friend struct mcnepp::qtdi::service_factory<BaseService>;

    Q_OBJECT
public:
    explicit BaseService(QObject* parent = nullptr);


    Q_PROPERTY(QTimer *timer READ timer WRITE setTimer NOTIFY timerChanged)

    Q_PROPERTY(QString foo READ foo WRITE setFoo NOTIFY fooChanged)

    virtual QString foo() const override {
        return m_foo;
    }

    void setFoo(const QString& foo) override {
        if(foo != m_foo) {
            this->m_foo = foo;
            emit fooChanged(foo);
        }
    }

    void init() override {
        ++initCalled;
    }



    QTimer* m_timer;
    int initCalled;
    QTimer *timer() const override;
    void setTimer(QTimer *newTimer);
    CyclicDependency *dependency() const;
    QObject* const m_InitialParent;



    Q_INVOKABLE void initContext(QApplicationContext* appContext) {
        m_appContext = appContext;
    }



    QApplicationContext* context() const {
        return m_appContext;
    }

signals:
    void timerChanged(QTimer*);

    void dependencyChanged();

    void fooChanged(const QString&);

    void signalWithoutProperty();

private:


    BaseService(CyclicDependency* dependency, QObject *parent);

    CyclicDependency* m_dependency;
    QApplicationContext* m_appContext;
    QString m_foo;
};


class DerivedService : public BaseService {

};



class CyclicDependency : public QObject {

    Q_OBJECT
public:
    explicit CyclicDependency(BaseService* dependency, QObject *parent = nullptr) : QObject(parent),
            m_dependency(dependency) {

    }

    explicit CyclicDependency(QObject *parent = nullptr) : QObject(parent),
            m_dependency(nullptr) {

    }


    Q_PROPERTY(BaseService *dependency READ dependency WRITE setDependency NOTIFY dependencyChanged FINAL)

    BaseService* m_dependency;

    BaseService *dependency() const;
    void setDependency(BaseService *newDependency);

signals:
    void dependencyChanged();
};


class BaseService2 : public QObject, public Interface1 {
    Q_OBJECT
public:
    explicit BaseService2(QObject* parent = nullptr) : QObject(parent),
        m_reference(nullptr)    {

    }

    Q_PROPERTY(BaseService2 *reference READ reference WRITE setReference NOTIFY referenceChanged FINAL)


    virtual QString foo() const override {
        return "BaseService";
    }

    virtual void setFoo(const QString&) override {

    }

    void init() override {
        ++initCalled;
    }

    void setReference(BaseService2* ref);

    BaseService2* reference() const;


    int initCalled = 0;
    BaseService2* m_reference;
signals:
    void referenceChanged();
};


class QObjectService : public QObject {

    Q_OBJECT

    Q_PROPERTY(QObject* dependency READ dependency WRITE setDependency NOTIFY dependencyChanged)
public:

    QObjectService() {

    }

    explicit QObjectService(const QObjectList& dependencies) :
        m_dependencies{dependencies} {

    }

    void setDependency(QObject* dep) {
        if(m_dependencies.empty()) {
            m_dependencies.push_back(dep);
            emit dependencyChanged(dep);
        } else {
            if(m_dependencies[0] != dep) {
                m_dependencies[0] = dep;
                emit dependencyChanged(dep);
            }
        }
    }

    QObject* dependency() const {
        return m_dependencies.empty() ? nullptr : m_dependencies[0];
    }

signals:
    void dependencyChanged(QObject*);

public:
    QObjectList m_dependencies;
};

class DependentService : public QObject {
public:
    explicit DependentService(Interface1* dependency) :
        DependentService(Address{""}, "", dependency)
    {

    }

    DependentService(const Address& address, const QString& url, Interface1* dependency) :
        m_dependency(dependency),
        m_address(address),
        m_url(url) {

    }
    void setBase(Interface1* base) {
        m_dependency = base;
    }

    Interface1* m_dependency;
    Address m_address;
    const QString m_url;


    Address address() const;
    void setAddress(const Address &newAddress);
};

class CardinalityNService : public QObject {
    Q_OBJECT
public:
    explicit CardinalityNService(const QList<Interface1*>& bases = QList<Interface1*>{}) :
            my_bases(bases) {

    }

    void addBase(Interface1* base) {
        my_bases.push_back(base);
    }

    void setBases(const QList<Interface1*>& bases) {
        my_bases = bases;
    }

    QList<Interface1*> my_bases;
};



class DependentServiceLevel2 : public QObject {
public:
    explicit DependentServiceLevel2(DependentService* dep) :
        m_dep{dep}{

    }

    explicit DependentServiceLevel2(CardinalityNService* card) :
        m_card{card}{

    }

    DependentService* const m_dep = nullptr;
    CardinalityNService* const m_card = nullptr;
};



class ServiceWithThreeArgs : public QObject {
public:
    ServiceWithThreeArgs(BaseService* base, DependentService* dep, BaseService2* base2) :
            m_base(base),
            m_dep(dep),
            m_base2(base2){

    }

    BaseService* m_base;
    DependentService* m_dep;
    BaseService2* m_base2;
};

class ServiceWithFourArgs : public QObject {
public:
    ServiceWithFourArgs(BaseService*, DependentService*, BaseService2*, ServiceWithThreeArgs* ) {

    }
};


class ServiceWithFiveArgs : public QObject {
public:
    ServiceWithFiveArgs(BaseService*, DependentService*, BaseService2*, ServiceWithThreeArgs*, ServiceWithFourArgs*) {

    }
};

class ServiceWithSixArgs : public QObject {
public:
    ServiceWithSixArgs(const QString&, BaseService2*, const QList<ServiceWithFiveArgs*>&, ServiceWithThreeArgs*, ServiceWithFourArgs*, double) {

    }
};


}

Q_DECLARE_METATYPE(mcnepp::qtditest::Address)
