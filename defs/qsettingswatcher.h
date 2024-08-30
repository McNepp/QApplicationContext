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


    void addWatched(PlaceholderResolver* resolver, const QMetaProperty& property, QObject* target, const service_config& config);

    int autoRefreshMillis() const;

    void setAutoRefreshMillis(int newRefreshMillis);

private:
    void add(QSettings* settings);

    void refreshFromSettings(QSettings* settings);


    struct watched_t {
        QPointer<PlaceholderResolver> resolver;
        QMetaProperty property;
        QPointer<QObject> target;
        service_config config;
        QVariant lastValue;
    };

    QApplicationContext* const m_context;
    std::deque<QPointer<QSettings>> m_Settings;
    QTimer* const m_SettingsWatchTimer;
    QFileSystemWatcher* const m_SettingsFileWatcher;
    std::deque<watched_t> m_watched;
};


}
