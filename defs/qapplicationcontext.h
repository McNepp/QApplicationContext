#pragma once
/** @file qapplicationcontext.h
 *  @brief Contains the class QApplicationContext and other related types and functions.
 *  @author McNepp
*/

#include <utility>
#include <typeindex>
#include <QObject>
#include <QVariant>
#include <QPointer>
#include <QUuid>
#include <QLoggingCategory>

namespace mcnepp::qtdi {

class QApplicationContext;






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

    Registration* registration() const {
        return m_registration;
    }


protected:
    Subscription(Registration* registration, QObject* targetContext, Qt::ConnectionType connectionType);


    virtual void notify(QObject*) = 0;


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
    /// \brief The service-type that this Registration manages.
    /// \return The service-type that this Registration manages.
    ///
    [[nodiscard]] virtual const std::type_info& service_type() const = 0;



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

    /**
     * @brief Subscribes to a Subscription.
     * <br>This function will retrieve the Subscription::registration() and invoke onSubscription(Subscription*)
     * on it.
     * @param subscription
     * @return the subscription (for convenience)
     */
    static Subscription* subscribe(Subscription* subscription) {
        if(subscription) {
            subscription->registration()->onSubscription(subscription);
        }
        return subscription;
    }

};




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
     * Should you register more Services that match this service_type, you may need to invoke this method again.
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
/// What you can do, however, is use one of the free functions service_type(registration_handle_t handle),
/// applicationContext(registration_handle_t handle).
///
using registration_handle_t = detail::Registration*;

///
/// \brief Obtains the service_type from a handle to a Registration.
/// \param handle the handle to the Registration.
/// \return the service_type if the handle is valid, `typeid(void)` otherwise.
///
inline const std::type_info& service_type(registration_handle_t handle) {
    return handle ? handle->service_type() : typeid(void);
}

///
/// \brief Obtains the QApplicationContext from a handle to a Registration.
/// \param handle the handle to the Registration.
/// \return the QApplicationContext if the handle is valid, `nullptr` otherwise.
///
inline QApplicationContext* applicationContext(registration_handle_t handle) {
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
/// \brief Obtains the registered Services from a handle to a ProxyRegistration.
/// \param handle the handle to the ProxyRegistration.
/// \return the Services that the ProxyRegistration knows of, or an empty List if the handle is not valid.
///
inline QList<service_registration_handle_t> registeredServices(proxy_registration_handle_t handle) {
    return handle ? handle->registeredServices() : QList<service_registration_handle_t>{};
}

///
/// \brief Obtains the registeredName from a handle to a ServiceRegistration.
/// \param handle the handle to the ServiceRegistration.
/// \return the name that this ServiceRegistration was registered with, or an empty String if the handle is not valid.
///
inline QString registeredName(service_registration_handle_t handle) {
    return handle ? handle->registeredName() : QString{};
}

///
/// \brief Obtains the registeredProperties from a handle to a ServiceRegistration.
/// \param handle the handle to the  ServiceRegistration.
/// \return the properties that this ServiceRegistration was registered with, or an empty Map if the handle is not valid.
///
inline QVariantMap registeredProperties(service_registration_handle_t handle) {
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
    bool isValid() const {
        return m_subscription;
    }

    /**
     * @brief Yields the underlying detail::Subscription.
     * @return the underlying detail::Subscription.
     */
    detail::Subscription* unwrap() const {
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

private:
    QPointer<detail::Subscription> m_subscription;
};



///
/// \brief A type-safe wrapper for a detail::Registration.
/// Instances of this class are being returned by the public function-templates QApplicationContext::registerService(),
/// QApplicationContext::registerObject() and QApplicationContext::getRegistration().
///
/// This class offers the type-safe function `subscribe()` which should be preferred over directly connecting to the signal `detail::Registration::objectPublished(QObject*)`.
///
/// A Registration contains a *non-owning pointer* to a Registration whose lifetime of is bound
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


    [[nodiscard]] const std::type_info& service_type() const {
        return mcnepp::qtdi::service_type(unwrap());
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
    registration_handle_t unwrap() const {
        return registrationHolder.get();
    }

    ///
    /// \brief Connects a service with another service from the same QApplicationContext.
    /// Whenever a service of the type `<D>` is published, it will be injected into every service
    /// of type `<S>`, using the supplied member-function.
    ///
    /// \tparam D the type of service that will be injected into Services of type `<S>`.
    /// \param injectionSlot the member-function to invoke when a service of type `<D>` is published.
    /// \return the Subscription created by this autowiring.
    ///
    template<typename D,typename R> Subscription autowire(R (S::*injectionSlot)(D*));







    ///
    /// \brief Does this Registration represent a valid %Registration?
    /// \return `true` if the underlying Registration is present.
    ///
    bool isValid() const {
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

    ///
    /// \brief Does this Registration represent an invalid %Registration?
    /// Equivalent to `!isValid()`.
    /// \return `true` if the underlying Registration is not present.
    ///
    bool operator!() const {
        return !unwrap();
    }



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

    template<typename D,typename R> class AutowireSubscription : public detail::Subscription {
        friend class Registration;

        AutowireSubscription(registration_handle_t registration, R (S::*setter)(D*), registration_handle_t target) : Subscription(registration, target, Qt::AutoConnection),
            m_setter(setter),
            m_target(target)
        {
        }

        void notify(QObject* obj) override {
            if(S* srv = dynamic_cast<S*>(obj)) {
               Registration<D> target{m_target};
               auto subscr = target.subscribe(srv, m_setter);
               if(subscr) {
                   subscriptions.push_back(subscr);
               }
            }
        }

        void cancel() override {
            detail::Subscription::cancel();
            for(auto iter = subscriptions.begin(); iter != subscriptions.end(); iter = subscriptions.erase(iter)) {
                iter->cancel();
            }
        }

        R (S::*m_setter)(D*);
        QPointer<detail::Registration> m_target;
        std::vector<mcnepp::qtdi::Subscription> subscriptions;
    };



    QPointer<detail::Registration> registrationHolder;
};


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

    service_registration_handle_t unwrap() const {
        //We can use static_cast here, as the constructor enforces the correct type:
        return static_cast<service_registration_handle_t>(Registration<S>::unwrap());
    }


    ///
    /// \brief Registers an alias for a Service.
    /// <br>If this function is successful, the Service can be referenced by the new name in addition to the
    /// name it was originally registered with. There can be multiple aliases for a Service.<br>
    /// Aliases must be unique within the ApplicationContext.
    /// \param alias the alias to use.
    /// \param descriptor
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
    /// \return a valid Registration if handle is not `nullptr` and if its service_type equals `typeid(S)`.
    /// \see unwrap().
    ///
    static ServiceRegistration<S> wrap(service_registration_handle_t handle) {
        if(handle && handle->service_type() == typeid(S)) {
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
     * Should you register more Services that match this service_type, you may need to invoke this method again.
     * @return the ServiceRegistrations that this proxy currently knows of.
     */
    [[nodiscard]] QList<ServiceRegistration<S>> registeredServices() const {
        QList<ServiceRegistration<S>> result;
        for(auto srv : mcnepp::qtdi::registeredServices(unwrap())) {
            result.push_back(ServiceRegistration<S>::wrap(srv));
        }
        return result;
    }

    proxy_registration_handle_t unwrap() const {
        //We can use static_cast here, as the constructor enforces the correct type:
        return static_cast<proxy_registration_handle_t>(Registration<S>::unwrap());
    }

    ///
    /// \brief Wraps a handle to a ProxyRegistration into a typesafe high-level ProxyRegistration.
    /// \param handle the handle to the ProxyRegistration.
    /// \return a valid Registration if handle is not `nullptr` and if its service_type equals `typeid(S)`.
    /// \see unwrap().
    ///
    static ProxyRegistration<S> wrap(proxy_registration_handle_t handle) {
        if(handle && handle->service_type() == typeid(S)) {
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
/// \param target the Registration with the target-property to which the source-property shall be bound.
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
/// \param target the Registration with the target-property to which the source-property shall be bound.
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
/// This template can be used to force the QApplicationContext to use a static factory-function instead of a constructor.
/// You may specialize this template for your own component-types.
///
/// If you do so, you must define a call-operator with a pointer to your component as its return-type
/// and as many arguments as are needed to construct an instance.
///
/// For example, if you have a service-type `MyService` with an inaccessible constructor for which only a static factory-function `MyService::create()` exists,
/// you may define the corresponding service_factory like this:
///
///     template<> struct service_factory<MyService> {
///       MyService* operator()() const {
///         return MyService::create();
///       }
///     };
///
/// Should the service-type `MyService` have a dependency of type `QNetworkAccessManager` that must be supplied to the factory-function,
/// the corresponding service_factory would be defined like this this:
///
///     template<> struct service_factory<MyService> {
///       MyService* operator()(QNetworkAccessManager* networkManager) const {
///         return MyService::create(networkManager);
///       }
///     };
///
template<typename S> struct service_factory;



/**
* \brief Specifies the kind of a service-dependency.
* Will be used as a non-type argument to Dependency, when registering a service.
* The following table sums up the characteristics of each type of dependency:
* <table><tr><th>&nbsp;</th><th>Normal behaviour</th><th>What if no dependency can be found?</th><th>What if more than one dependency can be found?</th></tr>
* <tr><td>MANDATORY</td><td>Injects one dependency into the dependent service.</td><td>If the dependency-type has an accessible default-constructor, this will be used to register and create an instance of that type.
* <br>If no default-constructor exists, publication of the ApplicationContext will fail.</td>
* <td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* <tr><td>OPTIONAL</td><td>Injects one dependency into the dependent service</td><td>Injects `nullptr` into the dependent service.</td>
* td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
* <tr><td>N</td><td>Injects all dependencies of the dependency-type that have been registered into the dependent service, using a `QList`</td>
* <td>Injects an empty `QList` into the dependent service.</td>
* <td>See 'Normal behaviour'</td></tr>
* <tr><td>PRIVATE_COPY</td><td>Injects a newly created instance of the dependency-type and sets its `QObject::parent()` to the dependent service.</td>
* <td>If the dependency-type has an accessible default-constructor, this will be used to create an instance of that type.<br>
* If no default-constructor exists, publication of the ApplicationContext will fail.</td>
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
    /// All Objects with the required service_type will be pushed into QList
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

/**
 * @brief Specifies the strategy for looking up ServiceRegistrations in ApplicationContexts.
 * See QApplicationContext::getRegistration().
 */
enum class LookupKind {
    ///
    /// All Services whose registered service_type match the requested type, will be looked up.
    STATIC,

    ///
    /// All Services that are convertible to the requested type at runtime, will be looked up.
    DYNAMIC
};


///
/// \brief Specifies a dependency of a service.
/// Can by used as a type-argument for QApplicationContext::registerService().
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
///     context->registerService(Service<Reader>{Dependency<DatabaseAccess>{}}, "reader");
///
///     context->registerService(Service<Reader>{inject<DatabaseAccess>()}, "reader");
///
/// However, if your service can do without a `DatabaseAccess`, you should register it like this:
///
///     context->registerService(Service<Reader>{injectIfPresent<DatabaseAccess>()}, "reader");
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
///     context->registerService(Service<Reader>{injectAll<DatabaseAccess>()}, "reader");
///
///
template<typename S,Kind c=Kind::MANDATORY> struct Dependency {
    static_assert(detail::could_be_qobject<S>::value, "Dependency must be potentially convertible to QObject");
    ///
    /// \brief the required name for this dependency.
    /// The default-value is the empty String, with the implied meaning <em>"any dependency of the correct type may be used"</em>.
    ///
    QString requiredName;


};


///
/// \brief Injects a mandatory Dependency.
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \return a mandatory Dependency on the supplied type.
///
template<typename S> constexpr Dependency<S,Kind::MANDATORY> inject(const QString& requiredName = "") {
    return Dependency<S,Kind::MANDATORY>{requiredName};
}

///
/// \brief Injects a mandatory Dependency on a ServiceRegistration.
/// This function utilizes the Registration::registeredName() of the supplied registration.
/// \param registration the Registration of the dependency.
/// \return a mandatory Dependency on the supplied registration.
///
template<typename S> constexpr Dependency<S,Kind::MANDATORY> inject(const ServiceRegistration<S>& registration) {
    return Dependency<S,Kind::MANDATORY>{registration.registeredName()};
}





///
/// \brief Injects an optional Dependency to another ServiceRegistration.
/// This function will utilize the Registration::registeredName() to match the dependency.
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \return an optional Dependency on the supplied type.
///
template<typename S> constexpr Dependency<S,Kind::OPTIONAL> injectIfPresent(const QString& requiredName = "") {
    return Dependency<S,Kind::OPTIONAL>{requiredName};
}

///
/// \brief Injects an optional Dependency on a ServiceRegistration.
/// This function utilizes the Registration::registeredName() of the supplied registration.
/// \param registration the Registration of the dependency.
/// \return an optional Dependency on the supplied registration.
///
template<typename S> constexpr Dependency<S,Kind::OPTIONAL> injectIfPresent(const ServiceRegistration<S>& registration) {
    return Dependency<S,Kind::OPTIONAL>{registration.registeredName()};
}



///
/// \brief Injects a 1-to-N Dependency.
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \return a 1-to-N Dependency on the supplied type.
///
template<typename S> constexpr Dependency<S,Kind::N> injectAll(const QString& requiredName = "") {
    return Dependency<S,Kind::N>{requiredName};
}

///
/// \brief Injects a 1-to-N Dependency on a ServiceRegistration.
/// This function utilizes the Registration::registeredName() of the supplied registration.
/// \param registration the Registration of the dependency.
/// \return a 1-to-N  Dependency on the supplied registration.
///
template<typename S> constexpr Dependency<S,Kind::N> injectAll(const ServiceRegistration<S>& registration) {
    return Dependency<S,Kind::N>{registration.registeredName()};
}


///
/// \brief Injects a Dependency of type Cardinality::PRIVATE_COPY
/// \param the required name of the dependency. If empty, no name is required.
/// \return a Dependency of the supplied type that will create its own private copy.
///
template<typename S> constexpr Dependency<S,Kind::PRIVATE_COPY> injectPrivateCopy(const QString& requiredName = "") {
    return Dependency<S,Kind::PRIVATE_COPY>{requiredName};
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
///     Service<QIODevice,QFile> service{ resolve("${filename:readme.txt}") };
///
/// \param expression may contain placeholders in the format `${identifier}` or `${identifier:defaultValue}`.
/// \return a Resolvable instance for the supplied type.
///
template<typename S = QString> Resolvable<S> resolve(const QString& expression) {
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
///     Service<QIODevice,QFile> service{ resolve("${filename}", QString{"readme.txt"}) };
/// \param expression may contain placeholders in the format `${identifier}`.
/// \param defaultValue the value to use if the placeholder cannot be resolved.
/// \return a Resolvable instance for the supplied type.
///
template<typename S> Resolvable<S> resolve(const QString& expression, const S& defaultValue) {
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
/// \param section the `QSettings::group()` to be used.
/// \param autowire if `true`, the QApplicationContext will attempt to initialize all Q_PROPERTYs of `QObject*`-type with the corresponding services.
/// \param initMethod if not empty, will be invoked during publication of the service.
/// \return the service_config.
///
inline service_config make_config(std::initializer_list<std::pair<QString,QVariant>> properties = {}, const QString& group = "", bool autowire = false, const QString& initMethod = "") {
    return service_config{properties, group, autowire, initMethod};
}


namespace detail {



using constructor_t = std::function<QObject*(const QVariantList&)>;

constexpr int VALUE_KIND = 0x10;
constexpr int RESOLVABLE_KIND = 0x20;


struct dependency_info {
    const std::type_info& type;
    int kind;
    constructor_t defaultConstructor;
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


    QObject* create(const QVariantList& dependencies) const {
        return constructor ? constructor(dependencies) : nullptr;
    }

    bool matches(const std::type_index& type) const {
        return type == service_type || type == impl_type;
    }


    const std::type_info& service_type;
    const std::type_info& impl_type;
    const QMetaObject* meta_object = nullptr;
    constructor_t constructor = nullptr;
    std::vector<dependency_info> dependencies;
};

///
/// \brief Determines whether two service_descriptors are deemed equal.
/// two service_descriptors are deemed equal if their service_type, impl_type,
/// dependencies and config are all equal.
/// \param left
/// \param right
/// \return `true` if the service_descriptors are equal to each other.
///
inline bool operator==(const service_descriptor &left, const service_descriptor &right) {
    if (&left == &right) {
        return true;
    }
    return left.service_type == right.service_type &&
           left.impl_type == right.impl_type &&
           left.dependencies == right.dependencies;
 }





 template<typename S,int=sizeof(service_factory<S>)> constexpr bool hasServiceFactory(S*) {
    return true;
 }

 constexpr bool hasServiceFactory(void*) {
    return false;
 }


 template<typename S> constexpr bool has_service_factory = hasServiceFactory(static_cast<S*>(nullptr));





template<typename S> constructor_t get_default_constructor() {
    if constexpr(std::conjunction_v<std::is_base_of<QObject,S>,std::is_default_constructible<S>>) {
        return [](const QVariantList&) { return new S;};
    }
    return nullptr;
}




template <typename S>
struct dependency_helper {
    using type = S;

    static S convert(const QVariant& arg) {
        return arg.value<S>();
    }


    static dependency_info info(S dep) {
        return { typeid(S), VALUE_KIND, constructor_t{}, "", QVariant::fromValue(dep) };
    }

};

template <typename S,Kind kind>
struct dependency_helper<Dependency<S,kind>> {
    using type = S;

    static S* convert(const QVariant& arg) {
        return dynamic_cast<S*>(arg.value<QObject*>());
    }

    static dependency_info info(Dependency<S,kind> dep) {
        return { typeid(S), static_cast<int>(kind), get_default_constructor<S>(), dep.requiredName };
    }


};




template <typename S>
struct dependency_helper<Dependency<S, Kind::N>> {

    using type = S;


    static dependency_info info(Dependency<S,Kind::N> dep) {
        return { typeid(S), static_cast<int>(Kind::N), get_default_constructor<S>(), dep.requiredName };
    }
    static QList<S*> convert(const QVariant& arg) {
        return convertQList<S>(arg.value<QObjectList>());
    }
};

template <typename S>
struct dependency_helper<Resolvable<S>> {

    using type = S;


    static dependency_info info(Resolvable<S> dep) {
        return { typeid(S), RESOLVABLE_KIND, constructor_t{}, dep.expression, dep.defaultValue };
    }

    static S convert(const QVariant& arg) {
        return arg.value<S>();
    }
};



template<typename S> auto convert_arg(const QVariant& arg) {
    return dependency_helper<S>::convert(arg);
}






template<typename First, typename...Tail> void make_dependencies(std::vector<dependency_info>& target, First first, Tail...tail) {
    target.push_back(dependency_helper<First>::info(first));
    if constexpr(sizeof...(Tail) > 0) {
        make_dependencies<Tail...>(target, tail...);
    }
}



template<typename... Dep> std::vector<dependency_info> dependencies(Dep...dep) {
    std::vector<dependency_info> result;
    if constexpr(sizeof...(Dep) > 0) {
        make_dependencies<Dep...>(result, dep...);
    }
    return result;
}



template <typename T> constructor_t service_creator() {
    return [](const QVariantList &dependencies) {
        if constexpr(detail::has_service_factory<T>) {
            return service_factory<T>{}();
        } else {
            return new T;
        }
    };
}

template <typename T, typename D1> constructor_t service_creator() {
        return [](const QVariantList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(convert_arg<D1>(dependencies[0]));
            } else {
                return new T{ convert_arg<D1>(dependencies[0]) };
            }
        };
    }

template <typename T, typename D1, typename D2>
    constructor_t service_creator() {
        return [](const QVariantList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(convert_arg<D1>(dependencies[0]), convert_arg<D2>(dependencies[1]));
            } else {
                return new T{ convert_arg<D1>(dependencies[0]), convert_arg<D2>(dependencies[1]) };
            }
        };
    }

template <typename T, typename D1, typename D2, typename D3>
    constructor_t service_creator() {
        return [](const QVariantList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(
                        convert_arg<D1>(dependencies[0]),
                        convert_arg<D2>(dependencies[1]),
                        convert_arg<D3>(dependencies[2]));
            } else {
            return new T{
                         convert_arg<D1>(dependencies[0]),
                         convert_arg<D2>(dependencies[1]),
                         convert_arg<D3>(dependencies[2])
                        };
            }
        };
    }

template <typename T, typename D1, typename D2, typename D3, typename D4>
    constructor_t service_creator() {
        return [](const QVariantList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(
                        convert_arg<D1>(dependencies[0]),
                        convert_arg<D2>(dependencies[1]),
                        convert_arg<D3>(dependencies[2]),
                        convert_arg<D4>(dependencies[3]));
            } else {
            return new T{
                         convert_arg<D1>(dependencies[0]),
                         convert_arg<D2>(dependencies[1]),
                         convert_arg<D3>(dependencies[2]),
                         convert_arg<D4>(dependencies[3])
                     };
            }
        };
    }

template <typename T, typename D1, typename D2, typename D3, typename D4, typename D5>
    constructor_t service_creator() {
        return [](const QVariantList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(convert_arg<D1>(dependencies[0]),
                        convert_arg<D2>(dependencies[1]),
                        convert_arg<D3>(dependencies[2]),
                        convert_arg<D4>(dependencies[3]),
                        convert_arg<D5>(dependencies[4]));
            } else
            return new T{
                             convert_arg<D1>(dependencies[0]),
                             convert_arg<D2>(dependencies[1]),
                             convert_arg<D3>(dependencies[2]),
                             convert_arg<D4>(dependencies[3]),
                             convert_arg<D5>(dependencies[4])
                         }; };
    }

template <typename T, typename D1, typename D2, typename D3, typename D4, typename D5, typename D6>
constructor_t service_creator() {
        return [](const QVariantList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(convert_arg<D1>(dependencies[0]),
                                            convert_arg<D2>(dependencies[1]),
                                            convert_arg<D3>(dependencies[2]),
                                            convert_arg<D4>(dependencies[3]),
                                            convert_arg<D5>(dependencies[4]),
                                            convert_arg<D6>(dependencies[5]));
            } else
                return new T{
                    convert_arg<D1>(dependencies[0]),
                    convert_arg<D2>(dependencies[1]),
                    convert_arg<D3>(dependencies[2]),
                    convert_arg<D4>(dependencies[3]),
                    convert_arg<D5>(dependencies[4]),
                    convert_arg<D6>(dependencies[5])
                }; };
}

using q_predicate_t = std::function<bool(QObject*)>;

template<typename S,LookupKind lookup> struct predicate_traits;

template<typename S> struct predicate_traits<S,LookupKind::STATIC> {
        static constexpr std::nullptr_t predicate() {
            return nullptr;
        }
};

template<typename S> struct predicate_traits<S,LookupKind::DYNAMIC> {
        static q_predicate_t predicate() {
            return [](QObject* ptr) { return dynamic_cast<S*>(ptr) != nullptr;};
        }
};


} // namespace detail

///
/// \brief Describes a service by its interface and implementation.
/// Compilation will fail if either `Srv` is not a sub-class of QObject, or if `Impl` is not a sub-class of `Srv`.

/// You may supply arbitrary arguments to Service' constructor. Those arguments will be passed on to the
/// constructor of the actual service when the QApplicationContext is published.
/// Example with no arguments:
///
///    context->registerService(Service<QIODevice,QFile>{QString{"readme.txt"}, "file");
///
///
template<typename Srv,typename Impl=Srv> struct Service {
    static_assert(std::is_base_of_v<QObject,Impl>, "Implementation-type must be a subclass of QObject");

    static_assert(std::is_base_of_v<Srv,Impl>, "Implementation-type must be a subclass of Service-type");

    using service_type = Srv;

    using impl_type = Impl;

    template<typename...Dep> Service(Dep...deps) : descriptor{typeid(Srv), typeid(Impl), &Impl::staticMetaObject} {
        descriptor.dependencies = detail::dependencies(deps...);
        descriptor.constructor = detail::service_creator<Impl,Dep...>();
    }


    detail::service_descriptor descriptor;
};








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
    /// \param serviceDeclaration denotes the Service.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type. If you want to distinguish the service-type from the implementation-type, you should supply a Service here.
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
        return registerService(Service<S>{}, objectName, config);
    }





    ///
    /// \brief Registers an object with this ApplicationContext.
    /// The object will immediately be published.
    /// You can either let the compiler's template-argument deduction figure out the servicetype `<S>` for you,
    /// or you can supply it explicitly, if it differs from the static type of the object.
    /// \param obj must be non-null. Also, must be convertible to QObject.
    /// \param objName the name for this Object in the ApplicationContext.
    /// *Note*: this name will not be set as the QObject::objectName(). It will be the internal name within the ApplicationContext only.
    /// \tparam S the service-type for the object.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> ServiceRegistration<S> registerObject(S* obj, const QString& objName = "") {
        static_assert(detail::could_be_qobject<S>::value, "Object is not convertible to QObject");
        return ServiceRegistration<S>::wrap(registerObject(objName, dynamic_cast<QObject*>(obj), service_descriptor{typeid(S), typeid(*obj)}));
    }

    ///
    /// \brief Obtains a ServiceRegistration for a service-type and name.
    /// <br>This function will look up Services by the names they were registered with.
    /// Additionally, it will look up any alias that might have been given, using ServiceRegistration::registerAlias(const QString&).
    /// \tparam S the required service-type.
    /// \param name the desired name of the registration.
    /// A valid ServiceRegistration will be returned only if exactly one Service that matches the name has been registered.
    /// \return a ServiceRegistration for the required type and name. If no single Service with a matching name and service-type could be found,
    /// an invalid ServiceRegistration will be returned.
    ///
    template<typename S> [[nodiscard]] ServiceRegistration<S> getRegistration(const QString& name) const {
        static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");
        return ServiceRegistration<S>::wrap(getRegistration(typeid(S), name));
    }

    ///
    /// \brief Obtains a ProxyRegistration for a service-type.
    /// <br>In contrast to the ServiceRegistration that is returned by registerService(),
    /// the ProxyRegistration returned by this function is actually a Proxy.<br>
    /// This Proxy manages all Services of the requested type, regardless of whether they have been registered prior
    /// to invoking getRegistration().<br>
    /// This means that if you subscribe to it using Registration::subscribe(), you will be notified
    /// about all published services that match the Service-type.<br>
    /// <table><tr><th>LookupKind</th><th>Type of managed Services</th></tr>
    /// <tr><td><tt>LookupKind::STATIC</tt></td><td>Those Services whose Registration::service_type() matches
    /// the requested <tt>typeid(S)</tt></td>
    /// </tr>
    /// <tr><td><tt>LookupKind::DYNAMIC</tt></td><td>Those Services that can be converted to the requested type using <tt>dynamic_cast&lt;S*&gt;()</tt>.</td>
    /// </tr>
    /// </table>
    /// \tparam S the required service-type.
    /// \tparam lookup determines the strategy for retrieving the ServiceRegistration-Proxy. See the table above for details!
    /// \return a ProxyRegistration that corresponds to all registration that match the service-type.
    ///
    template<typename S,LookupKind lookup = LookupKind::STATIC> [[nodiscard]] ProxyRegistration<S> getRegistration() const {
        static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");
        return ProxyRegistration<S>::wrap(getRegistration(typeid(S), lookup, detail::predicate_traits<S,lookup>::predicate(), detail::getMetaObject<S>()));
    }

    /**
     * @brief Obtains a List of all Services that have been registered.
     * <br>The element-type of the returned QList is the opaque type service_registration_handle_t.
     * You should not de-reference it, as its API may changed without notice.
     * <br>
     * What you can do, though, is use one of the free functions service_type(registration_handle_t),
     * registeredName(service_registration_handle_t), registeredProperties(service_registration_handle_t).
     * <br>Additionally, you may wrap the handles in a type-safe manner, using ServiceRegistration::wrap(service_registration_handle_t).
     * @return a List of all Services that have been registered.
     */
    virtual QList<service_registration_handle_t> getRegistrationHandles() const = 0;




    ///
    /// \brief Publishes this ApplicationContext.
    /// This method may be invoked multiple times.
    /// Each time it is invoked, it will attempt to instantiate all yet-unpublished services that have been registered with this ApplicationContext.
    /// \param allowPartial has the default-value `false`, this function will return immediately when a service cannot be published (due to missing dependencies,
    /// unresolveable properties, etc...).
    /// Additionally, the cause of such a failure will be logged with the level QtMsgType::QtCriticalMessage.
    /// if `allowPartial == true`, the function will attempt to publish as many pending services as possible.
    /// Failures that may be fixed by further registrations will be logged with the level QtMsgType::QtWarningMessage.
    /// \return `true` if there are no fatal errors and all services were published (in case `allowPartial == false`),
    /// or at least one service was published (in case `allowPartial == true`).
    ///
    virtual bool publish(bool allowPartial = false) = 0;



    ///
    /// \brief The number of published services.
    /// \return The number of published services.
    ///
    virtual unsigned published() const = 0;

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

    using q_predicate_t = detail::q_predicate_t;


    ///
    /// \brief Standard constructor.
    /// \param parent the optional parent of this QApplicationContext.
    ///
    explicit QApplicationContext(QObject* parent = nullptr);


    ///
    /// \brief Registers a service with this QApplicationContext.
    /// \param name
    /// \param descriptor
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
    /// \param service_type
    /// \param name the desired name of the service.
    /// A valid Registration will be returned only if exactly one Service of the requested type has been registered that matches
    /// the name.
    /// \return a Registration for the supplied service_type, or `nullptr` if no single Service with the requested type and matching name
    /// has been registered.
    ///
    virtual service_registration_handle_t getRegistration(const std::type_info& service_type, const QString& name) const = 0;

    ///
    /// \brief Obtains a Registration for a service_type.
    /// \param service_type the service-type to match the registrations.
    /// \param dynamicCheck This optional parameter is used to check whether a Service is actually an instance of the service_type.
    /// If `nullptr` is passed, this function will obtain only those Registrations where the registered service_type matches.
    /// \param metaObject the static QMetaObject for the type. If not available, `nullptr` can be passed.
    /// \return a Registration for the supplied service_type.
    ///
    virtual proxy_registration_handle_t getRegistration(const std::type_info& service_type, LookupKind lookup, q_predicate_t dynamicCheck, const QMetaObject* metaObject) const = 0;


    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// If you are implementing registerService(const QString&, service_descriptor*) and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    ///
    /// \param appContext the target on which to invoke registerService(const QString&, service_descriptor*).
    /// \param name
    /// \param descriptor
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
    /// If you are implementing getRegistration(const std::type_info&) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    ///
    /// \param appContext the target on which to invoke getRegistration(const std::type_info&) const.
    /// \param service_type
    /// \return the result of getRegistration(const std::type_info&,const QString&) const.
    ///
    static service_registration_handle_t delegateGetRegistration(const QApplicationContext& appContext, const std::type_info& service_type, const QString& name) {
        return appContext.getRegistration(service_type, name);
    }

    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// If you are implementing getRegistration(const std::type_info&) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    ///
    /// \param appContext the target on which to invoke getRegistration(const std::type_info&) const.
    /// \param service_type
    /// \return the result of getRegistration(const std::type_info&) const.
    ///
    static proxy_registration_handle_t delegateGetRegistration(const QApplicationContext& appContext, const std::type_info& service_type, LookupKind lookup, q_predicate_t dynamicCheck, const QMetaObject* metaObject) {
        return appContext.getRegistration(service_type, lookup, dynamicCheck, metaObject);
    }



    template<typename S> friend class ServiceRegistration;
};

template<typename S> template<typename D,typename R> Subscription Registration<S>::autowire(R (S::*injectionSlot)(D*)) {
    static_assert(detail::could_be_qobject<D>::value, "Service-type to be injected must be possibly convertible to QObject");
    if(!registrationHolder || !injectionSlot) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot autowire " << *this;
        return Subscription{};
    }
    auto subscription = new AutowireSubscription<D,R>{unwrap(), injectionSlot, applicationContext()->template getRegistration<D>().unwrap()};
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
