#include "appcontexttestclasses.h"

namespace mcnepp::qtditest {

BaseService::BaseService()
    : BaseService{nullptr, nullptr}
{

}

BaseService::BaseService(CyclicDependency* dependency, QObject *parent)
    : QObject{parent},
      m_dependency(dependency),
      m_timer(nullptr),
     initCalled(false),
     m_appContext(nullptr)
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
    emit timerChanged();
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

}
