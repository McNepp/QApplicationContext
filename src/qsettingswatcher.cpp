#include "qsettingswatcher.h"
#include "qconfigurationwatcherimpl.h"

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
        if(auto watcher = dynamic_cast<QConfigurationWatcherImpl*>(watched.get())) {
            watcher->checkChange();
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

void QSettingsWatcher::setPropertyValue(const property_descriptor &propertyDescriptor, QObject *target, const QVariant& value) {
    propertyDescriptor.setter(target, value);
    qCInfo(m_context->loggingCategory()).nospace() << "Refreshed property '" << propertyDescriptor.name << "' of " << target << " with value " << value;
}

void QSettingsWatcher::addWatchedProperty(PlaceholderResolver* resolver, q_variant_converter_t variantConverter, const property_descriptor& propertyDescriptor, QObject *target, const service_config& config)
{
    QConfigurationWatcher* watcher = new QConfigurationWatcherImpl{resolver, config, m_context};

    if(variantConverter) {
        connect(watcher, &QConfigurationWatcher::currentValueChanged, this, [this,propertyDescriptor,target,variantConverter](const QVariant& currentValue) {
            setPropertyValue(propertyDescriptor, target, variantConverter(currentValue.toString()));
        });

    } else {
        connect(watcher, &QConfigurationWatcher::currentValueChanged, this, [this,propertyDescriptor,target](const QVariant& currentValue) {
            setPropertyValue(propertyDescriptor, target, currentValue);
        });
    }
    m_watched.push_back(watcher);
    qCInfo(m_context->loggingCategory()).nospace().noquote() << "Watching property '" << propertyDescriptor.name << "' of " << target;
    m_SettingsWatchTimer->start();
}


QConfigurationWatcher *QSettingsWatcher::watchConfigValue(PlaceholderResolver *resolver)
{
    if(!resolver) {
        return nullptr;
    }
    if(!resolver->hasPlaceholders()) {
        qCInfo(m_context->loggingCategory()).noquote().nospace() << "Expression '" << resolver->expression() << "' will not be watched, as it contains no placeholders";
    }

    auto& watcher = m_watchedConfigValues[resolver->expression()];
    if(!watcher) {
        watcher = new QConfigurationWatcherImpl{resolver, service_config{}, m_context};
        m_watched.push_back(watcher);
        qCInfo(m_context->loggingCategory()).noquote().nospace() << "Watching expression '" << resolver->expression() << "'";
    }
    return watcher;
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
