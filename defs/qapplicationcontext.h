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
#include <QMetaMethod>
#include <QLoggingCategory>

namespace mcnepp::qtdi {

class QApplicationContext;

namespace detail {
    class Registration;
    class ServiceRegistration;
    class ProxyRegistration;
    class Subscription;
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
/// An opaque type that represents the ServiceRegistration on a low level.
/// <br>Clients should have no need to know any details about this type.
/// The only thing you may do directly with a service_registration_handle_t is check for validity using an if-expression.
/// In particular, you should not de-reference a handle, as its API might change without notice!
/// What you can do, however, is use one of the free functions registeredName(service_registration_handle_t handle),
/// registeredProperties(service_registration_handle_t).
/// applicationContext(registration_handle_t handle).
///
using service_registration_handle_t = detail::ServiceRegistration*;

///
/// An opaque type that represents the ProxyRegistration on a low level.
/// <br>Clients should have no need to know any details about this type.
/// The only thing you may do directly with a proxy_registration_handle_t is check for validity using an if-expression.
/// In particular, you should not de-reference a handle, as its API might change without notice!
/// What you can do, however, is use the free function registeredServices(proxy_registration_handle_t handle).
/// applicationContext(registration_handle_t handle).
///
using proxy_registration_handle_t = detail::ProxyRegistration*;

///
/// An opaque type that represents the Subscription on a low level.
/// <br>Clients should have no need to know any details about this type.
/// The only thing you may do directly with a registration_handle_t is check for validity using an if-expression.
/// In particular, you should not de-reference a handle, as its API might change without notice!
///
using subscription_handle_t = detail::Subscription*;


/**
* \brief Specifies the kind of a service-dependency.
* Will be used as a non-type argument to Dependency, when registering a service.
* The following table sums up the characteristics of each type of dependency:
* <table><tr><th>&nbsp;</th><th>Normal behaviour</th><th>What if no dependency can be found?</th><th>What if more than one dependency can be found?</th></tr>
* <tr><td>MANDATORY</td><td>Injects one dependency into the dependent service.</td><td>Publication of the ApplicationContext will fail.</td>
* <td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* <tr><td>OPTIONAL</td><td>Injects one dependency into the dependent service</td><td>Injects `nullptr` into the dependent service.</td>
* <td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* <tr><td>N</td><td>Injects all dependencies of the dependency-type that have been registered into the dependent service, using a `QList`</td>
* <td>Injects an empty `QList` into the dependent service.</td>
* <td>See 'Normal behaviour'</td></tr>
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
    N

};

/**
 * @brief Specifies the scope of a ServiceRegistration.
 * <br>Serves as a non-type template-argument for ServiceRegistration.
 * <table><tr><th>Scope</th><th>Produced by</th><th>Behaviour</th></tr>
 * <tr><td>UNKNOWN</td><td>QApplicationContext::getRegistrations(), QApplicationContext::getRegistration(const QString&).</td><td>Could be either SINGLETON or PROTOTYPE.</td></tr>
 * <tr><td>SINGLETON</td><td>QApplicationContext::registerService(service()).</td><td>The service will be instantiated on QApplicationContext::publish(bool).<br>A reference to a single instance will be injected into every dependent service.</td></tr>
 * <tr><td>PROTOTYPE</td><td>QApplicationContext::registerService(prototype()).</td><td>Instances of this service will only be created if another service needs it as a dependency.<br>
 * A new instance will be injected into every dependent service.</td></tr>
 * <tr><td>EXTERNAL</td><td>QApplicationContext::registerObject().</td><td>The service has been created externally.</td></tr>
 * <tr><td>TEMPLATE</td><td>QApplicationContext::registerService(serviceTemplate()).</td><td>No Instances will ever by created.<br>
 * The ServiceRegistration can be supplied as an additional parameter when registering other services.</td></tr>
 * </table>
 */
enum class ServiceScope {
    UNKNOWN,
    SINGLETON,
    PROTOTYPE,
    EXTERNAL,
    TEMPLATE
};

template<typename S> class Registration;

template<typename S,ServiceScope> class ServiceRegistration;


Q_DECLARE_LOGGING_CATEGORY(loggingCategory)




namespace detail {


template<ServiceScope scope> struct service_scope_traits {
    static constexpr bool is_binding_source = false;
    static constexpr bool is_constructable = false;
};

template<> struct service_scope_traits<ServiceScope::SINGLETON> {
    static constexpr bool is_binding_source = true;
    static constexpr bool is_constructable = true;
};


template<> struct service_scope_traits<ServiceScope::EXTERNAL> {
    static constexpr bool is_binding_source = true;
    static constexpr bool is_constructable = false;
};

template<> struct service_scope_traits<ServiceScope::PROTOTYPE> {
    static constexpr bool is_binding_source = false;
    static constexpr bool is_constructable = true;
};



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

///
/// \brief The Subscription created by Registrations.
/// This is an internal Q_OBJECT whose sole purpose it is to provide a signal
/// for publishing the current set of published Services.
/// <br>Subscriptions are passed to Registration::onSubscription(subscription_handle_t),
/// where one of two things happens:
/// <br>Either, the signal Subscription::objectPublished(QObject*) is emitted,
/// or the Registration connects its own incoming signal Registration::objectPublished(QObject*) to
/// the Subscription's outgoing signal.
///
class Subscription : public QObject {
    Q_OBJECT

    friend class Registration;

    template<typename S> friend class mcnepp::qtdi::Registration;


public:
    ///
    /// \brief Severs all Connections that this Subscription has made.
    ///
    virtual void cancel() = 0;

    ///
    /// \brief Connects to a Registration.
    /// <br>An implementation should invoke connect(registration_handle_t, subscription_handle_t, Qt::ConnectionType) and
    /// store the QMetaObject::Connection in a field. It should be disconnected in cancel().
    /// \param source the Registration whose signals shall be propagated.
    /// \param target the Subscription that shall re-emit the signal Subscription::objectPublished.
    ///
    virtual void connectTo(registration_handle_t source) = 0;


protected:
    explicit Subscription(QObject* parent = nullptr) : QObject(parent) {

    }

signals:

    void objectPublished(QObject*);
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

using q_init_t = std::function<void(QObject*,QApplicationContext*)>;

struct service_descriptor;





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






template<typename S> struct callable_adapter {

    template<typename A,typename R> static q_setter_t adaptSetter(R (S::*setter)(A)) {
        if(!setter) {
            return nullptr;
        }
        using arg_type = std::remove_cv_t<std::remove_reference_t<A>>;
        return [setter](QObject* obj,QVariant arg) {
            if constexpr(std::is_same_v<S,QObject>) {
                (obj->*setter)(arg.value<arg_type>());
            } else
            if(S* ptr = dynamic_cast<S*>(obj)) {
                (ptr->*setter)(arg.value<arg_type>());
            }
        };
    }

    template<typename F> static auto adapt(F callable) {
        return [callable](QObject* obj) {
            if constexpr(std::is_same_v<S,QObject>) {
                callable(obj);
            } else
            if(S* ptr = dynamic_cast<S*>(obj)) {
                callable(ptr);
            }
        };
    }

    template<typename R,typename T> static auto adapt(T* target, R(T::*memFun)(S*)) {
        return [target,memFun](QObject* obj) {
            if constexpr(std::is_same_v<S,QObject>) {
                (target->*memFun)(obj);
            } else
               if(S* ptr = dynamic_cast<S*>(obj)) {
                (target->*memFun)(ptr);
            }
        };
    }

};




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

    /**
      * @brief the QMetaObject of the Service.
      * <br>For every Registration obtained via QApplicationContext::registerService() or QApplicationContext::registerObject(),
      * this function will yield the QMetaObject belonging to the service's implementation-type.
      * <br>For every Registration obtained via QApplicationContext::getRegistration(), this method may yield `nullptr`, in case
      * that the type-argument specifies a non-QObject interface.
      * @return the QMetaObject of the Service, or `nullptr` if this is a Registration for a non-QObject interface.
      */
    virtual const QMetaObject* serviceMetaObject() const = 0;


    friend QDebug operator<<(QDebug out, const Registration& reg) {
        reg.print(out);
        return out;
    }

    /// Subscribes to a Subscription.
    /// <br>Invokes onSubscription(subscription_handle_t subscription).
    /// \brief subscribe
    /// \param subscription
    /// \return the subscription.
    ///
    subscription_handle_t subscribe(subscription_handle_t subscription) {
        if(subscription) {
            onSubscription(subscription);
        }
        return subscription;
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
     * @brief A Subscription shall be connected to this Registration.
     * <br>The Registration might immediately emit the signal Subscription::objectPublished(QObject*).
     * <br>Alternatively or in addition to that, it might connect itself to the Subscription using Subscription::connectTo(registration_handle_t).
     * @param subscription the Subscribtion that was connected.
     */
    virtual void onSubscription(subscription_handle_t subscription) = 0;

     /**
     * @brief Creates a Subscription for auto-wiring another Service into this.
     * <br>Will create a Subscription and also subscribe to it.
     * @param type the type of service to be injected into this.
     * @param injector the function to invoke for injecting the other service into this.
     * @param source the Registration for the source-service.
     * @return the Subscription, or `nullptr` if it could not be created.
     */
    virtual subscription_handle_t createAutowiring(const std::type_info& type, q_inject_t injector, registration_handle_t source) = 0;

};

///
/// \brief Convenience-function that connects the incoming signal of a Registration with the outgoing signal of a Subscription.
/// \param source
/// \param target
/// \param connectionType
/// \return the Connection.
///
inline QMetaObject::Connection connect(registration_handle_t source, subscription_handle_t target, Qt::ConnectionType connectionType = Qt::AutoConnection) {
    if(source && target) {
        return QObject::connect(source, &Registration::objectPublished, target, &Subscription::objectPublished, connectionType);
    }
    return QMetaObject::Connection{};
}



///
/// \brief A basic implementation of the detail::Subscription.
///
class CallableSubscription : public Subscription {
public:

    explicit CallableSubscription(QObject* parent = nullptr) : Subscription(parent)
    {
        out_connection = QObject::connect(this, &Subscription::objectPublished, this, &CallableSubscription::notify);
    }

    template<typename T,typename F> CallableSubscription(T* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) : Subscription(context)
    {
        static_assert(std::is_base_of_v<QObject,T>, "Context must be derived from QObject");
        out_connection = QObject::connect(this, &Subscription::objectPublished, context, callable, connectionType);
    }


    void cancel() override {
        QObject::disconnect(out_connection);
        QObject::disconnect(in_connection);
    }

    void connectTo(registration_handle_t source) override {
        in_connection = detail::connect(source, this);
    }

protected:

    virtual void notify(QObject*) {

    }

private:
    QMetaObject::Connection out_connection;
    QMetaObject::Connection in_connection;
};




class ServiceRegistration : public Registration {
    Q_OBJECT

    template<typename S,ServiceScope> friend class mcnepp::qtdi::ServiceRegistration;

public:
    ///
    /// \brief The name of this Registration.
    /// This property will yield the name that was passed to QApplicationContext::registerService(),
    /// or the synthetic name that was assigned by the ApplicationContext.
    ///
    Q_PROPERTY(QString registeredName READ registeredName CONSTANT)

    Q_PROPERTY(QVariantMap registeredProperties READ registeredProperties CONSTANT)

    Q_PROPERTY(ServiceScope scope READ scope CONSTANT)

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

    /**
     * @brief The service_descriptor that was used to register this Service.
     * @return The service_descriptor that was used to register this Service.
     */
    [[nodiscard]] virtual const service_descriptor& descriptor() const = 0;

    /**
     * @brief What scope has this ServiceRegistration?
     * <br>If it was obtained by QApplicationContext::registerService(), will be ServiceScope::SINGLETON.
     * <br>If it was obtained by QApplicationContext::registerObject(), will be ServiceScope::EXTERNAL.
     * <br>If it was obtained by QApplicationContext::registerPrototype(), will be ServiceScope::PROTOTYPE.
     * @return the scope of this ServiceRegistration.
     */
    [[nodiscard]] virtual ServiceScope scope() const = 0;


    virtual const QMetaObject* serviceMetaObject() const override;

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

    ///
    /// \brief Creates a binding from a property of this service to a property of a target-service.
    /// <br>The returned Subscription will have been subscribed to already!
    /// \param sourcePropertyName
    /// \param target
    /// \param targetProperty
    /// \return the Subscription for binding this service to a target-service, or `nullptr` if something went wrong.
    ///
    virtual subscription_handle_t createBindingTo(const char* sourcePropertyName, registration_handle_t target, const detail::property_descriptor& targetProperty) = 0;



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
    [[nodiscard]] virtual QList<service_registration_handle_t> registeredServices() const = 0;


    /**
     * @brief The type of service that this ProxyRegistrations stands for.
     * @return The type of service that this ProxyRegistrations stands for.
     */
    virtual const std::type_info& serviceType() const = 0;


protected:


    explicit ProxyRegistration(QObject* parent = nullptr) : Registration(parent) {

    }

};

    QMetaProperty findPropertyBySignal(const QMetaMethod& signalFunction, const QMetaObject* metaObject);


}




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
/// \brief Obtains the QMetaObject from a handle to a Registration.
/// \param handle the handle to the  Registration.
/// \return the QMetaObject that this Registration was registered with, or `nullptr` if this Registration is a proxy for a non-QObject interface.
///
[[nodiscard]] inline const QMetaObject* serviceMetaObject(registration_handle_t handle) {
    return handle ? handle->serviceMetaObject() : nullptr;
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

     explicit Subscription(subscription_handle_t subscription = nullptr) :
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
    [[nodiscard]] subscription_handle_t unwrap() const {
        return m_subscription;
    }

    /**
     * @brief Cancels this Subscription.
     * <br>**Thread-safety:** This function may only be called from the Thread that created this Subscription.
     * (This is not necessarily the QApplicationContext's thread.)
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

    [[nodiscard]] const QMetaObject* serviceMetaObject() const {
        return mcnepp::qtdi::serviceMetaObject(unwrap());
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
    /// <br>**Thread-safety:** This function may be safely called from any thread.
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

        auto subscription = new detail::CallableSubscription{context, detail::callable_adapter<S>::adapt(callable), connectionType};
        return Subscription{unwrap()->subscribe(subscription)};
      }

    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `objectPublished` signal and propagates new QObjects to the callable.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the setter immediately with the current publishedObjects().
    /// <br>**Thread-safety:** This function may be safely called from any thread.
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
        auto subscription = new detail::CallableSubscription{target, detail::callable_adapter<S>::adapt(target, setter), connectionType};
        return Subscription{unwrap()->subscribe(subscription)};
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
    /// <br>**Thread-safety:** This function may only be called from the QApplicationContext's thread.
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
    QPointer<detail::Registration> registrationHolder;
};


template<typename U> void swap(Registration<U>& reg1, Registration<U>& reg2) {
    reg1.registrationHolder.swap(reg2.registrationHolder);
}


///
/// \brief A type-safe wrapper for a detail::ServiceRegistration.
/// \tparam S the service-type.
/// \tparam SCP the ServiceScope. If this ServiceRegistration was obtained via QApplicationContext::registerService(service()),
/// it will have ServiceScope::SINGLETON.
/// <br>If this ServiceRegistration was obtained via QApplicationContext::registerObject(),
/// it will have ServiceScope::EXTERNAL.
/// <br>If this ServiceRegistration was obtained via QApplicationContext::registerService(prototype()), it will have ServiceScope::PROTOTYPE.
/// <br>Otherwise, it will have ServiceScope::UNKNOWN.
/// Instances of this class are being produces by the public function-templates QApplicationContext::registerService() and QApplicationContext::registerObject().
///
template<typename S,ServiceScope SCP=ServiceScope::UNKNOWN> class ServiceRegistration final : public Registration<S> {
    template<typename F,typename T,ServiceScope scope> friend Subscription bind(const ServiceRegistration<F,scope>&, const char*, Registration<T>&, const char*);

    template<typename F,typename T,typename A,typename R,ServiceScope scope> friend Subscription bind(const ServiceRegistration<F,scope>& source, const char* sourceProperty, Registration<T>& target, R(T::*setter)(A));


public:
    using service_type = S;

    static constexpr ServiceScope Scope = SCP;

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
     * <br>Additionally, compilation will only succeed if at least one of the following is true:
     * - the current and new scopes are equal
     * - the current scope is `ServiceScope::UNKNOWN`
     * - the new scope is `ServiceScope::UNKNOWN`
     *
     * \tparam U the new service-type.
     * \tparam newScope the new scope for the ServiceRegistration.
     * @return a ServiceRegistration of the requested type. May be invalid if this Registration is already invalid, or if
     * the types do not match
     */
    template<typename U,ServiceScope newScope=SCP> [[nodiscard]] ServiceRegistration<U,newScope> as() const {
        if constexpr(std::is_same_v<U,S> && newScope == SCP) {
            return *this;
        } else {
            static_assert(SCP == newScope || SCP == ServiceScope::UNKNOWN || newScope == ServiceScope::UNKNOWN, "Either current scope or new scope must be UNKNOWN");
            return ServiceRegistration<U,newScope>::wrap(unwrap());
        }
    }

    /**
     * @brief operator Implicit conversion to a ServiceRegistration with ServiceScope::UNKNOWN.
     */
    operator ServiceRegistration<S,ServiceScope::UNKNOWN>() const {
        if constexpr(SCP == ServiceScope::UNKNOWN) {
            return *this;
        } else {
            return ServiceRegistration<S,ServiceScope::UNKNOWN>::wrap(unwrap());
        }
    }




    ///
    /// \brief Registers an alias for a Service.
    /// <br>If this function is successful, the Service can be referenced by the new name in addition to the
    /// name it was originally registered with. There can be multiple aliases for a Service.<br>
    /// Aliases must be unique within the ApplicationContext.
    /// <br>**Thread-safety:** This function may be safely called from any thread.
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
    /// \return a valid Registration if handle is not `nullptr` and if `Registration::matches<S>()` and if `scope` matches the property `prototype` of the handle.
    /// \see unwrap().
    ///
    [[nodiscard]] static ServiceRegistration<S,SCP> wrap(service_registration_handle_t handle) {
        if(mcnepp::qtdi::matches<S>(handle)) {
            if constexpr(SCP == ServiceScope::UNKNOWN) {
                return ServiceRegistration<S,SCP>{handle};
            } else {
               return ServiceRegistration{handle->scope() == SCP ? handle : nullptr};
            }
        }
        return ServiceRegistration{};
    }




private:
    explicit ServiceRegistration(service_registration_handle_t reg) : Registration<S>{reg} {

    }




    Subscription bind(const char* sourceProperty, registration_handle_t target, const detail::property_descriptor& descriptor) const {
        static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
        static_assert(detail::service_scope_traits<SCP>::is_binding_source, "The scope of the service does not permit binding");

        if(!target || !*this) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind " << *this << " to " << target;
            return Subscription{};
        }
        auto subscription = unwrap() -> createBindingTo(sourceProperty, target, descriptor);
        return Subscription{subscription};
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
    [[nodiscard]] QList<ServiceRegistration<S,ServiceScope::UNKNOWN>> registeredServices() const {
        QList<ServiceRegistration<S,ServiceScope::UNKNOWN>> result;
        for(auto srv : mcnepp::qtdi::registeredServices(unwrap())) {
            result.push_back(ServiceRegistration<S,ServiceScope::UNKNOWN>::wrap(srv));
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
/// <br>**Thread-safety:** This function may only be called from the QApplicationContext's thread.
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param sourceProperty the name of the Q_PROPERTY in the source.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param targetProperty the name of the Q_PROPERTY in the target.
/// \tparam S the type of the source.
/// \tparam T the type of the target.
/// \return the Subscription established by this binding.
///
template<typename S,typename T,ServiceScope scope> inline Subscription bind(const ServiceRegistration<S,scope>& source, const char* sourceProperty, Registration<T>& target, const char* targetProperty) {
    static_assert(std::is_base_of_v<QObject,T>, "Target must be derived from QObject");
    return source.bind(sourceProperty, target.unwrap(), {targetProperty, nullptr});
}

///
/// \brief Binds a property of one ServiceRegistration to a Setter from  another Registration.
/// <br>All changes made to the source-property will be propagated to all Services represented by the target.
/// For each target-property, there can be only successful call to bind().
/// <br>**Thread-safety:** This function only may be called from the QApplicationContext's thread.
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param sourceProperty the name of the Q_PROPERTY in the source.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param setter the method in the target which shall be bound to the source-property.
/// \tparam S the type of the source.
/// \tparam T the type of the target.
/// \return the Subscription established by this binding.
///
template<typename S,typename T,typename A,typename R,ServiceScope scope> inline Subscription bind(const ServiceRegistration<S,scope>& source, const char* sourceProperty, Registration<T>& target, R(T::*setter)(A)) {
    if(!setter) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind " << source << " to null";
        return Subscription{};
    }
    return source.bind(sourceProperty, target.unwrap(), {"", detail::callable_adapter<T>::adaptSetter(setter)});
}


///
/// \brief Binds a property of one ServiceRegistration to a Setter from  another Registration.
/// <br>This function identifies the source-property by the signal that is emitted when the property changes.
/// The signal is specified in terms of a pointer to a member-function. This member-function must denote the signal corresponding
/// to a property of the source-service.
/// <br>All changes made to the source-property will be propagated to all Services represented by the target.
/// For each target-property, there can be only successful call to bind().
/// <br>**Thread-safety:** This function only may be called from the QApplicationContext's thread.
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param signalFunction the address of the member-function that is emitted as the signal for the property.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param setter the method in the target which shall be bound to the source-property.
/// \tparam S the type of the source.
/// \tparam T the type of the target.
/// \return the Subscription established by this binding.
///
template<typename S,typename T,typename AS,typename AT,typename R,ServiceScope scope> inline auto bind(const ServiceRegistration<S,scope>& source, void(S::*signalFunction)(AS), Registration<T>& target, R(T::*setter)(AT)) ->
    std::enable_if_t<std::is_convertible_v<AS,AT>,Subscription> {
    if(!setter || !signalFunction || !source || !target) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind " << source << " to null";
        return Subscription{};
    }
    if(auto signalProperty = detail::findPropertyBySignal(QMetaMethod::fromSignal(signalFunction), source.serviceMetaObject()); signalProperty.isValid()) {
        return bind(source, signalProperty.name(), target, setter);
    }
    return Subscription{};
}

///
/// \brief Binds a property of one ServiceRegistration to a Setter from  another Registration.
/// <br>This function identifies the source-property by the signal that is emitted when the property changes.
/// The signal is specified in terms of a pointer to a member-function. This member-function must denote the signal corresponding
/// to a property of the source-service.
/// <br>All changes made to the source-property will be propagated to all Services represented by the target.
/// For each target-property, there can be only successful call to bind().
/// <br>**Thread-safety:** This function only may be called from the QApplicationContext's thread.
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param signalFunction the address of the member-function that is emitted as the signal for the property.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param setter the method in the target which shall be bound to the source-property.
/// \tparam S the type of the source.
/// \tparam T the type of the target.
/// \return the Subscription established by this binding.
///
template<typename S,typename T,typename A,typename R,ServiceScope scope> inline Subscription bind(const ServiceRegistration<S,scope>& source, void(S::*signalFunction)(), Registration<T>& target, R(T::*setter)(A)) {
    if(!setter || !signalFunction || !source || !target) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind " << source << " to null";
        return Subscription{};
    }
    if(auto signalProperty = detail::findPropertyBySignal(QMetaMethod::fromSignal(signalFunction), source.serviceMetaObject()); signalProperty.isValid()) {
        return bind(source, signalProperty.name(), target, setter);
    }
    return Subscription{};
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
/// \brief Provides default-values for service_traits.
/// <br>Specializations of service_traits in client-code are encouraged to extend this type!
/// <br>this template provides the following declarations:
/// - service_type an alias for S.
/// - factory_type an alias for service_factory.
/// - initializer_type an alias for std::nullptr_t.
///

template<typename S> struct default_service_traits {
    static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");

    using service_type = S;

    using factory_type = service_factory<S>;

    using initializer_type = std::nullptr_t;
};


///
/// \brief The traits for services.
/// <br>Every specialization of this template must provide at least the following declarations:
/// - `service_type` the type of service that this traits describe.
/// - `factory_type` the type of the factory.
/// - `initializer_type` the type of the initializer. Must be one of the following:
///   -# pointer to a non-static member-function with no arguments
///   -# pointer to a non-static member-function with one parameter of type `QApplicationContext*`
///   -# address of a free function with one argument of the service-type.
///   -# address of a free function with two arguments, the second being of type `QApplicationContext*`.
///   -# type with a call-operator with one argument of the service-type.
///   -# type with a call-operator with two arguments, the second being of type `QApplicationContext*`.
///   -# `nullptr`
///
template<typename S> struct service_traits : default_service_traits<S> {
};

///
/// \brief A helper-template that converts a pointer to a function or a pointer to a member-function into a distinct type.
/// <br>The purpose of this template is to be used as the type-alias named `initializer_type` in the service_traits.
/// The type of `func` must be one of the following:
/// - `initializer_type` the type of the initializer. Must be one of the following:
///   -# pointer to a non-static member-function with no arguments
///   -# pointer to a non-static member-function with one parameter of type `QApplicationContext*`
///   -# address of a free function with one argument of the service-type.
///   -# address of a free function with two arguments, the second being of type `QApplicationContext*`.
///
/// Note that in contrast to the requirements for the declaration of the type-alias `initializer_type` in the service_traits,
/// both std::nullptr_t and types with a call-operator are not permitted here.<br>
/// The reason: those types can be aliased directly, without this helper-template!
/// \tparam func the function or pointer to member-function.
///
template<auto func> struct service_initializer {

    constexpr auto value() const {
        return func;
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
        return left.properties == right.properties && left.group == right.group && left.autowire == right.autowire;
    }


    QVariantMap properties;
    QString group;
    bool autowire = false;
};

///
/// \brief Makes a service_config.
/// <br>The service must have a Q_PROPERTY for every key contained in `properties`.<br>
/// Example:
///
///     `make_config({{"interval", 42}});`
/// will set the Q_PROPERTY `interval` to the value 42.
/// ### Private Properties
/// A key that starts with a dot is considered to denote a *private property*, and no attempt will be made to set a corresponding Q_PROPERTY
/// on the Service.<br>
/// There are two ways of putting such private properties to use:<br>
/// Either, you may evaluate them via a `QApplicationContextPostProcessor`.<br>
/// Or, you can use them in conjunction with a *service-template*.
/// Suppose, for example, you have a class `RestService` with a Q_PROPERTY `url`.
/// You want to construct this URL by using the same pattern for every service of type `RestService`.
/// However, one part of the URL will be unique for each Service.
/// This is how you would do this:
///
///     auto restServiceTemplate = context -> registerService<RestService>(serviceTemplate<RestService>(), "restTemplate", make_config({{"url", "https://myserver/rest/${path}"}}));
///
/// Now, whenever you register a concrete RestService, you must supply the `templateReg` as an additional argument.
/// Also, you must specify the value for `${path}` as a *private property*:
///
///     context -> registerService(service<RestService>(), restServiceTemplate, "temperatureService", make_config({{".path", "temperature"}}));
///
/// ### Placeholders
/// Values may contain *placeholders*, indicated by the syntax `${placeholder}`. Such a placeholder will be looked
/// up via `QApplicationContext::getConfigurationValue(const QString&)`.<br>
/// Example:
///
///     `make_config({{"interval", "${timerInterval}"}});`
/// will set the Q_PROPERTY `interval` to the value configured with the name `timerInterval`.
/// <br>Should you want to specify a property-value containg the character-sequence "${", you must escape this with the backslash.
/// ### Service-references
/// If a value starts with an ampersand, the property will be resolved with a registered service of that name.
/// Example:
///
///     `make_config({{"dataProvider", "&dataProviderService"}});`
/// will set the Q_PROPERTY `dataProvider` to the service that was registered under the name `dataProviderService`.
/// <br>Should you want to specify a property-value starting with an ampersand, you must escape this with the backslash.
/// \param properties the keys and value to be applied as Q_PROPERTYs.
/// \param group the `QSettings::group()` to be used.
/// \param autowire if `true`, the QApplicationContext will attempt to initialize all Q_PROPERTYs of `QObject*`-type with the corresponding services.
/// Those properties that have explicitly been supplied will not be auto-wired.
/// \return the service_config.
///
[[nodiscard]] inline service_config make_config(std::initializer_list<std::pair<QString,QVariant>> properties, const QString& group = "", bool autowire = false) {
    return service_config{properties, group, autowire};
}

///
/// \brief Makes a service_config that will autowire a service.
/// <br>Equivalent to `make_config({}, group, true)`.
/// \param group the `QSettings::group()` to be used.
/// \return the service_config.
/// \sa mcnepp::qtdi::make_config(std::initializer_list<std::pair<QString,QVariant>>, const QString&, bool)
///
[[nodiscard]] inline service_config make_autowire_config(const QString& group = "") {
    return service_config{{}, group, true};
}




namespace detail {




template<typename S,typename F> static auto adaptInitializer(F func, std::nullptr_t) -> std::enable_if_t<std::is_invocable_v<F,S*,QApplicationContext*>,q_init_t> {
    return [func](QObject* target,QApplicationContext* context) {
        if(auto ptr =dynamic_cast<S*>(target)) {
            func(ptr, context);
        }};

}

template<typename S,typename F> static auto adaptInitializer(F func, std::nullptr_t) -> std::enable_if_t<std::is_invocable_v<F,S*>,q_init_t> {
    return [func](QObject* target,QApplicationContext*) {
        if(auto ptr =dynamic_cast<S*>(target)) {
            func(ptr);
        }};

}


template<typename S,typename R> static q_init_t adaptInitializer(R (S::*init)(QApplicationContext*), std::nullptr_t) {
    if(!init) {
        return nullptr;
    }
    return adaptInitializer<S>(std::mem_fn(init), nullptr);
}

template<typename S,typename R> static q_init_t adaptInitializer(R (S::*init)(), std::nullptr_t) {
    if(!init) {
        return nullptr;
    }
    return adaptInitializer<S>(std::mem_fn(init), nullptr);
}


template<typename S,auto func> static q_init_t adaptInitializer(service_initializer<func> initializer, std::nullptr_t) {
    return adaptInitializer<S>(initializer.value(), nullptr);
}

//SFINAE fallback for everything that is not a callable:
template<typename S,typename F> static q_init_t adaptInitializer(F func,int*) {
    static_assert(std::is_same_v<F,std::nullptr_t>, "Type must be a callable object or a function or a member-function with either zero arguments or one argument of type QApplicationContext*");
    return func;
}


template<typename S,typename F> static q_init_t adaptInitializer(F func=F{}) {
    return adaptInitializer<S>(func, nullptr);
}




using constructor_t = std::function<QObject*(const QVariantList&)>;

constexpr int VALUE_KIND = 0x10;
constexpr int RESOLVABLE_KIND = 0x20;
constexpr int INVALID_KIND = 0xff;


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

    bool isValid() const {
        return kind != INVALID_KIND;
    }

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
    case INVALID_KIND:
        return false;
        //In all other cases, we use only the expression. (For RESOLVABLE_KIND, value contains the default-value, which we ignore deliberately)
    default:
        return info1.expression == info2.expression;
    }
}

QDebug operator << (QDebug out, const dependency_info& info);


struct service_descriptor {

    QObject* create(const QVariantList& args) const {
        return constructor ? constructor(args) : nullptr;
    }

    bool matches(const std::type_info& type) const {
        return type == impl_type || service_types.find(type) != service_types.end();
    }



    std::unordered_set<std::type_index> service_types;
    const std::type_info& impl_type;
    const QMetaObject* meta_object = nullptr;
    constructor_t constructor;
    std::vector<dependency_info> dependencies;
    q_init_t init_method;
};


QDebug operator << (QDebug out, const service_descriptor& descriptor);




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

    static auto converter(S) {
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

template <typename S,ServiceScope scope>
struct dependency_helper<mcnepp::qtdi::ServiceRegistration<S,scope>> {
    using type = S;



    static dependency_info info(const mcnepp::qtdi::ServiceRegistration<S,scope>& dep) {
        static_assert(scope != ServiceScope::TEMPLATE, "ServiceRegistration with ServiceScope::TEMPLATE cannot be a dependency");
        //It could still be ServiceScope::UNKNOWN statically, but ServiceScope::TEMPLATE at runtime:
        if(dep && dep.unwrap()->scope() != ServiceScope::TEMPLATE) {
            return { dep.unwrap()->descriptor().impl_type, static_cast<int>(Kind::MANDATORY), dep.registeredName() };
        }
        return { typeid(S), INVALID_KIND, dep.registeredName() };
    }

    static auto converter(const mcnepp::qtdi::ServiceRegistration<S,scope>&) {
        return default_argument_converter<S,Kind::MANDATORY>{};
    }

};

template <typename S>
struct dependency_helper<mcnepp::qtdi::ProxyRegistration<S>> {
    using type = S;



    static dependency_info info(const mcnepp::qtdi::ProxyRegistration<S>& dep) {
        if(dep) {
            return { dep.unwrap()->serviceType(), static_cast<int>(Kind::N)};
        }
        return { typeid(S), INVALID_KIND };
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

    static auto converter(const Resolvable<S>&) {
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
    return [factory](const QVariantList&) {
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


template<typename S> constexpr bool has_initializer = std::negation_v<std::is_same<std::nullptr_t,typename service_traits<S>::initializer_type>>;

template<bool found,typename First,typename...Tail> q_init_t getInitializer() {
    constexpr bool foundThis = has_initializer<First>;
    static_assert(!(foundThis && found), "Ambiguous initializers in advertised interfaces");
    if constexpr(sizeof...(Tail) > 0) {
        //Invoke recursively, until we either trigger an assertion or find no other initializer:
        auto result = getInitializer<foundThis,Tail...>();
        if constexpr(!foundThis) {
            return result;
        }
    }
    return adaptInitializer<First>(typename service_traits<First>::initializer_type{});

}



template<typename Srv,typename Impl,ServiceScope scope,typename F,typename...Dep> service_descriptor make_descriptor(F factory, Dep...deps) {
    detail::service_descriptor descriptor{{typeid(Srv)}, typeid(Impl), &Impl::staticMetaObject};
    if constexpr(has_initializer<Impl>) {
         descriptor.init_method = adaptInitializer<Impl>(typename service_traits<Impl>::initializer_type{});
    } else {
        descriptor.init_method = adaptInitializer<Srv>(typename service_traits<Srv>::initializer_type{});
    }
    (descriptor.dependencies.push_back(detail::dependency_helper<Dep>::info(deps)), ...);
    if constexpr(detail::service_scope_traits<scope>::is_constructable) {
        descriptor.constructor = service_creator<Impl>(factory, detail::dependency_helper<Dep>::converter(deps)...);
    }
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
/// \tparam scope the scope of the designated Service.
///
template<typename Srv,typename Impl=Srv,ServiceScope scope=ServiceScope::UNKNOWN> struct Service {
    static_assert(std::is_base_of_v<QObject,Impl>, "Implementation-type must be a subclass of QObject");

    static_assert(std::is_base_of_v<Srv,Impl>, "Implementation-type must be a subclass of Service-type");

    using service_type = Srv;

    using impl_type = Impl;


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
/// \tparam scope the scope of the designated Service.
///
template<typename Impl,ServiceScope scope> struct Service<Impl,Impl,scope> {
    static_assert(std::is_base_of_v<QObject,Impl>, "Implementation-type must be a subclass of QObject");


    using service_type = Impl;

    using impl_type = Impl;



    explicit Service(detail::service_descriptor&& descr) :
        descriptor{std::move(descr)} {
    }

    /**
     * @brief Specifies service-interfaces.
     *  <br>You must specify at least one interface (or otherwise compilation will fail). These interfaces will be available for lookup via QApplicationContext::getRegistration().
     *  They will also be used to satisfy dependencies that other services may have to this one.
     *  <br>This function may be invoked only on temporary instances.
     * <br>Compilation will fail if any of the following is true:
     * - Any of the interfaces is not a base of `Impl` (unless the `scope` of this Service is ServiceScope::TEMPLATE, in which case this is not verified).
     * - A type appears more than once in the set of types comprising `Impl` and `IFaces`.
     * - The service_traits *for more than one* of the interfaces have an `initializer_type` (i.e. one that is not std::nullptr_t).
     * In order to fix this error, you need to declare a valid `initializer_type` in the service_traits for the Implementation-type.
     * This will "override" the initializer from the interface.
     *
     * \tparam IFaces additional service-interfaces to be advertised. <b>At least one must be supplied.</b>
     * @return this Service.
     */
    template<typename...IFaces> Service<Impl,Impl,scope>&& advertiseAs() && {
        static_assert(sizeof...(IFaces) > 0, "At least one service-interface must be advertised.");
        //Check whether the Impl-type is derived from the service-interfaces (except for service-templates)
        if constexpr(scope != ServiceScope::TEMPLATE) {
            static_assert((std::is_base_of_v<IFaces,Impl> && ... ), "Implementation-type does not implement all advertised interfaces");
        }
        static_assert(detail::check_unique_types<Impl,IFaces...>(), "All advertised interfaces must be distinct");
        if(auto found = descriptor.service_types.find(descriptor.impl_type); found != descriptor.service_types.end()) {
           descriptor.service_types.erase(found);
        }
        (descriptor.service_types.insert(typeid(IFaces)), ...);
        if constexpr(!detail::has_initializer<Impl>) {
            descriptor.init_method = detail::getInitializer<false,IFaces...>();
        }
        return std::move(*this);
    }

    /**
     * @brief Specifies service-interfaces.
     *  <br>You must specify at least one interface (or otherwise compilation will fail). These interfaces will be available for lookup via QApplicationContext::getRegistration().
     *  They will also be used to satisfy dependencies that other services may have to this one.
     *  <br>This function may be invoked only on temporary instances.
     * <br>Compilation will fail if any of the following is true:
     * - Any of the interfaces is not a base of `Impl` (unless the `scope` of this Service is ServiceScope::TEMPLATE, in which case this is not verified).
     * - A type appears more than once in the set of types comprising `Impl` and `IFaces`.
     * - The service_traits *for more than one* of the interfaces have an `initializer_type` (i.e. one that is not std::nullptr_t).
     * In order to fix this error, you need to declare a valid `initializer_type` in the service_traits for the Implementation-type.
     * This will "override" the initializer from the interface.
     *
     * \tparam IFaces additional service-interfaces to be advertised. <b>At least one must be supplied.</b>
     * @return a Service with the advertised interfaces.
     */
    template<typename...IFaces> [[nodiscard]] Service<Impl,Impl,scope> advertiseAs() const& {
        return Service<Impl,Impl,scope>{*this}.advertiseAs<IFaces...>();
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
template<typename F,typename Impl=typename F::service_type,typename...Dep> [[nodiscard]]Service<Impl,Impl,ServiceScope::SINGLETON> serviceFactory(F factory, Dep...dependencies) {
    return Service<Impl,Impl,ServiceScope::SINGLETON>{detail::make_descriptor<Impl,Impl,ServiceScope::SINGLETON>(factory, dependencies...)};
}



///
/// \brief Creates a Service with the default service-factory.
/// \param dependencies the arguments to be injected into the service's constructor.
/// \tparam S the primary service-interface.
/// \tparam Impl the implementation-type of the service.
/// \return a Service-declaration
template<typename S,typename Impl=S,typename...Dep>  [[nodiscard]] Service<S,Impl,ServiceScope::SINGLETON> service(Dep...dependencies) {
    return Service<S,Impl,ServiceScope::SINGLETON>{detail::make_descriptor<S,Impl,ServiceScope::SINGLETON>(typename service_traits<Impl>::factory_type{}, dependencies...)};
}

///
/// \brief Creates a Service-prototype with the default service-factory.
/// \param dependencies the arguments to be injected into the service's constructor.
/// \tparam S the primary service-interface.
/// \tparam Impl the implementation-type of the service.
/// \return a Prototype-declaration
template<typename S,typename Impl=S,typename...Dep>  [[nodiscard]] Service<S,Impl,ServiceScope::PROTOTYPE> prototype(Dep...dependencies) {
    return Service<S,Impl,ServiceScope::PROTOTYPE>{detail::make_descriptor<S,Impl,ServiceScope::PROTOTYPE>(typename service_traits<Impl>::factory_type{}, dependencies...)};
}

///
/// \brief Creates a Service-template with no dependencies and no constructor.
/// <br>The returned Service cannot be instantiated. It just serves as an additional parameter
/// for registering other services.
/// <br>If you leave out the type-argument `Impl`, it will default to `QObject`.
/// <br>Should you want to ensure that every service derived from this service-template shall be advertised under
/// a certain interface, use Service::advertiseAs().
/// \tparam Impl the implementation-type of the service.
/// \return a Service that cannot be instantiated.
template<typename Impl=QObject>  [[nodiscard]] Service<Impl,Impl,ServiceScope::TEMPLATE> serviceTemplate() {
    return Service<Impl,Impl,ServiceScope::TEMPLATE>{detail::make_descriptor<Impl,Impl,ServiceScope::TEMPLATE>(nullptr)};
}



///
/// \brief A DI-Container for Qt-based applications.
///
class QApplicationContext : public QObject
{
    Q_OBJECT

public:


    ///
    /// \brief Obtains the global instance.
    /// <br>QApplicationContext's constructor will atomically install `this` as the global instance,
    /// unless another instance has already been registered.
    /// <br>QQpplicationContext's destructor will clear the global instance, if it currently points to `this`.
    /// <br>You may determine whether a QApplicationContext is the global instance using QApplicationContext::isGlobalInstance().
    /// <br>**Note:** the global instance will not be deleted automatically! It is the responsibilty of the user to delete it.
    /// \return  the global instance, or `nullptr` if no QApplicationContext is currently alive.
    ///
    static QApplicationContext* instance();



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

    ~QApplicationContext();



    ///
    /// \brief Registers a service with this ApplicationContext.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
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
    template<typename S,typename Impl,ServiceScope scope> auto registerService(const Service<S,Impl,scope>& serviceDeclaration, const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S,scope> {
        return ServiceRegistration<S,scope>::wrap(registerService(objectName, serviceDeclaration.descriptor, config, scope, nullptr));
    }


    ///
    /// \brief Registers a service with this ApplicationContext.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param serviceDeclaration comprises the services's primary advertised interface, its implementation-type and its dependencies to be injected
    /// via its constructor.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \param templateRegistration the registration of the service-template that this service shall inherit from. Must be valid!
    /// \tparam S the service-type. Constitutes the Service's primary advertised interface.
    /// \tparam Impl the implementation-type. The Service will be instantiated using this class' constructor.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename Impl,typename B,ServiceScope scope> auto registerService(const Service<S,Impl,scope>& serviceDeclaration, const ServiceRegistration<B,ServiceScope::TEMPLATE>& templateRegistration, const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S,scope> {
        static_assert(std::is_base_of_v<B,Impl>, "Service-type does not extend type of Service-template.");
        if(!templateRegistration) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register " << serviceDeclaration.descriptor << " with name '" << objectName << "'. Invalid service-template";
            return ServiceRegistration<S,scope>{};
        }
        return ServiceRegistration<S,scope>::wrap(registerService(objectName, serviceDeclaration.descriptor, config, scope, templateRegistration.unwrap()));
    }







    ///
    /// \brief Registers a service with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(service<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> auto registerService(const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S,ServiceScope::SINGLETON> {
        return registerService(service<S>(), objectName, config);
    }

    ///
    /// \brief Registers a service-prototype with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(prototype<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> auto registerPrototype(const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S,ServiceScope::PROTOTYPE> {
        return registerService(prototype<S>(), objectName, config);
    }





    ///
    /// \brief Registers a service-template with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(serviceTemplate<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// <br>If you leave out the type-argument `S`, it will default to `QObject`.
    /// \param config the Configuration for the service.
    /// <br>**Note:** Since a service-template may be used by services of types that are yet unknown, the properties supplied here cannot be validated.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S=QObject> auto registerServiceTemplate(const QString& objectName = "", const service_config& config = service_config{}) -> ServiceRegistration<S,ServiceScope::TEMPLATE> {
        return registerService(serviceTemplate<S>(), objectName, config);
    }

    ///
    /// \brief Registers an object with this ApplicationContext.
    /// The object will immediately be published.
    /// You can either let the compiler's template-argument deduction figure out the servicetype `<S>` for you,
    /// or you can supply it explicitly, if it differs from the static type of the object.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param obj must be non-null. Also, must be convertible to QObject.
    /// \param objName the name for this Object in the ApplicationContext.
    /// *Note*: this name will not be set as the QObject::objectName(). It will be the internal name within the ApplicationContext only.
    /// \tparam S the primary service-type for the object.
    /// \tparam IFaces additional service-interfaces to be advertised. If a type appears more than once in the set of types comprising `S` and `IFaces`, compilation will fail with a diagnostic.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename... IFaces> ServiceRegistration<S,ServiceScope::EXTERNAL> registerObject(S* obj, const QString& objName = "") {
        static_assert(detail::could_be_qobject<S>::value, "Object is not potentially convertible to QObject");
        QObject* qObject = dynamic_cast<QObject*>(obj);
        if(!qObject) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << obj << " as '" << objName << "'. Object is no QObject";
            return ServiceRegistration<S,ServiceScope::EXTERNAL>{};
        }
        if constexpr(sizeof...(IFaces) > 0) {
            static_assert(detail::check_unique_types<S,IFaces...>(), "All advertised interfaces must be distinct");
            auto check = detail::check_dynamic_types<S,IFaces...>(obj);
            if(!check.first)            {
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << qObject << " as '" << objName << "'. Object does not implement " << check.second.name();
                return ServiceRegistration<S,ServiceScope::EXTERNAL>{};
            }
        }
        std::unordered_set<std::type_index> ifaces;
        (ifaces.insert(typeid(S)), ..., ifaces.insert(typeid(IFaces)));
        return ServiceRegistration<S,ServiceScope::EXTERNAL>::wrap(registerService(objName, service_descriptor{ifaces, typeid(*obj), qObject->metaObject()}, service_config{}, ServiceScope::EXTERNAL, qObject));
    }

    ///
    /// \brief Obtains a ServiceRegistration for a service-type and name.
    /// <br>This function will look up Services by the names they were registered with.
    /// Additionally, it will look up any alias that might have been given, using ServiceRegistration::registerAlias(const QString&).
    /// <br>**Note:** If you do not provide an explicit type-argument, QObject will be assumed. This will, of course, match
    /// any service with the supplied name.<br>
    /// The returned ServiceRegistration may be narrowed to a more specific service-type using ServiceRegistration::as().
    /// <br>**Thread-safety:** This function may be called safely  from any thread.
    /// \tparam S the required service-type.
    /// \param name the desired name of the registration.
    /// A valid ServiceRegistration will be returned only if exactly one Service that matches the requested type and name has been registered.
    /// \return a ServiceRegistration for the required type and name. If no single Service with a matching name and service-type could be found,
    /// an invalid ServiceRegistration will be returned.
    ///
    template<typename S=QObject> [[nodiscard]] ServiceRegistration<S,ServiceScope::UNKNOWN> getRegistration(const QString& name) const {
        static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");
        return ServiceRegistration<S,ServiceScope::UNKNOWN>::wrap(getRegistrationHandle(name));
    }



    ///
    /// \brief Obtains a ProxyRegistration for a service-type.
    /// <br>In contrast to the ServiceRegistration that is returned by registerService(),
    /// the ProxyRegistration returned by this function is actually a Proxy.<br>
    /// This Proxy manages all Services of the requested type, regardless of whether they have been registered prior
    /// to invoking getRegistration().<br>
    /// This means that if you subscribe to it using Registration::subscribe(), you will be notified
    /// about all published services that match the Service-type.
    /// <br>**Thread-safety:** This function may be called safely  from any thread.
    /// \tparam S the required service-type.
    /// \return a ProxyRegistration that corresponds to all registration that match the service-type.
    ///
    template<typename S> [[nodiscard]] ProxyRegistration<S> getRegistration() const {
        static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");
        return ProxyRegistration<S>::wrap(getRegistrationHandle(typeid(S), detail::getMetaObject<S>()));
    }

    /**
     * @brief Obtains a List of all Services that have been registered.
     * <br>The ServiceRegistrations have a type-argument `QObject`. You may
     * want to narrow them to the expected service-type using ServiceRegistration::as().
     * <br>**Thread-safety:** This function may be called safely  from any thread.
     * @return a List of all Services that have been registered.
     */
    [[nodiscard]] QList<ServiceRegistration<QObject,ServiceScope::UNKNOWN>> getRegistrations() const {
        QList<ServiceRegistration<QObject,ServiceScope::UNKNOWN>> result;
        for(auto handle : getRegistrationHandles()) {
            result.push_back(ServiceRegistration<QObject,ServiceScope::UNKNOWN>::wrap(handle));
        }
        return result;
    }




    ///
    /// \brief Publishes this ApplicationContext.
    /// This method may be invoked multiple times.
    /// Each time it is invoked, it will attempt to instantiate all yet-unpublished services that have been registered with this ApplicationContext.
    /// <br>**Thread-safety:** This function may only be called from the QApplicationContext's thread.
    /// \param allowPartial has the default-value `false`, this function will either return all services or no service at all.
    /// If `allowPartial == true`, the function will attempt to publish as many pending services as possible.
    /// Failures that may be fixed by further registrations will be logged with the level QtMsgType::QtWarningMessage.
    /// \return `true` if there are no fatal errors and all services were published.
    ///
    virtual bool publish(bool allowPartial = false) = 0;



    ///
    /// \brief The number of published services.
    /// <br>**Thread-safety:** This function may be safely called from any thread.
    /// \return The number of published services.
    ///
    [[nodiscard]] virtual unsigned published() const = 0;

    /// \brief The number of yet unpublished services.
    /// <br>**Thread-safety:** This function may be safely called from any thread.
    /// \return the number of services that have been registered but not (yet) published.
    ///
    [[nodiscard]] virtual unsigned pendingPublication() const = 0;

    ///
    /// \brief Is this the global instance?
    /// \return `true` if `this` has been installed as the global instance.
    ///
    bool isGlobalInstance() const;


    ///
    /// \brief Retrieves a value from the ApplicationContext's configuration.
    /// <br>This function will be used to resolve placeholders in Service-configurations.
    /// Whenever a *placeholder* shall be looked up, the ApplicationContext will search the following sources, until it can resolve the *placeholder*:
    /// -# The environment, for a variable corresponding to the *placeholder*.
    /// -# The instances of `QSettings` that have been registered in the ApplicationContext.
    /// \sa mcnepp::qtdi::make_config()
    /// \param key the key to look up. In analogy to QSettings, the key may contain forward slashes to denote keys within sub-sections.
    /// \return the value, if it could be resolved. Otherwise, an invalid QVariant.
    ///
    [[nodiscard]] virtual QVariant getConfigurationValue(const QString& key) const = 0;

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
    /// \param baseObject in case of ServiceScope::EXTERNAL the Object to be registered. Otherwise, the (optional) pointer to the registration of a service-template.
    /// \return a Registration for the service, or `nullptr` if it could not be registered.
    ///
    virtual service_registration_handle_t registerService(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, QObject* baseObject) = 0;


    ///
    /// \brief Obtains a Registration for a service_type.
    /// \param service_type the service-type to match the registrations.
    /// \param metaObject the static QMetaObject for the type. If not available, `nullptr` can be passed.
    /// \return a Registration for the supplied service_type.
    ///
    virtual proxy_registration_handle_t getRegistrationHandle(const std::type_info& service_type, const QMetaObject* metaObject) const = 0;

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
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing registerService(const QString&, service_descriptor*) and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// <br>If this function is invoked with `appContext == nullptr`, it will return `nullptr`.
    /// \param appContext the target on which to invoke registerService(const QString&, service_descriptor*).
    /// \param name
    /// \param descriptor
    /// \param config
    /// \param baseObj
    /// \return the result of registerService(const QString&, service_descriptor*,const service_config&,ServiceScope,QObject*).
    ///
    static service_registration_handle_t delegateRegisterService(QApplicationContext* appContext, const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, QObject* baseObj) {
        if(!appContext) {
            return nullptr;
        }
        return appContext->registerService(name, descriptor, config, scope, baseObj);
    }



    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing getRegistrationHandle(const std::type_info&,const QMetaObject*) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// <br>If this function is invoked with `appContext == nullptr`, it will return `nullptr`.
    /// \param appContext the target on which to invoke getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    /// \param service_type
    /// \param metaObject the QMetaObject of the service_type. May be omitted.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    static proxy_registration_handle_t delegateGetRegistrationHandle(const QApplicationContext* appContext, const std::type_info& service_type, const QMetaObject* metaObject) {
        if(!appContext) {
            return nullptr;
        }
        return appContext->getRegistrationHandle(service_type, metaObject);
    }

    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing getRegistrationHandle(const QString&) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// <br>If this function is invoked with `appContext == nullptr`, it will return `nullptr`.
    /// \param appContext the target on which to invoke getRegistrationHandle(const QString&) const.
    /// \param name the name under which the service is looked up.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    static service_registration_handle_t delegateGetRegistrationHandle(const QApplicationContext* appContext, const QString& name) {
        if(!appContext) {
            return nullptr;
        }

        return appContext->getRegistrationHandle(name);
    }



    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing getRegistrationHandles() const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// <br>If this function is invoked with `appContext == nullptr`, it will return an empty QList.
    /// \param appContext the target on which to invoke getRegistrationHandles() const.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    static QList<service_registration_handle_t> delegateGetRegistrationHandles(const QApplicationContext* appContext) {
        if(!appContext) {
            return QList<service_registration_handle_t>{};
        }
        return appContext->getRegistrationHandles();
    }

    static bool setInstance(QApplicationContext*);

    static bool unsetInstance(QApplicationContext*);


    template<typename S,ServiceScope> friend class ServiceRegistration;

private:
    static std::atomic<QApplicationContext*> theInstance;
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
    return Subscription{subscription};
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
        size_t operator()(const mcnepp::qtdi::Subscription& sub, [[maybe_unused]] size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        std::hash<mcnepp::qtdi::subscription_handle_t> hasher;
    };

    template<typename S,mcnepp::qtdi::ServiceScope scope> struct hash<mcnepp::qtdi::ServiceRegistration<S,scope>> {
        size_t operator()(const mcnepp::qtdi::ServiceRegistration<S,scope>& sub, [[maybe_unused]] size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        std::hash<mcnepp::qtdi::service_registration_handle_t> hasher;
    };


    template<typename S> struct hash<mcnepp::qtdi::ProxyRegistration<S>> {
        size_t operator()(const mcnepp::qtdi::ProxyRegistration<S>& sub, [[maybe_unused]] size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        std::hash<mcnepp::qtdi::proxy_registration_handle_t> hasher;
    };

    template<> struct hash<mcnepp::qtdi::detail::dependency_info> {
        std::size_t operator()(const mcnepp::qtdi::detail::dependency_info& info) const {
            return typeHasher(info.type) ^ info.kind;
        }
        hash<std::type_index> typeHasher;
    };



}
