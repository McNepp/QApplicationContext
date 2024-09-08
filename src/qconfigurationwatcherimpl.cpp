#include "qconfigurationwatcherimpl.h"

namespace mcnepp::qtdi::detail {

QVariant QConfigurationWatcherImpl::currentValue() const
{
    return m_resolver->resolve(m_context, m_config);
}

QConfigurationWatcherImpl::QConfigurationWatcherImpl(PlaceholderResolver *resolver, const service_config& config, QApplicationContext *parent) :
    QConfigurationWatcher{parent},
    m_resolver{resolver},
    m_context{parent},
    m_config{config}
{
    m_lastValue = m_resolver->resolve(parent, config);
}

void QConfigurationWatcherImpl::checkChange()
{
    QVariant currentVal = currentValue();
    if(currentVal != m_lastValue) {
        emit currentValueChanged(currentVal);
        m_lastValue = currentVal;
    }
}

}
