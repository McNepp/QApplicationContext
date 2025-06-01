#include "qconfigurationwatcherimpl.h"

namespace mcnepp::qtdi::detail {



QVariant QConfigurationWatcherImpl::currentValue() const
{
    return m_lastValue;
}



QConfigurationWatcherImpl::QConfigurationWatcherImpl(PlaceholderResolver *resolver, const QString& group, QVariantMap& additionalProperties, QApplicationContext *parent) :
    QConfigurationWatcher{parent},
    m_resolver{resolver},
    m_context{parent},
    m_group{group},
    m_additionalProperties{additionalProperties}
{
    m_lastValue = m_resolver->resolve(group, additionalProperties);
    if(!m_lastValue.isValid()) {
        emit errorOccurred();
    }
}

void QConfigurationWatcherImpl::checkChange()
{
    m_resolver->clearPlaceholders(m_additionalProperties);
    QVariant currentVal = m_resolver->resolve(m_group, m_additionalProperties);
    if(!currentVal.isValid()) {
        emit errorOccurred();
        return;
    }

    if(currentVal != m_lastValue) {
        emit currentValueChanged(currentVal);
        m_lastValue = currentVal;
    }
}

}
