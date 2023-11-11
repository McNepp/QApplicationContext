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
#include <QLoggingCategory>

namespace mcnepp::qtdi {

class QApplicationContext;

Q_DECLARE_LOGGING_CATEGORY(loggingCategory)


namespace detail {
template<typename S> auto couldBeQObject(S* ptr) -> decltype(dynamic_cast<QObject*>(ptr));

void couldBeQObject(void*);


template<typename S> constexpr bool could_be_qobject = std::is_same_v<decltype(couldBeQObject(static_cast<S*>(nullptr))),QObject*>;

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

}




///
/// \brief A  type that serves as a "handle" for registrations in a QApplicationContext.
/// This class has a read-only Q_PROPERTY `publishedObjects' with a corresponding signal `publishedObjectsChanged`.
/// However, it would be quite unwieldly to use that property directly in order to get hold of the
/// Objects managed by this Registration.
/// Rather, use the class-template `ServiceRegistration` and its type-safe method `ServiceRegistration::subscribe()`.
///
class Registration : public QObject {
    Q_OBJECT

    /// \brief The List of published Objects managed by this Registration.
    /// If this is a Registration obtained by QApplicationContext::registerService(), the List
    /// can comprise of at most one Object.
    /// However, if the Registration was obtained by QApplicationContext::getRegistration(),
    /// it may comprise multiple objects of the supplied service-type.
    ///
    Q_PROPERTY(QObjectList publishedObjects READ publishedObjects NOTIFY publishedObjectsChanged)

    ///
    /// \brief The name of this Registration.
    /// This property will only yield a non-empty String if this Registration was obtained via QApplicationContext::registerService().
    /// For Registrations obtained via QApplicationContext::getRegistration(), it will yield the empty String.
    ///
    Q_PROPERTY(QString registeredName READ registeredName CONSTANT)

    /// \brief the maximum number of Objects that match this Registration.
    /// For Registrations obtained via QApplicationContext::registerService(), this function will always yield 1.
    /// For Registrations obtained via QApplicationContext::getRegistration(), this function may yield anything between 0 and
    /// the number of currently registered services of this type.
    ///
    Q_PROPERTY(unsigned maxPublications READ maxPublications NOTIFY maxPublicationsChanged)

public:

    template<typename S> friend class ServiceRegistration;

    ///
    /// \brief The service-type that this Registration manages.
    /// \return The service-type that this Registration manages.
    ///
    [[nodiscard]] virtual const std::type_info& service_type() const = 0;

    /// \brief The List of published Objects managed by this Registration.
    /// If this is a Registration obtained by QApplicationContext::registerService(), the List
    /// can comprise of at most one Object.
    /// However, if the Registration was obtained by QApplicationContext::getRegistration(),
    /// it may comprise multiple objects of the supplied service-type.
    ///
    [[nodiscard]] virtual QObjectList publishedObjects() const = 0;


    ///
    /// \brief Yields the ApplicationContext that this Registration belongs to.
    /// \return the ApplicationContext that this Registration belongs to.
    ///
    [[nodiscard]] virtual QApplicationContext* applicationContext() const = 0;

    ///
    /// \brief The name of this Registration.
    /// This property will only yield a non-empty String if this Registration was obtained via QApplicationContext::registerService().
    /// For Registrations obtained via QApplicationContext::getRegistration(), it will yield the empty String.
    /// \return the name of this Registration if it was obtained via QApplicationContext::registerService(), the empty String otherwise.
    ///
    [[nodiscard]] virtual QString registeredName() const = 0;

    /// \brief the maximum number of Objects that match this Registration.
    /// For Registrations obtained via QApplicationContext::registerService(), this function will always yield 1.
    /// For Registrations obtained via QApplicationContext::getRegistration(), this function may yield anything between 0 and
    /// the number of currently registered services of this type.
    [[nodiscard]] virtual unsigned maxPublications() const = 0;

    ///
    /// \brief Writes information about this Registration to QDebug.
    /// \param out
    ///
    virtual void print(QDebug out) const = 0;


signals:

    ///
    /// \brief Signals when a service has been published.
    ///
    void publishedObjectsChanged();

    ///
    /// \brief Signals when another service matching this Registration has been registered.
    ///
    void maxPublicationsChanged(unsigned);

protected:

    explicit Registration(QObject* parent = nullptr) : QObject(parent) {

    }

    virtual ~Registration() = default;

    using injector_t = std::function<void(QObject*)>;
    using binder_t = std::function<injector_t(QObject*)>;



    class PublicationNotifier {
    public:
        explicit PublicationNotifier(Registration* source) :
            m_source(source){
        }

        void operator()() const {
            for(auto obj : m_source->publishedObjects()) {
                if(publishedObjects.insert(obj).second) {
                    notify(obj);
                }
            }
        }
    protected:

        ~PublicationNotifier() = default; //We never want to delete polymorphically

        virtual void notify(QObject*) const = 0;
    private:
        Registration* const m_source;
        mutable std::unordered_set<QObject*> publishedObjects;
    };

    virtual bool registerAutoWiring(const std::type_info& typ, binder_t binder) = 0;
};

///
/// \brief A type-safe wrapper for a Registration.
/// Instances of this class are being returned by the public function-templates QApplicationContext::registerService(),
/// QApplicationContext::registerObject() and QApplicationContext::getRegistration().
///
/// This class offers the type-safe function `subscribe()` which should be preferred over directly connecting to the signal `Registration::publishedObjectsChanged()`.
///
/// A ServiceRegistration contains a *non-owning pointer* to a Registration whose lifetime of is bound
/// to the QApplicationContext. The ServiceRegistration will become invalid after the corresponding QApplicationContext has been destructed.
///
template<typename S> class ServiceRegistration final {
    friend class QApplicationContext;

public:

    ServiceRegistration() {

    }






    /// \brief The List of published services managed by this Registration.
    /// This is a type-safe version of Registration::publishedObjects().
    /// \return the List of published services.
    ///
    [[nodiscard]] QList<S*> publishedObjects() const {
        if(!registrationHolder) {
            return QList<S*>{};
        }
        return detail::convertQList<S>(registrationHolder->publishedObjects());
    }

    ///
    /// \brief Yields the QApplicationContext that manages this Registration.
    /// \return the QApplicationContext that manages this Registration, or `nullptr` if this ServiceRegistration is invalid.
    ///
    [[nodiscard]] QApplicationContext* applicationContext() const {
        if(!registrationHolder) {
            return nullptr;
        }
        return registrationHolder->applicationContext();
    }


    ///
    /// \brief The name of this Registration.
    /// This property will only yield a non-empty String if this Registration was obtained via QApplicationContext::registerService().
    /// For Registrations obtained via QApplicationContext::getRegistration(), it will yield the empty String.
    /// \return the name of this Registration if it was obtained via QApplicationContext::registerService(), the empty String otherwise.
    ///
    [[nodiscard]] QString registeredName() const {
        if(!registrationHolder) {
            return QString{};
        }
        return registrationHolder->registeredName();
    }

    /// \brief the maximum number of Objects that match this Registration.
    /// For Registrations obtained via QApplicationContext::registerService(), this function will always yield 1.
    /// For Registrations obtained via QApplicationContext::getRegistration(), this function may yield anything between 0 and
    /// the number of currently registered services of this type.
    [[nodiscard]] unsigned maxPublications() const {
        if(!registrationHolder) {
            return 0;
        }

        return registrationHolder->maxPublications();
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
    /// \return the Connection representing this subscription.
    ///
    template<typename F> QMetaObject::Connection subscribe(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        if(!registrationHolder) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot subscribe to " << *this;
            return QMetaObject::Connection{};
        }

        auto connection = QObject::connect(registrationHolder, &Registration::publishedObjectsChanged, context, CallableNotifier<F>{registrationHolder, callable}, connectionType);
        emit registrationHolder -> publishedObjectsChanged();
        return connection;
    }

    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the setter immediately with the current publishedObjects().
    /// \tparam T the target of the subscription. Must be derived from QObject.
    /// \param target the object on which the setter will be invoked.
    /// \param setter the method that will be invoked.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    /// \return the Connection representing this subscription.
    ///
    template<typename T,typename R> QMetaObject::Connection subscribe(T* target, R (T::*setter)(S*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        static_assert(std::is_base_of_v<QObject,T>, "Target must be derived from QObject");
        if(!registrationHolder) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot subscribe to " << *this;
            return QMetaObject::Connection{};
        }
        auto connection = QObject::connect(registrationHolder, &Registration::publishedObjectsChanged, target, SetterNotifier<T,R>{registrationHolder, target, setter}, connectionType);
        emit registrationHolder -> publishedObjectsChanged();
        return connection;

    }



    ///
    /// \brief Yields the wrapped Registration.
    /// \return the wrapped Registration, or `nullptr` if this ServiceRegistration wraps no valid Registration.
    ///
    Registration* unwrap() const {
        return registrationHolder.get();
    }

    ///
    /// \brief Connects a service with another service from the same QApplicationContext.
    /// Whenever a service of the type `<D>` is published, it will be injected into every service
    /// of type `<S>`, using the supplied member-function.
    ///
    /// You may autowire every dependent type `<D>` at most once. This function will return `false` if you invoke
    /// it multiple times for the same type `<D>`.
    /// \tparam D the type of service that will be injected into Services of type `<S>`.
    /// \param injectionSlot the member-function to invoke when a service of type `<D>` is published.
    /// \return `true` if this Registration was wired for the first time for the type `<D>`.
    ///
    template<typename D,typename R> bool autowire(R (S::*injectionSlot)(D*)) {
        if(!registrationHolder) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot autowire " << *this;
            return false;
        }
        return registrationHolder->registerAutoWiring(typeid(D), [injectionSlot](QObject* target) {
            Registration::injector_t injector;
            if(S* typedTarget = dynamic_cast<S*>(target)) {
                injector = [injectionSlot,typedTarget](QObject* dependency) {
                    (typedTarget->*injectionSlot)(dynamic_cast<D*>(dependency));
                };
            }
            return injector;
        });
    }



    ///
    /// \brief Does this ServiceRegistration represent a valid %Registration?
    /// \return `true` if the underlying Registration is present.
    ///
    bool isValid() const {
        return unwrap() != nullptr;
    }

    ///
    /// \brief Does this ServiceRegistration represent a valid %Registration?
    /// Equivalent to isValid().
    /// \return `true` if the underlying Registration is present.
    ///
    explicit operator bool() const {
        return unwrap();
    }

    ///
    /// \brief Does this ServiceRegistration represent an invalid %Registration?
    /// Equivalent to `!isValid()`.
    /// \return `true` if the underlying Registration is not present.
    ///
    bool operator!() const {
        return !unwrap();
    }



private:
    explicit ServiceRegistration(Registration* reg) : registrationHolder{reg}
    {
    }




    template<typename F> struct CallableNotifier : public Registration::PublicationNotifier {

        CallableNotifier(Registration* source, F c) : PublicationNotifier(source),
            callable(c) {
        }

        void notify(QObject* obj) const override {
            if(S* ptr = dynamic_cast<S*>(obj)) {
               callable(ptr);
            }
        }

        F callable;
    };

    template<typename T,typename R> struct SetterNotifier : public Registration::PublicationNotifier {

        SetterNotifier(Registration* source, T* target, R (T::*setter)(S*)) : PublicationNotifier(source),
            m_setter(setter),
            m_target(target){
        }

        void notify(QObject* obj) const override {
            if(S* ptr = dynamic_cast<S*>(obj)) {
               (m_target->*m_setter)(ptr);
            }
        }

        T* const m_target;
        R (T::*m_setter)(S*);
    };



    QPointer<Registration> registrationHolder;
};

///
/// \brief Tests two ServiceRegistrations for equality.
///
/// Two ServiceRegistrations are deemed equal if the pointers returned by ServiceRegistration::unwrap() point to the same Registration
/// **and** if they both report `true` via ServiceRegistration::isValid().
/// \param reg1
/// \param reg2
/// \return `true` if the two ServiceRegistrations are logically equal.
///
template<typename S1,typename S2> bool operator==(const ServiceRegistration<S1>& reg1, const ServiceRegistration<S2>& reg2) {
    return reg1.unwrap() == reg2.unwrap() && reg1;
}

///
/// \brief Tests two ServiceRegistrations for difference.
///
/// Two ServiceRegistrations are deemed different if the pointers returned by ServiceRegistration::unwrap() do not point to the same Registration
/// **or** if they both report `false` via ServiceRegistration::isValid().
/// \param reg1
/// \param reg2
/// \return `true` if the two ServiceRegistrations are logically different.
///
template<typename S1,typename S2> bool operator!=(const ServiceRegistration<S1>& reg1, const ServiceRegistration<S2>& reg2) {
    return reg1.unwrap() != reg2.unwrap() || !reg1;
}

template<typename S> QDebug operator<<(QDebug out, const ServiceRegistration<S>& reg) {
    if(reg) {
        reg.unwrap()->print(out);
    } else {
        out.noquote().nospace() << "ServiceRegistration for service-type '" << typeid(S).name() << "' [invalid]";
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
    static_assert(detail::could_be_qobject<S>, "Dependency must be potentially convertible to QObject");
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
        return ServiceRegistration<S>{registerService(objectName, serviceDeclaration.descriptor, config)};
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
        static_assert(detail::could_be_qobject<S>, "Object is not convertible to QObject");
        return ServiceRegistration<S>{registerObject(objName, dynamic_cast<QObject*>(obj), service_descriptor{typeid(S), typeid(*obj)})};
    }

    ///
    /// \brief Obtains a ServiceRegistration for a service-type.
    /// In contrast to the ServiceRegistration that is returned by registerService(),
    /// the ServiceRegistration returned by this function manages all Services of the requested type.
    /// This means that if you subscribe to it using ServiceRegistration::subscribe(), you will be notified
    /// about all those published services.
    /// \return a ServiceRegistration that manages all Services of the requested type.
    ///
    template<typename S> [[nodiscard]] ServiceRegistration<S> getRegistration() const {
        return ServiceRegistration<S>{getRegistration(typeid(S))};
    }



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
    virtual Registration* registerService(const QString& name, const service_descriptor& descriptor, const service_config& config) = 0;

    ///
    /// \brief Registers an Object with this QApplicationContext.
    /// \param name
    /// \param obj
    /// \param descriptor
    /// \return a Registration for the object, or `nullptr` if it could not be registered.
    ///
    virtual Registration* registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) = 0;

    ///
    /// \brief Obtains a Registration for a service_type.
    /// \param service_type
    /// \return a Registration for the supplied service_type.
    ///
    virtual Registration* getRegistration(const std::type_info& service_type) const = 0;

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
    static Registration* delegateRegisterService(QApplicationContext& appContext, const QString& name, const service_descriptor& descriptor, const service_config& config) {
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
    static Registration* delegateRegisterObject(QApplicationContext& appContext, const QString& name, QObject* obj, const service_descriptor& descriptor) {
        return appContext.registerObject(name, obj, descriptor);
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
    static Registration* delegateGetRegistration(const QApplicationContext& appContext, const std::type_info& service_type) {
        return appContext.getRegistration(service_type);
    }


};


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
