#pragma once
#include "qapplicationcontext.h"
#include "placeholderresolver.h"
#include <QSettings>
#include <QTimer>
#include <QFileSystemWatcher>
#include <deque>

namespace mcnepp::qtdi::detail {


class QSettingsWatcher : public QObject {
    Q_OBJECT

    Q_PROPERTY(int autoRefreshMillis READ autoRefreshMillis WRITE setAutoRefreshMillis NOTIFY autoRefreshMillisChanged)

signals:
    void autoRefreshMillisChanged(int);

public:
    static constexpr int DEFAULT_REFRESH_MILLIS = 5000;

    explicit QSettingsWatcher(QApplicationContext* parent);


    void addWatchedProperty(PlaceholderResolver* resolver, q_variant_converter_t variantConverter, const property_descriptor& propertyDescriptor, QObject* target, const service_config& config);

    QConfigurationWatcher* watchConfigValue(PlaceholderResolver* resolver);

    int autoRefreshMillis() const;

    void setAutoRefreshMillis(int newRefreshMillis);

private:
    void add(QSettings* settings);

    void refreshFromSettings(QSettings* settings);

    void setPropertyValue(const property_descriptor &property, QObject *target, const QVariant& value);

    QApplicationContext* const m_context;
    std::deque<QPointer<QSettings>> m_Settings;
    QTimer* const m_SettingsWatchTimer;
    QFileSystemWatcher* const m_SettingsFileWatcher;
    std::deque<QPointer<QConfigurationWatcher>> m_watched;
    std::unordered_map<QString,QPointer<QConfigurationWatcher>> m_watchedConfigValues;
};


}
