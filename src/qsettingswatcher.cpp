#include "qsettingswatcher.h"
#include "qconfigurationwatcherimpl.h"
#include <QFile>
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

void QSettingsWatcher::handleRemovedFile(QSettings *settings) {
    qCInfo(m_context->loggingCategory()).nospace() << "QSettings-file " << settings ->fileName() << " has been deleted.";
    // Check in regular intervals whether the file will re-appear:
    QTimer* checkTimer = new QTimer{};

    connect(checkTimer, &QTimer::timeout, this, [checkTimer,settings,this] {
        if(QFile::exists(settings->fileName())) {
            checkTimer->stop();
            checkTimer->deleteLater();
            qCInfo(m_context->loggingCategory()).nospace() << "QSettings-file " << settings ->fileName() << " has been restored.";
            // The file is back! Re-add it to the QFileSystemWatcher and then immediately refresh the settings:
            m_SettingsFileWatcher->addPath(settings->fileName());
            refreshFromSettings(settings);
        }
    });
    checkTimer->start(200);

}

void QSettingsWatcher::refreshFromSettings(QSettings *settings)
{
    if(settings) {
        if(!QFile::exists(settings->fileName())) {
            handleRemovedFile(settings);
            return;
        }
        qCInfo(m_context->loggingCategory()).nospace() << "Refreshing QSettings " << settings ->fileName();
        settings->sync();
    } else {
        qCInfo(m_context->loggingCategory()) << "Refreshing all QSettings";
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
    qCInfo(m_context->loggingCategory()).nospace().noquote() << "Refreshed property '" << propertyDescriptor.name << "' of " << target << " with value " << value;
}

void QSettingsWatcher::addWatchedProperty(PlaceholderResolver* resolver, q_variant_converter_t variantConverter, const property_descriptor& propertyDescriptor, QObject *target, const QString& group, QVariantMap& additionalProperties)
{
    QConfigurationWatcher* watcher = new QConfigurationWatcherImpl{resolver, group, additionalProperties, m_context};

    if(variantConverter) {
        connect(watcher, &QConfigurationWatcher::currentValueChanged, this, [this,propertyDescriptor,target,variantConverter](const QVariant& currentValue) {
            setPropertyValue(propertyDescriptor, target, variantConverter(currentValue.toString()));
        });

    } else {
        connect(watcher, &QConfigurationWatcher::currentValueChanged, this, [this,propertyDescriptor,target](const QVariant& currentValue) {
            setPropertyValue(propertyDescriptor, target, currentValue);
        });
    }
   connect(watcher, &QConfigurationWatcher::errorOccurred, this, [this,watcher,name = propertyDescriptor.name] {
        qCWarning(m_context->loggingCategory()).nospace().noquote() << "Watched property '" << name << "' could not be resolved and maintains previous value " << watcher->currentValue();
    });
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
        watcher = new QConfigurationWatcherImpl{resolver, {}, m_resolvedProperties, m_context};
        m_watched.push_back(watcher);
        qCInfo(m_context->loggingCategory()).noquote().nospace() << "Watching expression '" << resolver->expression() << "'";
    }
    m_SettingsWatchTimer->start();
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
