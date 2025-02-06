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

struct service_config;


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
/// serviceConfig(service_registration_handle_t).
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


Q_DECLARE_LOGGING_CATEGORY(defaultLoggingCategory)




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

// We cannot make use of service_scope_traits here, since we also want to test this at runtime:
constexpr bool is_binding_source(ServiceScope scope) {
    return scope != ServiceScope::TEMPLATE;
}

#ifdef __cpp_lib_remove_cvref
template<typename T> using remove_cvref_t = std::remove_cvref_t<T>;
#else
template<typename T> using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;
#endif

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

using q_init_t = std::function<void(QObject*,QApplicationContext*)>;

using q_variant_converter_t = std::function<QVariant(const QString&)>;

struct service_descriptor;



template<typename T> auto hasQVariantSupport(T* ptr) -> decltype(QVariant{*ptr});

void hasQVariantSupport(void*);


template<typename T> constexpr bool has_qvariant_support = std::is_same_v<decltype(hasQVariantSupport(static_cast<T*>(nullptr))),QVariant>;

inline void convertVariant(QVariant& var, q_variant_converter_t converter) {
    if(converter) {
        var = converter(var.toString());
    }
}



template<typename T> struct default_string_converter {
    T operator()(const QString& str) const {
        return T{str};
    }
};



template<typename T,bool=has_qvariant_support<T> || std::is_convertible_v<T,QObject*>> struct variant_converter_traits;


template<typename T> struct variant_converter_traits<T,true> {
    using type = std::nullptr_t;

    static constexpr std::nullptr_t makeConverter(std::nullptr_t = nullptr) {
        return nullptr;
    }


};

template<typename T> struct variant_converter_traits<T,false> {
    using type = default_string_converter<T>;


    template<typename C=type> static q_variant_converter_t makeConverter(C converter = C{}) {
        static_assert(std::is_convertible_v<std::invoke_result_t<C,QString>,T>, "return-type of converter does not match");
        return [converter](const QString& str) { return QVariant::fromValue(converter(str));};
    }

};



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



template<typename T> struct qvariant_cast {
    T operator()(const QVariant& v) const {
        return v.value<T>();
    }
};

template<typename T> struct qvariant_cast<QList<T*>> {
    QList<T*> operator()(const QVariant& v) const {
        return convertQList<T>(v.value<QObjectList>());
    }
};



template<typename S> struct callable_adapter {

    template<typename A,typename R> static q_setter_t adaptSetter(R (S::*setter)(A)) {
        if(!setter) {
            return nullptr;
        }
        using arg_type = detail::remove_cvref_t<A>;
        return [setter](QObject* obj,QVariant arg) {
            if(S* ptr = dynamic_cast<S*>(obj)) {
                (ptr->*setter)(qvariant_cast<arg_type>{}(arg));
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
    [[nodiscard]] virtual const QMetaObject* serviceMetaObject() const = 0;


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
class BasicSubscription : public Subscription {
public:

    explicit BasicSubscription(QObject* parent = nullptr) : Subscription{parent} {

    }

    template<typename S,typename F> void connectOut(S* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection)
    {
        static_assert(std::is_base_of_v<QObject,S>, "Context must be derived from QObject");
        out_connection = QObject::connect(this, &Subscription::objectPublished, context, callable, connectionType);
    }


    void cancel() override;

    void connectTo(registration_handle_t source) override;


private:
    QMetaObject::Connection out_connection;
    QMetaObject::Connection in_connection;
};



///
/// \brief A subscription that connects multiple services.
///
class MultiServiceSubscription : public BasicSubscription {
    Q_OBJECT
public:

    using target_list_t = QList<registration_handle_t>;

signals:
    void objectsPublished(const QObjectList&);

protected:
    explicit MultiServiceSubscription(const target_list_t& targets, QObject* parent);


    void cancel() override;

    virtual MultiServiceSubscription* newChild(const target_list_t& targets) = 0;

    virtual QMetaObject::Connection connectObjectsPublished() = 0;

private:

    void onObjectPublished(QObject*);

    void onLastObjectPublished(QObject*);

    const target_list_t m_targets;
    QObjectList m_boundObjects;
    QList<QPointer<Subscription>> m_children;
    QMetaObject::Connection m_objectsPublishedConnection;
};



template<typename S,typename SIG,typename T,typename SLT> class ConnectionSubscription : public MultiServiceSubscription {
public:


    ConnectionSubscription(const target_list_t& targets, SIG theSignal, SLT theSlot, Qt::ConnectionType connectionType, QObject* parent) :
        MultiServiceSubscription{targets, parent},
        m_signal{theSignal},
        m_slot{theSlot},
        m_connectionType{connectionType} {
    }

protected:

    virtual MultiServiceSubscription* newChild(const target_list_t& targets) override {
        return new ConnectionSubscription<S,SIG,T,SLT>{targets, m_signal, m_slot, m_connectionType, this};
    }

    virtual QMetaObject::Connection connectObjectsPublished() override {
        return connect(this, &MultiServiceSubscription::objectsPublished, this, &ConnectionSubscription::notify);
    }

    virtual void cancel() override {
        for(auto& connection : connections) {
            QObject::disconnect(connection);
        }
        MultiServiceSubscription::cancel();
    }

private:
    void notify(const QObjectList& targets) {
        if(S* s = dynamic_cast<S*>(targets[0])) {
            if(T* t = dynamic_cast<T*>(targets[1])) {
                connections.push_back(connect(s, m_signal, t, m_slot, m_connectionType));
            }
        }
    }



    SIG m_signal;
    SLT m_slot;
    Qt::ConnectionType m_connectionType;
    QList<QMetaObject::Connection> connections;
};

template<typename F,typename...S> struct notifier;

template<typename F,typename S1,typename S2> struct notifier<F,S1,S2> {
    F callable;

    void operator()(const QObjectList& objs) {
        if(S1* s = dynamic_cast<S1*>(objs[0])) {
            if(S2* t = dynamic_cast<S2*>(objs[1])) {
                callable(s, t);
            }
        }
    }
};

template<typename F,typename S1,typename S2,typename S3> struct notifier<F,S1,S2,S3> {
    F callable;

    void operator()(const QObjectList& objs) {
        if(S1* s = dynamic_cast<S1*>(objs[0])) {
            if(S2* t = dynamic_cast<S2*>(objs[1])) {
                if(S3* u = dynamic_cast<S3*>(objs[2])) {
                    callable(s, t, u);
                }
            }
        }
    }
};

template<typename F,typename S1,typename S2,typename S3,typename S4> struct notifier<F,S1,S2,S3,S4> {
    F callable;

    void operator()(const QObjectList& objs) {
        if(S1* s = dynamic_cast<S1*>(objs[0])) {
            if(S2* t = dynamic_cast<S2*>(objs[1])) {
                if(S3* u = dynamic_cast<S3*>(objs[2])) {
                    if(S4* w = dynamic_cast<S4*>(objs[3])) {
                        callable(s, t, u, w);
                    }
                }
            }
        }
    }
};

template<typename F,typename S1,typename S2,typename S3,typename S4,typename S5> struct notifier<F,S1,S2,S3,S4,S5> {
    F callable;

    void operator()(const QObjectList& objs) {
        if(S1* s = dynamic_cast<S1*>(objs[0])) {
            if(S2* t = dynamic_cast<S2*>(objs[1])) {
                if(S3* u = dynamic_cast<S3*>(objs[2])) {
                    if(S4* w = dynamic_cast<S4*>(objs[3])) {
                        if(S5* x = dynamic_cast<S5*>(objs[4])) {
                            callable(s, t, u, w, x);
                        }
                    }
                }
            }
        }
    }
};

template<typename F,typename...S> class CombiningSubscription : public MultiServiceSubscription {
public:

    CombiningSubscription(const target_list_t& targets, QObject* context, F callable, Qt::ConnectionType connectionType) :
        MultiServiceSubscription{targets, context},
        m_context{context},
        m_notifier{callable},
        m_connectionType{connectionType} {
    }

protected:

    virtual MultiServiceSubscription* newChild(const target_list_t& targets) override {
        return new CombiningSubscription<F,S...>{targets, m_context, m_notifier.callable, m_connectionType};
    }

    virtual QMetaObject::Connection connectObjectsPublished() override {
        return connect(this, &MultiServiceSubscription::objectsPublished, m_context, [this](const QObjectList& objs) {
            m_notifier(objs);
        }, m_connectionType);
    }

    void notify(const QObjectList& objs);



private:

    QObject* m_context;
    notifier<F,S...> m_notifier;
    Qt::ConnectionType m_connectionType;
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

    Q_PROPERTY(ServiceScope scope READ scope CONSTANT)

    ///
    /// \brief The name of this Registration.
    /// This property will yield the name that was passed to QApplicationContext::registerService(),
    /// or the synthetic name that was assigned by the ApplicationContext.
    /// \return the name of this Registration.
    ///
    [[nodiscard]] virtual QString registeredName() const = 0;

    /**
     * @brief The configuration that was supplied upon registration.
     * @return The configuration that was supplied upon registration.
     */
    [[nodiscard]] virtual const service_config& config() const = 0;

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

    QMetaProperty findPropertyBySignal(const QMetaMethod& signalFunction, const QMetaObject* metaObject, const QLoggingCategory& loggingCatgegory);

///
/// \brief Yields the name of a *private property*, incorporating some binary data.
/// \return a String starting with a dot and comprising a representation of the supplied binary data.
///
    QString uniquePropertyName(const void*, std::size_t);


    ///
    /// \brief The return-type of mcnepp::qtdi::injectParent().
    /// This is an empty struct. It serves as a 'type-tag' for which there
    /// is a specialization of the template mcnepp::qtdi::detail::dependency_helper.
    /// That specialization will create a dependency_info with mcnepp::qtdi::detail::PARENT_PLACEHOLDER_KIND.
    ///
    struct ParentPlaceholder {
    };

    enum class ConfigValueType {
        DEFAULT,
        AUTO_REFRESH_EXPRESSION,
        SERVICE,
        SERVICE_LIST
    };

    struct ConfigValue {
        QVariant expression;
        ConfigValueType configType = ConfigValueType::DEFAULT;
        q_setter_t propertySetter = nullptr;
        q_variant_converter_t variantConverter = nullptr;
    };


    inline bool operator==(const ConfigValue& left, const ConfigValue& right) {
        //Two ConfigValues shall be deemed equal when the expressions and configTypes are equal. We deliberately ignore the function-pointer-members.
        return left.expression == right.expression && left.configType == right.configType;
    }

    ///
    /// \brief A type-safe configuration-entry.
    /// <br>This is an intermediate type that will be used solely to populate a ServiceConfig.
    ///
    template<typename T> struct service_config_entry_t {
        QString name;
        detail::ConfigValue value;
    };


#ifdef __GNUG__
    QString demangle(const char*);

    inline QString type_name(const std::type_info& info) {
        return demangle(info.name());
    }

    inline QString type_name(const std::type_index& info) {
        return demangle(info.name());
    }
#else
    inline const char* type_name(const std::type_info& info) {
        return info.name();
    }

    inline const char* type_name(const std::type_index& info) {
        return info.name();
    }
#endif



    ///
    /// \brief Checks whether the current thread is the thread assigned to a QObject.
    /// <br>This function allows it to reject some functions if they are not invoked
    /// from the thread belonging to the QApplicationContext, without the need to #include<QThread>.
    /// \return the result of the expression `obj && obj->thread() == QThread::currentThread()`.
    ///
    bool hasCurrentThreadAffinity(QObject* obj);

}// end namespace detail



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
/// \brief Obtains the QLoggingCategory from a handle to a Registration.
/// \param handle the handle to the Registration.
/// \return the QLoggingCategory of the associated QApplicationContext, if the handle is valid. Otherwise, mcnepp:qtdi::defaultLoggingCategory()
///
[[nodiscard]] const QLoggingCategory& loggingCategory(registration_handle_t handle);


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
/// \brief Obtains the service configuration from a handle to a ServiceRegistration.
/// \param handle the handle to the ServiceRegistration.
/// \return the service configuration from that this ServiceRegistration was registered with, or an empty service_config if the handle is not valid.
///
[[nodiscard]] const service_config& serviceConfig(service_registration_handle_t handle);



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
    /// \return the Subscription if `this->isValid()`
    ///
    template<typename F> std::enable_if_t<std::is_invocable_v<F,S*>,Subscription> subscribe(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        if(!registrationHolder || !context) {
            qCCritical(loggingCategory(unwrap())).noquote().nospace() << "Cannot subscribe to " << *this;
            return Subscription{};
        }

        auto subscription = new detail::BasicSubscription{context};
        subscription->connectOut(context, [callable](QObject* obj) {
                if(S* srv = dynamic_cast<S*>(obj)) {
                    callable(srv);
                }
        }, connectionType);
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
    /// \return  the Subscription if `this->isValid() && target != nullptr && setter != nullptr`
    ///
      template<typename T,typename R> std::enable_if_t<std::is_base_of_v<QObject,T>,Subscription> subscribe(T* target, R (T::*setter)(S*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        if(!setter || !target) {
            qCCritical(loggingCategory(unwrap())).noquote().nospace() << "Cannot subscribe to " << *this << " with null";
            return Subscription{};
        }
        return subscribe(target, std::bind(std::mem_fn(setter), target, std::placeholders::_1), connectionType);
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
    /// for the type `<D>`, an invalid Subscription will be returned.
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

    explicit Registration(QPointer<detail::Registration>&& reg) : registrationHolder{std::move(reg)}
    {
    }


    ~Registration() {

    }

    QPointer<detail::Registration> release() {
        return std::move(registrationHolder);
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

    template<typename T,ServiceScope TSCP> friend class ServiceRegistration;

public:
    using service_type = S;

    static constexpr ServiceScope Scope = SCP;

    [[nodiscard]] QString registeredName() const {
        return mcnepp::qtdi::registeredName(unwrap());
    }

    [[nodiscard]] const service_config& config() const {
        return mcnepp::qtdi::serviceConfig(unwrap());
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
    template<typename U,ServiceScope newScope=SCP> [[nodiscard]] ServiceRegistration<U,newScope> as() const& {
        if constexpr(std::is_same_v<U,S> && newScope == SCP) {
            return *this;
        } else {
            static_assert(SCP == newScope || SCP == ServiceScope::UNKNOWN || newScope == ServiceScope::UNKNOWN, "Either current scope or new scope must be UNKNOWN");
            return ServiceRegistration<U,newScope>::wrap(unwrap());
        }
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
    template<typename U,ServiceScope newScope=SCP> [[nodiscard]] ServiceRegistration<U,newScope> as() && {
        if constexpr(std::is_same_v<U,S> && newScope == SCP) {
            return std::move(*this);
        } else {
            static_assert(SCP == newScope || SCP == ServiceScope::UNKNOWN || newScope == ServiceScope::UNKNOWN, "Either current scope or new scope must be UNKNOWN");
            return ServiceRegistration<U,newScope>::unsafe_wrap(Registration<S>::release());
        }
    }


    /**
     * @brief operator Implicit conversion to a ServiceRegistration with ServiceScope::UNKNOWN.
     */
    operator ServiceRegistration<S,ServiceScope::UNKNOWN>() const& {
        if constexpr(SCP == ServiceScope::UNKNOWN) {
            return *this;
        } else {
            return ServiceRegistration<S,ServiceScope::UNKNOWN>::wrap(unwrap());
        }
    }

    /**
     * @brief operator Implicit conversion to a ServiceRegistration with ServiceScope::UNKNOWN.
     */
    operator ServiceRegistration<S,ServiceScope::UNKNOWN>()&& {
        if constexpr(SCP == ServiceScope::UNKNOWN) {
            return std::move(*this);
        } else {
            return ServiceRegistration<S,ServiceScope::UNKNOWN>::unsafe_wrap(Registration<S>::release());
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
            qCCritical(loggingCategory(unwrap())).noquote().nospace() << "Cannot register alias '" << alias << "' for " << *this;
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
        return unsafe_wrap(QPointer<detail::Registration>{handle});
    }




private:
    explicit ServiceRegistration(service_registration_handle_t reg) : Registration<S>{reg} {

    }

    explicit ServiceRegistration(QPointer<detail::Registration>&& reg) : Registration<S>{std::move(reg)} {

    }

    [[nodiscard]] static ServiceRegistration<S,SCP> unsafe_wrap(QPointer<detail::Registration>&& handle) {
        if(mcnepp::qtdi::matches<S>(handle.get())) {
            if constexpr(SCP == ServiceScope::UNKNOWN) {
                return ServiceRegistration<S,SCP>{std::move(handle)};
            } else {
                //We assume that the handle is actually a service_registration_handle_t:
                if(static_cast<service_registration_handle_t>(handle.get())->scope() == SCP) {
                    return ServiceRegistration{std::move(handle)};
                }
            }
        }
        return ServiceRegistration{};
    }


    Subscription bind(const char* sourceProperty, registration_handle_t target, const detail::property_descriptor& descriptor) const {
        static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
        static_assert(detail::service_scope_traits<SCP>::is_binding_source, "The scope of the service does not permit binding");

        if(!target || !*this) {
            qCCritical(loggingCategory(unwrap())).noquote().nospace() << "Cannot bind " << *this << " to " << target;
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

    template<typename U> friend class ProxyRegistration;

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
        return unsafe_wrap(QPointer<detail::Registration>{handle});
    }

    template<typename U> [[nodiscard]] ProxyRegistration<U> as() const& {
        if constexpr(std::is_same_v<U,S>) {
            return *this;
        } else {
            return ProxyRegistration<U>::wrap(unwrap());
        }
    }

    template<typename U> [[nodiscard]] ProxyRegistration<U> as() && {
        if constexpr(std::is_same_v<U,S>) {
            return std::move(*this);
        } else {
            return ProxyRegistration<U>::unsafe_wrap(Registration<S>::release());
        }
    }


private:

    [[nodiscard]] static ProxyRegistration<S> unsafe_wrap(QPointer<detail::Registration>&& handle) {
        //This function assumes that the handle is actually a proxy_registration_handle_t
        if(matches<S>(handle.get())) {
            return ProxyRegistration<S>{std::move(handle)};
        }
        return ProxyRegistration{};
    }

    explicit ProxyRegistration(proxy_registration_handle_t reg) : Registration<S>{reg} {

    }

    explicit ProxyRegistration(QPointer<detail::Registration>&& reg) : Registration<S>{std::move(reg)} {

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
template<typename S,typename T,ServiceScope scope> Subscription bind(const ServiceRegistration<S,scope>& source, const char* sourceProperty, Registration<T>& target, const char* targetProperty) {
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
template<typename S,typename T,typename A,typename R,ServiceScope scope> Subscription bind(const ServiceRegistration<S,scope>& source, const char* sourceProperty, Registration<T>& target, R(T::*setter)(A)) {
    if(!setter) {
        qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Cannot bind " << source << " to null";
        return Subscription{};
    }
    return source.bind(sourceProperty, target.unwrap(), {detail::uniquePropertyName(&setter, sizeof setter).toLatin1(), detail::callable_adapter<T>::adaptSetter(setter)});
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
template<typename S,typename T,typename AS,typename AT,typename R,ServiceScope scope> auto bind(const ServiceRegistration<S,scope>& source, void(S::*signalFunction)(AS), Registration<T>& target, R(T::*setter)(AT)) ->
    std::enable_if_t<std::is_convertible_v<AS,AT>,Subscription> {
    if(!setter || !signalFunction || !source || !target) {
        qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Cannot bind " << source << " to target";
        return Subscription{};
    }
    if(auto signalProperty = detail::findPropertyBySignal(QMetaMethod::fromSignal(signalFunction), source.serviceMetaObject(), loggingCategory(source.unwrap())); signalProperty.isValid()) {
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
template<typename S,typename T,typename A,typename R,ServiceScope scope> Subscription bind(const ServiceRegistration<S,scope>& source, void(S::*signalFunction)(), Registration<T>& target, R(T::*setter)(A)) {
    if(!setter || !signalFunction || !source || !target) {
        qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Cannot bind " << source << " to " << target;
        return Subscription{};
    }
    if(auto signalProperty = detail::findPropertyBySignal(QMetaMethod::fromSignal(signalFunction), source.serviceMetaObject(), loggingCategory(source.unwrap())); signalProperty.isValid()) {
        return bind(source, signalProperty.name(), target, setter);
    }
    return Subscription{};
}




///
/// \brief Connects a signal of one Service to a slot of another service.
/// <br>This function is the ApplicationContext-aware equivalent to QObject::connect().
/// Instead of supplying the QObjects for source and target, you supply the Registrations instead.
/// <br>Whenever an instance of the source-service is published, it will subscribe to the publication of the target-service.
/// Once the target-service is published, the connection of the sourceSignal with the targetSlot will take place.
/// <br>In case source and target represent the same Registration, the connection will still take place.
/// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
/// \param source the registration of the source-service.
/// \param sourceSignal will be passed as the second argument to QObject::connect().
/// \param target the registration of the source-service.
/// \param targetSlot will be passed as the fourth argument to QObject::connect().
/// \param connectionType will be passed as the last argument to QObject::connect() when the actual connection of the signal with the slot will be made.
/// \return a Subscription. Cancelling this Subscription will disconnect any connections that have already been made between the source-service
/// and the target-service.
///
template<typename S,typename SIG,typename T,typename SLT> Subscription connectServices(Registration<S>& source, SIG sourceSignal, Registration<T>& target, SLT targetSlot, Qt::ConnectionType connectionType = Qt::AutoConnection) {
static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
    static_assert(std::is_base_of_v<QObject,T>, "Target must be derived from QObject");
    if(!source || !target) {
        qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Cannot connect " << source << " to " << target;
        return Subscription{};
    }
    if(!detail::hasCurrentThreadAffinity(source.applicationContext())) {
        qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Wrong thread for connecting " << source << " to " << target;
        return Subscription{};
    }
    auto subscription = new detail::ConnectionSubscription<S,SIG,T,SLT>{QList<registration_handle_t>{target.unwrap()}, sourceSignal, targetSlot, connectionType, target.unwrap()};
    return Subscription{source.unwrap()->subscribe(subscription)};
}

///
/// \brief A combination of Services that can be subscribed.
/// <br>An instance of this type will be returned by mcnepp::qtdi::combine().
/// <br>This type is neither copyable nor moveable.
/// <br>It is meant to be treated solely as a temporary object, on which ServiceCombination::subscribe() shall be invoked.
///
template<typename...S> class ServiceCombination {
    static_assert(sizeof...(S) > 1, "A ServiceCombination must combine at least two services!");

public:
    ServiceCombination(const ServiceCombination&) = delete;
    ServiceCombination(ServiceCombination&&) = delete;
    ServiceCombination& operator=(const ServiceCombination&) = delete;
    ServiceCombination& operator=(ServiceCombination&&) = delete;

    explicit ServiceCombination(Registration<S>&... registrations) {
        if(!add(registrations...)) {
            qCCritical(defaultLoggingCategory()).noquote().nospace() << "Cannot combine invalid Registrations";
        }
    }

    ///
    /// \brief Subscribes to this ServiceCombination.
    /// <br>**Note:** the callable will be invoked once for *every distinct combination of services*.
    /// In particular, if one of the supplied Registrations was of ServiceScope::PROXY, the callable will be invoked
    /// at least as many times as there are instances of the service-type.
    /// \param context
    /// \param callable will be invoked once for *every distinct combination of services*.
    /// \param connectionType
    /// \return a valid Subscription if all of the supplied Registrations are valid.
    ///
    template<typename F> std::enable_if_t<std::is_invocable_v<F,S*...>,Subscription> subscribe(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection)&& {
        if(m_registrations.empty()) {
            return Subscription{};
        }
        registration_handle_t first = m_registrations.front();
        m_registrations.pop_front();
        auto subscription = new detail::CombiningSubscription<F,S...>{m_registrations, context, callable, connectionType};
        return Subscription{first->subscribe(subscription)};
    }

private:

    template<typename First,typename...Tail> bool add(Registration<First>& first, Registration<Tail>&... tail) {
        if(!first) {
            return false;
        }
        if constexpr(sizeof...(Tail) > 0) {
            if(!add(tail...)) {
                return false;
            }
        }
        m_registrations.push_front(first.unwrap());
        return true;
    }
    QList<registration_handle_t> m_registrations;
};


///
/// \brief Combines two ore more ServiceRegistrations.
/// <br>This function returns a temporary object of type ServiceCombination.
/// The only thing you can do with it is invoke ServiceCombination::subscribe().
/// \tparam S the service-types. Must contain at least two entries. Curently, combining up to five services is supported.
/// \param registrations the list of registrations. Must contain at least two entries. Curently, combining up to five services is supported.
/// \return a ServiceCombination which can be subscribed.
///
template<typename...S> std::enable_if_t<(sizeof...(S) > 1),ServiceCombination<S...>> combine(Registration<S>&...registrations) {
    return ServiceCombination<S...>{registrations...};
}

///
/// \brief Subscribes to the publication of two services.
/// <br>This is a convenience-function equivalent to the following code:
///
///     firstService.subscribe(context, [&secondService,callable,connectionType](S1* source) {
///          secondService.subscribe(context, [source,callable](S2* target) { callable(source, target); }, connectionType);
///     }, connectionType);
///
/// <br>In case first and second represent the same Registration, the connection will still take place.
/// \param firstService the first registration.
/// \param secondService the second registration.
/// \param context the context for the subscription
/// \param callable will be invoked with two services.
/// \param connectionType
/// \tparam S1 the type of the first service.
/// \tparam S2 the type of the second service.
/// \tparam F must be a callable type, as if it had the signature `F(S* source, T* target)`.
/// \return a Subscription. Cancelling this Subscription will disconnect any connections that have already been made between the source-service
/// and the target-service.
///
template<typename S1,typename S2,typename F> [[deprecated("Use combine() instead")]] std::enable_if_t<std::is_invocable_v<F,S1*,S2*>,Subscription> subscribeToServices(Registration<S1>& firstService, Registration<S2>& secondService, QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
    return combine(firstService, secondService).subscribe(context, callable, connectionType);
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
        out.noquote().nospace() << "Registration for service-type '" << detail::type_name(typeid(S)) << "' [invalid]";
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
/// a QVariant to the target-type, i.e. it must have an `operator()(const QVariant&)`.
template<typename S,Kind kind=Kind::MANDATORY> struct Dependency {
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
/// \tparam S the service-type of the dependency.
/// \return a mandatory Dependency on the supplied type.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,Kind::MANDATORY> inject(const QString& requiredName = "") {
    return Dependency<S,Kind::MANDATORY>{requiredName};
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
/// \tparam S the service-type of the dependency.
/// \return an optional Dependency on the supplied type.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,Kind::OPTIONAL> injectIfPresent(const QString& requiredName = "") {
    return Dependency<S,Kind::OPTIONAL>{requiredName};
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
/// \tparam S the service-type of the dependency.
/// \return a 1-to-N Dependency on the supplied type.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,Kind::N> injectAll(const QString& requiredName = "") {
    return Dependency<S,Kind::N>{requiredName};
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
    detail::q_variant_converter_t variantConverter;
};

///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// Use this function to supply resolvable arguments to the constructor of a Service.
/// The result of resolving the placeholder must be a String that is convertible via `QVariant::value<T>()` to the desired type.
///
/// The result-type of the constructor-argument must be explicitly specified via the type-argument `<S>`, unless it is QString.
/// This function is a simplified overload of another function. See mcnepp::qtdi::resolve(const QString&,const S&,C) for more details!
///
/// ### Example
///
///     auto serviceDecl = service<QIODevice,QFile>(resolve("${filename:readme.txt}"));
/// \tparam S the result-type of the resolved constructor-argument.
/// \param expression may contain placeholders in the format `${identifier}` or `${identifier:defaultValue}`.
/// \return a Resolvable instance for the supplied type.
///
template<typename S=QString> [[nodiscard]] Resolvable<S> resolve(const QString& expression) {
    return Resolvable<S>{expression, QVariant{}, detail::variant_converter_traits<S>::makeConverter()};
}





///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// The result of resolving the placeholder must
/// be a String that is convertible via `QVariant::value<T>()` to the desired type.<br>
///
/// ### Placeholders
/// Values may contain *placeholders*, indicated by the syntax `${placeholder}`. Such a placeholder will be looked
/// up via `QApplicationContext::getConfigurationValue(const QString&,bool)`.<br>
/// Example:
///
///     auto serviceDecl = service<QIODevice,QFile>(resolve("${filename}", QString{"readme.txt"}));
/// will inject the value that is configured under the key "filename" into the QFile. If the key cannot be found, the default value "readme.txt" will be used instead.
/// <br>Should you want to specify a property-value containg the character-sequence "${", you must escape this with the backslash.
///
/// **Note:** The expression is allowed to specify embedded default-values using the format `${identifier:defaultValue}`.
/// However, this does not make much sense, as it would render the parameter `defaultValue` useless,
/// since the embedded default-value would always take precedence!
///
/// ### Lookup in sub-sections
/// Every key will be looked up in the section that has been provided via as an argument to mcnepp::qitdi::withGroup(const QString&), argument, unless the key itself starts with a forward slash,
/// which denotes the root-section.
///
/// A special syntax is available for forcing a key to be looked up in parent-sections if it cannot be resolved in the provided section:
///
/// Insert */ right after the opening sequence of the placeholder.
///
///     //Unfortunately, Doxygen cannot deal with the character-sequence "asterisk followed by slash" correctly in code-blocks.
///     //Thus, in the following example, we put a space between the asterisk and the slash:
///     context -> registerService(service<QIODevice,QFile>(resolve("${* /filename}")), "file", config() << withyGroup("files"));
///
/// The key "filename" will first be searched in the section "files". If it cannot be found, it will be searched in the root-section.
///
///
/// \tparam S the result-type of the resolved constructor-argument.
/// \tparam C the type of the converter. Must be a callable that accepts a QString and returns a value of type `T`. The default will invoke the constructor
/// `T{const QString&}` if it exists.
/// \param defaultValue the value to use if the placeholder cannot be resolved.
/// \param expression a String, possibly containing one or more placeholders.
/// \param converter Will be used to convert the resolved expression into a value.
/// \return a Resolvable instance for the supplied type.
///
template<typename S,typename C,typename=std::invoke_result_t<C,QString>> [[nodiscard]] Resolvable<S>  resolve(const QString& expression, const S& defaultValue, C converter) {
    return Resolvable<S>{expression, QVariant::fromValue(defaultValue), detail::variant_converter_traits<S>::makeConverter(converter)};
}




///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// <br>This is an overload of mcnepp::qtdi::resolve(const QString&,const S&,C) without the default-value.
///
template<typename S,typename C,typename=std::invoke_result_t<C,QString>> [[nodiscard]] Resolvable<S>  resolve(const QString& expression, C converter) {
    return Resolvable<S>{expression, QVariant{}, detail::variant_converter_traits<S>::makeConverter(converter)};
}


///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// <br>This is an overload of mcnepp::qtdi::resolve(const QString&,const S&,C) without the explicit converter.
///
template<typename S> [[nodiscard]] Resolvable<S>  resolve(const QString& expression, const S& defaultValue) {
    return Resolvable<S>{expression, QVariant::fromValue(defaultValue), detail::variant_converter_traits<S>::makeConverter()};
}



///
/// \brief Creates a placeholder for injecting the ApplicationContext into a service as the parent.
/// <br>Usually, this will not be necessary, as the QApplicationContext will set itself as the service's parent
/// after creation, using QObject::setParent(QObject*).
/// <br>However, there can be QObject-derived classes where the `parent` argument is not optional in the constructor,
/// so it has to be supplied explicitly.
/// <br>**Note:** Notwithstanding its self-documenting name, this function cannot ensure that the ApplicationContext is actually passed to the constructor
/// as the `parent` argument. However, in the vast majority of cases it will be the last argument that denotes ths `parent`.
/// \return an opaque type that will cause the ApplicationContext to inject itself as a service's parent.
///
inline detail::ParentPlaceholder injectParent() {
    return detail::ParentPlaceholder{};
}






///
/// \brief Configures a service for an ApplicationContext.
///
struct service_config final {
    using entry_type = std::pair<QString,detail::ConfigValue>;
    using map_type = QMap<QString,detail::ConfigValue>;

    ///
    /// Denotes a function that can be applied to a service_config via the overloaded `operator <<`.
    /// \see operator<<(config_modifier modifier)
    ///
    using config_modifier=std::function<void(service_config&)>;


    friend inline bool operator==(const service_config& left, const service_config& right) {
        return left.properties == right.properties && left.group == right.group && left.autowire == right.autowire && left.autoRefresh == right.autoRefresh;
    }


    ///
    /// \brief Applies a *modifier* to this configuration.
    /// <br>Usage is analogous to *iostream-manipulator*. Example:
    ///
    ///        config() << withAutowire;
    ///
    /// \param modifier will be applied to this instance.
    /// \return `this` instance.
    /// \see mcnepp::qtdi::withAutowire(service_config&)
    /// \see mcnepp::qtdi::withAutoRefresh(service_config&)
    /// \see mcnepp::qtdi::withGroup(const QString&)
    ///
    service_config& operator<<(config_modifier modifier) {
        modifier(*this);
        return *this;
    }


    ///
    /// \brief Adds an entry to this service_config.
    /// \param entry an entry that was created by one of the overloads of mcnepp::qtdi::entry() or mcnepp::qtdi::autoRefresh().
    /// \return `this` instance.
    ///
    service_config& operator<<(const service_config::entry_type& entry) {
        properties.insert(entry.first, entry.second);
        return *this;
    }


    ///
    /// \brief The keys and corresponding values.
    ///
    map_type properties;

    ///
    /// \brief The optional group for the configuration.
    ///
    QString group;

    ///
    /// \brief Determines whether all Q_PROPERTYs that refer to other services shall be auto-wired by the ApplicationContext.
    ///
    bool autowire = false;


    ///
    /// \brief Shall all properties be automatically refreshed?
    ///
    bool autoRefresh = false;
};

///
/// \brief Enables auto-refresh for a service_config.
/// <br>This function is not meant to be invoked directly.
/// <br>Rather, its usage is analogous to that of *iostream-manipulators* from the standard-libray:
///
///
///     config() << withAutoRefresh << entry("objectName", "${myService}");
///
inline void withAutoRefresh(service_config& cfg) {
    cfg.autoRefresh = true;
};

///
/// \brief Applies auto-wiring to a service_config.
/// <br>This function is not meant to be invoked directly.
/// <br>Rather, its usage is analogous to that of *iostream-manipulators* from the standard-libray:
///
///
///     config() << withAutowire << entry("objectName", "${myService}");
///
inline void withAutowire(service_config& cfg) {
    cfg.autowire = true;
};


///
/// \brief Sets the group for a service_config.
/// <br>The usage of this function is analogous to that of *iostream-manipulators* from the standard-libray:
///
///
///     config() << withGroup("myServices") << entry("objectName", "${myService}");
///
inline service_config::config_modifier withGroup(const QString& name) {
    return [name](service_config& cfg) { cfg.group = name;};
}


///
/// \brief Makes a service_config and populates it with properties.
/// <br>The service must have a Q_PROPERTY for every key contained in `properties`.<br>
/// Example:
///
///     `config({{"interval", 42}});`
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
///     auto restServiceTemplate = context -> registerService<RestService>(serviceTemplate<RestService>(), "restTemplate", config({{"url", "https://myserver/rest/${path}"}}));
///
/// Now, whenever you register a concrete RestService, you must supply the `templateReg` as an additional argument.
/// Also, you must specify the value for `${path}` as a *private property*:
///
///     context -> registerService(service<RestService>(), restServiceTemplate, "temperatureService", config({{".path", "temperature"}}));
///
/// ### Placeholders
/// Values may contain *placeholders*, indicated by the syntax `${placeholder}`. Such a placeholder will be looked
/// up via `QApplicationContext::getConfigurationValue(const QString&,bool)`.<br>
/// Example:
///
///     config({{"interval", "${timerInterval}"}});
/// will set the Q_PROPERTY `interval` to the value configured with the name `timerInterval`.
/// <br>Should you want to specify a property-value containg the character-sequence "${", you must escape this with the backslash.
/// ### Lookup in sub-sections
/// Every key will be looked up in the provided section, as specified by the `group` argument, unless the key itself starts with a forward slash,
/// which denotes the root-section.
///
/// A special syntax is available for forcing a key to be looked up in parent-sections if it cannot be resolved in the provided section:
///
/// Insert */ right after the opening sequence of the placeholder.
///
///     //Unfortunately, Doxygen cannot deal with the character-sequence "asterisk followed by slash" correctly in code-blocks.
///     //Thus, in the following example, we put a space between the asterisk and the slash:
///     config({{"interval", "${* /timerInterval}"}}) << withGroup("timers");
///
/// The key "timerInterval" will first be searched in the section "timers". If it cannot be found, it will be searched in the root-section.
///
/// ### Service-references
/// If a value starts with an ampersand, the property will be resolved with a registered service of that name.
/// Example:
///
///     config({{"dataProvider", "&dataProviderService"}});
/// will set the Q_PROPERTY `dataProvider` to the service that was registered under the name `dataProviderService`.
/// <br>Should you want to specify a property-value starting with an ampersand, you must escape this with the backslash.
/// \param properties the keys and value to be applied as Q_PROPERTYs.
/// \return the service_config.
[[nodiscard]] inline service_config config(std::initializer_list<std::pair<QString,QVariant>> properties) {
    service_config cfg;
    for(auto& entry : properties) {
        cfg.properties.insert(entry.first, detail::ConfigValue{entry.second, detail::ConfigValueType::DEFAULT});
    }
    return cfg;
}







///
/// Makes a default service_config.
/// <br>The returned service_config can then be further modified, before it gets passed to QApplicationContext::registerService().
/// <br>Modification is done in a fashion similar to *iostreams*, i.e. by using the overloaded `operator <<`.
/// <br>You add a configuration-entry like this:
///
///     config() << entry("url", "http://mcnepp.com");
///
/// Several configuration-entries can be chained conveniently:
///
///     config() << entry("url", "http://mcnepp.com") << entry("objectName", "${dataProvider}");
///
/// You can also specify the group within the QSettings from where the configuration-entries shall be resolved:
///
///     config() << withGroup("dataProviders") << entry("url", "http://mcnepp.com") << entry("objectName", "${dataProvider}");
///
/// If you want to enable *autowirng* for a service-registration, you can use mcnepp::qtdi::withAutowire(service_config&) like this:
///
///     config() << withGroup("dataProviders") << withAutowire << entry("url", "http://mcnepp.com") << entry("objectName", "${dataProvider}");
///
/// And finally, should you want to enable *auto-refresh* on all configuration-entries, you can use mcnepp::qtdi::withAutoRefresh(service_config&) like this:
///
///     config() << withGroup("dataProviders") << withAutoRefresh << entry("url", "http://mcnepp.com") << entry("objectName", "${dataProvider}");
///
/// \return the service_config.
[[nodiscard]] inline service_config config() {
    return service_config{};
}

///
/// \brief A type-safe configuration-entry.
/// <br>This is an intermediate type that will be used solely to populate a ServiceConfig.
///
template<typename S> using service_config_entry=detail::service_config_entry_t<S>;



///
/// \brief A type-safe service-configuration.
/// <br>This class is the type-safe equivalent to the service_config.
/// <br>In contrast to service_config, configuration-values are specified using method-pointers, not property-names.
/// <br>Also, there does not have to be a Q_PROPERTY declared for each configuration-value. You may use the syntax for *private properties*,
/// i.e. a name preceded by a dot to indicate that.
///
template<typename S> struct ServiceConfig {

    ///
    /// \brief Applies a *modifier* to this configuration.
    /// <br>Usage is analogous to *iostream-manipulator*. Example:
    ///
    ///        config() << withAutowire;
    ///
    /// \param modifier will be applied to this instance.
    /// \return `this` instance.
    /// \see mcnepp::qtdi::withAutowire(service_config&)
    /// \see mcnepp::qtdi::withAutoRefresh(service_config&)
    /// \see mcnepp::qtdi::withGroup(const QString&)
    ///
    ServiceConfig<S>& operator<<(service_config::config_modifier manip) {
        manip(data);
        return *this;
    }


    ///
    /// \brief Adds an entry to this ServiceConfig.
    /// \param entry an entry that was created by one of the overloads of mcnepp::qtdi::entry() or mcnepp::qtdi::autoRefresh().
    /// \return `this` instance.
    ///
    ServiceConfig<S>& operator<<(const service_config_entry<S>& entry) {
        data.properties.insert(entry.name, entry.value);
        return *this;
    }



    ///
    /// \brief Adds an entry to this ServiceConfig.
    /// \param entry an entry that was created by one of the overloads of mcnepp::qtdi::entry() or mcnepp::qtdi::autoRefresh().
    /// \return `this` instance.
    ///
    ServiceConfig<S>& operator<<(const service_config::entry_type& entry) {
        data.properties.insert(entry.first, entry.second);
        return *this;
    }

    service_config data;
};





///
/// \brief Adds a type-safe entry, creating a strongly typed ServiceConfig.
/// \param entry an entry that was created by one of the overloads of mcnepp::qtdi::entry() or mcnepp::qtdi::autoRefresh().
/// \return a strongly typed ServiceConfig.
template<typename S> [[nodiscard]] ServiceConfig<S> operator<<(service_config&& cfg, const service_config_entry<S>& entry) {
    cfg.properties.insert(entry.name, entry.value);
    return ServiceConfig<S>{std::move(cfg)};
}

///
/// \brief Adds a type-safe entry, creating a strongly typed ServiceConfig.
/// \param entry an entry that was created by one of the overloads of mcnepp::qtdi::entry() or mcnepp::qtdi::autoRefresh().
/// \return a strongly typed ServiceConfig.
template<typename S> [[nodiscard]] ServiceConfig<S> operator<<(const service_config& cfg, const service_config_entry<S>& entry) {
    ServiceConfig<S> copy{cfg};
    copy.data.properties.insert(entry.name, entry.value);
    return copy;
}




///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
///
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the property-value.
/// \param expression will be resolved when the service is being configured. May contain *placeholders*.
/// \param converter (optional) specifies a converter that constructs an argument of type `A` from a QString.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,typename C=typename detail::variant_converter_traits<detail::remove_cvref_t<A>>::type> [[nodiscard]] service_config_entry<S> entry(R(S::*propertySetter)(A), const QString& expression, C converter=C{}) {
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }

    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{expression, detail::ConfigValueType::DEFAULT, detail::callable_adapter<S>::adaptSetter(propertySetter), detail::variant_converter_traits<detail::remove_cvref_t<A>>::makeConverter(converter)}};
}

///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the property-value.
/// \param value will be set when the service is being configured.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A> [[nodiscard]] service_config_entry<S> entry(R(S::*propertySetter)(A), A value) {
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }

    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(value), detail::ConfigValueType::DEFAULT, detail::callable_adapter<S>::adaptSetter(propertySetter)}};
}

///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the instance of the service of type `<A>`.
/// \param reg the registration for the service-instance that will be injected into the configured service.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,ServiceScope scope> [[nodiscard]] auto entry(R(S::*propertySetter)(A*), const ServiceRegistration<A,scope>& reg) -> std::enable_if_t<detail::is_binding_source(scope),service_config_entry<S>>
{
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }
    if(!reg || !detail::is_binding_source(reg.unwrap() -> scope())) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot inject invalid ServiceRegistration";
        return {".invalid", QVariant{}};
    }
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(reg.unwrap()), detail::ConfigValueType::SERVICE, detail::callable_adapter<S>::adaptSetter(propertySetter)}};
}

///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with a `QList<A*>`, comprising all service-instances that have been published for the
/// suppolied ProxyRegistration.
/// \param reg the registration for those services that will be injected into the configured service.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,typename L> [[nodiscard]] auto entry(R(S::*propertySetter)(L), const ProxyRegistration<A>& reg) -> std::enable_if_t<std::is_convertible_v<L,QList<A*>>,service_config_entry<S>>
{
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }
    if(!reg) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot inject invalid ServiceRegistration";
        return {".invalid", QVariant{}};
    }
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(reg.unwrap()), detail::ConfigValueType::SERVICE_LIST, detail::callable_adapter<S>::adaptSetter(propertySetter), nullptr}};
}


///
/// \brief Creates a type-safe, auto-refreshing configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
/// <br>In order to demonstrate the purpose, consider this example of a normal, non-updating service-configuration for a QTimer:
///
///     context->registerService<QTimer>("timer", config() << entry(&QTimer::setInterval, "${timerInterval}"));
///
/// The member-function will be initialized from the value of the configuration-key `"timerInterval"` as it
/// is in the moment the timer is instantiated.
///
/// <br>Now, contrast this with:
///
///     context->registerService<QTimer>("timer", config() << autoRefresh(&QTimer::setInterval, "${timerInterval}"));
///
/// Whenever the value for the configuration-key `"timerInterval"` changes in the underlying QSettings-Object, the
/// expression `"${timerInterval}"` will be re-evaluated and the member-function of the timer will be updated accordingly.
/// <br>In case all properties for one service shall be auto-refreshed, there is a more concise way of specifying it:
///
///     context->registerService<QTimer>("timer", config() << withAutoRefresh << entry(&QObject::setObjectName, "theTimer") << entry("interval", &QTimer::setInterval, "${timerInterval}"));
///
/// **Note:** Auto-refreshing an optional feature that needs to be explicitly enabled for mcnepp::qtdi::StandardApplicationContext
/// by putting a configuration-entry into one of the QSettings-objects registered with the context:
///
///     [qtdi]
///     enableAutoRefresh=true
///     ; Optionally, specify the refresh-period:
///     autoRefreshMillis=2000
///
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the property-value.
/// \param expression will be resolved when the service is being configured. May contain *placeholders*.
/// \param converter (optional) specifies a converter that constructs an argument of type `A` from a QString.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,typename C=typename detail::variant_converter_traits<detail::remove_cvref_t<A>>::type> [[nodiscard]] service_config_entry<S> autoRefresh(R(S::*propertySetter)(A), const QString& expression, C converter=C{}) {
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{expression, detail::ConfigValueType::AUTO_REFRESH_EXPRESSION, detail::callable_adapter<S>::adaptSetter(propertySetter), detail::variant_converter_traits<detail::remove_cvref_t<A>>::makeConverter(converter)}};
}





///
/// \brief Creates a configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
/// \param name the name of the property to be configured. **Note:** this name must refer to a Q_PROPERTY of the service-type!
/// The only exception is a *private property*, i.e. a configuration-entry that shall not be resolved automatically.
/// In that case, the name must start with a dot.
/// \param value will be used as the property's value.
/// \return a configuration for a service.
///
[[nodiscard]] inline service_config::entry_type entry(const QString& name, const QVariant& value) {
    return {name, detail::ConfigValue{value, detail::ConfigValueType::DEFAULT}};
}


///
/// \brief Specifies that a value for a configured Q_PROPERTY shall be automatically updated at runtime.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::config() using the `operator <<`.
/// <br>In order to demonstrate the purpose, consider this example of a normal, non-updating service-configuration for a QTimer:
///
///     context->registerService<QTimer>("timer", config({{"interval", "${timerInterval}"}}));
///
/// The Q_PROPERTY QTimer::interval will be initialized from the value of the configuration-key `"timerInterval"` as it
/// is in the moment the timer is instantiated.
///
/// <br>Now, contrast this with:
///
///     context->registerService<QTimer>("timer", config({autoRefresh("interval", "${timerInterval}")}));
///
/// Whenever the value for the configuration-key `"timerInterval"` changes in the underlying QSettings-Object, the
/// expression `"${timerInterval}"` will be re-evaluated and the Q_PROPERTY of the timer will be updated accordingly.
/// <br>In case all properties for one service shall be auto-refreshed, there is a more concise way of specifying it:
///
///     context->registerService<QTimer>("timer", config() << withAutoRefresh << entry("objectName", "theTimer") << entry("interval", "${timerInterval}"));
///
/// **Note:** Auto-refreshing an optional feature that needs to be explicitly enabled for mcnepp::qtdi::StandardApplicationContext
/// by putting a configuration-entry into one of the QSettings-objects registered with the context:
///
///     [qtdi]
///     enableAutoRefresh=true
///     ; Optionally, specify the refresh-period:
///     autoRefreshMillis=2000
///
/// \param name the name of the configuration-entry.
/// \param expression a String, possibly containing one or more placeholders.
/// \return an  entry that will ensure that the expression will be re-evaluated when the underlying QSettings changes.
[[nodiscard]] inline service_config::entry_type autoRefresh(const QString& name, const QString& expression) {
    return {name, detail::ConfigValue{expression, detail::ConfigValueType::AUTO_REFRESH_EXPRESSION, nullptr, nullptr}};
}




///
/// \brief Create a type-safe service-configuration.
/// <br>A type-safe entry can be created by invoking mcnepp::qtdi::entry() with a member-function as its first argument.
/// For example:
///
///     context->registerService<QQTimer>("timer", config({entry(&QTimer::setInterval, 1000), entry(&QTimer::singleShot, "true")}));
///
/// Note that is possible to mix type-safe configuration-entries with Q_PROPERTY-based configuration-entries:
///
///     context->registerService<QQTimer>("timer", config({entry(&QTimer::setInterval, 1000)}) << entry("singleShot", "true"));
///
/// However, there is a caveat: Even though the above service-registrations are *logically equivalent*, they are *technically different*.
/// Thus, executing the second registration after the first one will not be considered *idempotent*.
/// This means that the second registration will fail (i.e. return an invalid ServiceRegistration).
///
/// \param entries the list of key/value-pairs that will be used to configure a Service of type `<S>`.
/// \return a type-safe service-configuration.
///
template<typename S> [[nodiscard]] ServiceConfig<S> config(std::initializer_list<service_config_entry<S>> entries) {
    ServiceConfig<S> cfg;
    for(auto& entry : entries) {
        cfg.data.properties.insert(entry.name, entry.value);
    }
    return cfg;
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
constexpr int PARENT_PLACEHOLDER_KIND = 0x40;
constexpr int INVALID_KIND = 0xff;




struct dependency_info {
    const std::type_info& type;
    int kind;
    QString expression; //RESOLVABLE_KIND: The resolvable expression. VALUE_KIND: empty. Otherwise: the required name of the dependency.
    QVariant value; //VALUE_KIND: The injected value. RESOLVABLE_KIND: the default-value.
    q_variant_converter_t variantConverter; //Only valid for kind == RESOLVABLE_KIND

    bool isValid() const {
        return kind != INVALID_KIND;
    }

    bool has_required_name() const {
        switch(kind) {
        case VALUE_KIND:
        case RESOLVABLE_KIND:
        case PARENT_PLACEHOLDER_KIND:
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
    case PARENT_PLACEHOLDER_KIND:
        return true;
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

    static auto converter() {
        return &convert;
    }

};

template <typename S,Kind kind>
struct dependency_helper<Dependency<S,kind>> {
    using type = S;


    static dependency_info info(const Dependency<S,kind>& dep) {
        return { typeid(S), static_cast<int>(kind), dep.requiredName };
    }

    static auto converter() {
        return default_argument_converter<S,kind>{};
    }
};

template <typename S,ServiceScope scope>
struct dependency_helper<mcnepp::qtdi::ServiceRegistration<S,scope>> {
    using type = S;



    static dependency_info info(const mcnepp::qtdi::ServiceRegistration<S,scope>& dep) {
        static_assert(is_binding_source(scope), "ServiceRegistration with this scope cannot be a dependency");
        //It could still be ServiceScope::UNKNOWN statically, but ServiceScope::TEMPLATE at runtime:
        if(dep && is_binding_source(dep.unwrap()->scope())) {
            return { dep.unwrap()->descriptor().impl_type, static_cast<int>(Kind::MANDATORY), dep.registeredName() };
        }
        return { typeid(S), INVALID_KIND, dep.registeredName() };
    }

    static auto converter() {
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

    static auto converter() {
        return default_argument_converter<S,Kind::N>{};
    }

};


template <typename S>
struct dependency_helper<Resolvable<S>> {

    using type = S;



    static dependency_info info(const Resolvable<S>& dep) {
        return { typeid(S), RESOLVABLE_KIND, dep.expression, dep.defaultValue, dep.variantConverter };
    }

    static auto converter() {
        return &dependency_helper<S>::convert;
    }
};



template <>
struct dependency_helper<ParentPlaceholder> {

    using type = ParentPlaceholder;



    static dependency_info info(const ParentPlaceholder&) {
        return { typeid(QObject*), PARENT_PLACEHOLDER_KIND};
    }

    static auto converter() {
        return &dependency_helper<QObject*>::convert;
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
        descriptor.constructor = service_creator<Impl>(factory, detail::dependency_helper<Dep>::converter()...);
    }
    return descriptor;
}



} // namespace detail

///
/// \brief Describes a service by its interface and implementation.
/// Compilation will fail if either `Srv` is not a sub-class of QObject, or if `Impl` is not a sub-class of `Srv`.
/// <br>The preferred way of creating Services is the function mcnepp::qtdi::service().
///
/// Example with one argument of type `QString`:
///
///     context->registerService(service<QIODevice,QFile>(QString{"readme.txt"), "file");
///
/// Instead of providing one primary service-interface, you may advertise multiple services-interfaces explicitly:
///
///     context->registerService(service<QFile>(QString{"readme.txt").advertiseAs<QFileDevice,QIODevice>(), "file");
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
    template<typename...IFaces> Service<Srv,Impl,scope>&& advertiseAs() && {
        static_assert(sizeof...(IFaces) > 0, "At least one service-interface must be advertised.");
        //Check whether the Impl-type is derived from the service-interfaces (except for service-templates)
        if constexpr(scope != ServiceScope::TEMPLATE) {
            static_assert((std::is_base_of_v<IFaces,Impl> && ... ), "Implementation-type does not implement all advertised interfaces");
        }
        if constexpr(std::is_same_v<Srv,Impl>) {
            static_assert(detail::check_unique_types<Impl,IFaces...>(), "All advertised interfaces must be distinct");
            if(auto found = descriptor.service_types.find(descriptor.impl_type); found != descriptor.service_types.end()) {
                descriptor.service_types.erase(found);
            }
        } else {
            static_assert(detail::check_unique_types<Impl,Srv,IFaces...>(), "All advertised interfaces must be distinct");
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
    template<typename...IFaces> [[nodiscard]] Service<Srv,Impl,scope> advertiseAs() const& {
        return Service<Srv,Impl,scope>{*this}.advertiseAs<IFaces...>();
    }

    /**
     * @brief Specifies an *init-method* for this service.
     * <br>This overrides the init-method that is deduced from the `initializer_type` of the service's service_traits.
     * The init-method will be used for this service-instance only.
     * <br>The initializer must one of:
     *
     * - a callable object with one argument of a pointer to the service's implementation-type
     * - a callable object with two arguments, the first being a pointer to the service's implementation type, and the second being a pointer to QApplicationContext.
     * - a member-function of the service's implementation-type with no arguments.
     * - a member-function of the service's implementation-type with one argument of pointer to QApplicationContext.
     *
     * @tparam I the type of the initializer.
     * @param initializer Will be invoked after all properties have been set and before the signal for the publication is emitted.
     * @return this instance
     */
    template<typename I> Service<Srv,Impl,scope>&& withInit(I initializer) && {
        descriptor.init_method = detail::adaptInitializer<Impl>(initializer);
        return std::move(*this);
    }

    /**
     * @brief Specifies an *init-method* for this service.
     * <br>This overrides the init-method that is deduced from the `initializer_type` of the service's service_traits.
     * The init-method will be used for this service-instance only.
     * <br>The initializer must one of:
     *
     * - a callable object with one argument of a pointer to the service's implementation-type
     * - a callable object with two arguments, the first being a pointer to the service's implementation type, and the second being a pointer to QApplicationContext.
     * - a member-function of the service's implementation-type with no arguments.
     * - a member-function of the service's implementation-type with one argument of pointer to QApplicationContext.
     *
     * @tparam I the type of the initializer.
     * @param initializer Will be invoked after all properties have been set and before the signal for the publication is emitted.
     * @return a service with the supplied initializer.
     */
    template<typename I> Service<Srv,Impl,scope> withInit(I initializer) const& {
        return Service<Srv,Impl,scope>{*this}.withInit(initializer);
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
/// \brief Watches a configuration-value.
/// <br>Instances will be returned from QApplicationContext::watchConfigValue(const QString&).
/// <br>The current value can be obtained through the Q_PROPERTY currentValue().
/// <br>Should the underlying configuration be modified, the signal currentValueChanged(const QVariant&) will be emitted.
///
class QConfigurationWatcher : public QObject {
    Q_OBJECT
public:
    Q_PROPERTY(QVariant currentValue READ currentValue NOTIFY currentValueChanged)

    ///
    /// \brief Obtains the current configuration-value.
    /// \return the current configuration-value.
    ///
    virtual QVariant currentValue() const = 0;

signals:
    ///
    /// \brief the underlying configuration has changed.
    ///
    void currentValueChanged(const QVariant&);

    /// \brief The value could not be resolved.
    /// **Note:** When an error occurs, the currentValue() remains at the last valid value.
    void errorOccurred();
protected:
    explicit QConfigurationWatcher(QObject* parent = nullptr) : QObject{parent} {

    }
};

///
/// \brief Provides access to the configuration of a QApplicationContext.
///
class QConfigurationResolver {
public:
    ///
    /// \brief Retrieves a value from the ApplicationContext's configuration.
    /// <br>This function will be used to resolve placeholders in Service-configurations.
    /// Whenever a *placeholder* shall be looked up, the ApplicationContext will search the following sources, until it can resolve the *placeholder*:
    /// -# The environment, for a variable corresponding to the *placeholder*.
    /// -# The instances of `QSettings` that have been registered in the ApplicationContext.
    /// \sa mcnepp::qtdi::config()
    /// \param key the key to look up. In analogy to QSettings, the key may contain forward slashes to denote keys within sub-sections.
    /// \param searchParentSections determines whether the key shall be searched recursively in the parent-sections.
    /// \return the value, if it could be resolved. Otherwise, an invalid QVariant.
    ///
    [[nodiscard]] virtual QVariant getConfigurationValue(const QString& key, bool searchParentSections = false) const = 0;

    ///
    /// \brief Resolves an expression.
    /// <br>**Thread-safety:** This function may be safely called from any thread.
    /// \param expression will be parsed in order to determine the QConfigurationWatcher::currentValue().
    /// <br>In case the expression is a simple String, it will be returned as is.
    /// <br>The expression may contain one or more *placeholders* which will be resolved using the underlying configuration.
    /// A *placeholder* is enclosed in curly brackets with a preceding dollar-sign.<br>
    /// `"${name}"` will be resolved with the configuration-entry `"name"`.<br>
    /// `"${network/name}"` will be resolved with the configuration-entry `"name"` from the section `"network"`.<br>
    /// `"${host}://${url}"` will be resolved with the result of the concatenation of the configuration-entry `"host"`,
    /// a colon and two slashes and the configuration-entry `"url"`.<br>
    /// The special character-sequence asterisk-slash indicates that a value shall be resolved in a section and all its parent-sections:<br>
    /// `"* /network/hosts/${host}"` will be resolved with the configuration-entry `"name"` from the section `"network/hosts"`, or
    /// its parent sections.
    /// \return a valid QVariant, or an invalid QVariant if the expression could not be parsed.
    ///
    [[nodiscard]] virtual QVariant resolveConfigValue(const QString& expression) = 0;


    virtual ~QConfigurationResolver() = default;


    static QString makePath(const QString& section, const QString& path);

    static bool removeLastPath(QString& s);


};


///
/// \brief A DI-Container for Qt-based applications.
///
class QApplicationContext : public QObject, public QConfigurationResolver
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
        return ServiceRegistration<S,scope>::wrap(registerServiceHandle(objectName, serviceDeclaration.descriptor, config, scope, nullptr));
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
    /// \tparam S the service-type. Constitutes the Service's primary advertised interface.
    /// \tparam Impl the implementation-type. The Service will be instantiated using this class' constructor.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename Impl,ServiceScope scope> auto registerService(const Service<S,Impl,scope>& serviceDeclaration, const QString& objectName, const ServiceConfig<Impl>& config) -> ServiceRegistration<S,scope> {
        return registerService(serviceDeclaration, objectName, config.data);
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
        return ServiceRegistration<S,scope>::wrap(registerServiceHandle(objectName, serviceDeclaration.descriptor, config, scope, templateRegistration.unwrap()));
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
    template<typename S,typename Impl,typename B,ServiceScope scope> auto registerService(const Service<S,Impl,scope>& serviceDeclaration, const ServiceRegistration<B,ServiceScope::TEMPLATE>& templateRegistration, const QString& objectName, const ServiceConfig<Impl>& config) -> ServiceRegistration<S,scope> {
        return registerService(serviceDeclaration, templateRegistration, objectName, config.data);
    }





    ///
    /// \brief Registers a service with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(service<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the type-safe Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> auto registerService(const QString& objectName, const ServiceConfig<S>& config) -> ServiceRegistration<S,ServiceScope::SINGLETON> {
        return registerService(service<S>(), objectName, config.data);
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
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << qObject << " as '" << objName << "'. Object does not implement " << detail::type_name(check.second);
                return ServiceRegistration<S,ServiceScope::EXTERNAL>{};
            }
        }
        std::unordered_set<std::type_index> ifaces;
        (ifaces.insert(typeid(S)), ..., ifaces.insert(typeid(IFaces)));
        return ServiceRegistration<S,ServiceScope::EXTERNAL>::wrap(registerServiceHandle(objName, service_descriptor{ifaces, typeid(*obj), qObject->metaObject()}, service_config{}, ServiceScope::EXTERNAL, qObject));
    }

    ///
    /// \brief Obtains a ServiceRegistration for a name.
    /// <br>This function will look up Services by the names they were registered with.
    /// Additionally, it will look up any alias that might have been given, using ServiceRegistration::registerAlias(const QString&).
    /// <br>The returned ServiceRegistration may be narrowed to a more specific service-type using ServiceRegistration::as().
    /// <br>**Thread-safety:** This function may be called safely  from any thread.
    /// \param name the desired name of the registration.
    /// A valid ServiceRegistration will be returned only if exactly one Service that matches the requested name has been registered.
    /// \return a ServiceRegistration for the required type and name. If no single Service with a matching name could be found,
    /// an invalid ServiceRegistration will be returned.
    ///
    [[nodiscard]] ServiceRegistration<QObject,ServiceScope::UNKNOWN> getRegistration(const QString& name) const {
        return ServiceRegistration<QObject,ServiceScope::UNKNOWN>::wrap(getRegistrationHandle(name));
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
    [[nodiscard]] bool isGlobalInstance() const;
    ///
    /// \brief Obtains a QConfigurationWatcher for an expression.
    /// <br>If autoRefreshEnabled() and the `expression` can be successfully parsed, this function returns an instance of QConfigurationWatcher.
    /// <br>Using the Q_PROPERTY QConfigurationWatcher::currentValue(), you can then track the current configuration.
    /// <br>**Thread-safety:** This function may be safely called from any thread. The returned QConfigurationWatcher's will have a *thread-affinity* to the QApplicationContext's thread.
    /// \param expression will be parsed in order to determine the QConfigurationWatcher::currentValue().
    /// <br>The expression shall contain one or more *placeholders* which will be resolved using the underlying configuration.
    /// A *placeholder* is enclosed in curly brackets with a preceding dollar-sign.<br>
    /// `"${name}"` will be resolved with the configuration-entry `"name"`.<br>
    /// `"${network/name}"` will be resolved with the configuration-entry `"name"` from the section `"network"`.<br>
    /// `"${host}://${url}"` will be resolved with the result of the concatenation of the configuration-entry `"host"`,
    /// a colon and two slashes and the configuration-entry `"url"`.<br>
    /// The special character-sequence asterisk-slash indicates that a value shall be resolved in a section and all its parent-sections:<br>
    /// `"* /network/hosts/${host}"` will be resolved with the configuration-entry `"name"` from the section `"network/hosts"`, or
    /// its parent sections.
    /// \return QConfigurationWatcher that watches the expression, or `nullptr` if the expression could not be parsed, or
    /// if auto-refresh has not been enabled.
    /// \sa autoRefreshEnabled()
    ///
    [[nodiscard]] virtual QConfigurationWatcher* watchConfigValue(const QString& expression) = 0;

    ///
    /// \brief Has auto-refresh been enabled?
    /// <br>If enabled, you may use watchConfigValue(const QString&) in order to watch configuration-values.
    /// <br>Also, it is possible to use mcnepp::qtdi::autoRefresh(const QString&) to force automatic updates of service-properties,
    /// whenever the corresponding configuration-values is modified.
    /// <br>When using a StandardApplicationContext, auto-refresh can be enabled by putting a configuration-entry into one of the QSettings-objects registered with the context:
    ///
    ///     [qtdi]
    ///     enableAutoRefresh=true
    ///     ; Optionally, specify the refresh-period:
    ///     autoRefreshMillis=2000
    ///
    /// \return `true` if auto-refresh has been enabled.
    ///
    [[nodiscard]] virtual bool autoRefreshEnabled() const = 0;

    ///
    /// \brief The QLoggingCategory that this ApplicationContext uses.
    /// \return The QLoggingCategory that this ApplicationContext uses.
    /// \sa mcnepp::qtdi::defaultLoggingCategory()
    ///
    [[nodiscard]] virtual const QLoggingCategory& loggingCategory() const = 0;



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
    /// \param scope determines the service's lifeycle
    /// \param baseObject in case of ServiceScope::EXTERNAL the Object to be registered. Otherwise, the (optional) pointer to the registration of a service-template.
    /// \return a Registration for the service, or `nullptr` if it could not be registered.
    ///
    virtual service_registration_handle_t registerServiceHandle(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, QObject* baseObject) = 0;


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
    /// <br>registeredName(service_registration_handle_t), serviceConfig(service_registration_handle_t).
    /// <br>Additionally, you may wrap the handles in a type-safe manner, using ServiceRegistration::wrap(service_registration_handle_t).
    ///
    /// \param name the desired name of the service.
    /// A valid handle to a Registration will be returned only if exactly one Service has been registered that matches
    /// the name.
    /// \return a handle to a Registration for the supplied name, or `nullptr` if no single Service has been registered with the name.
    ///
    [[nodiscard]] virtual service_registration_handle_t getRegistrationHandle(const QString& name) const = 0;


    /**
     * @brief Obtains a List of all Services that have been registered.
     * <br>The element-type of the returned QList is the opaque type service_registration_handle_t.
     * You should not de-reference it, as its API may changed without notice.
     * <br>
     * What you can do, though, is use one of the free functions matches(registration_handle_t),
     * registeredName(service_registration_handle_t), serviceConfig(service_registration_handle_t).
     * <br>Additionally, you may wrap the handles in a type-safe manner, using ServiceRegistration::wrap(service_registration_handle_t).
     * @return a List of all Services that have been registered.
     */
    [[nodiscard]] virtual QList<service_registration_handle_t> getRegistrationHandles() const = 0;


    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing registerServiceHandle(const QString&, const service_descriptor&, const service_config&, ServiceScope, QObject*) and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// \param appContext the target on which to invoke registerServiceHandle(const QString&, const service_descriptor&, const service_config&, ServiceScope, QObject*).
    /// \param name the name of the registered service.
    /// \param descriptor describes the service.
    /// \param config configuration of the service.
    /// \param scope detemines the service's lifecyle.
    /// \param baseObj in case of ServiceScope::EXTERNAL the Object to be registered. Otherwise, the (optional) pointer to the registration of a service-template.
    /// \return the result of registerService(const QString&, service_descriptor*,const service_config&,ServiceScope,QObject*).
    ///
    static service_registration_handle_t delegateRegisterService(QApplicationContext* appContext, const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, QObject* baseObj) {
        if(!appContext) {
            return nullptr;
        }
        return appContext->registerServiceHandle(name, descriptor, config, scope, baseObj);
    }



    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing getRegistrationHandle(const std::type_info&,const QMetaObject*) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// \param appContext the target on which to invoke getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    /// \param service_type
    /// \param metaObject the QMetaObject of the service_type. May be omitted.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    [[nodiscard]] static proxy_registration_handle_t delegateGetRegistrationHandle(const QApplicationContext* appContext, const std::type_info& service_type, const QMetaObject* metaObject) {
        if(!appContext) {
            return nullptr;
        }
        return appContext->getRegistrationHandle(service_type, metaObject);
    }

    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing getRegistrationHandle(const QString&) const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// \param appContext the target on which to invoke getRegistrationHandle(const QString&) const.
    /// \param name the name under which the service is looked up.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    [[nodiscard]] static service_registration_handle_t delegateGetRegistrationHandle(const QApplicationContext* appContext, const QString& name) {
        if(!appContext) {
            return nullptr;
        }

        return appContext->getRegistrationHandle(name);
    }



    ///
    /// \brief Allows you to invoke a protected virtual function on another target.
    /// <br>If you are implementing getRegistrationHandles() const and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// \param appContext the target on which to invoke getRegistrationHandles() const.
    /// \return the result of getRegistrationHandle(const std::type_info&,const QMetaObject*) const.
    ///
    static QList<service_registration_handle_t> delegateGetRegistrationHandles(const QApplicationContext* appContext) {
        if(!appContext) {
            return QList<service_registration_handle_t>{};
        }
        return appContext->getRegistrationHandles();
    }

    ///
    /// \brief Connects the signals of a source-context with the corresponding signals of a target-context.
    /// <br>This convenience-function may help with implementing your own implementation of QApplicationContext.
    /// \param sourceContext the context that emits the signals
    /// \param targetContext the context that shall propagate the signals
    /// \param connectionType will be used to make the connections.
    ///
    static void delegateConnectSignals(QApplicationContext* sourceContext, QApplicationContext* targetContext, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        connect(sourceContext, &QApplicationContext::pendingPublicationChanged, targetContext, &QApplicationContext::pendingPublicationChanged, connectionType) ;
        connect(sourceContext, &QApplicationContext::publishedChanged, targetContext, &QApplicationContext::publishedChanged, connectionType);
    }


    ///
    /// \brief Sets an ApplicationContext as the *global instance*.
    /// <br>This function will only succeed if there is currently *no global instance*.
    /// \param ctx the context to set.
    /// \sa instance()
    /// \return `true` if the supplied context could be set as the *global instance*
    ///
    static bool setInstance(QApplicationContext* ctx);

    ///
    /// \brief Removes the *global instance*.
    /// <br>This function will only succeed if the supplied context is currently the *global instance*.
    /// \param ctx the context assumed to be the current *global instance*.
    /// \sa instance()
    /// \return `true` if the supplied context was the *global instance*
    static bool unsetInstance(QApplicationContext* ctx);


private:
    static std::atomic<QApplicationContext*> theInstance;
};

template<typename S> template<typename D,typename R> Subscription Registration<S>::autowire(R (S::*injectionSlot)(D*)) {
    if(!injectionSlot || !registrationHolder) {
        qCCritical(loggingCategory(unwrap())).noquote().nospace() << "Cannot autowire " << *this << " with " << injectionSlot;
        return Subscription{};
    }
    if(!detail::hasCurrentThreadAffinity(applicationContext())) {
        qCCritical(loggingCategory(unwrap())).noquote().nospace() << "Wrong thread for autowiring " << *this;
        return Subscription{};
    }

    auto target = this->applicationContext()->template getRegistration<D>();
    auto callable = std::mem_fn(injectionSlot);
    auto subscription = new detail::CombiningSubscription<decltype(callable),S,D>{QList<registration_handle_t>{target.unwrap()}, target.unwrap(), callable, Qt::AutoConnection};
    return Subscription{unwrap()->subscribe(subscription)};
}




///
/// \brief A mix-in interface for classes that may modify services before publication.
/// The method process(service_registration_handle_t, QObject*,const QVariantMap&) will be invoked for each service after its properties have been set, but
/// before an *init-method* is invoked.
///
class QApplicationContextPostProcessor {
public:
    ///
    /// \brief Processes each service published by an ApplicationContext.
    /// \param handle references the service-registration.
    /// \param service the service-instance
    /// \param resolvedProperties the resolved properties for this service.
    ///
    virtual void process(service_registration_handle_t handle, QObject* service, const QVariantMap& resolvedProperties) = 0;

    virtual ~QApplicationContextPostProcessor() = default;
};



}


namespace std {
    template<> struct hash<mcnepp::qtdi::Subscription> {
        size_t operator()(const mcnepp::qtdi::Subscription& sub, [[maybe_unused]] size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        hash<mcnepp::qtdi::subscription_handle_t> hasher;
    };

    template<typename S,mcnepp::qtdi::ServiceScope scope> struct hash<mcnepp::qtdi::ServiceRegistration<S,scope>> {
        size_t operator()(const mcnepp::qtdi::ServiceRegistration<S,scope>& sub, [[maybe_unused]] size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        hash<mcnepp::qtdi::service_registration_handle_t> hasher;
    };


    template<typename S> struct hash<mcnepp::qtdi::ProxyRegistration<S>> {
        size_t operator()(const mcnepp::qtdi::ProxyRegistration<S>& sub, [[maybe_unused]] size_t seed = 0) const {
            return hasher(sub.unwrap());
        }

        hash<mcnepp::qtdi::proxy_registration_handle_t> hasher;
    };

    template<> struct hash<mcnepp::qtdi::detail::dependency_info> {
        std::size_t operator()(const mcnepp::qtdi::detail::dependency_info& info) const {
            return typeHasher(info.type) ^ info.kind;
        }
        hash<type_index> typeHasher;
    };



}




