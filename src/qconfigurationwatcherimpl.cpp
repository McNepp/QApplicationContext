#include "qconfigurationwatcherimpl.h"

namespace mcnepp::qtdi::detail {

QVariant QConfigurationWatcherImpl::currentValue() const
{
    return m_lastValue;
}

QConfigurationWatcherImpl::QConfigurationWatcherImpl(PlaceholderResolver *resolver, const service_config& config, QApplicationContext *parent) :
    QConfigurationWatcher{parent},
    m_resolver{resolver},
    m_context{parent},
    m_config{config}
{
    m_lastValue = m_resolver->resolve(parent, config);
    if(!m_lastValue.isValid()) {
        emit errorOccurred();
    }
}

void QConfigurationWatcherImpl::checkChange()
{
    QVariant currentVal = m_resolver->resolve(m_context, m_config);
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
