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
    BaseService();


    Q_PROPERTY(QTimer *timer READ timer WRITE setTimer NOTIFY timerChanged)

    Q_PROPERTY(QString foo READ foo WRITE setFoo NOTIFY fooChanged)

    virtual QString foo() const override {
        return m_foo;
    }

    void setFoo(const QString& foo) override {
        if(foo != m_foo) {
            this->m_foo = foo;
            emit fooChanged();
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



    Q_INVOKABLE void initContext(QApplicationContext* appContext) {
        m_appContext = appContext;
    }



    QApplicationContext* context() const {
        return m_appContext;
    }

signals:
    void timerChanged();

    void dependencyChanged();

    void fooChanged();

private:


    explicit BaseService(CyclicDependency* dependency, QObject *parent = nullptr);

    CyclicDependency* m_dependency;
    QApplicationContext* m_appContext;
    QString m_foo;
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




class DependentService : public QObject {
public:
    explicit DependentService(Interface1* dependency) :
        DependentService(0, "", dependency)
    {

    }

    DependentService(int id, const QString& url, Interface1* dependency) :
        m_dependency(dependency),
        m_id(id),
        m_url(url) {

    }
    void setBase(Interface1* base) {
        m_dependency = base;
    }

    Interface1* m_dependency;
    const int m_id;
    const QString m_url;


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
    ServiceWithFourArgs(BaseService&, DependentService&, BaseService2&, ServiceWithThreeArgs& ) {

    }
};


class ServiceWithFiveArgs : public QObject {
public:
    ServiceWithFiveArgs(BaseService*, DependentService*, BaseService2*, ServiceWithThreeArgs*, ServiceWithFourArgs*) {

    }
};

class ServiceWithSixArgs : public QObject {
public:
    ServiceWithSixArgs(const QString&, BaseService2*, const std::vector<ServiceWithFiveArgs*>&, ServiceWithThreeArgs*, ServiceWithFourArgs*, double) {

    }
};


} // com::neppert::contexttest

