#include "qapplicationcontext.h"
#include "placeholderresolver.h"

namespace mcnepp::qtdi::detail {
class QConfigurationWatcherImpl : public QConfigurationWatcher {


    // QConfigurationQWatcher interface
public:
    QVariant currentValue() const override;

    QConfigurationWatcherImpl(PlaceholderResolver* resolver, const QString& group, QVariantMap& additionalProperties, QApplicationContext* parent);

    void checkChange();

private:
    PlaceholderResolver* const m_resolver;
    QApplicationContext* m_context;
    QString m_group;
    QVariantMap& m_additionalProperties;
    QVariant m_lastValue;
};
}
