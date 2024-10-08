#include "appcontexttestclasses.h"

namespace mcnepp::qtditest {

Q_LOGGING_CATEGORY(testLogging, "qtditest")

BaseService::BaseService(QObject* parent)
    : BaseService{nullptr, parent}
{

}

BaseService::BaseService(CyclicDependency* dependency, QObject *parent)
    : QObject{parent},
      m_dependency(dependency),
      m_timer(nullptr),
     m_appContext(nullptr),
    initCalled(0),
    m_foo("BaseService"),
    m_InitialParent(parent)
{

}

QTimer *BaseService::timer() const
{
    return m_timer;
}

void BaseService::setTimer(QTimer *newTimer)
{
    if (m_timer == newTimer)
        return;
    m_timer = newTimer;
    emit timerChanged(newTimer);
}


CyclicDependency *BaseService::dependency() const
{
    return m_dependency;
}


BaseService *CyclicDependency::dependency() const
{
    return m_dependency;
}

void CyclicDependency::setDependency(BaseService *newDependency)
{
    if (m_dependency == newDependency)
        return;
    m_dependency = newDependency;
    emit dependencyChanged();
}

void BaseService2::setReference(BaseService2 *ref)
{
    if (m_reference == ref)
        return;
    m_reference = ref;
    emit referenceChanged();
}

BaseService2* BaseService2::reference() const {
    return m_reference;
}

Address DependentService::address() const
{
    return m_address;
}

void DependentService::setAddress(const Address &newAddress)
{
    if (m_address == newAddress)
        return;
    m_address = newAddress;
}


}
