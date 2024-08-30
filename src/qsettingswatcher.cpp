#include "qsettingswatcher.h"

namespace mcnepp::qtdi::detail {

QSettingsWatcher::QSettingsWatcher(QApplicationContext *parent) : QObject{parent},
    m_context{parent},
    m_SettingsWatchTimer{new QTimer{this}},
    m_SettingsFileWatcher{new QFileSystemWatcher{this}}
{
    m_SettingsWatchTimer->setInterval(DEFAULT_REFRESH_MILLIS);
    connect(m_SettingsWatchTimer, &QTimer::timeout, this, [this] {refreshFromSettings(nullptr); });
    parent->getRegistration<QSettings>().subscribe(this, &QSettingsWatcher::add);
}


void QSettingsWatcher::refreshFromSettings(QSettings *settings)
{
    if(settings) {
        qCInfo(m_context->loggingCategory()).nospace() << "Refreshing QSettings " << settings ->fileName();
        settings->sync();
    } else {
        for(auto setting : m_Settings) {
            if(setting) {
                setting->sync();
            }
        }
    }


    for(auto& watched : m_watched) {
        if(watched.target && watched.resolver) {
            QVariant currentValue = watched.resolver->resolve(m_context, watched.config);
            if(currentValue != watched.lastValue) {
                if(watched.property.write(watched.target, currentValue)) {
                    qCInfo(m_context->loggingCategory()).nospace() << "Refreshed property '" << watched.property.name() << "' of " << watched.target << " with value " << currentValue;
                } else {
                   qCCritical(m_context->loggingCategory()).nospace() << "Could not refresh property " << watched.property.name() << " of " << watched.target << " with value " << currentValue;
                }
                watched.lastValue = currentValue;
            }
        }
    }
}

bool hasFile(QSettings *settings) {
    switch(settings->format()) {
    case QSettings::IniFormat:
        return true;
    case QSettings::NativeFormat:
        return !QSysInfo::productType().contains("windows", Qt::CaseInsensitive);
    default:
        return false;
    }
}

void QSettingsWatcher::add(QSettings *settings) {
    m_Settings.push_back(settings);
    if(hasFile(settings)) {
        m_SettingsFileWatcher->addPath(settings->fileName());
        connect(m_SettingsFileWatcher, &QFileSystemWatcher::fileChanged, this, [this,settings] {refreshFromSettings(settings); });
        qCInfo(m_context->loggingCategory()).nospace() << "Watch QSettings-file " << settings->fileName();
    } else {
        qCInfo(m_context->loggingCategory()).nospace() << "Refresh QSettings " << settings->fileName() << " every " << autoRefreshMillis() << "milliseconds";
    }
}

void QSettingsWatcher::addWatched(PlaceholderResolver* resolver, const QMetaProperty &property, QObject *target, const service_config& config)
{
    m_watched.push_back(watched_t{resolver, property, target, config, resolver->resolve(m_context, config)});
    qCInfo(m_context->loggingCategory()).nospace().noquote() << "Watching property '" << property.name() << "' of " << target;
    m_SettingsWatchTimer->start();
}

int QSettingsWatcher::autoRefreshMillis() const
{
    return m_SettingsWatchTimer->interval();
}

void QSettingsWatcher::setAutoRefreshMillis(int newRefreshMillis)
{
    if(newRefreshMillis == m_SettingsWatchTimer->interval()) {
        return;
    }
    m_SettingsWatchTimer->setInterval(newRefreshMillis);
    emit autoRefreshMillisChanged(newRefreshMillis);
}

}
