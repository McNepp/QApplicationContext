#include "qapplicationcontext.h"
#include "placeholderresolver.h"

namespace mcnepp::qtdi::detail {
class QConfigurationWatcherImpl : public QConfigurationWatcher {


    // QConfigurationQWatcher interface
public:
    QVariant currentValue() const override;

    QConfigurationWatcherImpl(PlaceholderResolver* resolver, const service_config& config, QApplicationContext* parent);

    void checkChange();

private:
    PlaceholderResolver* const m_resolver;
    QApplicationContext* m_context;
    service_config m_config;
    QVariant m_lastValue;
};
}
