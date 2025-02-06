#pragma once

#include "qapplicationcontext.h"
#include <QCoreApplication>


namespace mcnepp::qtdi {

///
/// \brief Extensible Implementation of QApplicationContext.
/// <br>This class provides a means of implementating the interface QApplicationContext with the potential for
/// additional functionality.
/// <br>The "canonical way" of instantiating QApplicationContext is by means of the class
/// StandardApplicationContext.
/// <br>However, should you want to provide a customized implementation, or should you want to implement
/// additional interfaces, ApplicationContextImplBase may be put to use.
/// \tparam C the base-interface that this class shall extend. A compiler-time error will be generated if C does not extend QApplicationContext.
///
template<typename C> class ApplicationContextImplBase : public C {

    static_assert(std::is_base_of_v<QApplicationContext,C>,"Implemented type must extend QApplicationContext");



public:

    QVariant getConfigurationValue(const QString &key, bool searchParentSections = false) const override {
        return m_delegate->getConfigurationValue(key, searchParentSections);
    }
    QVariant resolveConfigValue(const QString &expression) override {
        return m_delegate->resolveConfigValue(expression);
    }
    bool publish(bool allowPartial = false) override {
        return m_delegate->publish(allowPartial);
    }
    unsigned int published() const override {
        return m_delegate->published();
    }
    unsigned int pendingPublication() const override {
        return m_delegate->pendingPublication();
    }
    QConfigurationWatcher *watchConfigValue(const QString &expression) override {
        return m_delegate->watchConfigValue(expression);
    }
    bool autoRefreshEnabled() const override {
        return m_delegate->autoRefreshEnabled();
    }
    const QLoggingCategory &loggingCategory() const override {
        return m_delegate->loggingCategory();
    }

protected:

    ///
    /// An alias for this type.
    /// May be used by derived classes when overrding methods.
    ///
    using base_t = ApplicationContextImplBase;

    ///
    /// \brief Constructor with explicit logging-category.
    /// \param loggingCategory
    /// \param parent
    ///
    explicit ApplicationContextImplBase(const QLoggingCategory& loggingCategory, QObject* parent = nullptr) :
        C{parent},
        m_delegate(newDelegate(loggingCategory, this))    {
        QApplicationContext::delegateConnectSignals(m_delegate, this);
    }

    ///
    /// \brief Standard constructor.
    /// Uses `defaultLoggingCategory()`
    /// \param parent
    ///
    explicit ApplicationContextImplBase(QObject* parent = nullptr) :
        ApplicationContextImplBase(defaultLoggingCategory(), parent) {

    }

    service_registration_handle_t registerServiceHandle(const QString &name, const QApplicationContext::service_descriptor &descriptor, const service_config &config, ServiceScope scope, QObject *baseObject) override {
        return QApplicationContext::delegateRegisterService(m_delegate, name, descriptor, config, scope, baseObject);
    }

    proxy_registration_handle_t getRegistrationHandle(const std::type_info &service_type, const QMetaObject *metaObject) const override {
        return QApplicationContext::delegateGetRegistrationHandle(m_delegate, service_type, metaObject);
    }

    service_registration_handle_t getRegistrationHandle(const QString &name) const override {
        return QApplicationContext::delegateGetRegistrationHandle(m_delegate, name);
    }

    QList<service_registration_handle_t> getRegistrationHandles() const override {
        return QApplicationContext::delegateGetRegistrationHandles(m_delegate);
    }

    ///
    /// \brief Obtains the delegate.
    /// \return the QApplicationContext that this instance uses as an internal delegate.
    ///
    QApplicationContext* delegate() {
        return m_delegate;
    }

    ///
    /// \brief Obtains the delegate.
    /// \return the QApplicationContext that this instance uses as an internal delegate.
    ///
    const QApplicationContext* delegate() const {
        return m_delegate;
    }

    ///
    /// \brief Sets this ApplicationContext as the *global instance*.
    /// <br>Derived classes should invoke this method as the last line of their constructor.
    /// <br>**Note:** This method intentionally *hides* the method from QApplicationContext.
    /// (It cannot override it, as it is not `virtual`. However, it is meant to be invoked from the constructor,
    /// therefore being virtual would not make sense).
    /// \sa instance()
    /// \return `true` if `this` could be set as the *global instance*
    bool setAsGlobalInstance() {
        QApplicationContext::unsetInstance(m_delegate);
        return QApplicationContext::setAsGlobalInstance();
    }

private:
    QApplicationContext* const m_delegate;
};



}
