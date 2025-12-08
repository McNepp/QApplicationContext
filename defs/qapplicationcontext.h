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
#include <QRegularExpression>

namespace mcnepp::qtdi {

class QApplicationContext;
class Condition;

namespace detail {
    class Registration;
    class ServiceRegistration;
    class ProxyRegistration;
    class Subscription;
    struct service_config;
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
 * <tr><td>SERVICE_GROUP</td><td>QApplicationContext::registerService(serviceGroup() << service()).</td><td>The services belonging to the service-group will be instantiated on QApplicationContext::publish(bool).<br>
 * Multiple references will be injected into every dependent service.</td></tr>
 * </table>
 */
enum class ServiceScope {
    UNKNOWN,
    SINGLETON,
    PROTOTYPE,
    EXTERNAL,
    TEMPLATE,
    SERVICE_GROUP
};

///
/// \brief Determines whether a %Service's *init-method* is invoked before or after the service is published.
/// <br>
/// <table>
/// <tr><td>DEFAULT</td><td>the *init-method* is invoked as the last step **before** the
/// publication of the service is announced.</td></tr>
/// <tr><td>AFTER_PUBLICATION</td><td>the *init-method* is invoked immediately **after** the
/// publication of the service has been announced.</td></tr>
/// </table>
///
enum class ServiceInitializationPolicy {
    DEFAULT,
    AFTER_PUBLICATION
};

template<typename S> class Registration;

template<typename S,ServiceScope> class ServiceRegistration;


Q_DECLARE_LOGGING_CATEGORY(defaultLoggingCategory)

///
/// A set of profiles.
/// Will be used at registration in order to specify for which profiles a %Service shall be active.
/// <br>Each QApplicationContext has a set of mcnepp::qtdi::QApplicationContext::activeProfiles().
///
using Profiles = QSet<QString>;


namespace detail {

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

template<> struct service_scope_traits<ServiceScope::SERVICE_GROUP> {
    static constexpr bool is_binding_source = false;
    static constexpr bool is_constructable = true;
};


// We cannot make use of service_scope_traits here, since we also want to test this at runtime:
constexpr bool is_allowed_as_dependency(ServiceScope scope) {
    return scope != ServiceScope::TEMPLATE && scope != ServiceScope::SERVICE_GROUP;
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
    using target_type = S*;

    target_type operator()(const QVariant& arg) const {
        return dynamic_cast<target_type>(arg.value<QObject*>());
    }
};


template<typename S> struct default_argument_converter<S,Kind::N> {
    using target_type = QList<S*>;

    target_type operator()(const QVariant& arg) const {
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

Q_SIGNALS:

    void objectPublished(QObject*);
};


template<typename T,typename M=const QMetaObject*> struct meta_type_traits {
    static constexpr std::nullptr_t getMetaObject() {
        return nullptr;
    }
};

template<typename T> struct meta_type_traits<T,decltype(&T::staticMetaObject)> {
    static constexpr const QMetaObject* getMetaObject() {
        return &T::staticMetaObject;
    }
};

using q_setter_t = std::function<void(QObject*,QVariant)>;

using q_init_t = std::function<void(QObject*,QApplicationContext*)>;

using q_variant_converter_t = std::function<QVariant(const QString&)>;

struct service_descriptor;



template<typename T,typename=QVariant> struct has_qvariant_support : std::false_type {

};

template<typename T> struct has_qvariant_support<T,decltype(QVariant{std::declval<T>()})> : std::true_type {

};


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



template<typename T,bool=std::disjunction_v<has_qvariant_support<T>,std::is_convertible<T,QObject*>>> struct variant_converter_traits;


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

///
/// \brief Describes the property of the source.
/// <br>A property may be identifier either by its name, or by its signalMethod.
/// In the latter case, the name will be empty.
///
struct source_property_descriptor {
    QByteArray name;
    QMetaMethod signalMethod;
};

inline QDebug operator << (QDebug out, const property_descriptor& descriptor) {
    if(!descriptor.name.isEmpty()) {
        out.noquote().nospace() << "property '" << descriptor.name << "'";
    }
    return out;
}

inline QDebug operator << (QDebug out, const source_property_descriptor& descriptor) {
    if(!descriptor.name.isEmpty()) {
        return out.noquote().nospace() << "property '" << descriptor.name << "'";
    }
    if(descriptor.signalMethod.isValid()) {
        return out.noquote().nospace() << "property '" << descriptor.signalMethod.name() << "'";
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




template<typename S,typename A,typename F> std::enable_if_t<std::is_invocable_v<F,S*,A>,q_setter_t> adaptSetter(F func) {
    using arg_type = detail::remove_cvref_t<A>;
    return [func](QObject* obj,QVariant arg) {
        if(S* ptr = dynamic_cast<S*>(obj)) {
            std::invoke(func,ptr, qvariant_cast<arg_type>{}(arg));
        }
    };
}

template<typename S> using simple_signal_t = void(S::*)();

template<typename SRC,typename SIGN,typename TGT,typename SLOT> struct property_change_signal_traits {
    static constexpr bool is_compatible = false;
};


template<typename SRC,typename TGT,typename R,typename TgtArg> struct property_change_signal_traits<SRC,simple_signal_t<SRC>,TGT,R(TGT::*)(TgtArg)> {
    using arg_type = TgtArg;
    //A parameterless signal is considered compatible with any setter, even though we cannot verify whether the source-property's type matches the target-property's
    static constexpr bool is_compatible = true;
};

template<typename SRC,typename TGT,typename R,typename TgtArg> struct property_change_signal_traits<SRC,simple_signal_t<SRC>,TGT,R(*)(TGT*,TgtArg)> {
    using arg_type = TgtArg;
    //A parameterless signal is considered compatible with any two-arg function, even though we cannot verify whether the source-property's type matches the target-property's
    static constexpr bool is_compatible = true;
};

template<typename SRC,typename TGT,typename R,typename TgtArg> struct property_change_signal_traits<SRC,simple_signal_t<SRC>,TGT,std::function<R(TGT*,TgtArg)>> {
    using arg_type = TgtArg;
    //A parameterless signal is considered compatible with any two-arg function, even though we cannot verify whether the source-property's type matches the target-property's
    static constexpr bool is_compatible = true;
};

template<typename SRC,typename SrcArg,typename TGT,typename R,typename TgtArg> struct property_change_signal_traits<SRC,void(SRC::*)(SrcArg),TGT,R(TGT::*)(TgtArg)> {
    using arg_type = TgtArg;
    static constexpr bool is_compatible = std::is_invocable_v<std::function<void(TgtArg)>,SrcArg>;
};

template<typename SRC,typename SrcArg,typename TGT,typename SLOT> struct property_change_signal_traits<SRC,void(SRC::*)(SrcArg),TGT,SLOT> {
    using arg_type = SrcArg;
    static constexpr bool is_compatible = std::is_invocable_v<SLOT,SRC*,SrcArg>;
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
        QDebugStateSaver save{out};
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

Q_SIGNALS:

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

Q_SIGNALS:
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


template<typename F,typename...S> class CombiningSubscription : public MultiServiceSubscription {
public:

    CombiningSubscription(const target_list_t& targets, QObject* context, F callable, Qt::ConnectionType connectionType) :
        MultiServiceSubscription{targets, context},
        m_context{context},
        m_callable{callable},
        m_connectionType{connectionType} {
    }

protected:

    virtual MultiServiceSubscription* newChild(const target_list_t& targets) override {
        return new CombiningSubscription<F,S...>{targets, m_context, m_callable, m_connectionType};
    }

    virtual QMetaObject::Connection connectObjectsPublished() override {
        return connect(this, &MultiServiceSubscription::objectsPublished, m_context, [this](const QObjectList& objs) {
            call(std::index_sequence_for<S...>{}, objs);
        }, m_connectionType);
    }




private:

    template<std::size_t...Indices> void call(std::index_sequence<Indices...>,const QObjectList& objs) {
        m_callable(dynamic_cast<S*>(objs[Indices])...);
    }

    QObject* m_context;
    F m_callable;
    Qt::ConnectionType m_connectionType;
};




class ServiceRegistration : public Registration {
    Q_OBJECT

    template<typename S,ServiceScope> friend class mcnepp::qtdi::ServiceRegistration;

    friend subscription_handle_t bind(service_registration_handle_t, const detail::source_property_descriptor&, registration_handle_t, const detail::property_descriptor&);

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

    /**
     * @brief The Condition for which this %Service was registered.
     * <br>The default is Condition::always().
     * @return The Condition for which this %Service was registered.
     */
    [[nodiscard]] virtual const Condition& registeredCondition() const = 0;


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
    /// \param sourcePropertyDescriptor
    /// \param target
    /// \param targetProperty
    /// \return the Subscription for binding this service to a target-service, or `nullptr` if something went wrong.
    ///
    virtual subscription_handle_t createBindingTo(const source_property_descriptor& sourcePropertyDescriptor, registration_handle_t target, const detail::property_descriptor& targetProperty) = 0;
};

subscription_handle_t bind(service_registration_handle_t source, const source_property_descriptor& sourcePropertyDescriptor, registration_handle_t target, const property_descriptor& targetPropertyDescriptor);

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

    QString makeConfigPath(const QString& section, const QString& path);

    bool removeLastConfigPath(QString& s);


///
/// \brief Generates a unique name of a property.
/// \param binaryData if not `nullptr`, will be translated into a hexadecimal representation.
/// \return a String comprising a representation of the supplied binary data.
///
    QString uniquePropertyName(const void* binaryData, std::size_t);

    template<typename F> QString uniqueName(F func) {
        if constexpr(std::disjunction_v<std::is_pointer<F>,std::is_member_function_pointer<F>>) {
            return uniquePropertyName(&func, sizeof func);
        } else {
            return uniquePropertyName(nullptr, 0);
        }
    }


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
        PRIVATE
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
    /// <br>This is an intermediate type that will be used solely to populate a Service.
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
            return left.properties == right.properties && left.group == right.group && left.autowire == right.autowire && left.autoRefresh == right.autoRefresh && left.serviceGroupPlaceholder == right.serviceGroupPlaceholder;
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

        ///
        /// \brief the name of the placeholder for service-groups.
        ///
        QString serviceGroupPlaceholder;
    };

    inline service_config merge_config(const service_config& first, const service_config& second) {
        service_config merged{first};
        if(first.group.isEmpty()) {
            merged.group = second.group;
        }
        if(first.serviceGroupPlaceholder.isEmpty()) {
            merged.serviceGroupPlaceholder = second.serviceGroupPlaceholder;
        }
        merged.autoRefresh |= second.autoRefresh;
        merged.autowire |= second.autowire;
        merged.properties.insert(second.properties);

        return merged;
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
    /// \brief Specifies a dependency of a service.
    /// <br>Can by used as a type-argument for QApplicationContext::registerService().
    /// Usually, you will not instantiate `Dependency`directly. Rather, you will use one of the functions
    /// mcnepp::qtdi::inject(), mcnepp::qtdi::injectIfPresent() or mcnepp::qtdi::injectAll().
    ///
    /// \tparam S the service-interface of the Dependency
    /// \tparam kind the kind of Dependency
    /// \sa mcnepp::qtdi::inject()
    /// \sa mcnepp::qtdi::injectIfPresent()
    /// \sa mcnepp::qtdi::injectAll()
    template<typename S,Kind kind> struct Dependency {
        static_assert(could_be_qobject<S>::value, "Dependency must be potentially convertible to QObject");
        ///
        /// \brief the required name for this dependency.
        /// The default-value is the empty String, with the implied meaning <em>"any dependency of the correct type may be used"</em>.
        ///
        QString requiredName;
    };

    ///
    /// \brief Specifies a dependency on another Service.
    /// The dependency shall be resolved by invoking an "accessor" function that
    /// accepts a pointer to the Service of type S and yields a pointer to an Object of type R.
    ///
    template<typename S,typename R> struct ComputedDependency : Dependency <S,Kind::MANDATORY> {
        std::function<R(S*)> dependencyAccessor;
    };

}// end namespace detail



///
/// \brief Makes conditional activation of Services possible.
/// <br>An instance of this type is passed to QApplicationContext::registerService().
/// <br>The default ist Condition::always().
///
class Condition final {
public:

    ///
    /// \brief An intermediate type that is used to create Conditions based on active profiles.
    /// <br>Can be used via the singleton Condition::Profile
    /// <br>Examples:
    ///
    ///     context->registerService(service<QTimer>(), "timer", Condition::Profile == "test");
    ///
    ///     context->registerService(service<QTimer>(), "timer", Condition::Profile & Profiles{"default", "prod"});
    ///
     class ProfileHelper {
         friend class Condition;
     public:
         ///
         /// \brief Yields a Condition that tests for a specific profile.
         /// <br>The condition will be met if the *active profiles* contain the supplied one.
         /// \param profile the profile to test for
         /// \return a Condition that tests for a specific profile.
         ///
         Condition operator ==(QAnyStringView profile) const;

         ///
         /// \brief Yields a Condition that tests for absence of a specific profile.
         /// <br>The condition will be met if the *active profiles* do not contain the supplied one.
         /// \param profile the profile to test for
         /// \return a Condition that tests for absence of a specific profile.
         ///
        Condition operator !=(QAnyStringView profile) const;


        /// \brief Yields a Condition that tests for a choice of profiles.
        /// <br>The condition will be met if the *active profiles* contain at least one of the supplied ones.
        /// \param profiles
        /// \return a Condition that tests for a choice of profiles.
        ///
        Condition operator&(const Profiles& profiles) const;

        /// \brief Yields a Condition that tests for a choice of profiles.
        /// <br>The condition will be met if the *active profiles* contain at least one of the supplied ones.
        /// \param profiles
        /// \return a Condition that tests for a choice of profiles.
        ///
        Condition operator&(Profiles&& profiles) const;


        /// \brief Yields a Condition that tests for absence of a choice of profiles.
        /// <br>The condition will be met if the *active profiles* contain none of the supplied ones.
        /// \param profiles
        /// \return a Condition that tests for absence of a choice of profiles.
        ///
        Condition operator^(const Profiles& profiles) const;

        /// \brief Yields a Condition that tests for absence of a choice of profiles.
        /// <br>The condition will be met if the *active profiles* contain none of the supplied ones.
        /// \param profiles
        /// \return a Condition that tests for absence of a choice of profiles.
        ///
        Condition operator^(Profiles&& profiles) const;

    private:
        ProfileHelper() = default;
        ProfileHelper(const ProfileHelper&) = delete;
    };


     ///
     /// \brief The global ProfileHelper.
     /// <br>Allows you to create Profile-based Conditions. Examples:
     ///
     ///     context->registerService(service<QTimer>(), "timer", Condition::Profile == "test");
     ///
     ///     context->registerService(service<QTimer>(), "timer", Condition::Profile & Profiles{"default", "prod"});
     ///
     static constexpr ProfileHelper Profile{};



    ///
    /// \brief An intermediate type that is used to create Conditions based on configuration-entries.
    /// <br>Can be used via the singleton Condition::Config
    /// <br>The only available operation is the subscript-operator, which selects a configuration-entry.
    ///
    class ConfigHelper final {
        friend class Condition;
        static constexpr int MATCH_TYPE_EQUALS = 0;
        static constexpr int MATCH_TYPE_NOT_EQUALS = 1;
        static constexpr int MATCH_TYPE_LESS = 2;
        static constexpr int MATCH_TYPE_GREATER = 3;
        static constexpr int MATCH_TYPE_LESS_OR_EQUAL = 4;
        static constexpr int MATCH_TYPE_GREATER_OR_EQUAL = 5;

        using predicate_t = std::function<bool(const QVariant&,const QVariant&)>;

        friend class Matchers;

    public:

        ///
        /// \brief An intermediate type that is used to create Conditions based on configuration-entries.
        ///
        class Entry final {
            friend class ConfigHelper;
        public:


            ///
            /// \brief Yields a Condition that is met if the configuration-entry is resolvable.
            /// \return a Condition that is met if the configuration-entry is resolvable.
            ///
            Condition exists() const;

            ///
            /// \brief Yields a Condition that is met if the configuration-entry is not resolvable.
            /// <br>Given a `Condition cond`, the expression `!cond` is equivalent to `!cond.exists()`.
            /// \return a Condition that is met if the configuration-entry is not resolvable.
            ///
            Condition operator!() const;

            ///
            /// \brief Yields a Condition that is met if the configuration-entry is equal to a specific value.
            /// \param refValue the value to test for.
            /// \return a Condition that is met if the configuration-entry is resolvable and equals the supplied value.
            ///
            Condition operator ==(const QVariant& refValue) const;

            ///
            /// \brief Yields a Condition that is met if the configuration-entry is not equal to a specific value.
            /// \param refValue the value to test for.
            /// \return a Condition that is met if the configuration-entry is either not resolvable or does not equal the supplied value.
            ///
            Condition operator !=(const QVariant& refValue) const;

            ///
            /// \brief Yields a Condition that is met if the configuration-entry is less than a specific value.
            /// <br>**Note:** An invalid (unresolvable) configuration-entry will never be less than anything!
            /// \param refValue the value to test for.
            /// \return a Condition that is met if the configuration-entry is resolvable and less than the supplied value.
            ///
            template<typename T,typename L=std::less<T>> Condition operator<(T&& refValue) const {
                return Condition{matcherForConfigEntry(m_expression, QVariant::fromValue(std::forward<T>(refValue)), MATCH_TYPE_LESS, lessThan<T,L>())};
            }


            ///
            /// \brief Yields a Condition that is met if the configuration-entry is less than or equal to a specific value.
            /// <br>**Note:** An invalid (unresolvable) configuration-entry will never be less than anything!
            /// \param refValue the value to test for.
            /// \return a Condition that is met if the configuration-entry is resolvable and less than or equal to the supplied value.
            ///
            template<typename T,typename L=std::less<T>> Condition operator<=(T&& refValue) const {
                return Condition{matcherForConfigEntry(m_expression, QVariant::fromValue(std::forward<T>(refValue)), MATCH_TYPE_LESS_OR_EQUAL, lessThan<T,L>())};
            }

            ///
            /// \brief Yields a Condition that is met if the configuration-entry is greater than a specific value.
            /// <br>**Note:** An invalid (unresolvable) configuration-entry will never be greater than anything!
            /// \param refValue the value to test for.
            /// \return a Condition that is met if the configuration-entry is resolvable and greater than the supplied value.
            ///
            template<typename T,typename L=std::less<T>> Condition operator>(T&& refValue) const {
                return Condition{matcherForConfigEntry(m_expression, QVariant::fromValue(std::forward<T>(refValue)), MATCH_TYPE_GREATER, lessThan<T,L>())};
            }

            ///
            /// \brief Yields a Condition that is met if the configuration-entry is greater than or equal to a specific value.
            /// <br>**Note:** An invalid (unresolvable) configuration-entry will never be greater than anything!
            /// \param refValue the value to test for.
            /// \return a Condition that is met if the configuration-entry is resolvable and greater than or equal to the supplied value.
            ///
            template<typename T,typename L=std::less<T>> Condition operator>=(T&& refValue) const {
                return Condition{matcherForConfigEntry(m_expression, QVariant::fromValue(std::forward<T>(refValue)), MATCH_TYPE_GREATER_OR_EQUAL, lessThan<T,L>())};
            }


            ///
            /// \brief Yields a Condition that is met if the configuration-entry matches a regular expression.
            /// \param regEx the expression to match.
            /// \return a Condition that is met if the configuration-entry is resolvable and its string-representation matches the regEx.
            ///
            Condition matches(const QRegularExpression& regEx) const;

            ///
            /// \brief Yields a Condition that is met if the configuration-entry matches a regular expression.
            /// \param regEx the String containing the expression to match.
            /// \param options will be passed to QRegularExpression's constructor.
            /// \return a Condition that is met if the configuration-entry is resolvable and its string-representation matches the regEx.
            ///
            Condition matches(const QString& regEx, QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption) const;



        private:

            template<typename T,typename L> static predicate_t lessThan() {
                return [less=L{}](const QVariant& left, const QVariant& right) {
                        return less(left.value<T>(), right.value<T>());
                    };
            }

            explicit constexpr Entry(QAnyStringView expression) : m_expression{expression} {

            }

            QAnyStringView m_expression;
        };

        ///
        /// \brief Selects a configuration-entry.
        /// \param expression comprised of literal strings and *placeholders*.
        /// \return a resolvable entry.
        ///
        constexpr Entry operator[](QAnyStringView expression) const {
            return Entry{expression};
        }
    private:
        ConfigHelper() = default;
        ConfigHelper(const ConfigHelper&) = delete;
    };

    ///
    /// \brief The global ConfigHelper.
    /// <br>Allows you to create Configuration-based Conditions. Example:
    ///
    ///     context->registerService(service<QTimer>(), "timer", Condition::Config["${timer/singleShot}"] == true);
    ///
    static constexpr ConfigHelper Config{};


    friend QDebug operator<<(QDebug out, const Condition& cond);

    ///
    /// \brief Is this Condition met?
    /// \param context the ApplicationContext for which the Condition is evaluated.
    /// \return `true` if this Condition was met.
    ///
    bool matches(QApplicationContext* context) const;

    ///
    /// \brief A Condition that is always met.
    /// <br>This is the default for QApplicationContext::registerService().
    /// <br>**Note:** Just like with any other Condition, this Condition can be negated.
    /// However, that will result in the "Never" Condition which is of no apparent use.
    /// \return A Condition that is always met.
    ///
    static Condition always();

    ///
    /// \brief Is this a Condition related to Profiles?
    /// \return `true` if this is a Condition obtained via Condition::Profile.
    ///
    bool hasProfiles() const;


    friend bool operator==(const Condition& left, const Condition& right);

    friend bool operator!=(const Condition& left, const Condition& right);

    ///
    /// \brief Does this Condition overlap with another one?
    /// <br>Two Service-registrations with the same name but different Conditions are permitted only
    /// if those Conditions do not overlap().
    /// <br>**Note:** Condition::always() overlaps with any other Condition!
    /// \param other
    /// \return `true` if this Condition overlaps with `other`.
    ///
    bool overlaps(const Condition& other) const;

    ///
    /// \brief Is this the "always" Condition?
    /// \return `true` if this is the Condition obtained via always().
    ///
    bool isAlways() const;

    ///
    /// \brief Negates this Condition.
    /// <br>Yields a Condition that *matches* whenever this Condition does not match.
    /// \return a Condition that is the negation of this Condition.
    ///
    Condition operator!() const;



private:

    friend class Matchers;

    class Matcher : public QSharedData {
    public:
        virtual ~Matcher() = default;

        virtual bool matches(QApplicationContext*) const = 0;

        virtual void print(QDebug out) const = 0;

        virtual bool equals(const Matcher* other) const = 0;

        virtual Matcher* otherwise() const = 0;

        virtual bool hasProfiles() const {
            return false;
        }

        virtual bool overlaps(const Matcher* other) const {
            return equals(other);
        }

        virtual bool isAlways() const {
            return false;
        }
    };

    explicit Condition(Matcher* matcher) : m_data{matcher} {

    }

    static Matcher* matcherForConfigEntry(QAnyStringView expression, const QVariant& refValue, int matchType, ConfigHelper::predicate_t lessPredicate);

    QExplicitlySharedDataPointer<Matcher> m_data;
};

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
/// \brief Obtains the Condition for which the %Service was registered.
/// <br>The default is Condition::always()
/// \param handle the handle to the ServiceRegistration.
/// \return the Condition for which the %Service was registered.
///
[[nodiscard]] inline Condition registeredCondition(service_registration_handle_t handle) {
    return handle ? handle->registeredCondition() : Condition::always();
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

    template<typename T,ServiceScope TSCP> friend class ServiceRegistration;
public:
    using service_type = S;

    static constexpr ServiceScope Scope = SCP;

    [[nodiscard]] QString registeredName() const {
        return mcnepp::qtdi::registeredName(unwrap());
    }

    [[nodiscard]] service_registration_handle_t unwrap() const {
        //We can use static_cast here, as the constructor enforces the correct type:
        return static_cast<service_registration_handle_t>(Registration<S>::unwrap());
    }

    /**
     * @brief The Condition for which this %Service was registered.
     * <br>The default is Condition::always()
     * @return The Condition for which this %Service was registered.
     */
    [[nodiscard]] Condition registeredCondition() const {
        return mcnepp::qtdi::registeredCondition(unwrap());
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

namespace detail {

//Tests whether the type T is a ServiceRegistration with type Srv and an arbitrary ServiceScope:
template<typename Srv,typename T> struct is_service_registration : std::false_type {};

template<typename Srv,ServiceScope scope> struct is_service_registration<Srv,mcnepp::qtdi::ServiceRegistration<Srv,scope>> : std::true_type {};

}

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
template<typename S,typename T,ServiceScope scope> Subscription bind(const ServiceRegistration<S,scope>& source, const char* sourceProperty, const Registration<T>& target, const char* targetProperty) {
    static_assert(std::is_base_of_v<QObject,T>, "Target must be derived from QObject");
    static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
    static_assert(detail::service_scope_traits<scope>::is_binding_source, "The scope of the service does not permit binding");

    return Subscription{detail::bind(source.unwrap(), {sourceProperty}, target.unwrap(), {targetProperty, nullptr})};
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
/// \tparam SLT the type of the slot
/// \return the Subscription established by this binding.
///
template<typename S,typename T,typename SLT,ServiceScope scope> auto bind(const ServiceRegistration<S,scope>& source, const char* sourceProperty, const Registration<T>& target, SLT setter) ->
std::enable_if_t<detail::property_change_signal_traits<S,detail::simple_signal_t<S>,T,SLT>::is_compatible,Subscription> {
    static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
    static_assert(detail::service_scope_traits<scope>::is_binding_source, "The scope of the service does not permit binding");

    if(!setter) {
        qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Cannot bind " << source << " to null";
        return Subscription{};
    }
    return Subscription{detail::bind(source.unwrap(), {sourceProperty}, target.unwrap(), {detail::uniqueName(setter).toLatin1(), detail::adaptSetter<T,typename detail::property_change_signal_traits<S,detail::simple_signal_t<S>,T,SLT>::arg_type>(setter)})};
}





///
/// \brief Binds a property of one ServiceRegistration to a slot from  another Registration.
/// <br>This function identifies the source-property by the signal that is emitted when the property changes.
/// The signal is specified in terms of a pointer to a member-function. This member-function must denote the signal corresponding
/// to a property of the source-service, according to `QMetaMethod::fromSignal(SI)`.
/// <br>If `SI` denotes a signal with no argument, then `F` must denote a pointer to a member-function of `T`, taking one argument. There will be no compile-time check
/// whether the type of the source-property actually matches the type of the target-property!
/// <br>If `SI` denotes a signal with one argument, then `F` shall either denote a pointer to a member-function of `T`, taking one argument that is assignment-compatible with
/// the signal's argument, or it shall denote a callable object that can be invoked with one argument of type `T*` and one argument of the signal's argument-type.
/// <br>All changes made to the source-property will be propagated to all Services represented by the target.
/// For each target-property, there can be only successful call to bind().
/// <br>**Thread-safety:** This function only may be called from the QApplicationContext's thread.
/// \param source the ServiceRegistration with the source-property to which the target-property shall be bound.
/// \param signalFunction the address of the member-function that is emitted as the signal for the property.
/// \param target the Registration with the target-property to which the source-property shall be bound.
/// \param func the method in the target which shall be bound to the source-property.
/// \tparam S the type of the source.
/// \tparam T the type of the target
/// \tparam A the type of the property.
/// \tparam SIGN the type of the signal-function. Must be a non-static member-function of S with either no arguments or one argument of type A.
/// \tparam SLT the type of the slot. Must either be a non-static member-function of T with one argument,
/// or a callable object taking two arguments, one of type `T*` and the second of the signal's argument-type.
/// \return the Subscription established by this binding.
///
template<typename S,typename T,typename SIGN,typename SLT,ServiceScope scope> auto bind(const ServiceRegistration<S,scope>& source, SIGN signalFunction, const Registration<T>& target, SLT func) ->
    std::enable_if_t<detail::property_change_signal_traits<S,SIGN,T,SLT>::is_compatible,Subscription> {
    static_assert(std::is_base_of_v<QObject,S>, "Source must be derived from QObject");
    static_assert(detail::service_scope_traits<scope>::is_binding_source, "The scope of the service does not permit binding");

    if(signalFunction) {
        if(auto signalMethod = QMetaMethod::fromSignal(signalFunction); signalMethod.isValid()) {
            return Subscription{detail::bind(source.unwrap(), {{}, signalMethod}, target.unwrap(), {detail::uniqueName(func).toLatin1(), detail::adaptSetter<T,typename detail::property_change_signal_traits<S,SIGN,T,SLT>::arg_type>(func)})};
        }
    }

    qCCritical(loggingCategory(source.unwrap())).noquote().nospace() << "Cannot bind " << source << " to " << target;
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
template<typename S,typename SIG,typename T,typename SLT> Subscription connectServices(const Registration<S>& source, SIG sourceSignal, const Registration<T>& target, SLT targetSlot, Qt::ConnectionType connectionType = Qt::AutoConnection) {
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

    explicit ServiceCombination(const Registration<S>&... registrations) {
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

    template<typename First,typename...Tail> bool add(const Registration<First>& first, const Registration<Tail>&... tail) {
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
template<typename...S> std::enable_if_t<(sizeof...(S) > 1),ServiceCombination<S...>> combine(const Registration<S>&...registrations) {
    return ServiceCombination<S...>{registrations...};
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
/// - initializer_type an alias for std::nullptr_t.
/// - static constexpr ServiceInitializationPolicy initialization_policy = ServiceInitializationPolicy::DEFAULT;
/// - factory_type an alias for service_factory.
///
/// \tparam S the service-type
/// \tparam I the type of the initializer. Defaults to std::nullptr_t.
/// \tparam serviceInitPolicy determines whether the initializer will be invoked before publication (the default) or after it.
/// \tparam F the type of the service-factory. Defaults to mcnepp::qtdi::service_factory.
///

template<typename S,typename I=std::nullptr_t,ServiceInitializationPolicy serviceInitPolicy=ServiceInitializationPolicy::DEFAULT,typename F=service_factory<S>> struct default_service_traits {
    static_assert(detail::could_be_qobject<S>::value, "Type must be potentially convertible to QObject");

    using service_type = S;

    using initializer_type = I;

    static constexpr ServiceInitializationPolicy initialization_policy = serviceInitPolicy;

    using factory_type = F;
};


///
/// \brief The traits for services.
/// <br>Every specialization of this template must provide at least the following declarations:
/// - `service_type` the type of service that this traits describe.
/// - `initializer_type` the type of the initializer. Must be one of the following:
///   -# pointer to a non-static member-function with no arguments
///   -# pointer to a non-static member-function with one parameter of type `QApplicationContext*`
///   -# address of a free function with one argument of the service-type.
///   -# address of a free function with two arguments, the second being of type `QApplicationContext*`.
///   -# type with a call-operator with one argument of the service-type.
///   -# type with a call-operator with two arguments, the second being of type `QApplicationContext*`.
///   -# `nullptr`
/// - a `static constexpr` value named `initialization_policy` of type `ServiceInitializationPolicy`
/// - `factory_type` the type of the factory.
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
/// Denotes the kind of dependency.
///
using DependencyKind = detail::Kind;

///
/// An opaque type describing a dependency for a %Service.
///
template<typename S,DependencyKind kind> using Dependency = detail::Dependency<S,kind>;


///
/// \brief Injects a mandatory Dependency.
/// <br>This will instruct the ApplicationContext to find exactly one Dependency of the service-type and inject it
/// into the *dependent %Service*.
/// <br>The lookup may additionally be further restricted using the name of another %Service.
/// <br>Should there not be exactly one %Service of the service-type available, the instantiation of the dependent %Service will fail.
/// <br>
/// Suppose you have a service-type `Reader` that needs a mandatory pointer to a `DatabaseAccess` in its constructor:
///
///     class Reader : public QObject {
///       public:
///         explicit Reader(DatabaseAccess* dao, QObject* parent = nullptr);
///     };
///
/// A %Service of type `Reader` may be registered like this:
///
///     context->registerService(service<Reader>(inject<DatabaseAccess>()), "reader");
///
///
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \tparam S the service-type of the dependency.
/// \return a mandatory Dependency on the supplied type.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,DependencyKind::MANDATORY> inject(const QString& requiredName = {}) {
    return Dependency<S,DependencyKind::MANDATORY>{requiredName};
}




///
/// \brief Injects a *computed* Dependency.
/// <br>The dependency will be obtained from another, previously registered service by means of an "accessor" function.
/// <br>**Note:** The dependency will be obtained from the providing service **before** its properties have been configured,
/// and also **before** the init-method of the providing service has run.
/// <br>In other words: you should only inject computed dependencies that are fully initialized in the providing service's constructor.
/// \param registration the Registration of the Service that provides the computed dependency.
/// \param accessor will be used to obtain the dependency from the service.
/// \tparam S the service-type of the dependency.
/// \tparam F the type of the accessor function. Must be either a callable that accepts a pointer to S, or a pointer to a member-function of S.
/// \return an opaque Object representing the Dependency.
///
template<typename S,typename F,ServiceScope scope> auto inject(const ServiceRegistration<S,scope>& registration, F accessor) ->
std::enable_if_t<detail::is_allowed_as_dependency(scope) && std::is_invocable_v<F,S*>,detail::ComputedDependency<S,std::invoke_result_t<F,S*>>> {
    //ServiceScope could be UNKNOWN statically, which passes the check, but TEMPLATE at runtime:
    if(!registration || !detail::is_allowed_as_dependency(registration.unwrap() -> scope())) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot inject ServiceRegistration " << registration;
        return {".invalid", nullptr};
    }
    auto f = [accessor](S* srv) { return std::invoke(accessor, srv);};
    return detail::ComputedDependency<S,typename std::invoke_result_t<F,S*>>{registration.registeredName(), f};
}




///
/// \brief Injects an optional Dependency.
/// <br>This will instruct the ApplicationContext to find exactly one Dependency of the service-type and inject it
/// into the *dependent %Service*.
/// <br>The lookup may additionally be further restricted using the name of another %Service.
/// <br>Should there not be exactly one %Service of the service-type available, the dependent %Service will be created
/// with a `nullptr` passed as the dependency.
/// <br>
/// Suppose you have a service-type `Reader` that accepts an optional pointer to a `DatabaseAccess` in its constructor:
///
///     class Reader : public QObject {
///       public:
///         explicit Reader(DatabaseAccess* dao = nullptr, QObject* parent = nullptr);
///     };
///
/// A %Service of type `Reader` may be registered like this:
///
///     context->registerService(service<Reader>(injectIfPresent<DatabaseAccess>()), "reader");
///
///
/// \param requiredName the required name of the dependency. If empty, no name is required.
/// \tparam S the service-type of the dependency.
/// \return an optional Dependency on the supplied type.
///
template<typename S> [[nodiscard]] constexpr Dependency<S,DependencyKind::OPTIONAL> injectIfPresent(const QString& requiredName = {}) {
    return Dependency<S,DependencyKind::OPTIONAL>{requiredName};
}





///
/// \brief Injects a 1-to-N Dependency.
/// <br>This will instruct the ApplicationContext to find as many Dependencies of the service-type and inject them
/// into the *dependent %Service*.
/// <br>The lookup may additionally be further restricted using the names of other %Services.
/// <br>Should there not be any %Service of the service-type available, the dependent %Service will be created
/// with an empty `QList` passed as the dependency.
/// <br>
/// Suppose you have a service-type `Collector` that needs a `QList<Reader>` in its constructor:
///
///     class Collector : public QObject {
///       public:
///         explicit Collector(const QList<Reader>& readers, QObject* parent = nullptr);
///     };
///
/// A %Service of type `Collector` may be registered like this:
///
///     context->registerService(service<Collector>(injectAll<Reader>()), "collector");
///
///
/// \param requiredNames the required names for the dependencies. If empty, no name is required.
/// \tparam S the service-type of the dependency.
/// \return a 1-to-N Dependency on the supplied type.
///
template<typename S,typename...Names> [[nodiscard]] auto injectAll(Names&&...requiredNames) ->
std::enable_if_t<std::conjunction_v<std::is_convertible<detail::remove_cvref_t<Names>,QString>...>,Dependency<S,DependencyKind::N>> {
    QStringList requiredNamesList;
    (requiredNamesList.push_back(requiredNames), ...);
    return Dependency<S,DependencyKind::N>{requiredNamesList.join(',')};
}

///
/// \brief Injects a 1-to-N Dependency.
/// <br>You may restrict the set of dependencies by supplying some ServiceRegistrations, of which the ServiceRegistration::registeredName()
/// will be used.
/// \param first the first ServiceRegistration.
/// \param tail more ServiceRegistrations.
/// \tparam S the service-type of the dependency.
/// \tparam Reg a list of values implicitly convertible to `Registration<S>`.
/// \return a 1-to-N  Dependency on the supplied registration.
///
template<typename S,ServiceScope scope,typename...Reg> [[nodiscard]] auto injectAll(const ServiceRegistration<S,scope>& first, Reg&&... tail) ->
    std::enable_if_t<std::conjunction_v<detail::is_service_registration<S,detail::remove_cvref_t<Reg>>...>,Dependency<S,DependencyKind::N>> {
    QStringList requiredNames;
    (requiredNames.push_back(first.registeredName()), ..., requiredNames.push_back(tail.registeredName()));
    return Dependency<S,DependencyKind::N>{requiredNames.join(',')};
}

///
/// An opaque type denoting a constructor-argument for a %Service.
/// \sa mcnepp::qtdi::resolve(const QString&)
///
template<typename S> using Resolvable = detail::Resolvable<S>;


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
///     context -> registerService(service<QIODevice,QFile>(resolve("${* /filename}")) << withGroup("files"));
///
/// The key "filename" will first be searched in the section "files". If it cannot be found, it will be searched in the root-section.
///
///
/// \tparam S the result-type of the resolved constructor-argument.
/// \tparam C the type of the converter. Must be a callable that accepts a QString and returns a value of type `S`. The default will invoke the constructor
/// `S{const QString&}` if it exists.
/// \param defaultValue the value to use if the placeholder cannot be resolved.
/// \param expression a String, possibly containing one or more placeholders.
/// \param converter Will be used to convert the resolved expression into a value.
/// \return a Resolvable instance for the supplied type.
///
template<typename S,typename C> [[nodiscard]] auto resolve(const QString& expression, const S& defaultValue, C converter) ->
std::enable_if_t<std::is_convertible_v<std::invoke_result_t<C,QString>,S>,Resolvable<S>> {
    return Resolvable<S>{expression, QVariant::fromValue(defaultValue), detail::variant_converter_traits<S>::makeConverter(converter)};
}




///
/// \brief Specifies a constructor-argument that shall be resolved by the QApplicationContext.
/// <br>This is an overload of mcnepp::qtdi::resolve(const QString&,const S&,C) without the default-value.
///
template<typename S,typename C> [[nodiscard]] auto resolve(const QString& expression, C converter) ->
std::enable_if_t<std::is_convertible_v<std::invoke_result_t<C,QString>,S>,Resolvable<S>> {
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


template<typename S> using service_config_entry=detail::service_config_entry_t<S>;




///
/// \brief Enables auto-refresh for a service_config.
/// <br>This function is not meant to be invoked directly.
/// <br>Rather, its usage is analogous to that of *iostream-manipulators* from the standard-libray:
///
///
///     context->registerService(service<MyService>() << withAutoRefresh << propValue("objectName", "${myService}"));
///
inline void withAutoRefresh(detail::service_config& cfg) {
    cfg.autoRefresh = true;
};

///
/// \brief Applies auto-wiring to a service_config.
/// <br>This function is not meant to be invoked directly.
/// <br>Rather, its usage is analogous to that of *iostream-manipulators* from the standard-library:
///
///
///     context->registerService(service<MyService>() << withAutowire);
///
/// Applying auto-wiring has the following consequences:
///
/// - After the %Service has been instantiated, all its properties (obtained via its QMetaObject) will be inspected.
/// - Properties will be resolved using the registered QSettings: If a matching configuration-entry
/// can be found in the section specified by mcnepp::qtdi::withGroup(const QString&), this entry's value will be injected.<br>
/// Otherwise, if a matching configuration-entry can be found in the section matching the %Service's registered name, that entry's value will be injected.
/// - Properties for which no configuration-entry can be found will be injected if a matching service
/// has been registered under the name of the property is present.<br>Otherwise, if exactly one service of the property's type
/// has been registered, that will be injected.
///
/// Example: Suppose there is the following class:
///
///     class PingService {
///        public:
///           Q_PROPERTY(QNetworkAccessManager* networkManager READ networkManager WRITE setNetworkManager NOTIFY networkManagerChanged)
///           Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
///     };
///
/// Then we assume a file `"context.ini"` with the following contents:
///
///     [ping]
///     timeout=5000
///
/// Then, with the following code, the `PingService` will be auto-wired:
///
///     context->registerService(service<QSettings>("context.ini", QSettings::IniFormat));
///     context->registerService<QNetworkAccessManager>("networkManager");
///     context->registerService(service<PingService>() << withAutowire, "ping");
///
///
inline void withAutowire(detail::service_config& cfg) {
    cfg.autowire = true;
};


///
/// \brief Sets the group for a service_config.
/// <br>The usage of this function is analogous to that of *iostream-manipulators* from the standard-libray:
///
///     context->registerService(service<MyService>() << withGroup("myServices") << propValue("objectName", "${myService}"));
///
/// \param groupExpression the group to use. This expression may contain *placeholders*, which will be resolved against the configuration at the time
/// the service is published.
inline detail::service_config::config_modifier withGroup(const QString& groupExpression) {
    return [groupExpression](detail::service_config& cfg) { cfg.group = groupExpression;};
}















///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
///
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the property-value.
/// \param expression will be resolved when the service is being configured. May contain *placeholders*.
/// \param converter specifies a converter that constructs an argument of type `A` from a QString.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,typename C> [[nodiscard]] auto propValue(R(S::*propertySetter)(A), const QString& expression, C converter) ->
    std::enable_if_t<std::is_convertible_v<std::invoke_result_t<C,QString>,A>,service_config_entry<S>>

{
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }

    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{expression, detail::ConfigValueType::DEFAULT, detail::adaptSetter<S,A>(propertySetter), detail::variant_converter_traits<detail::remove_cvref_t<A>>::makeConverter(converter)}};
}

///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
///
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the property-value.
/// \param expression will be resolved when the service is being configured. May contain *placeholders*.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A> [[nodiscard]] service_config_entry<S> propValue(R(S::*propertySetter)(A), const QString& expression) {
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }

    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{expression, detail::ConfigValueType::DEFAULT, detail::adaptSetter<S,A>(propertySetter), detail::variant_converter_traits<detail::remove_cvref_t<A>>::makeConverter()}};
}



///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the property-value.
/// \param value will be set when the service is being configured.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A> [[nodiscard]] service_config_entry<S> propValue(R(S::*propertySetter)(A), A value) {
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }

    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(value), detail::ConfigValueType::DEFAULT, detail::adaptSetter<S,A>(propertySetter)}};
}



///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with the instance of the service of type `<A>`.
/// \param reg the registration for the service-instance that will be injected into the configured service.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,ServiceScope scope> [[nodiscard]] auto propValue(R(S::*propertySetter)(A*), const ServiceRegistration<A,scope>& reg) -> std::enable_if_t<detail::is_allowed_as_dependency(scope),service_config_entry<S>>
{
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }
    if(!reg || !detail::is_allowed_as_dependency(reg.unwrap() -> scope())) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot inject ServiceRegistration " << reg;
        return {".invalid", QVariant{}};
    }
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(reg.unwrap()), detail::ConfigValueType::SERVICE, detail::adaptSetter<S,A*>(propertySetter)}};
}




///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with a `QList<A*>`, comprising all service-instances that have been published for the
/// supplied ProxyRegistration.
/// \param reg the registration for those services that will be injected into the configured service.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,typename L> [[nodiscard]] auto propValue(R(S::*propertySetter)(L), const ProxyRegistration<A>& reg) -> std::enable_if_t<std::is_convertible_v<L,QList<A*>>,service_config_entry<S>>
{
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }
    if(!reg) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot inject invalid ServiceRegistration";
        return {".invalid", QVariant{}};
    }
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(reg.unwrap()), detail::ConfigValueType::SERVICE, detail::adaptSetter<S,L>(propertySetter), nullptr}};
}

///
/// \brief Creates a type-safe configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// \tparam S the service-type.
/// \param propertySetter the member-function that will be invoked with a `QList<A*>`, comprising all service-instances that have been published for the
/// supplied Service-group.
/// \param reg the registration for the Service-group whose services will be injected into the configured service.
/// \return a type-safe configuration for a service.
///
template<typename S,typename R,typename A,typename L> [[nodiscard]] auto propValue(R(S::*propertySetter)(L), const ServiceRegistration<A,ServiceScope::SERVICE_GROUP>& reg) -> std::enable_if_t<std::is_convertible_v<L,QList<A*>>,service_config_entry<S>>
{
    if(!propertySetter) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot set invalid property";
        return {".invalid", QVariant{}};
    }
    if(!reg) {
        qCCritical(defaultLoggingCategory()).nospace() << "Cannot inject invalid ServiceRegistration";
        return {".invalid", QVariant{}};
    }
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{QVariant::fromValue(reg.unwrap()), detail::ConfigValueType::SERVICE, detail::adaptSetter<S,L>(propertySetter), nullptr}};
}




///
/// \brief Creates a type-safe, auto-refreshing configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// <br>In order to demonstrate the purpose, consider this example of a normal, non-updating service-configuration for a QTimer:
///
///     context->registerService(service<QTimer>() << propValue(&QTimer::setInterval, "${timerInterval}"), "timer");
///
/// The member-function will be initialized from the value of the configuration-key `"timerInterval"` as it
/// is in the moment the timer is instantiated.
///
/// <br>Now, contrast this with:
///
///     context->registerService(service<QTimer>() << autoRefresh(&QTimer::setInterval, "${timerInterval}"), "timer");
///
/// Whenever the value for the configuration-key `"timerInterval"` changes in the underlying QSettings-Object, the
/// expression `"${timerInterval}"` will be re-evaluated and the member-function of the timer will be updated accordingly.
/// <br>In case all properties for one service shall be auto-refreshed, there is a more concise way of specifying it:
///
///     context->registerService(service<QTimer>() << withAutoRefresh << propValue(&QObject::setObjectName, "theTimer") << propValue("interval", &QTimer::setInterval, "${timerInterval}"), "timer");
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
    return {detail::uniquePropertyName(&propertySetter, sizeof propertySetter), detail::ConfigValue{expression, detail::ConfigValueType::AUTO_REFRESH_EXPRESSION, detail::adaptSetter<S,A>(propertySetter), detail::variant_converter_traits<detail::remove_cvref_t<A>>::makeConverter(converter)}};
}





///
/// \brief Creates a configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// \param name the name of the property to be configured. **Note:** this name must refer to a Q_PROPERTY of the service-type!
/// \param value will be used as the property's value.
/// \return a configuration for a service.
///
[[nodiscard]]inline detail::service_config::entry_type propValue(const QString& name, const QVariant& value) {
    return {name, detail::ConfigValue{value, detail::ConfigValueType::DEFAULT}};
}

///
/// \brief Creates a configuration-entry for a service.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// \param name the name of the placeholder to be configured.
/// \param value will be used as the property's value.
/// \return a configuration for a service.
///
[[nodiscard]]inline detail::service_config::entry_type placeholderValue(const QString& name, const QVariant& value) {
    return {name, detail::ConfigValue{value, detail::ConfigValueType::PRIVATE}};
}




///
/// \brief Specifies that a value for a configured Q_PROPERTY shall be automatically updated at runtime.
/// <br>The resulting service_config_entry can then be passed to mcnepp::qtdi::service() using the `operator <<`.
/// <br>In order to demonstrate the purpose, consider this example of a normal, non-updating service-configuration for a QTimer:
///
///     context->registerService(service<QTimer>() << propValue("interval", "${timerInterval}"), "timer");
///
/// The Q_PROPERTY QTimer::interval will be initialized from the value of the configuration-key `"timerInterval"` as it
/// is in the moment the timer is instantiated.
///
/// <br>Now, contrast this with:
///
///     context->registerService(service<QTimer>() << autoRefresh("interval", "${timerInterval}"), "timer");
///
/// Whenever the value for the configuration-key `"timerInterval"` changes in the underlying QSettings-Object, the
/// expression `"${timerInterval}"` will be re-evaluated and the Q_PROPERTY of the timer will be updated accordingly.
/// <br>In case all properties for one service shall be auto-refreshed, there is a more concise way of specifying it:
///
///     context->registerService(service<QTimer>() << withAutoRefresh << propValue("objectName", "theTimer") << propValue("interval", "${timerInterval}"), "timer");
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
[[nodiscard]] inline detail::service_config::entry_type autoRefresh(const QString& name, const QString& expression) {
    return {name, detail::ConfigValue{expression, detail::ConfigValueType::AUTO_REFRESH_EXPRESSION, nullptr, nullptr}};
}








namespace detail {


template<typename S,typename F> static auto adaptInitializer(F func) -> std::enable_if_t<std::is_invocable_v<F,S*,QApplicationContext*>,q_init_t> {
    return [func](QObject* target,QApplicationContext* context) {
        if(auto ptr =dynamic_cast<S*>(target)) {
            std::invoke(func, ptr, context);
        }};

}

template<typename S,typename F,typename...Args> static auto adaptInitializer(F func, Args&&...args) -> std::enable_if_t<std::is_invocable_v<F,S*,Args...>,q_init_t> {
    return [func=std::bind(func, std::placeholders::_1, std::forward<Args>(args)...)](QObject* target,QApplicationContext*) {
        if(auto ptr =dynamic_cast<S*>(target)) {
            std::invoke(func, ptr);
        }};

}





template<typename S,auto func> static q_init_t adaptInitializer(service_initializer<func> initializer) {
    return adaptInitializer<S>(initializer.value());
}

template<typename S> static q_init_t adaptInitializer(std::nullptr_t) {
    return nullptr;
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
    ServiceInitializationPolicy initialization_policy = ServiceInitializationPolicy::DEFAULT;
};


QDebug operator << (QDebug out, const service_descriptor& descriptor);




///
/// \brief Determines whether two service_descriptors are deemed equal.
/// two service_descriptors are deemed equal if their service_types, impl_type,
/// dependencies and config are all equal, and if either both have no init_method,
/// or both have an init_method.
/// \param left
/// \param right
/// \return `true` if the service_descriptors are equal to each other.
///
inline bool operator==(const service_descriptor &left, const service_descriptor &right) {
    if (&left == &right) {
        return true;
    }
    if(left.service_types != right.service_types || left.impl_type != right.impl_type || left.dependencies != right.dependencies) {
        return false;
    }
    return static_cast<bool>(left.init_method) == static_cast<bool>(right.init_method);
 }

inline bool operator!=(const service_descriptor &left, const service_descriptor &right) {
    return !(left == right);
}












template <typename S>
struct dependency_helper {
    using type = S;

    using arg_type = S;

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

template <typename S,Kind kind>
struct dependency_helper<Dependency<S,kind>> {
    using type = S;

    using arg_type = typename default_argument_converter<S,kind>::target_type;

    static dependency_info info(const Dependency<S,kind>& dep) {
        return { typeid(S), static_cast<int>(kind), dep.requiredName };
    }

    static auto converter(const Dependency<S,kind>&) {
        return default_argument_converter<S,kind>{};
    }
};

template <typename S,ServiceScope scope>
struct dependency_helper<mcnepp::qtdi::ServiceRegistration<S,scope>> {
    using type = S;

    using arg_type = S*;

    static dependency_info info(const mcnepp::qtdi::ServiceRegistration<S,scope>& dep) {
        static_assert(is_allowed_as_dependency(scope), "ServiceRegistration with this scope cannot be a dependency");
        //It could still be ServiceScope::UNKNOWN statically, but ServiceScope::TEMPLATE at runtime:
        if(dep && is_allowed_as_dependency(dep.unwrap()->scope())) {
            return { dep.unwrap()->descriptor().impl_type, static_cast<int>(Kind::MANDATORY), dep.registeredName() };
        }
        return { typeid(S), INVALID_KIND, dep.registeredName() };
    }

    static auto converter(const mcnepp::qtdi::ServiceRegistration<S,scope>&) {
        return default_argument_converter<S,Kind::MANDATORY>{};
    }

};

template <typename S>
struct dependency_helper<mcnepp::qtdi::ServiceRegistration<S,ServiceScope::SERVICE_GROUP>> {
    using type = S;

    using arg_type = QList<S*>;

    static dependency_info info(const mcnepp::qtdi::ServiceRegistration<S,ServiceScope::SERVICE_GROUP>& dep) {
        if(dep) {
            return { typeid(S), static_cast<int>(Kind::N), dep.registeredName()};
        }
        return { typeid(S), INVALID_KIND };
    }

    static auto converter(const mcnepp::qtdi::ServiceRegistration<S,ServiceScope::SERVICE_GROUP>& ) {
        return default_argument_converter<S,Kind::N>{};
    }

};


template <typename S>
struct dependency_helper<mcnepp::qtdi::ProxyRegistration<S>> {
    using type = S;

    using arg_type = QList<S*>;

    static dependency_info info(const mcnepp::qtdi::ProxyRegistration<S>& dep) {
        if(dep) {
            return { dep.unwrap()->serviceType(), static_cast<int>(Kind::N)};
        }
        return { typeid(S), INVALID_KIND };
    }

    static auto converter(const mcnepp::qtdi::ProxyRegistration<S>& ) {
        return default_argument_converter<S,Kind::N>{};
    }

};


template <typename S>
struct dependency_helper<Resolvable<S>> {

    using type = S;

    using arg_type = S;

    static dependency_info info(const Resolvable<S>& dep) {
        return { typeid(S), RESOLVABLE_KIND, dep.expression, dep.defaultValue, dep.variantConverter };
    }

    static auto converter(const Resolvable<S>&) {
        return &dependency_helper<S>::convert;
    }
};

template <typename S,typename R>
struct dependency_helper<ComputedDependency<S,R>> {

    using arg_type = R;

    static dependency_info info(const ComputedDependency<S,R>& dep) {
        if(!dep.dependencyAccessor) {
            return { typeid(S), INVALID_KIND };
        }
        return { typeid(S), static_cast<int>(Kind::MANDATORY), dep.requiredName };
    }

    static auto converter(const ComputedDependency<S,R>& dep) {
        return [accessor=dep.dependencyAccessor](const QVariant& var) {
            return accessor(dynamic_cast<S*>(var.value<QObject*>()));
        };
    }
};



template <>
struct dependency_helper<ParentPlaceholder> {

    using type = ParentPlaceholder;

    using arg_type = QObject*;

    static dependency_info info(const ParentPlaceholder&) {
        return { typeid(QObject*), PARENT_PLACEHOLDER_KIND};
    }

    static auto converter(const ParentPlaceholder&) {
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


template<typename F,typename...Converters> struct service_creator_impl {
    F factory;

    std::tuple<Converters...> converters;

    template<std::size_t...Indices> auto call(std::index_sequence<Indices...>, const QVariantList& dependencies) const {
        return factory(std::get<Indices>(converters)(dependencies[Indices])...);
    }

    auto operator()(const QVariantList& dependencies) const {
        return call(std::index_sequence_for<Converters...>{}, dependencies);
    }

};


template<typename F,typename...Converters> auto service_creator(F factory, Converters&&...converters) {
    return service_creator_impl<F,Converters...>{factory, std::make_tuple(converters...)};
}

template <typename F> constructor_t service_creator(F factory) {
    return [factory](const QVariantList&) {
        return factory();
    };
}


template<typename S> constexpr bool has_initializer = std::negation_v<std::is_same<std::nullptr_t,typename service_traits<S>::initializer_type>>;

template<bool found,typename First,typename...Tail> q_init_t getInitializer(ServiceInitializationPolicy& initializationPolicy) {
    constexpr bool foundThis = has_initializer<First>;
    static_assert(!(foundThis && found), "Ambiguous initializers in advertised interfaces");
    if constexpr(sizeof...(Tail) > 0) {
        //Invoke recursively, until we either trigger an assertion or find no other initializer:
        auto result = getInitializer<foundThis,Tail...>(initializationPolicy);
        if constexpr(!foundThis) {
            return result;
        }
    } else {
        initializationPolicy = service_traits<First>::initialization_policy;
        return adaptInitializer<First>(typename service_traits<First>::initializer_type{});
    }

}



template<typename Srv,typename Impl,ServiceScope scope,typename F,typename...Dep> service_descriptor make_descriptor(F factory, Dep...deps) {
    detail::service_descriptor descriptor{{typeid(Srv)}, typeid(Impl), &Impl::staticMetaObject};
    if constexpr(has_initializer<Impl>) {
        descriptor.initialization_policy = service_traits<Impl>::initialization_policy;
        descriptor.init_method = adaptInitializer<Impl>(typename service_traits<Impl>::initializer_type{});
    } else {
        descriptor.initialization_policy = service_traits<Srv>::initialization_policy;
        descriptor.init_method = adaptInitializer<Srv>(typename service_traits<Srv>::initializer_type{});
    }
    (descriptor.dependencies.push_back(detail::dependency_helper<Dep>::info(deps)), ...);
    if constexpr(detail::service_scope_traits<scope>::is_constructable) {
        descriptor.constructor = service_creator(factory, detail::dependency_helper<Dep>::converter(deps)...);
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

    static_assert(std::is_base_of_v<Srv,Impl> || scope == ServiceScope::TEMPLATE, "Implementation-type must be a subclass of Service-type");

    using service_type = Srv;

    using impl_type = Impl;


    explicit Service(detail::service_descriptor&& descr) :
        descriptor{std::move(descr)} {
    }

    explicit Service(detail::service_descriptor&& descr, detail::service_config&& cfg) :
        descriptor{std::move(descr)},
        config{std::move(cfg)} {
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
    template<typename...IFaces> std::enable_if_t<(sizeof...(IFaces) > 0),Service<Srv,Impl,scope>>&& advertiseAs() && {
        //Check whether the Impl-type is derived from the service-interfaces (except for service-templates)
        if constexpr(scope != ServiceScope::TEMPLATE) {
            static_assert(std::conjunction_v<std::is_base_of<IFaces,Impl>...>, "Implementation-type does not implement all advertised interfaces");
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
            descriptor.init_method = detail::getInitializer<false,IFaces...>(descriptor.initialization_policy);
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
    template<typename...IFaces> [[nodiscard]] std::enable_if_t<(sizeof...(IFaces) > 0),Service<Srv,Impl,scope>> advertiseAs() const& {
        return Service<Srv,Impl,scope>{*this}.advertiseAs<IFaces...>();
    }

    /**
     * @brief Specifies an *init-method* for this service.
     * <br>This overrides the init-method that is deduced from the `initializer_type` of the service's service_traits.
     * The init-method will be used for this service-instance only.
     * <br>The initializer must one of:
     *
     * - a callable object with the first argument being a pointer to the service's implementation-type and further arguments that match the supplied args.
     * - a callable object with the first argument being a pointer to the service's implementation type, the second being a pointer to QApplicationContext and further arguments that match the supplied args.
     * - a member-function of the service's implementation-type with arguments that match the supplied args.
     * - a member-function of the service's implementation-type with the first argument being a pointer to QApplicationContext and further arguments that match the supplied args.
     *
     * @tparam I the type of the initializer.
     * @tparam initializationPolicy determines whether the *init-method* will be invoked before or after the service has been published.
     * @param initializer Will be invoked after all properties have been set and before the signal for the publication is emitted.
     * @param args further arguments that will be bound to the invocation.
     * @return this instance
     */
    template<ServiceInitializationPolicy initializationPolicy = ServiceInitializationPolicy::DEFAULT,typename I,typename...Args> Service<Srv,Impl,scope>&& withInit(I initializer, Args&&...args) && {
        descriptor.initialization_policy = initializationPolicy;
        descriptor.init_method = detail::adaptInitializer<Impl>(initializer, std::forward<Args>(args)...);
        return std::move(*this);
    }

    /**
     * @brief Specifies an *init-method* for this service.
     * <br>This overrides the init-method that is deduced from the `initializer_type` of the service's service_traits.
     * The init-method will be used for this service-instance only.
     * <br>The initializer must one of:
     *
     * - a callable object with the first argument being a pointer to the service's implementation-type and further arguments that match the supplied args.
     * - a callable object with the first argument being a pointer to the service's implementation type, the second being a pointer to QApplicationContext and further arguments that match the supplied args.
     * - a member-function of the service's implementation-type with arguments that match the supplied args.
     * - a member-function of the service's implementation-type with the first argument being a pointer to QApplicationContext and further arguments that match the supplied args.
     *
     * @tparam I the type of the initializer.
     * @tparam initializationPolicy determines whether the *init-method* will be invoked before or after the service has been published.
     * @param initializer Will be invoked after all properties have been set and before the signal for the publication is emitted.
     * @param args further arguments that will be bound to the invocation.
     * @return a service with the supplied initializer.
     */
    template<ServiceInitializationPolicy initializationPolicy = ServiceInitializationPolicy::DEFAULT,typename I,typename...Args> Service<Srv,Impl,scope> withInit(I initializer, Args&&...args) const& {
        return Service<Srv,Impl,scope>{*this}.withInit<initializationPolicy>(initializer, std::forward<Args>(args)...);
    }


    ///
    /// \brief Adds a type-safe configuration-entry to this Service.
    /// \param entry will be added to the configuration.
    /// \return this Service.
    ///
    Service<Srv,Impl,scope>& operator<<(const service_config_entry<Impl>& entry) {
        config.properties.insert(entry.name, entry.value);
        return *this;
    }

    ///
    /// \brief Adds a type-safe configuration-entry to this Service.
    /// \param entry will be added to the configuration.
    /// \return this Service.
    /// \note this function needs to be declared as a function-template. Otherwise, a duplicate function error would occur in case `Impl` and `Srv` denote the same type.
    ///
    template<typename T> auto operator<<(const service_config_entry<T>& entry) -> std::enable_if_t<std::is_same_v<T,Srv>,Service<Srv,Impl,scope>&> {
        config.properties.insert(entry.name, entry.value);
        return *this;
    }


    ///
    /// \brief Adds an untyped configuration-entry to this Service.
    /// \param entry will be added to the configuration.
    /// \return this Service.
    ///
    Service<Srv,Impl,scope>& operator<<(const detail::service_config::entry_type& entry) {
        config.properties.insert(entry.first, entry.second);
        return *this;
    }

    ///
    /// \brief Applies a *modifier* to this Service's configuration.
    /// \param modifier will be applied to this Service's configuration.
    /// \return this Service.
    Service<Srv,Impl,scope>& operator<<(const detail::service_config::config_modifier modifier) {
        if(modifier) {
            modifier(config);
        }
        return *this;
    }



    detail::service_descriptor descriptor;
    detail::service_config config;
};


///
/// \brief Creates a Service with an explicit factory.
/// \param factory the factory to use. Must be a *Callable* object, i.e. provide an `Impl* operator()` that accepts
/// the arguments derived from the dependencies and yields a pointer to the created Object.
/// <br>The factory-type shall contain a type-declaration `service_type` which denotes
/// the type of the service's implementation.
/// \param dependencies the arguments to be injected into the factory.
/// \tparam F the type of the factory.
/// \tparam Impl the implementation-type of the service. If the factory-type F contains
/// a type-declaration `service_type`, Impl will be deduced as that type.
/// \return a Service that will use the provided factory.
template<typename F,typename Impl=typename F::service_type,typename...Dep> [[nodiscard]] auto service(F factory, Dep...dependencies) ->
    std::enable_if_t<std::is_invocable_v<F,typename detail::dependency_helper<Dep>::arg_type...>,Service<Impl,Impl,ServiceScope::SINGLETON>>
{
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
/// \brief A type that allows creating Service-groups.
/// <br>Instances of this type are used solely as temporaries, returned by mcnepp::qtdi::serviceGroup(QAnyStringView,QAnyStringView).
///
struct ServiceGroup {




    ///
    /// \brief Creates a strongly typed Service-group.
    /// <br>The following example will register a Service-group with 3 services of type `QTimer`, for which the property `interval` will be set
    /// to the values 1, 500 and 1000:
    ///
    ///     context->registerService(serviceGroup("interval", "1,500,1000") << service<QTimer>() << propValue(&QTimer::setInterval, "${interval}"));
    ///
    /// <br>The following example will register a Service-group with multiple services of type `RESTService`.
    /// The configuration-entry `urls` is expected to comprise a comma-separated list of values.
    /// Each `RESTService` will be initialized with one of these values:
    ///
    ///     context->registerService(serviceGroup("url", "${urls}") << service<RESTService>(resolve("${url}")));
    /// \tparam S the primary service-interface.
    /// \tparam Impl the implementation-type of the service.
    /// \param service the original service-declaration.
    /// \return a Service-declaration for a Service-group.
    template<typename S,typename Impl>  [[nodiscard]] Service<S,Impl,ServiceScope::SERVICE_GROUP> operator<<(Service<S,Impl,ServiceScope::SINGLETON>&& service)&& {
        service.config.serviceGroupPlaceholder = placeholder.toString();
        service.config.properties.insert(service.config.serviceGroupPlaceholder , detail::ConfigValue{groupExpression.toString(), detail::ConfigValueType::PRIVATE});
        return Service<S,Impl,ServiceScope::SERVICE_GROUP>{std::move(service.descriptor), std::move(service.config)};
    }

    ///
    /// \brief Creates a strongly typed Service-group.
    /// <br>The following example will register a Service-group with 3 services of type `QTimer`, for which the property `interval` will be set
    /// to the values 1, 500 and 1000:
    ///
    ///     context->registerService(serviceGroup("interval", "1,500,1000") << service<QTimer>() << propValue(&QTimer::setInterval, "${interval}"));
    ///
    /// <br>The following example will register a Service-group with multiple services of type `RESTService`.
    /// The configuration-entry `urls` is expected to comprise a comma-separated list of values.
    /// Each `RESTService` will be initialized with one of these values:
    ///
    ///     context->registerService(serviceGroup("url", "${urls}") << service<RESTService>(resolve("${url}")));
    /// \tparam S the primary service-interface.
    /// \tparam Impl the implementation-type of the service.
    /// \param service the original service-declaration.
    /// \return a Service-declaration for a Service-group.
    template<typename S,typename Impl>  [[nodiscard]] Service<S,Impl,ServiceScope::SERVICE_GROUP> operator<<(const Service<S,Impl,ServiceScope::SINGLETON>& service)&& {
        detail::service_config cfg{service.config};
        cfg.serviceGroupPlaceholder = placeholder.toString();
        cfg.properties.insert(cfg.serviceGroupPlaceholder , detail::ConfigValue{groupExpression.toString(), detail::ConfigValueType::PRIVATE});
        return Service<S,Impl,ServiceScope::SERVICE_GROUP>{detail::service_descriptor{service.descriptor}, std::move(cfg)};
    }


    QAnyStringView placeholder;
    QAnyStringView groupExpression;
};

///
/// \brief Creates a Service-group with the default service-factory.
/// <br>For a Service-group to be useful, you must invoke mcnepp::qtdi::ServiceGroup::operator<<().
/// <br>The following example will register a Service-group with 3 services of type `QTimer`, for which the property `interval` will be set
/// to the values 1, 500 and 1000:
///
///     context->registerService(serviceGroup("interval", "1,500,1000") << service<QTimer>() << propValue(&QTimer::setInterval, "${interval}"));
///
/// <br>The following example will register a Service-group with multiple services of type `RESTService`.
/// The configuration-entry `urls` is expected to comprise a comma-separated list of values.
/// Each `RESTService` will be initialized with one of these values:
///
///     context->registerService(serviceGroup("url", "${urls}") << service<RESTService>(resolve("${url}")));
///
///
/// \param placeholder will be set to each of the values from the `groupExpression`.
/// \param groupExpression shall be resolved to a comma-separated list of values.
/// \return a ServiceGroup with the supplied placeholder and groupExpression.
[[nodiscard]] inline ServiceGroup serviceGroup(QAnyStringView placeholder, QAnyStringView groupExpression) {
    return {placeholder, groupExpression};
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
/// <br>If you leave out the type-argument `S` or `Impl`, it will default to `QObject`.
/// <br>Should you want to ensure that every service derived from this service-template shall be advertised under
/// a certain interface, use Service::advertiseAs().
/// \tparam S the Service-interface of the template.
/// \tparam Impl the implementation-type of the template.
/// \return a Service that cannot be instantiated.
template<typename S=QObject,typename Impl=S>  [[nodiscard]] Service<S,Impl,ServiceScope::TEMPLATE> serviceTemplate() {
    return Service<S,Impl,ServiceScope::TEMPLATE>{detail::make_descriptor<S,Impl,ServiceScope::TEMPLATE>(nullptr)};
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

Q_SIGNALS:
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
    [[nodiscard]] static QApplicationContext* instance();



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

    using service_config = detail::service_config;

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
    /// \param condition determines whether the service will become active on publication.
    /// <br>If the service shall be active unconditionally, supply Condition::always().
    /// \tparam S the service-type. Constitutes the Service's primary advertised interface.
    /// \tparam Impl the implementation-type. The Service will be instantiated using this class' constructor.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename Impl,ServiceScope scope> auto registerService(const Service<S,Impl,scope>& serviceDeclaration, const QString& objectName = {}, const Condition& condition = Condition::always()) -> ServiceRegistration<S,scope> {
        return ServiceRegistration<S,scope>::wrap(registerServiceHandle(objectName, serviceDeclaration.descriptor, serviceDeclaration.config, scope, condition, nullptr));
    }










    ///
    /// \brief Registers a service with this ApplicationContext.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param serviceDeclaration comprises the services's primary advertised interface, its implementation-type and its dependencies to be injected
    /// via its constructor.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param templateRegistration the registration of the service-template that this service shall inherit from. Must be valid!
    /// \param condition determines whether the service will become active on publication.
    /// <br>If the service shall be active unconditionally, supply Condition::always().
    /// \tparam S the service-type. Constitutes the Service's primary advertised interface.
    /// \tparam Impl the implementation-type. The Service will be instantiated using this class' constructor.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S,typename Impl,typename B,ServiceScope scope> auto registerService(const Service<S,Impl,scope>& serviceDeclaration, const ServiceRegistration<B,ServiceScope::TEMPLATE>& templateRegistration, const QString& objectName = {}, const Condition& condition = Condition::always()) -> ServiceRegistration<S,scope> {
        static_assert(std::is_base_of_v<B,Impl>, "Service-type does not extend type of Service-template.");
        if(!templateRegistration) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register " << serviceDeclaration.descriptor << " with name '" << objectName << "'. Invalid service-template";
            return ServiceRegistration<S,scope>{};
        }
        return ServiceRegistration<S,scope>::wrap(registerServiceHandle(objectName, serviceDeclaration.descriptor, serviceDeclaration.config, scope, condition, templateRegistration.unwrap()));
    }










    ///
    /// \brief Registers a service with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(service<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> auto registerService(const QString& objectName = {}) -> ServiceRegistration<S,ServiceScope::SINGLETON> {
        return registerService(service<S>(), objectName);
    }



    ///
    /// \brief Registers a service-prototype with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(prototype<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S> auto registerPrototype(const QString& objectName = {}) -> ServiceRegistration<S,ServiceScope::PROTOTYPE> {
        return registerService(prototype<S>(), objectName);
    }






    ///
    /// \brief Registers a service-template with no dependencies with this ApplicationContext.
    /// This is a convenience-function equivalent to `registerService(serviceTemplate<S>(), objectName, config)`.
    /// <br>**Thread-safety:** This function may only be called from the ApplicationContext's thread.
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// <br>If you leave out the type-argument `S`, it will default to `QObject`.
    /// <br>**Note:** Since a service-template may be used by services of types that are yet unknown, the properties supplied here cannot be validated.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or an invalid ServiceRegistration if it could not be registered.
    ///
    template<typename S=QObject> auto registerServiceTemplate(const QString& objectName = {}) -> ServiceRegistration<S,ServiceScope::TEMPLATE> {
        return registerService(serviceTemplate<S>(), objectName);
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
    template<typename S,typename... IFaces> ServiceRegistration<S,ServiceScope::EXTERNAL> registerObject(S* obj, const QString& objName = {}) {
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
        return ServiceRegistration<S,ServiceScope::EXTERNAL>::wrap(registerServiceHandle(objName, service_descriptor{ifaces, typeid(*obj), qObject->metaObject()}, service_config{}, ServiceScope::EXTERNAL, Condition::always(), qObject));
    }

    ///
    /// \brief Obtains a ServiceRegistration for a name.
    /// <br>This function will look up Services by the names they were registered with.
    /// Additionally, it will look up any alias that might have been given, using ServiceRegistration::registerAlias(const QString&).
    /// <br>The function will take into account the QApplicationContext::activeProfiles(). If more than one profile is active, and if more than one %Service
    /// with the supplied name has been registered, this function will log an error and return an invalid ServiceRegistration.
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
        return ProxyRegistration<S>::wrap(getRegistrationHandle(typeid(S), detail::meta_type_traits<S>::getMetaObject()));
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
    /// \brief Retrieves a value from the ApplicationContext's configuration.
    /// <br>This function will be used to resolve placeholders in Service-configurations.
    /// Whenever a *placeholder* shall be looked up, the ApplicationContext will search the following sources, until it can resolve the *placeholder*:
    /// -# The environment, for a variable corresponding to the *placeholder*.
    /// -# The instances of `QSettings` that have been registered in the ApplicationContext.
    /// \param key the key to look up. In analogy to QSettings, the key may contain forward slashes to denote keys within sub-sections.
    /// \param searchParentSections determines whether the key shall be searched recursively in the parent-sections.
    /// \return the value, if it could be resolved. Otherwise, an invalid QVariant.
    ///
    [[nodiscard]] virtual QVariant getConfigurationValue(const QString& key, bool searchParentSections = false) const = 0;

    ///
    /// \brief Obtains configuration-keys available in this ApplicationContext.
    /// <br>The keys will be returned in the same order that the underlying QSettings yield them.
    /// <br>Keys that are present in more than one QSettings will be returned only once.
    /// <br>In contrast to getConfigurationValue(const QString&, bool), this function does not consider environment variables.
    /// \param section determines which keys will be returned. An empty string denotes the "root".
    /// Sub-sections shall be delimited by forward slashes, in analogy to QSettings.
    /// \return a list with the keys that are present in the supplied section. The return keys will **comprise the supplied section**.
    /// This is necessary if they are to be useful when supplied to getConfigurationValue(const QString&, bool).
    ///
    [[nodiscard]] virtual QStringList configurationKeys(const QString& section = {}) const = 0;


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
    /// \param group the config-group in which to look.
    /// \param resolvedPlaceholders will consulted for looking up placeholders. It will also be filled with all placeholders that have been looked up.
    /// \return a valid QVariant, or an invalid QVariant if the expression could not be parsed.
    ///
    [[nodiscard]] virtual QVariant resolveConfigValue(const QString& expression, const QString& group, QVariantMap& resolvedPlaceholders) = 0;

    QVariant resolveConfigValue(const QString& expression, const QString& group = {}) {
        QVariantMap resolvedPlaceholders;
        return resolveConfigValue(expression, group, resolvedPlaceholders);
    }



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
    /// \brief What profiles are active?
    /// <br>Obtains the profiles that have been activated for this ApplicationContext.
    /// <br>If not otherwise specified, this function shall yield a List with one entry named `"default"`.
    /// <br>When using a StandardApplicationContext, the active profiles can be determined by putting a configuration-entry into one of the QSettings-objects registered with the context:
    ///
    ///     [qtdi]
    ///     activeProfiles=unit-test,integration-test
    ///
    /// \return the list of active profiles. No profile will occur more than once in the returned list. The profiles will be in no particular order.
    ///
    [[nodiscard]] virtual Profiles activeProfiles() const = 0;

    ///
    /// \brief The QLoggingCategory that this ApplicationContext uses.
    /// \return The QLoggingCategory that this ApplicationContext uses.
    /// \sa mcnepp::qtdi::defaultLoggingCategory()
    ///
    [[nodiscard]] virtual const QLoggingCategory& loggingCategory() const = 0;



Q_SIGNALS:

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
    /// \param condition determines whether the service shall become active on publication.
    /// \param baseObject in case of ServiceScope::EXTERNAL the Object to be registered. Otherwise, the (optional) pointer to the registration of a service-template.
    /// \return a Registration for the service, or `nullptr` if it could not be registered.
    ///
    virtual service_registration_handle_t registerServiceHandle(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, const Condition& condition, QObject* baseObject) = 0;


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
    /// <br>registeredName(service_registration_handle_t).
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
    /// <br>If you are implementing registerServiceHandle(const QString&, const service_descriptor&, const service_config&, ServiceScope, const Condition&, QObject*) and want to delegate
    /// to another implementation, access-rules will not allow you to invoke the function on another target.
    /// \param appContext the target on which to invoke registerServiceHandle(const QString&, const service_descriptor&, const service_config&, ServiceScope, const Condition&, QObject*).
    /// \param name the name of the registered service.
    /// \param descriptor describes the service.
    /// \param config configuration of the service.
    /// \param scope detemines the service's lifecyle.
    /// \param condition determines whether the service shall become active on publication.
    /// \param baseObj in case of ServiceScope::EXTERNAL the Object to be registered. Otherwise, the (optional) pointer to the registration of a service-template.
    /// \return the result of registerService(const QString&, service_descriptor*,const service_config&,ServiceScope,QObject*).
    ///
    static service_registration_handle_t delegateRegisterService(QApplicationContext* appContext, const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, const Condition& condition, QObject* baseObj) {
        if(!appContext) {
            return nullptr;
        }
        return appContext->registerServiceHandle(name, descriptor, config, scope, condition, baseObj);
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

    ///
    /// \brief Sets this ApplicationContext as the *global instance*.
    /// <br>Derived classes should invoke this method as the last line of their constructor.
    /// This method will set `this` as the global instance.
    /// <br>**Why can this not be done automatically in ApplicationContextImplBase's constructor?**
    /// <br>Unfortunately, this would violate the C++ standard. ApplicationContextImplBase's constructor runs
    /// before the constructor of the derived class. If it did set the global instance to `this`,
    /// a reference to an incomplete object would be accessible by external code via QApplicationContext::instance()
    /// \sa instance()
    /// \return `true` if `this` could be set as the *global instance*
    ///
    bool setAsGlobalInstance() {
        if(setInstance(this)) {
            qCInfo(loggingCategory()).noquote().nospace() << "Installed " << this << " as global instance";
            return true;
        }
        return false;
    }

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
    /// \param resolvedPlaceholders the resolved placeholders for this service.
    ///
    virtual void process(service_registration_handle_t handle, QObject* service, const QVariantMap& resolvedPlaceholders) = 0;

    virtual ~QApplicationContextPostProcessor() = default;
};



///
/// \brief Creates an ApplicationContext as a *delegate* for another context.
/// <br>With the help of this function you can implement the interface QApplicationContext yourself.
/// <br>The `delegatingContext` will be used for various purposes:
/// - It will become the `QObject::parent()` of the *delegate*.
/// - It will be injected into *init-methods* of services that choose to provide such a method.
/// - It will be attached to all ServiceRegistrations and can be obtained via ServiceRegistration::applicationContext().
/// \param loggingCategory will be used for logging.
/// \param delegatingContext the context that will delegate its call to the *delegate*.
/// \return an ApplicationContext that serves as the *delegate* for the `delegatingContext`.
///
QApplicationContext* newDelegate(const QLoggingCategory& loggingCategory, QApplicationContext* delegatingContext);

///
/// \brief Creates an ApplicationContext as a *delegate* for another context.
/// <br>Equivalent to `newDelegate(defaultLoggingCategory(), delegatingContext)`.
/// \sa mcnepp::qtdi::newDelegate(const QLoggingCategory& , QApplicationContext* )
/// \param delegatingContext the context that will delegate its call to the *delegate*.
/// \return an ApplicationContext that serves as the *delegate* for the `delegatingContext`.
///
inline QApplicationContext* newDelegate(QApplicationContext* delegatingContext) {
    return newDelegate(defaultLoggingCategory(), delegatingContext);
}



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




