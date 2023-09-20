#pragma once
#include "qapplicationcontext.h"
#include <QTimer>
#include <QObject>

namespace mcnepp::qtditest {

using namespace qtdi;


class Interface1 {
public:

    virtual ~Interface1() = default;

    virtual QString foo() const = 0;
};



class CyclicDependency;



class BaseService : public QObject, public Interface1
{
    friend struct mcnepp::qtdi::service_factory<BaseService>;

    Q_OBJECT
public:
    explicit BaseService(QObject *parent = nullptr);


    Q_PROPERTY(QTimer *timer READ timer WRITE setTimer NOTIFY timerChanged)

    virtual QString foo() const override {
        return "BaseService";
    }



    QTimer* m_timer;
    QTimer *timer() const;
    void setTimer(QTimer *newTimer);
    CyclicDependency *dependency() const;

    Q_INVOKABLE void init() {
        initCalled = true;
    }

    Q_INVOKABLE void initContext(QApplicationContext* appContext) {
        m_appContext = appContext;
    }


    bool wasInitialized() const {
        return initCalled;
    }

    QApplicationContext* context() const {
        return m_appContext;
    }

signals:
    void timerChanged();

    void dependencyChanged();

private:


    explicit BaseService(CyclicDependency* dependency, QObject *parent = nullptr);

    CyclicDependency* m_dependency;
    bool initCalled;
    QApplicationContext* m_appContext;
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
public:
    explicit BaseService2(QObject* parent = nullptr) : QObject(parent) {

    }

    virtual QString foo() const override {
        return "BaseService";
    }
};




class DependentService : public QObject {
public:
    explicit DependentService(Interface1* dependency) :
    m_dependency(dependency)
    {

    }
    void setBase(Interface1* base) {
        m_dependency = base;
    }

    Interface1* m_dependency;


};

class CardinalityNService : public QObject {
    Q_OBJECT
public:
    explicit CardinalityNService(const QList<Interface1*>& bases) :
            my_bases(bases) {

    }
    void addBase(Interface1* base) {
        my_bases.push_back(base);
    }

    QList<Interface1*> my_bases;
};



class DependentServiceLevel2 : public QObject {
public:
    explicit DependentServiceLevel2(DependentService*) {

    }
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



} // com::neppert::contexttest

