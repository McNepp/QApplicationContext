#pragma once
/** @file qapplicationcontext.h
 *  @brief Contains the class QApplicationContext and other related types and functions.
 *  @author McNepp
*/

#include <utility>
#include <typeindex>
#include <unordered_set>
#include <QObject>
#include <QVariant>
#include <QPointer>
#include <QUuid>
#include <QLoggingCategory>

namespace mcnepp::qtdi {

class QApplicationContext;


/**
* \brief Specifies the kind of a service-dependency.
* Will be used as a non-type argument to Dependency, when registering a service.
* The following table sums up the characteristics of each type of dependency:
* <table><tr><th>&nbsp;</th><th>Normal behaviour</th><th>What if no dependency can be found?</th><th>What if more than one dependency can be found?</th></tr>
* <tr><td>MANDATORY</td><td>Injects one dependency into the dependent service.</td><td>Publication of the ApplicationContext will fail.</td>
* <td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* <tr><td>OPTIONAL</td><td>Injects one dependency into the dependent service</td><td>Injects `nullptr` into the dependent service.</td>
* td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* <tr><td>N</td><td>Injects all dependencies of the dependency-type that have been registered into the dependent service, using a `QList`</td>
* <td>Injects an empty `QList` into the dependent service.</td>
* <td>See 'Normal behaviour'</td></tr>
* <tr><td>PRIVATE_COPY</td><td>Injects a newly created instance of the dependency-type and sets its `QObject::parent()` to the dependent service.</td>
* <td>Publication of the ApplicationContext will fail.</td>
* <td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* </table>
*/
enum class Kind {
    ///
    /// This dependency must be present in the ApplicationContext.
    MANDATORY,
    /// This dependency need not be present in the ApplicationContext.
    /// If not, `nullptr` will be provided.
    OPTIONAL,
    ///
    /// All Objects with the required service-type will be pushed into QList
    /// and provided to the constructor of the service that depends on them.
    N,
    ///
    /// This dependency must be present in the ApplicationContext.
    /// A copy will be made and provided to the constructor of the service that depends on them.
    /// This copy will not be published in the ApplicationContext.
    /// After construction, the QObject::parent() of the dependency will
    /// be set to the service that owns it.
    ///
    PRIVATE_COPY
};




template<typename S> class Registration;

template<typename S> class ServiceRegistration;

class Subscription;

Q_DECLARE_LOGGING_CATEGORY(loggingCategory)


namespace detail {

template<typename S,typename V=QObject*> struct could_be_qobject : std::false_type {};

template<typename S> struct could_be_qobject<S,decltype(dynamic_cast<QObject*>(static_cast<S*>(nullptr)))> : std::true_type {};


template <typename T>
QList<T*> convertQList(const QObjectList &list) {
    QList<T *> result;
    for (auto obj : list) {
        if(T* ptr = dynamic_cast<T*>(obj)) {
            result.push_back(ptr);
        }
    }
    return result;
}

template <>
inline QObjectList convertQList<QObject>(const QObjectList &list) {
    return list;
}

template<typename S,Kind kind> struct default_argument_converter {
    S* operator()(const QVariant& arg) const {
        return dynamic_cast<S*>(arg.value<QObject*>());
    }
};


template<typename S> struct default_argument_converter<S,Kind::N> {
    QList<S*> operator()(const QVariant& arg) const {
        return convertQList<S>(arg.value<QObjectList>());
    }
};



class Registration;

///
/// \brief The Subscription created by Registrations.
/// This is an internal Q_OBJECT whose sole purpose it is to provide a signal
/// for publishing the current set of published Services.<br>
/// Internally, a Subscription comprises two QMetaObject::Connections:
/// one outgoing to the Client that wants to be informed about a Service's publication,
/// and one incoming from the associated Registration.
///
class Subscription : public QObject {
    Q_OBJECT

    friend class Registration;

    template<typename S> friend class mcnepp::qtdi::Registration;


public:
    virtual void cancel() {
        QObject::disconnect(out_connection);
        QObject::disconnect(in_connection);
    }

    [[nodiscard]] Registration* registration() const {
        return m_registration;
    }


protected:
    Subscription(Registration* registration, QObject* targetContext, Qt::ConnectionType connectionType);


    virtual void notify(QObject*) = 0;

    /**
     * @brief Subscribes to this Subscription.
     * <br>This function will retrieve the Subscription::registration() and invoke onSubscription(Subscription*)
     * on it.
     * @param subscription
     */
    void subscribe();

signals:

    void objectPublished(QObject*);

private:


    QMetaObject::Connection out_connection;
    QMetaObject::Connection in_connection;
    Registration* const m_registration;
};

template<typename S> constexpr auto getMetaObject(S*) -> decltype(&S::staticMetaObject) {
    return &S::staticMetaObject;
}

inline constexpr std::nullptr_t getMetaObject(void*) {
    return nullptr;
}

template<typename S> constexpr const QMetaObject* getMetaObject() {
    return getMetaObject(static_cast<S*>(nullptr));
}

using q_setter_t = std::function<void(QObject*,QVariant)>;

using q_inject_t = std::function<void(QObject*,QObject*)>;


struct property_descriptor {
    QByteArray name;
    q_setter_t setter;
};

inline QDebug operator << (QDebug out, const property_descriptor& descriptor) {
    if(!descriptor.name.isEmpty()) {
        out.noquote().nospace() << "property '" << descriptor.name << "'";
    }
    return out;
}






///
/// \brief A  type that serves as a "handle" for registrations in a QApplicationContext.
/// This class has a signal objectPublished(QObject*).
/// However, it would be quite unwieldly to use that property directly in order to get hold of the
/// Objects managed by this Registration.
/// Rather, use the class-template `Registration` and its type-safe method `Registration::subscribe()`.
///
class Registration : public QObject {
    Q_OBJECT

    friend class Subscription;

public:

    template<typename S> friend class mcnepp::qtdi::Registration;


    ///
    /// \brief Does this Registration match a type?
    /// \param type the request.
    /// \return `true` if the implementation or the service-interface of this Registration matches
    /// the requested type.
    ///
    [[nodiscard]] virtual bool matches(const std::type_info& type) const = 0;



    ///
    /// \brief Yields the ApplicationContext that this Registration belongs to.
    /// \return the ApplicationContext that this Registration belongs to.
    ///
    [[nodiscard]] virtual QApplicationContext* applicationContext() const = 0;



    friend QDebug operator<<(QDebug out, const Registration& reg) {
        reg.print(out);
        return out;
    }



signals:

    ///
    /// \brief Signals when a service has been published.
    ///
    void objectPublished(QObject*);




protected:


    explicit Registration(QObject* parent = nullptr) : QObject(parent) {

     }

    virtual ~Registration() = default;

     ///
     /// \brief Writes information about this Registration to QDebug.
     /// \param out
     ///
     virtual void print(QDebug out) const = 0;




    /**
     * @brief A Subscription has been connected to this Registration.

     * @param subscription the Subscribtion that was connected.
     */
    virtual void onSubscription(Subscription* subscription) = 0;

    virtual Subscription* createAutowiring(const std::type_info& type, q_inject_t injector, Registration* source) = 0;

    /**
     * @brief Subscribes to a Subscription.
     * <br>This function will retrieve the Subscription::registration() and invoke onSubscription(Subscription*)
     * on it.
     * @param subscription
     * @return the subscription (for convenience)
     */
    static Subscription* subscribe(Subscription* subscription);
};


inline void Subscription::subscribe() {
    registration()->onSubscription(this);
}

inline Subscription* Registration::subscribe(Subscription* subscription) {
    if(subscription) {
        subscription->subscribe();
    }
    return subscription;
}


inline Subscription::Subscription(Registration* registration, QObject* targetContext, Qt::ConnectionType connectionType) :
    QObject(targetContext),
    m_registration(registration)
{
    in_connection = QObject::connect(registration, &Registration::objectPublished, this, &Subscription::objectPublished);
    out_connection = QObject::connect(this, &Subscription::objectPublished, targetContext, [this](QObject* obj) { notify(obj); }, connectionType);
}


class ServiceRegistration : public Registration {
    Q_OBJECT


    template<typename S> friend class mcnepp::qtdi::ServiceRegistration;

public:
    ///
    /// \brief The name of this Registration.
    /// This property will yield the name that was passed to QApplicationContext::registerService(),
    /// or the synthetic name that was assigned by the ApplicationContext.
    ///
    Q_PROPERTY(QString registeredName READ registeredName CONSTANT)

    Q_PROPERTY(QVariantMap registeredProperties READ registeredProperties CONSTANT)

    ///
    /// \brief The name of this Registration.
    /// This property will yield the name that was passed to QApplicationContext::registerService(),
    /// or the synthetic name that was assigned by the ApplicationContext.
    /// \return the name of this Registration.
    ///
    [[nodiscard]] virtual QString registeredName() const = 0;

    /**
     * @brief The properties that were supplied upon registration.
     * @return The properties that were supplied upon registration.
     */
    [[nodiscard]] virtual QVariantMap registeredProperties() const = 0;

protected:


    explicit ServiceRegistration(QObject* parent = nullptr) : Registration(parent) {

    }

    ///
    /// \brief Registers an alias for a Service.
    /// <br>If this function is successful, the Service can be referenced by the new name in addition to the
    /// name it was originally registered with. There can be multiple aliases for a Service.<br>
    /// Aliases must be unique within the ApplicationContext.
    /// \param alias the alias to use.
    /// \param descriptor
    /// \return `true` if the alias could be registered. false, if this alias has already been registered before with a different registration.
    ///
    virtual bool registerAlias(const QString& alias) = 0;

   virtual detail::Subscription* createBindingTo(const char* sourcePropertyName, Registration* target, const detail::property_descriptor& targetProperty) = 0;

};

class ProxyRegistration : public Registration {
    Q_OBJECT


public:

    /**
     * @brief Yields the ServiceRegistrations that this proxy currently knows of.
     * This method returns a snapshot of the ServiceRegistrations that have been currently registered.
     * Should you register more Services that match this service-type, you may need to invoke this method again.
     * @return the ServiceRegistrations that this proxy currently knows of.
     */
    [[nodiscard]] virtual QList<ServiceRegistration*> registeredServices() const = 0;


protected:


    explicit ProxyRegistration(QObject* parent = nullptr) : Registration(parent) {

    }


};


}

///
/// An opaque type that represents the Registration on a low level.
/// <br>Clients should have no need to know any details about this type.
/// The only thing you may do directly with a registration_handle_t is check for validity using an if-expression.
/// In particular, you should not de-reference a handle, as its API might change without notice!
/// What you can do, however, is use one of the free functions matches(registration_handle_t handle),
/// applicationContext(registration_handle_t handle).
///
using registration_handle_t = detail::Registration*;

///
/// \brief Determines whether a handle to a Registration matches a type.
/// \param handle the handle to the Registration.
/// \tparam T the type to query.
/// \return `true` if the handle is valid and matches the type.
///
template<typename T> [[nodiscard]] inline bool matches(registration_handle_t handle) {
    return handle && handle->matches(typeid(T));
}

///
/// \brief Obtains the QApplicationContext from a handle to a Registration.
/// \param handle the handle to the Registration.
/// \return the QApplicationContext if the handle is valid, `nullptr` otherwise.
///
[[nodiscard]] inline QApplicationContext* applicationContext(registration_handle_t handle) {
    return handle ? handle->applicationContext() : nullptr;
}


///
/// An opaque type that represents the ServiceRegistration on a low level.
/// <br>Clients should have no need to know any details about this type.
/// The only thing you may do directly with a service_registration_handle_t is check for validity using an if-expression.
/// In particular, you should not de-reference a handle, as its API might change without notice!
/// What you can do, however, is use one of the free functions registeredName(service_registration_handle_t handle),
/// registeredProperties(service_registration_handle_t).
/// applicationContext(registration_handle_t handle).
///
using service_registration_handle_t = detail::ServiceRegistration*;

/**
 * @brief Narrows a handle to a registration to a handle to a ServiceRegistration.
 * <br>If the handle actually points to a ServiceRegistration, this function will cast it
 * to the more specific type. Otherwise, it will return an invalid handle.
 * @param handle a valid or invalid handle to a registration.
 * @return a handle to the ServiceRegistration that the original handle was pointing to, or an invalid handle if the original handle
 * was not pointing to a ServiceRegistration.
 */
[[nodiscard]] inline service_registration_handle_t asService(registration_handle_t handle) {
    return dynamic_cast<service_registration_handle_t>(handle);
}

///
/// An opaque type that represents the ProxyRegistration on a low level.
/// <br>Clients should have no need to know any details about this type.
/// The only thing you may do directly with a proxy_registration_handle_t is check for validity using an if-expression.
/// In particular, you should not de-reference a handle, as its API might change without notice!
/// What you can do, however, is use the free function registeredServices(proxy_registration_handle_t handle).
/// applicationContext(registration_handle_t handle).
///
using proxy_registration_handle_t = detail::ProxyRegistration*;

/**
 * @brief Narrows a handle to a registration to a handle to a ProxyRegistration.
 * <br>If the handle actually points to a ProxyRegistration, this function will cast it
 * to the more specific type. Otherwise, it will return an invalid handle.
 * @param handle a valid or invalid handle to a registration.
 * @return a handle to the ProxyRegistration that the original handle was pointing to, or an invalid handle if the original handle
 * was not pointing to a ProxyRegistration.
 */
[[nodiscard]] inline proxy_registration_handle_t asProxy(registration_handle_t handle) {
    return dynamic_cast<proxy_registration_handle_t>(handle);
}


///
/// \brief Obtains the registered Services from a handle to a ProxyRegistration.
/// \param handle the handle to the ProxyRegistration.
/// \return the Services that the ProxyRegistration knows of, or an empty List if the handle is not valid.
///
[[nodiscard]] inline QList<service_registration_handle_t> registeredServices(proxy_registration_handle_t handle) {
    return handle ? handle->registeredServices() : QList<service_registration_handle_t>{};
}

///
/// \brief Obtains the registeredName from a handle to a ServiceRegistration.
/// \param handle the handle to the ServiceRegistration.
/// \return the name that this ServiceRegistration was registered with, or an empty String if the handle is not valid.
///
[[nodiscard]] inline QString registeredName(service_registration_handle_t handle) {
    return handle ? handle->registeredName() : QString{};
}

///
/// \brief Obtains the registeredProperties from a handle to a ServiceRegistration.
/// \param handle the handle to the  ServiceRegistration.
/// \return the properties that this ServiceRegistration was registered with, or an empty Map if the handle is not valid.
///
[[nodiscard]] inline QVariantMap registeredProperties(service_registration_handle_t handle) {
    return handle ? handle->registeredProperties() : QVariantMap{};
}



/**
 * @brief An opaque handle to a detail::Subscription.
 * Instances of this class will be returned by Registration::subscribe().<br>
 * The only thing you can do with a Subscription is test for validity and Subscription::cancel().
 */
class Subscription final {
public:

     explicit Subscription(detail::Subscription* subscription = nullptr) :
        m_subscription(subscription) {

    }

    /**
     * @brief Was this Subscription successful?
     * Identical to isValid().
     * @return true if this Subscription was successfully created.
     */
    explicit operator bool() const {
        return isValid();
    }

    /**
     * @brief Was this Subscription successful?
     * @return true if this Subscription was successfully created.
     */
    [[nodiscard]] bool isValid() const {
        return m_subscription;
    }

    /**
     * @brief Yields the underlying detail::Subscription.
     * @return the underlying detail::Subscription.
     */
    [[nodiscard]] detail::Subscription* unwrap() const {
        return m_subscription;
    }

    /**
     * @brief Cancels this Subscription.
     */
    void cancel() {
        if(m_subscription) {
            m_subscription->cancel();
            m_subscription.clear();
        }
    }

    friend void swap(Subscription& reg1, Subscription& reg2);


private:
    QPointer<detail::Subscription> m_subscription;
};

///
/// \brief Tests two Subscriptions for equality.
///
/// Two Subscriptions are deemed equal if the pointers returned by Subscriptions::unwrap() point to the same Subscriptions
/// **and** if they both report `true` via Subscriptions::isValid().
/// \param sub1
/// \param sub2
/// \return `true` if the two Subscriptions are logically equal.
///
inline bool operator==(const Subscription& sub1, const Subscription& sub2) {
    return sub1.unwrap() == sub2.unwrap() && sub1;
}


inline void swap(Subscription& reg1, Subscription& reg2) {
    reg1.m_subscription.swap(reg2.m_subscription);
}


///
/// \brief A type-safe wrapper for a detail::Registration.
/// <br>This is a non-instantiable base-class.
/// As such, it defines the operations common to both ServiceRegistration and ProxyRegistration.
/// <br>Its most important operation is the type-safe function `subscribe()` which should be preferred over directly connecting to the signal `detail::Registration::objectPublished(QObject*)`.
/// <br>
/// A Registration contains a *non-owning pointer* to a detail::Registration whose lifetime is bound
/// to the QApplicationContext. The Registration will become invalid after the corresponding QApplicationContext has been destructed.
///
template<typename S> class Registration {


    template<typename U> friend class Registration;

public:





    ///
    /// \brief Yields the QApplicationContext that manages this Registration.
    /// \return the QApplicationContext that manages this Registration, or `nullptr` if this Registration is invalid.
    ///
    [[nodiscard]] QApplicationContext* applicationContext() const {
        return mcnepp::qtdi::applicationContext(unwrap());
    }


    /**
     * @brief Tests whether this Registration matches a type.
     * <br>This function will yield `true` if this is valid Registration and its underlying registration-handle
     * matches the requested type. This will be the case (at least) if U is either an advertised service-interface
     * or the implementation-type of the service.
     * @return `true` if this Registration matches a type.
     */
    template<typename U> [[nodiscard]] bool matches() const {
        return mcnepp::qtdi::matches<U>(unwrap());
    }





    ///
    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the callable immediately with the current publishedObjects().
    /// \tparam F is assumed to be a Callable that accepts an argument of type `S*`.
    /// \param context the target-context.
    /// \param callable the piece of code to execute.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    /// \return the Subscription.
    ///
    template<typename F> Subscription subscribe(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        if(!registrationHolder || !context) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot subscribe to " << *this;
            return Subscription{};
        }


        auto subscription = new CallableSubscription<F>{unwrap(), callable, context, connectionType};
        return Subscription{detail::Registration::subscribe(subscription)};
      }

    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `objectPublished` signal and propagates new QObjects to the callable.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the setter immediately with the current publishedObjects().
    /// \tparam T the target of the subscription. Must be derived from QObject.
    /// \param target the object on which the setter will be invoked.
    /// \param setter the method that will be invoked.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    /// \return the Subscription.
    ///
    template<typename T,typename R> Subscription subscribe(T* target, R (T::*setter)(S*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        static_assert(std::is_base_of_v<QObject,T>, "Target must be derived from QObject");
        if(!registrationHolder || !setter || !target) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot subscribe to " << *this;
            return Subscription{};
        }
        auto subscription = new SetterSubscription<T,R>{unwrap(), target, setter, connectionType};
        return Subscription{detail::Registration::subscribe(subscription)};
     }



    ///
    /// \brief Yields the wrapped handle to the Registration.
    /// \return the wrapped handle to the Registration, or `nullptr` if this Registration wraps no valid Registration.
    ///
    [[nodiscard]] registration_handle_t unwrap() const {
        return registrationHolder.get();
    }

    ///
    /// \brief Connects a service with another service from the same QApplicationContext.
    /// Whenever a service of the type `<D>` is published, it will be injected into every service
    /// of type `<S>`, using the supplied member-function.
    /// <br>For each source-type `D`, you can register at most one autowiring.
    /// \tparam D the type of service that will be injected into Services of type `<S>`.
    /// \param injectionSlot the member-function to invoke when a service of type `<D>` is published.
    /// \return the Subscription created by this autowiring. If an autowiring has already been registered
    /// for the type `D´, an invalid Subscription will be returned.
    ///
    template<typename D,typename R> Subscription autowire(R (S::*injectionSlot)(D*));







    ///
    /// \brief Does this Registration represent a valid %Registration?
    /// \return `true` if the underlying Registration is present.
    ///
    [[nodiscard]] bool isValid() const {
        return unwrap() != nullptr;
    }

    ///
    /// \brief Does this Registration represent a valid %Registration?
    /// Equivalent to isValid().
    /// \return `true` if the underlying Registration is present.
    ///
    explicit operator bool() const {
        return isValid();
    }

    template<typename U> friend void swap(Registration<U>& reg1, Registration<U>& reg2);


protected:
    explicit Registration(registration_handle_t reg = nullptr) : registrationHolder{reg}
    {
    }

    ~Registration() {

    }
private:

    template<typename F> class CallableSubscription : public detail::Subscription {
        friend class Registration;

        explicit CallableSubscription(registration_handle_t registration, F callable, QObject* context, Qt::ConnectionType connectionType) : Subscription(registration, context, connectionType),
            m_callable(callable) {

        }

        virtual void notify(QObject* obj) {
            if(S* srv = dynamic_cast<S*>(obj)) {
                m_callable(srv);
            }
        }
        F m_callable;
    };




    template<typename T,typename R> class SetterSubscription : public detail::Subscription {
        friend class Registration;

        SetterSubscription(registration_handle_t registration, T* target, R (T::*setter)(S*), Qt::ConnectionType connectionType) : Subscription(registration, target, connectionType),
            m_setter(setter),
            m_target(target){
        }

        virtual void notify(QObject* obj) override {
            if(S* ptr = dynamic_cast<S*>(obj)) {
               (m_target->*m_setter)(ptr);
            }
        }

        T* const m_target;
        R (T::*m_setter)(S*);
    };


    QPointer<detail::Registration> registrationHolder;
};


template<typename U> void swap(Registration<U>& reg1, Registration<U>& reg2) {
    reg1.registrationHolder.swap(reg2.registrationHolder);
}


///
/// \brief A type-safe wrapper for a detail::ServiceRegistration.
/// Instances of this class are being produces by the public function-templates QApplicationContext::registerService() and QApplicationContext::registerObject().
///
template<typename S> class ServiceRegistration final : public Registration<S> {
    template<typename F,typename T> friend Subscription bind(const ServiceRegistration<F>&, const char*, Registration<T>&, const char*);

    template<typename F,typename T,typename A,typename R> friend Subscription bind(const ServiceRegistration<F>& source, const char* sourceProperty, Registration<T>& target, R(T::*setter)(A));

public:

    [[nodiscard]] QString registeredName() const {
        return mcnepp::qtdi::registeredName(unwrap());
    }

    [[nodiscard]] QVariantMap registeredProperties() const {
        return mcnepp::qtdi::registeredProperties(unwrap());
    }

    [[nodiscard]] service_registration_handle_t unwrap() const {
        //We can use static_cast here, as the constructor enforces the correct type:
        return static_cast<service_registration_handle_t>(Registration<S>::unwrap());
    }


    /**
     * @brief Attempts to cast this ServiceRegistration to a type.
     * <br>This function will yield a valid Registration only if this is valid Registration and its underlying registration-handle
     * matches the requested type. This will be the case (at least) if U is either an advertised service-interface
     * or the implementation-type of the service.
     * @return a ServiceRegistration of the requested type. May be invalid if this Registration is already invalid, or if
     * the types do not match
     */
    template<typename U> [[nodiscard]] ServiceRegistration<U> as() const {
        if constexpr(std::is_same_v<U,S>) {
            return *this;
        }
        auto handle = unwrap();
        if(!matches<U>(handle)) {
            return ServiceRegistration<U>{};
        }
        return ServiceRegistration<U>::wrap(handle);
    }




    ///
    /// \brief Registers an alias for a Service.
    /// <br>If this function is successful, the Service can be referenced by the new name in addition to the
    /// name it was originally registered with. There can be multiple aliases for a Service.<br>
    /// Aliases must be unique within the ApplicationContext.
    /// \param alias the alias to use.
    /// \return `true` if the alias could be registered. `false` if this alias has already been registered before with a different registration.
    ///
    bool registerAlias(const QString& alias) {
        if(!Registration<S>::isValid()) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register alias '" << alias << "' for " << *this;
            return false;
        }
        return  unwrap()->registerAlias(alias);
    }

    ServiceRegistration() = default;

    ///
    /// \brief Wraps a handle to a ServiceRegistration into a typesafe high-level ServiceRegistration.
    /// \param handle the handle to the ServiceRegistration.
    /// \return a valid Registration if handle is not `nullptr` and if `Registration::matches<S>()`.
    /// \see unwrap().
    ///
    [[nodiscard]] static ServiceRegistration<S> wrap(service_registration_handle_t handle) {
        if(mcnepp::qtdi::matches<S>(handle)) {
            return ServiceRegistration<S>{handle};
        }
        return ServiceRegistration{};
    }




private:
    explicit ServiceRegistration(service_registration_handle_t reg) : Registration<S>{reg} {

    }




    Subscription bind(const char* sourceProperty, registration_handle_t target, const detail::property_descriptor& descriptor) const {
        static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
        if(!target || !*this) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind " << *this << " to " << target;
            return Subscription{};
        }
        auto subscription = unwrap() -> createBindingTo(sourceProperty, target, descriptor);
        return Subscription{detail::Registration::subscribe(subscription)};
     }

};



/**
 * @brief A Registration that manages several ServiceRegistrations of the same type.
 * You can do almost everything with a ProxyRegistration that you can do with a ServiceRegistration,
 * except use it as a source for property-bindings using bind().
 * Instances of this class are produced by QApplicationContext::getRegistration();
 */
template<typename S> class ProxyRegistration final : public Registration<S> {
public:

    ProxyRegistration() = default;

    /**
     * @brief Yields the ServiceRegistrations that this proxy currently knows of.
     * This method returns a snapshot of the ServiceRegistrations that have been currently registered.
     * Should you register more Services that match this service-type, you may need to invoke this method again.
     * @return the ServiceRegistrations that this proxy currently knows of.
     */
    [[nodiscard]] QList<ServiceRegistration<S>> registeredServices() const {
        QList<ServiceRegistration<S>> result;
        for(auto srv : mcnepp::qtdi::registeredServices(unwrap())) {
            result.push_back(ServiceRegistration<S>::wrap(srv));
        }
        return result;
    }

    [[nodiscard]] proxy_registration_handle_t unwrap() const {
        //We can use static_cast here, as the constructor enforces the correct type:
        return static_cast<proxy_registration_handle_t>(Registration<S>::unwrap());
    }

    ///
    /// \brief Wraps a handle to a ProxyRegistration into a typesafe high-level ProxyRegistration.
    /// \param handle the handle to the ProxyRegistration.
    /// \return a valid Registration if handle is not `nullptr` and if `Registration::matches<S>()`.
    /// \see unwrap().
    ///
    [[nodiscard]] static ProxyRegistration<S> wrap(proxy_registration_handle_t handle) {
        if(matches<S>(handle)) {
            return ProxyRegistration<S>{handle};
        }
        return ProxyRegistration{};
    }


private:
    explicit ProxyRegistration(proxy_registration_handle_t reg) : Registration<S>{reg} {

    }
};



///
/// \brief Tests two Registrations for equality.
///
/// Two Registrations are deemed equal if the pointers returned by Registration::unwrap() point to the same Registration
/// **and** if they both report `true` via Registration::isValid().
/// \param reg1
/// \param reg2
/// \return `true` if the two Registrations are logically equal.
///
template<typename S1,typename S2> bool operator==(const Registration<S1>& reg1, const Registration<S2>& reg2) {
    return reg1.unwrap() == reg2.unwrap() && reg1;
}



///
/// \brief Binds a property of one ServiceRegistration to a property from  another Registration.
/// <br>All changes made to the source-property will be propagated to the target-property.
/// For each target-property, there can be only successful call to bind().
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param sourceProperty the name of the Q_PROPERTY in the source.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param targetProperty the name of the Q_PROPERTY in the target.
/// \tparam S the type of the source.
/// \tparam T the type of the target.
/// \return the Subscription established by this binding.
///
template<typename S,typename T> inline Subscription bind(const ServiceRegistration<S>& source, const char* sourceProperty, Registration<T>& target, const char* targetProperty) {
    static_assert(std::is_base_of_v<QObject,T>, "Target must be derived from QObject");
    return source.bind(sourceProperty, target.unwrap(), {targetProperty, nullptr});
}

///
/// \brief Binds a property of one ServiceRegistration to a Setter from  another Registration.
/// <br>All changes made to the source-property will be propagated to all Services represented by the target.
/// For each target-property, there can be only successful call to bind().
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param sourceProperty the name of the Q_PROPERTY in the source.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param setter the method in the target which shall be bound to the source-property.
/// \tparam S the type of the source.
/// \tparam T the type of the target.
/// \return the Subscription established by this binding.
///
template<typename S,typename T,typename A,typename R> inline Subscription bind(const ServiceRegistration<S>& source, const char* sourceProperty, Registration<T>& target, R(T::*setter)(A)) {
    if(!setter) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind " << source << " to null";
        return Subscription{};
    }
    detail::q_setter_t theSetter = [setter](QObject* target,QVariant arg) {
        if(T* srv = dynamic_cast<T*>(target)) {
            (srv->*setter)(arg.value<std::remove_cv_t<std::remove_reference_t<A>>>());
        }
    };
    return source.bind(sourceProperty, target.unwrap(), {"", theSetter });
}



///
/// \brief Tests two Registrations for difference.
///
/// Two Registrations are deemed different if the pointers returned by Registration::unwrap() do not point to the same Registration
/// **or** if they both report `false` via eRegistration::isValid().
/// \param reg1
/// \param reg2
/// \return `true` if the two Registrations are logically different.
///
template<typename S1,typename S2> bool operator!=(const Registration<S1>& reg1, const Registration<S2>& reg2) {
    return reg1.unwrap() != reg2.unwrap() || !reg1;
}

template<typename S> QDebug operator<<(QDebug out, const Registration<S>& reg) {
    if(reg) {
        out <<  *reg.unwrap();
    } else {
        out.noquote().nospace() << "Registration for service-type '" << typeid(S).name() << "' [invalid]";
    }
    return out;
}




///
/// \brief A template that can be specialized to override the standard way of instantiating services.
/// <br>This template can be used to force the QApplicationContext to use a static factory-function instead of a constructor.
/// You may specialize this template for your own component-types.
///
/// If you do so, you must define a call-operator with a pointer to your component as its return-type
/// and as many arguments as are needed to construct an instance.
/// <br>Additionally, the factory should contain a type-declaration `service_type`.
///
/// The specialization must reside in namespace mcnepp::qtdi.
///
/// For example, if you have a service-type `MyService` with an inaccessible constructor for which only a static factory-function `MyService::create()` exists,
/// you may define the corresponding service_factory like this:
///
///     namespace mcnepp::qtdi {
///     template<> struct service_factory<MyService> {
///       using service_type = MyService;
///
///       MyService* operator()() const {
///         return MyService::create();
///       }
///     };
///     }
///
/// Should the service-type `MyService` have a dependency of type `QNetworkAccessManager` that must be supplied to the factory-function,
/// the corresponding service_factory would be defined like this this:
///
///     namespace mcnepp::qtdi {
///     template<> struct service_factory<MyService> {
///       using service_type = MyService;
///
///       MyService* operator()(QNetworkAccessManager* networkManager) const {
///         return MyService::create(networkManager);
///       }
///     };
///     }
/// \tparam S the service-type.
template<typename S> struct service_factory {
    using service_type = S;

    template<typename...Args> S* operator()(Args&&...args) const {
        return new S{std::forward<Args>(args)...};
    }
};






///
/// \brief Specifies a dependency of a service.
/// <br>Can by used as a type-argument for QApplicationContext::registerService().
/// In the standard-case of a mandatory relationship, the use of the `kind` argument is optional.
/// Suppose you have a service-type `Reader` that needs a mandatory pointer to a `DatabaseAccess` in its constructor:
///     class Reader : public QObject {
///       public:
///         explicit Update(DatabaseAccess* dao, QObject* parent = nullptr);
///     };
///
/// Usually, you will not instantiate `Dependency`directly. Rather, you will use one of the functions
/// inject(), injectIfPresent() or injectAll().
///
/// Thus, the following two lines would be completely equivalent:
///
///     context->registerService(service<Reader>(Dependency<DatabaseAccess>{}), "reader");
///
///     context->registerService(service<Reader>(inject<DatabaseAccess>()), "reader");
///
/// However, if your service can do without a `DatabaseAccess`, you should register it like this:
///
///     context->registerService(service<Reader>(injectIfPresent<DatabaseAccess>()), "reader");
///
/// Consider the case where your `Reader` takes a List of `DatabaseAccess` instances:
///
///     class Reader : public QObject {
///       public:
///         explicit Update(const QList<DatabaseAccess>& daos, QObject* parent = nullptr);
///     };
///
/// In that case, it would be registered in an ApplicationContext using the following line:
///
///     context->registerService(service<Reader>(injectAll<DatabaseAccess>()), "reader");
///
/// <b>Note:</b> In many cases, you may already have the ServiceRegistration for the dependency at hand.
/// In that case, you can simply pass that to the Service's constructor, without the need for wrapping it via inject(const ServiceRegistration&):
///
///     auto accessReg = context->registerService<DatabaseAccess>();
///
///     context->registerService(service<Reader>(accessReg), "reader");
///
/// \tparam S the service-interface of the Dependency
/// \tparam kind the kind of Dependency
/// \tparam C the type of the converter. This must be a *default-constructible Callable* class capable of converting
/// a QVariant to the target-type, i.e. it must have an `operator()(const QVariant&)`.
template<typename S,Kind kind=Kind::MANDATORY,typename C=detail::default_argument_converter<S,kind>> struct Dependency {
    static_assert(detail::could_be_qobject<S>::value, "Dependency must be potentially convertible to QObject");
    ///
    /// \brief the required name for this dependency.
    /// The default-value is the empty String, with the implied meaning <em>"any dependency of the correct type may be used"</em>.
    ///
    QString requiredName;

    ///
    /// \brief The (optional) converter. This must be a *default-constructible Callable* class capable of converting
    /// a QVariant to the target-type, i.e. it must have an `operator()(const QVariant&)`.
    ///
    C converter;
};


///
/// \brief Injects a mandatory Dependency.
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \param converter an optional *Callable* object that can convert a QVariant to the target-type.
/// \tparam S the service-type of the dependency.
/// \tparam C the type of an optional *Callable* object that can convert a QVariant to the target-type.
/// \return a mandatory Dependency on the supplied type.
///
template<typename S,typename C=detail::default_argument_converter<S,Kind::MANDATORY>> [[nodiscard]] constexpr Dependency<S,Kind::MANDATORY,C> inject(const QString& requiredName = "", C converter = C{}) {
    return Dependency<S,Kind::MANDATORY,C>{requiredName, converter};
}

///
/// \brief Injects a mandatory Dependency on a Registration.
/// <br><b>Note:</b> If the registration is actually a ServiceRegistration, you do not need
/// to use inject(const Registration<S>& registration) at all! Rather, you can pass the ServiceRegistration
/// directly to the Service's constructor.
/// \param registration the Registration of the dependency.
/// \tparam S the service-type of the dependency.
/// \return a mandatory Dependency on the supplied registration.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,Kind::MANDATORY> inject(const Registration<S>& registration) {
    if(auto srv = asService(registration.unwrap())) {
        return Dependency<S,Kind::MANDATORY>{registeredName(srv)};
    }
    return Dependency<S,Kind::MANDATORY>{};
}





///
/// \brief Injects an optional Dependency to another ServiceRegistration.
/// This function will utilize the Registration::registeredName() to match the dependency.
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \param converter an optional *Callable* object that can convert a QVariant to the target-type.
/// \tparam S the service-type of the dependency.
/// \tparam C the type of an optional *Callable* object that can convert a QVariant to the target-type.
/// \return an optional Dependency on the supplied type.
///
template<typename S,typename C=detail::default_argument_converter<S,Kind::OPTIONAL>> [[nodiscard]] constexpr Dependency<S,Kind::OPTIONAL,C> injectIfPresent(const QString& requiredName = "", C converter = C{}) {
    return Dependency<S,Kind::OPTIONAL,C>{requiredName, converter};
}

///
/// \brief Injects an optional Dependency on a Registration.
/// \param registration the Registration of the dependency.
/// \tparam S the service-type of the dependency.
/// \return an optional Dependency on the supplied registration.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,Kind::OPTIONAL> injectIfPresent(const Registration<S>& registration) {
    if(auto srv = asService(registration.unwrap())) {
        return Dependency<S,Kind::OPTIONAL>{registeredName(srv)};
    }
    return Dependency<S,Kind::OPTIONAL>{};
}



///
/// \brief Injects a 1-to-N Dependency.
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \param converter an optional *Callable* object that can convert a QVariant to the target-type.
/// \tparam S the service-type of the dependency.
/// \tparam C the type of an optional *Callable* object that can convert a QVariant to the target-type.
/// \return a 1-to-N Dependency on the supplied type.
///
template<typename S,typename C=detail::default_argument_converter<S,Kind::N>> [[nodiscard]] constexpr Dependency<S,Kind::N,C> injectAll(const QString& requiredName = "", C converter = C{}) {
    return Dependency<S,Kind::N,C>{requiredName, converter};
}

///
/// \brief Injects a 1-to-N Dependency on a Registration.
/// <br><b>Note:</b> If the registration is actually a ProxyRegistration, you do not need
/// to use inject(const Registration<S>& registration) at all! Rather, you can pass the ProxyRegistration
/// directly to the Service's constructor.
/// \param registration the Registration of the dependency.
/// \tparam S the service-type of the dependency.
/// \return a 1-to-N  Dependency on the supplied registration.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,Kind::N> injectAll(const Registration<S>& registration) {
    if(auto srv = asService(registration.unwrap())) {
        return Dependency<S,Kind::N>{registeredName(srv)};
    }
    return Dependency<S,Kind::N>{};
}


///
/// \brief Injects a Dependency of type Cardinality::PRIVATE_COPY
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \param converter an optional *Callable* object that can convert a QVariant to the target-type.
/// \tparam S the service-type of the dependency.
/// \tparam C the type of an optional *Callable* object that can convert a QVariant to the target-type.
/// \return a Dependency of the supplied type that will create its own private copy.
///
template<typename S,typename C=detail::default_argument_converter<S,Kind::PRIVATE_COPY>> [[nodiscard]] constexpr Dependency<S,Kind::PRIVATE_COPY,C> injectPrivateCopy(const QString& requiredName = "", C converter = C{}) {
    return Dependency<S,Kind::PRIVATE_COPY,C>{requiredName, converter};
}

///
/// \brief A placeholder for a resolvable constructor-argument.
/// Use the function resolve(const QString&) to pass a resolvable argument to a service
/// with QApplicationContext::registerService().
///
template<typename S> struct Resolvable {
    QString expression;
    QVariant defaultValue;
};

///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// Use this function to supply resolvable arguments to the constructor of a Service.
/// The result of resolving the placeholder must be a String that is convertible via `QVariant::value<T>()` to the desired type.
/// ### Example
///
///     auto serviceDecl = service<QIODevice,QFile>(resolve("${filename:readme.txt}"));
///
/// \param expression may contain placeholders in the format `${identifier}` or `${identifier:defaultValue}`.
/// \return a Resolvable instance for the supplied type.
///
template<typename S = QString> [[nodiscard]] Resolvable<S> resolve(const QString& expression) {
    return Resolvable<S>{expression};
}

///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// The result of resolving the placeholder must
/// be a String that is convertible via `QVariant::value<T>()` to the desired type.<br>
/// **Note:** The expression is allowed to specify embedded default-values using the format `${identifier:defaultValue}`.
/// However, this does not make much sense, as it would render the parameter `defaultValue` useless,
/// since the embedded default-value would always take precedence!
/// ### Example
///
///     auto serviceDecl = service<QIODevice,QFile>(resolve("${filename}", QString{"readme.txt"}));
/// \param expression may contain placeholders in the format `${identifier}`.
/// \param defaultValue the value to use if the placeholder cannot be resolved.
/// \return a Resolvable instance for the supplied type.
///
template<typename S> [[nodiscard]] Resolvable<S> resolve(const QString& expression, const S& defaultValue) {
    return Resolvable<S>{expression, QVariant::fromValue(defaultValue)};
}


///
/// \brief Configures a service for an ApplicationContext.
///
struct service_config final {
    using entry_type = std::pair<QString,QVariant>;


    friend inline bool operator==(const service_config& left, const service_config& right) {
        return left.properties == right.properties && left.group == right.group && left.autowire == right.autowire && left.initMethod == right.initMethod;
    }


    QVariantMap properties;
    QString group;
    bool autowire = false;
    QString initMethod;
};

///
/// \brief Makes a service_config.
/// \param properties the keys and value to be applied as Q_PROPERTYs.
/// \param group the `QSettings::group()` to be used.
/// \param autowire if `true`, the QApplicationContext will attempt to initialize all Q_PROPERTYs of `QObject*`-type with the corresponding services.
/// \param initMethod if not empty, will be invoked during publication of the service.
/// \return the service_config.
///
[[nodiscard]] inline service_config make_config(std::initializer_list<std::pair<QString,QVariant>> properties = {}, const QString& group = "", bool autowire = false, const QString& initMethod = "") {
    return service_config{properties, group, autowire, initMethod};
}


namespace detail {



using constructor_t = std::function<QObject*(const QVariantList&)>;

constexpr int VALUE_KIND = 0x10;
constexpr int RESOLVABLE_KIND = 0x20;


template<typename S> struct argument_converter {


    S operator()(const QVariant& arg) const {
        return arg.value<S>();
    }
};

template<typename S,Kind kind,typename C> struct argument_converter<Dependency<S,kind,C>> {

    auto operator()(const QVariant& arg) const {
        return converter(arg);
    }

    C converter;
};



struct dependency_info {
    const std::type_info& type;
    int kind;
    QString expression; //RESOLVABLE_KIND: The resolvable expression. VALUE_KIND: empty. Otherwise: the required name of the dependency.
    QVariant value; //VALUE_KIND: The injected value. RESOLVABLE_KIND: the default-value.

    bool has_required_name() const {
        switch(kind) {
        case VALUE_KIND:
        case RESOLVABLE_KIND:
            return false;
        default:
            return !expression.isEmpty();

        }
    }

};

inline bool operator==(const dependency_info& info1, const dependency_info& info2) {
    if(info1.kind != info2.kind) {
        return false;
    }
    if(info1.type != info2.type) {
        return false;
    }
    switch(info1.kind) {
    case VALUE_KIND:
        return info1.value == info2.value;
        //In all other cases, we use only the expression. (For RESOLVABLE_KIND, value contains the default-value, which we ignore deliberately)
    default:
        return info1.expression == info2.expression;
    }
}


struct service_descriptor {
    using type_set = std::unordered_set<std::type_index>;


    QObject* create(const QVariantList& dependencies) const {
        return constructor ? constructor(dependencies) : nullptr;
    }

    bool matches(const std::type_index& type) const {
        return type == impl_type || service_types.find(type) != service_types.end();
    }

    /**
     * @brief Is this service_descriptor compatible with another one?
     * <br>For this to be true, all of the following criteria must be true:
     * <br>The impl_types must be identical.
     * <br>The set of service_types must either be equal, or
     * one set of service_types must be a true sub-set of the other.
     * <br>The dependencies must match.
     * @param other
     * @return true if the other descriptor matches this one.
     */
    bool matches(const service_descriptor& other) const {
        if(impl_type != other.impl_type || dependencies != other.dependencies) {
            return false;
        }
        //The straight-forward case: both sets are equal.
        if(service_types == other.service_types) {
            return true;
        }
        //Otherwise, if the sets have the same size, one cannot be a sub-set of the other:
        if(service_types.size() == other.service_types.size()) {
            return false;
        }
        const type_set& larger = service_types.size() > other.service_types.size() ? service_types : other.service_types;
        const type_set& smaller = service_types.size() < other.service_types.size() ? service_types : other.service_types;
        for(auto& type : smaller) {
            //If at least one item of the smaller set cannot be found in the larger set
            if(larger.find(type) == larger.end()) {
                return false;
            }
        }
        return true;
    }



    type_set service_types;
    const std::type_info& impl_type;
    const QMetaObject* meta_object = nullptr;
    constructor_t constructor = nullptr;
    std::vector<dependency_info> dependencies;
};



///
/// \brief Determines whether two service_descriptors are deemed equal.
/// two service_descriptors are deemed equal if their service_types, impl_type,
/// dependencies and config are all equal.
/// \param left
/// \param right
/// \return `true` if the service_descriptors are equal to each other.
///
inline bool operator==(const service_descriptor &left, const service_descriptor &right) {
    if (&left == &right) {
        return true;
    }
    return left.service_types == right.service_types &&
           left.impl_type == right.impl_type &&
           left.dependencies == right.dependencies;
 }

inline bool operator!=(const service_descriptor &left, const service_descriptor &right) {
    return !(left == right);
}












template <typename S>
struct dependency_helper {
    using type = S;


    static S convert(const QVariant& arg) {
        return arg.value<S>();
    }


    static dependency_info info(S dep) {
        return { typeid(S), VALUE_KIND, "", QVariant::fromValue(dep) };
    }

    static auto converter(S dep) {
        return &convert;
    }

};

template <typename S,Kind kind,typename C>
struct dependency_helper<Dependency<S,kind,C>> {
    using type = S;


    static dependency_info info(const Dependency<S,kind,C>& dep) {
        return { typeid(S), static_cast<int>(kind), dep.requiredName };
    }

    static C converter(const Dependency<S,kind,C>& dep) {
        return dep.converter;
    }
};

template <typename S>
struct dependency_helper<mcnepp::qtdi::ServiceRegistration<S>> {
    using type = S;



    static dependency_info info(const mcnepp::qtdi::ServiceRegistration<S>& dep) {
        return { typeid(S), static_cast<int>(Kind::MANDATORY), dep.registeredName() };
    }

    static auto converter(const mcnepp::qtdi::ServiceRegistration<S>& dep) {
        return default_argument_converter<S,Kind::MANDATORY>{};
    }

};

template <typename S>
struct dependency_helper<mcnepp::qtdi::ProxyRegistration<S>> {
    using type = S;



    static dependency_info info(const mcnepp::qtdi::ProxyRegistration<S>&) {
        return { typeid(S), static_cast<int>(Kind::N)};
    }

    static auto converter(const mcnepp::qtdi::ProxyRegistration<S>&) {
        return default_argument_converter<S,Kind::N>{};
    }

};


template <typename S>
struct dependency_helper<Resolvable<S>> {

    using type = S;



    static dependency_info info(const Resolvable<S>& dep) {
        return { typeid(S), RESOLVABLE_KIND, dep.expression, dep.defaultValue };
    }

    static auto converter(const Resolvable<S>& resolv) {
        return &dependency_helper<S>::convert;
    }
};








template<typename Head,typename...Tail> constexpr bool check_unique_types() {
    //Check Head against all of Tail:
    if constexpr((std::is_same_v<Head,Tail> || ...)) {
        return false;
    }
    if constexpr(sizeof...(Tail) > 1) {
        //Check Head of remainder against remainder:
        return check_unique_types<Tail...>();
    } else {
        return true;
    }
}




template<typename T,typename First, typename...Tail> constexpr std::pair<bool,const std::type_info&> check_dynamic_types(T* obj) {
    if(!dynamic_cast<First*>(obj)) {
        return {false, typeid(First)};
    }
    if constexpr(sizeof...(Tail) > 0) {
        return check_dynamic_types<T,Tail...>(obj);
    }
    return {true, typeid(void)};
}




template <typename T,typename F> constructor_t service_creator(F factory) {
    return [factory](const QVariantList &dependencies) {
        return factory();
    };
}

template <typename T, typename F, typename D1> constructor_t service_creator(F factory, D1 conv1) {
        return [factory,conv1](const QVariantList &dependencies) {
        return factory(conv1(dependencies[0]));
        };
    }

template <typename T,typename F, typename D1, typename D2>
    constructor_t service_creator(F factory, D1 conv1, D2 conv2) {
        return [factory,conv1,conv2](const QVariantList &dependencies) {
            return factory(conv1(dependencies[0]), conv2(dependencies[1]));
        };
    }

template <typename T,typename F, typename D1, typename D2, typename D3>
    constructor_t service_creator(F factory, D1 conv1, D2 conv2, D3 conv3) {
        return [factory,conv1,conv2,conv3](const QVariantList &dependencies) {
                return factory(
                        conv1(dependencies[0]),
                        conv2(dependencies[1]),
                        conv3(dependencies[2]));
        };
    }

template <typename T,typename F, typename D1, typename D2, typename D3, typename D4>
    constructor_t service_creator(F factory, D1 conv1, D2 conv2, D3 conv3, D4 conv4) {
        return [factory,conv1,conv2,conv3,conv4](const QVariantList &dependencies) {
                return factory(
                        conv1(dependencies[0]),
                        conv2(dependencies[1]),
                        conv3(dependencies[2]),
                        conv4(dependencies[3]));
        };
    }

template <typename T,typename F, typename D1, typename D2, typename D3, typename D4, typename D5>
    constructor_t service_creator(F factory, D1 conv1, D2 conv2, D3 conv3, D4 conv4, D5 conv5) {
        return [factory,conv1,conv2,conv3,conv4,conv5](const QVariantList &dependencies) {
                return factory(conv1(dependencies[0]),
                        conv2(dependencies[1]),
                        conv3(dependencies[2]),
                        conv4(dependencies[3]),
                        conv5(dependencies[4]));
    };
    }

template <typename T,typename F, typename D1, typename D2, typename D3, typename D4, typename D5, typename D6>
constructor_t service_creator(F factory, D1 conv1, D2 conv2, D3 conv3, D4 conv4, D5 conv5, D6 conv6) {
        return [factory,conv1,conv2,conv3,conv4,conv5,conv6](const QVariantList &dependencies) {
                return factory(conv1(dependencies[0]),
                                            conv2(dependencies[1]),
                                            conv3(dependencies[2]),
                                            conv4(dependencies[3]),
                                            conv5(dependencies[4]),
                                            conv6(dependencies[5]));
                };
}


template<typename Srv,typename Impl,typename F,typename...Dep> service_descriptor make_descriptor(F factory, Dep...deps) {
    detail::service_descriptor descriptor{{typeid(Srv)}, typeid(Impl), &Impl::staticMetaObject};
    (descriptor.dependencies.push_back(detail::dependency_helper<Dep>::info(deps)), ...);
    descriptor.constructor = service_creator<Impl>(factory, detail::dependency_helper<Dep>::converter(deps)...);
    return descriptor;
}




} // namespace detail

///
/// \brief Describes a service by its interface and implementation.
/// Compilation will fail if either `Srv` is not a sub-class of QObject, or if `Impl` is not a sub-class of `Srv`.
/// <br><b>Note:</b> This template has a specialization for the case where the implementation-type is identical
/// to the service-type. That specialization offers an additional method for advertising a service with
/// more than one service-interface!<br>
/// constructor of the actual service when the QApplicationContext is published.
/// <br>The preferred way of creating Services is the function mcnepp::qtdi::service().
///
/// Example with one argument:
///
///     context->registerService(service<QIODevice,QFile>(QString{"readme.txt"), "file");
///
/// \tparam Srv the primary service-interface. The service will be advertised as this type. If you only supply this type-argument,
/// the primary service-interface will be identical to the service's implementation-type.
/// \tparam Impl the implementation-type of the service.
/// You may supply arbitrary arguments to Service' constructor. Those arguments will be passed on to the
///
template<typename Srv,typename Impl=Srv> struct Service {
    static_assert(std::is_base_of_v<QObject,Impl>, "Implementation-type must be a subclass of QObject");

    static_assert(std::is_base_of_v<Srv,Impl>, "Implementation-type must be a subclass of Service-type");

    using service_type = Srv;

    using impl_type = Impl;

    template<typename...Dep> [[deprecated("Use function service() instead")]] Service(Dep...deps) : descriptor{detail::make_descriptor<Srv,Impl>(service_factory<Impl>{}, deps...)} {
    }

    explicit Service(detail::service_descriptor&& descr) :
        descriptor{std::move(descr)} {
    }

    detail::service_descriptor descriptor;
};

///
/// \brief Describes a service by its implementation and possibly multiple service-interfaces.
/// Compilation will fail if `Impl` is not a sub-class of QObject.
/// <br>The preferred way of creating Services is the function mcnepp::qtdi::service().
/// <br>You may supply arbitrary arguments to this function. Those arguments will be passed on to the
/// constructor of the actual service when the QApplicationContext is published.
/// Example with one argument:
///
///     context->registerService(service<QFile>(QString{"readme.txt").advertiseAs<QIODevice,QFileDevice>(), "file");
///
/// \tparam Impl the implementation-type of the service. If you do not specify additional service-interfaces,
/// this will become also the primary service-interface.<br>
///
///
template<typename Impl> struct Service<Impl,Impl> {
    static_assert(std::is_base_of_v<QObject,Impl>, "Implementation-type must be a subclass of QObject");


    using service_type = Impl;

    using impl_type = Impl;


    template<typename...Dep> [[deprecated("Use function service() instead")]] Service(Dep...deps) : descriptor{detail::make_descriptor<Impl,Impl>(service_factory<Impl>{}, deps...)} {
    }


    explicit Service(detail::service_descriptor&& descr) :
        descriptor{std::move(descr)} {
    }

    /**
     * @brief Specifies service-interfaces.
     *  <br>You must specify at least one interface (or otherwise compilation will fail). These interfaces will be available for lookup via QApplicationContext::getRegistration().
     *  They will also be used to satisfy dependencies that other services may have to this one.
     *  <br>This function may be invoked only on temporary instances.
     * \tparam IFaces additional service-interfaces to be advertised. <b>At least one must be supplied.</b>
     * <br>If a type appears more than once in the set of types comprising `Impl` and `IFaces`, compilation will fail with a diagnostic.
     * @return this Service.
     */
    template<typename...IFaces> Service<Impl,Impl>&& advertiseAs() && {
        static_assert(sizeof...(IFaces) > 0, "At least one service-interface must be advertised.");
        static_assert((std::is_base_of_v<IFaces,Impl> && ... ), "Implementation-type does not implement all advertised interfaces");
        static_assert(detail::check_unique_types<Impl,IFaces...>(), "All advertised interfaces must be distinct");
        if(auto found = descriptor.service_types.find(descriptor.impl_type); found != descriptor.service_types.end()) {
           descriptor.service_types.erase(found);
        }
        (descriptor.service_types.insert(typeid(IFaces)), ...);
        return std::move(*this);
    }

    /**
     * @brief Specifies service-interfaces.
     *  <br>You must specify at least one interface (or otherwise compilation will fail). These interfaces will be available for lookup via QApplicationContext::getRegistration().
     *  They will also be used to satisfy dependencies that other services may have to this one.
     *  \tparam IFaces the service-interfaces. <b>At least one must be supplied.</b>
     * @return a Service with the advertised interfaces.
     */
    template<typename...IFaces> [[nodiscard]] Service<Impl,Impl> advertiseAs() const& {
        return Service<Impl,Impl>{*this}.advertiseAs<IFaces...>();
    }



    detail::service_descriptor descriptor;
};

///
/// \brief Creates a Service with an explicit factory.
/// \param factory the factory to use. Must be a *Callable* object, i.e. provide an `Impl* operator()` that accepts
/// the arguments derived from the dependencies and yields a pointer to the created Object.
/// <br>The factory-type should contain a type-declaration `service_type` which denotes
/// the type of the service's implementation.
/// \param dependencies the arguments to be injected into the factory.
/// \tparam F the type of the factory.
/// \tparam Impl the implementation-type of the service. If the factory-type F contains
/// a type-declaration `service_type`, Impl will be deduced as that type.
/// \return a Service that will use the provided factory.
template<typename F,typename Impl=typename F::service_type,typename...Dep> [[nodiscard]]Service<Impl,Impl> serviceWithFactory(F factory, Dep...dependencies) {
    return Service<Impl,Impl>{detail::make_descriptor<Impl,Impl>(factory, dependencies...)};
}



///
/// \brief Creates a Service with the default service-factory.
/// \param dependencies the arguments to be injected into the service's constructor.
/// \tparam the primary service-interface.
/// \tparam Impl the implementation-type of the service.
/// \return a Service that will use the provided factory.
template<typename S,typename Impl=S,typename...Dep>  [[nodiscard]]Service<S,Impl> service(Dep...dependencies) {
    return Service<S,Impl>{detail::make_descriptor<S,Impl>(service_factory<Impl>{}, dependencies...)};
}



///
/// \brief A DI-Container for Qt-based applications.
///
class QApplicationContext : public QObject
{
    Q_OBJECT

public:

    ///
    /// \brief How many services have been published?
    /// This property will initially yield `false`, until publish(bool) is invoked.
    /// **Note:** This property will **not** transition back to `0` upon destruction of this ApplicationContext!
    /// \return the number of published services.
    ///
    Q_PROPERTY(unsigned published READ published NOTIFY publishedChanged)

    ///
    /// \brief How many services have been registered and not yet published?
    /// This property will initially yield `0`.
    /// Then, it will increase with every successfull registerService() invocation.
    /// If publish(bool) is invoked successfully, the property will again yield `0`.
    /// **Note:** This property will **not** transition back to `0` upon destruction of this ApplicationContext!
    /// \return the number of registered but not yet published services.
    ///
    Q_PROPERTY(unsigned pendingPublication READ pendingPublication NOTIFY pendingPublicationChanged)



    ///
    /// \brief everything needed to describe service.
    ///
    using service_descriptor = detail::service_descriptor;

    ///
    /// \brief describes a service-dependency.
    ///
    using dependency_info = detail::dependency_info;




    ///
    /// \brief Registers a service with this ApplicationContext.
    /// \param serviceDeclaration comprises the services's primary advertised interface, its implementation-type and its dependencies to be injected
    /// via its constructor.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type. Constitutes the Service's primary advertised interface.
    /// \tparam Impl the implementation-type. The Service will be instantiated using this class' constructor.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename Impl> auto registerService(const Service<S,Impl>& serviceDeclaration, const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S> {
        return ServiceRegistration<S>::wrap(registerService(objectName, serviceDeclaration.descriptor, config));
    }


    ///
    /// \brief Registers a service with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(Service<S>{}, objectName, config)`.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> auto registerService(const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S> {
        return registerService(service<S>(), objectName, config);
    }





    ///
    /// \brief Registers an object with this ApplicationContext.
    /// The object will immediately be published.
    /// You can either let the compiler's template-argument deduction figure out the servicetype `<S>` for you,
    /// or you can supply it explicitly, if it differs from the static type of the object.
    /// \param obj must be non-null. Also, must be convertible to QObject.
    /// \param objName the name for this Object in the ApplicationContext.
    /// *Note*: this name will not be set as the QObject::objectName(). It will be the internal name within the ApplicationContext only.
    /// \tparam S the primary service-type for the object.
    /// \tparam IFaces additional service-interfaces to be advertised. If a type appears more than once in the set of types comprising `S` and `IFaces`, compilation will fail with a diagnostic.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename... IFaces> ServiceRegistration<S> registerObject(S* obj, const QString& objName = "") {
        static_assert(detail::could_be_qobject<S>::value, "Object is not potentially convertible to QObject");
        QObject* qObject = dynamic_cast<QObject*>(obj);
        if(!qObject) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << obj << " as '" << objName << "'. Object is no QObject";
            return ServiceRegistration<S>{};
        }
        if constexpr(sizeof...(IFaces) > 0) {
            static_assert(detail::check_unique_types<S,IFaces...>(), "All advertised interfaces must be distinct");
            auto check = detail::check_dynamic_types<S,IFaces...>(obj);
            if(!check.first)            {
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << qObject << " as '" << objName << "'. Object does not implement " << check.second.name();
                return ServiceRegistration<S>{};
            }
        }
        std::unordered_set<std::type_index> ifaces;
        (ifaces.insert(typeid(S)), ..., ifaces.insert(typeid(IFaces)));
        return ServiceRegistration<S>::wrap(registerObject(objName, qObject, service_descriptor{ifaces, typeid(*obj)}));
    }

    ///
    /// \brief Obtains a ServiceRegistration for a service-type and name.
    /// <br>This function will look up Services by the names they were registered with.
    /// Additionally, it will look up any alias that might have been given, using ServiceRegistration::registerAlias(const QString&).
    /// \tparam S the required service-type.
    /// \param name the desired name of the registration.
    /// A valid ServiceRegistration will be returned only if exactly one Service that matches the requested type and name has been registered.
    /// \return a ServiceRegistration for the required type and name. If no single Service with a matching name and service-type could be found,
    /// an invalid ServiceRegistration will be returned.
    /// \sa getRegistrationHandle(const QString&) const.
    ///
    template<typename S> [[nodiscard]] ServiceRegistration<S> getRegistration(const QString& name) const {
        static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");
        return ServiceRegistration<S>::wrap(getRegistrationHandle(name));
    }

    ///
    /// \brief Obtains a ProxyRegistration for a service-type.
    /// <br>In contrast to the ServiceRegistration that is returned by registerService(),
    /// the ProxyRegistration returned by this function is actually a Proxy.<br>
    /// This Proxy manages all Services of the requested type, regardless of whether they have been registered prior
    /// to invoking getRegistration().<br>
    /// This means that if you subscribe to it using Registration::subscribe(), you will be notified
    /// about all published services that match the Service-type.

    /// \tparam S the required service-type.
    /// \return a ProxyRegistration that corresponds to all registration that match the service-type.
    ///
    template<typename S> [[nodiscard]] ProxyRegistration<S> getRegistration() const {
        static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");
        return ProxyRegistration<S>::wrap(getRegistrationHandle(typeid(S), detail::getMetaObject<S>()));
    }

    /**
     * @brief Obtains a List of all Services that have been registered.
     * <br>The element-type of the returned QList is the opaque type service_registration_handle_t.
     * You should not de-reference it, as its API may changed without notice.
     * <br>
     * What you can do, though, is use one of the free functions matches(registration_handle_t),
     * registeredName(service_registration_handle_t), registeredProperties(service_registration_handle_t).
     * <br>Additionally, you may wrap the handles in a type-safe manner, using ServiceRegistration::wrap(service_registration_handle_t).
     * @return a List of all Services that have been registered.
     */
    [[nodiscard]] virtual QList<service_registration_handle_t> getRegistrationHandles() const = 0;

    ///
    /// \brief Obtains a handle to a Registration for a name.
    /// <br>The type of the returned handle is the opaque type service_registration_handle_t.
    /// You should not de-reference it, as its API may changed without notice.
    /// <br>What you can do, though, is use one of the free functions matches(registration_handle_t),
    /// <br>registeredName(service_registration_handle_t), registeredProperties(service_registration_handle_t).
    /// <br>Additionally, you may wrap the handles in a type-safe manner, using ServiceRegistration::wrap(service_registration_handle_t).
    ///
    /// \param name the desired name of the service.
    /// A valid handle to a Registration will be returned only if exactly one Service has been registered that matches
    /// the name.
    /// \return a handle to a Registration for the supplied name, or `nullptr` if no single Service has been registered with the name.
    /// \sa getRegistration(const QString&) const.
    ///
    [[nodiscard]] virtual service_registration_handle_t getRegistrationHandle(const QString& name) const = 0;



    ///
    /// \brief Publishes this ApplicationContext.
    /// This method may be invoked multiple times.
    /// Each time it is invoked, it will attempt to instantiate all yet-unpublished services that have been registered with this ApplicationContext.
    /// \param allowPartial has the default-value `false`, this function will either return all services or no service at all.
    /// If `allowPartial == true`, the function will attempt to publish as many pending services as possible.
    /// Failures that may be fixed by further registrations will be logged with the level QtMsgType::QtWarningMessage.
    /// \return `true` if there are no fatal errors and all services were published.
    ///
    virtual bool publish(bool allowPartial = false) = 0;



    ///
    /// \brief The number of published services.
    /// \return The number of published services.
    ///
    [[nodiscard]] virtual unsigned published() const = 0;

    /// \brief The number of yet unpublished services.
    /// \return the number of services that have been registered but not (yet) published.
    ///
    [[nodiscard]] virtual unsigned pendingPublication() const = 0;



signals:

    ///
    /// \brief Signals that the published() property has changed.
    /// May be emitted upon publish(bool).
    /// **Note:** the signal will not be emitted on destruction of this ApplicationContext!
    ///
    void publishedChanged();

    ///
    /// \brief Signals that the pendingPublication() property has changed.
    /// May be emitted upon registerService(), registerObject() or publish(bool).
    /// **Note:** the signal will not be emitted on destruction of this ApplicationContext!
    ///
    void pendingPublicationChanged();


protected:



    ///
    /// \brief Standard constructor.
    /// \param parent the optional parent of this QApplicationContext.
    ///
    explicit QApplicationContext(QObject* parent = nullptr);


    ///
    /// \brief Registers a service with this QApplicationContext.
    /// \param name the name of the service.
    /// \param descriptor the descriptor of the service.
    /// \param config the configuration of the service.
    /// \return a Registration for the service, or `nullptr` if it could not be registered.
    ///
    virtual service_registration_handle_t registerService(const QString& name, const service_descriptor& descriptor, const service_config& config) = 0;

    ///
    /// \brief Registers an Object with this QApplicationContext.
    /// \param name
    /// \param obj
    /// \param descriptor
    /// \return a Registration for the object, or `nullptr` if it could not be registered.
    ///
    virtual service_registration_handle_t registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) = 0;



    ///
    /// \brief Obtains a Registration for a service_type.
    /// \param service_type the service-type to match the registrations.
    /// \param metaObject the static QMetaObject for the type. If not available, `nullptr` can be passed.
    /// \return a Registration for the supplied service_type.
    ///
    virtual proxy_registration_handle_t getRegistrationHandle(const std::type_info& service_type, const QMetaObject* metaObject) const = 0;


    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// If you are implementing registerService(const QString&, service_descriptor*) and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    ///
    /// \param appContext the target on which to invoke registerService(const QString&, service_descriptor*).
    /// \param name
    /// \param descriptor
    /// \param config
    /// \return the result of registerService(const QString&, service_descriptor*).
    ///
    static service_registration_handle_t delegateRegisterService(QApplicationContext& appContext, const QString& name, const service_descriptor& descriptor, const service_config& config) {
        return appContext.registerService(name, descriptor, config);
    }

    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// If you are implementing registerObject(const QString& name, QObject*, service_descriptor*) and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    ///
    /// \param appContext the target on which to invoke registerObject(const QString& name, QObject*, service_descriptor*).
    /// \param name
    /// \param obj
    /// \param descriptor
    /// \return the result of registerObject<S>(const QString& name, QObject*, service_descriptor*).
    ///
    static service_registration_handle_t delegateRegisterObject(QApplicationContext& appContext, const QString& name, QObject* obj, const service_descriptor& descriptor) {
        return appContext.registerObject(name, obj, descriptor);
    }


    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// If you are implementing getRegistrationHandle(const std::type_info&,const QMetaObject*) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    ///
    /// \param appContext the target on which to invoke getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    /// \param service_type
    /// \param metaObject the QMetaObject of the service_type. May be omitted.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    static proxy_registration_handle_t delegateGetRegistrationHandle(const QApplicationContext& appContext, const std::type_info& service_type, const QMetaObject* metaObject) {
        return appContext.getRegistrationHandle(service_type, metaObject);
    }



    template<typename S> friend class ServiceRegistration;
};

template<typename S> template<typename D,typename R> Subscription Registration<S>::autowire(R (S::*injectionSlot)(D*)) {
    static_assert(detail::could_be_qobject<D>::value, "Service-type to be injected must be possibly convertible to QObject");
    if(!registrationHolder || !injectionSlot) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot autowire " << *this;
        return Subscription{};
    }
    detail::q_inject_t injector = [injectionSlot](QObject* target,QObject* source) {
        if(S* targetSrv = dynamic_cast<S*>(target)) {
            if(D* sourceSrv = dynamic_cast<D*>(source)) {
                (targetSrv->*injectionSlot)(sourceSrv);
            }
        }
    };
    auto subscription = unwrap()->createAutowiring(typeid(D), injector, applicationContext()->template getRegistration<D>().unwrap());
    return Subscription{detail::Registration::subscribe(subscription)};
}




///
/// \brief A mix-in interface for classes that may modify services before publication.
/// The method process(QApplicationContext*, QObject*,const QVariantMap&) will be invoked for each service after its properties have been set, but
/// before an *init-method* is invoked.
///
class QApplicationContextPostProcessor {
public:
    ///
    /// \brief Processes each service published by an ApplicationContext.
    /// \param appContext
    /// \param service
    /// \param resolvedProperties
    ///
    virtual void process(QApplicationContext* appContext, QObject* service, const QVariantMap& resolvedProperties) = 0;

    virtual ~QApplicationContextPostProcessor() = default;
};




}

namespace std {
    template<> struct hash<mcnepp::qtdi::Subscription> {
        size_t operator()(const mcnepp::qtdi::Subscription& sub, size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        std::hash<mcnepp::qtdi::detail::Subscription*> hasher;
    };

    template<typename S> struct hash<mcnepp::qtdi::ServiceRegistration<S>> {
        size_t operator()(const mcnepp::qtdi::ServiceRegistration<S>& sub, size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        std::hash<mcnepp::qtdi::detail::ServiceRegistration*> hasher;
    };


    template<typename S> struct hash<mcnepp::qtdi::ProxyRegistration<S>> {
        size_t operator()(const mcnepp::qtdi::ProxyRegistration<S>& sub, size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        std::hash<mcnepp::qtdi::detail::ProxyRegistration*> hasher;
    };

}
