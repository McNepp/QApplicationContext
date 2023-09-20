#pragma once
#include <utility>
#include <typeindex>
#include <unordered_set>
#include <QObject>
#include <QVariant>
#include <QLoggingCategory>
#include "cardinality.h"

namespace com::neppert::context {

class QApplicationContext;

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
    Q_PROPERTY(QObjectList publishedObjects READ getPublishedObjects NOTIFY publishedObjectsChanged)

public:
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
    [[nodiscard]] virtual QObjectList getPublishedObjects() const = 0;

    ///
    /// \brief Yields the ApplicationContext that this Registration belongs to.
    /// \return the ApplicationContext that this Registration belongs to.
    ///
    [[nodiscard]] virtual QApplicationContext* applicationContext() const = 0;

signals:

    void publishedObjectsChanged();

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
            for(auto obj : m_source->getPublishedObjects()) {
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

    static bool delegateRegisterAutoWiring(Registration* reg, const std::type_info& type, binder_t binder) {
        return reg->registerAutoWiring(type, binder);
    }
};

///
/// \brief A type-safe wrapper for a Registration.
/// Instances of this class are being returned by the public function-templates QApplicationContext::registerService(),
/// QApplicationContext::registerObject() and QApplicationContext::getRegistration().
///
/// This class offers the type-safe function `subscribe()` which should be preferred over directly connecting to the signal `publishedObjectsChanged()`.
///
template<typename S> class ServiceRegistration : public Registration {
    friend class QApplicationContext;

public:
    [[nodiscard]] virtual const std::type_info& service_type() const override {
        return unwrap()->service_type();
    }



    [[nodiscard]] virtual QObjectList getPublishedObjects() const override {
        return unwrap()->getPublishedObjects();
    }

    [[nodiscard]] QList<S*> getPublishedServices() const {
        QList<S*> result;
        for(auto obj : getPublishedObjects()) {
            if(S* ptr = dynamic_cast<S*>(obj)) {
                result.push_back(ptr);
            }
        }
        return result;
    }

    [[nodiscard]] virtual QApplicationContext* applicationContext() const override {
        return unwrap()->applicationContext();
    }




    ///
    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the callable immediately with the current getPublishedObjects().
    /// \param context the target-context.
    /// \param callable the piece of code to execute.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    /// \return the Connection representing this subscription.
    ///
    template<typename F> QMetaObject::Connection subscribe(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        auto connection = connect(this, &Registration::publishedObjectsChanged, context, CallableNotifier<F>{this, callable}, connectionType);
        emit publishedObjectsChanged();
        return connection;
    }

    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the setter immediately with the current getPublishedObjects().
    /// \param target the object on which the setter will be invoked.
    /// \param setter the method that will be invoked.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    /// \return the Connection representing this subscription.
    ///
    template<typename T,typename R> QMetaObject::Connection subscribe(T* target, R (T::*setter)(S*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        auto connection = connect(this, &Registration::publishedObjectsChanged, target, SetterNotifier<T,R>{this, target, setter}, connectionType);
        emit publishedObjectsChanged();
        return connection;

    }



    Registration* unwrap() const {
        return static_cast<Registration*>(parent());
    }

    ///
    /// \brief Connects a service with another service from the same QApplicationContext.
    /// Whenever a service of the type `<D>` is published, it will be injected into every service
    /// of type `<S>`, using the supplied member-function.
    ///
    /// You may autowire every dependent type `<D>` at most once. This function will return `false` if you invoke
    /// it multiple times for the same type `<D>`.
    /// \param injectionSlot the member-function to invoke when a service of type `<D>` is published.
    /// \return `true` if this Registration was wired for the first time for the type `<D>`.
    ///
    template<typename D,typename R> bool autowire(R (S::*injectionSlot)(D*)) {
        return registerAutoWiring(typeid(D), [injectionSlot](QObject* target) {
            injector_t injector;
            if(S* typedTarget = dynamic_cast<S*>(target)) {
                injector = [injectionSlot,typedTarget](QObject* dependency) {
                    (typedTarget->*injectionSlot)(dynamic_cast<D*>(dependency));
                };
            }
            return injector;
        });
    }



protected:
    virtual bool registerAutoWiring(const std::type_info& type, binder_t binder) override {
        return delegateRegisterAutoWiring(unwrap(), type, binder);
    }

private:
    explicit ServiceRegistration(Registration* reg) : Registration(reg)
    {
        connect(reg, &Registration::publishedObjectsChanged, this, &Registration::publishedObjectsChanged);
    }

    static ServiceRegistration* wrap(Registration* reg) {
        return reg ? new ServiceRegistration(reg) : nullptr;
    }



    template<typename F> struct CallableNotifier : public PublicationNotifier {

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

    template<typename T,typename R> struct SetterNotifier : public PublicationNotifier {

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



};

///
/// \brief A template that can be specialized to override the standard way of instantiating services.
/// This template can be used to force the QApplicationContext to use a static factory-function instead of a constructor.
/// You may specialize this template for your own component-types.
///
/// If you do so, you must define a call-operator with a pointer to your component as its return-type
/// and as many arguments as are needed to construct an instance.
///
/// For example, if you have a service-type `MyService` with an inaccessible constructor for which only a static factory-function `MySerivce::create()` exists,
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


///
/// \brief Describes a service by its interface and implementation.
///
/// Compilation will fail if either `Srv` is not a sub-class of QObject, or if `Impl` is not a sub-class of `Srv`.
///
/// This type can be used as a type-argument for QApplicationContext::registerService(const QString&, const QApplicationContext::Config&).
/// Example:
///
///    context->registerService<Service<DatabaseAccess,OracleDatabaseAccess>>("dao");
///
///
template<typename Srv,typename Impl> struct Service {
    static_assert(std::is_base_of_v<QObject,Impl>, "Implementation-type must be a subclass of QObject");

    static_assert(std::is_base_of_v<Srv,Impl>, "Implementation-type must be a subclass of Service-type");

    using service_type = Srv;

    using impl_type = Impl;
};



///
/// \brief Specifies a dependency of a service.
/// Can by used as a type-argument for QApplicationContext::registerService().
/// In the standard-case of a mandatory relationship, the use of `Dependency` is optional.
/// Suppose you have a service-type `Reader` that needs a mandatory pointer to a `DatabaseAccess` in its constructor:
///     class Reader : public QObject {
///       public:
///         explicit Update(DatabaseAccess* dao, QObject* parent = nullptr);
///     };
///
/// In that case, the following two lines would be completely equivalent:
///
///     context->registerService<Reader,DatabaseAccess>("reader");
///
///     context->registerService<Reader,Dependency<DatabaseAccess,Cardinality::MANDATORY>>("reader");
///
/// However, if your service can do without a `DatabaseAccess`, you should register it like this:
///
///     context->registerService<Reader,Dependency<DatabaseAccess,Cardinality::OPTIONAL>>("reader");
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
///     context->registerService<Reader,Dependency<DatabaseAccess,Cardinality::N>>("reader");
///
/// As you can see from the previous examples, the type `Dependency` was never actually instantiated!
///
/// Usually, it is sufficient to specify it as a type-argument to the function QApplicationContext::registerService(const QString&,const service_config&).
/// All necessary information can be gained by just looking at `Dependency`'s template-arguments.
///
/// There is, however, one use-case where Dependency must be instantiated: if you want to specify the required name for a dependency, in order
/// to disambiguate it.
///
/// In that case, you'll use one of the other overloads of QApplicationContext::registerService(), as shown in the following example.
///
/// Please note that now you **do not** have to specify the type-argument for the Dependency anymore, as it will be deduced from the supplied
/// function-argument:
///
///     context->registerService<Reader>("reader", Dependency<DatabaseAccess,Cardinality::N>{"oracleDatabaseAccess"});
///
///
template<typename S,Cardinality c=Cardinality::MANDATORY> struct Dependency {
    ///
    /// \brief the required name for this dependency.
    /// The default-value is the empty String, with the implied meaning <em>"any dependency of the correct type may be used"</em>.
    ///
    QString requiredName;
};


///
/// \brief Configures a service for an ApplicationContext.
///
struct service_config final {
    using entry_type = std::pair<QString,QVariant>;


    friend inline bool operator==(const service_config& left, const service_config& right) {
        return left.properties == right.properties && left.autowire == right.autowire && left.initMethod == right.initMethod;
    }


    QVariantMap properties;
    bool autowire = false;
    QString initMethod;
};


namespace detail {



using constructor_t = std::function<QObject*(const QObjectList&)>;


struct dependency_info {
    const std::type_info* m_type;
    Cardinality cardinality;
    constructor_t defaultConstructor;
    QString requiredName;
    const std::type_info& type() const {
        return *m_type;
    }
};

inline bool operator==(const dependency_info& info1, const dependency_info& info2) {
    return info1.type() == info2.type() && info1.cardinality == info2.cardinality && info1.requiredName == info2.requiredName;
}

struct service_descriptor {


    QObject* create(const QObjectList& dependencies) const {
        return constructor ? constructor(dependencies) : nullptr;
    }

    bool matches(const std::type_index& type) const {
        return type == service_type || type == impl_type;
    }


    const std::type_info& service_type;
    const std::type_info& impl_type;
    constructor_t constructor;
    std::vector<dependency_info> dependencies;
    service_config config;
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
           left.dependencies == right.dependencies &&
           left.config == right.config;
 }




 template<typename S> auto couldBeQObject(S* ptr) -> decltype(dynamic_cast<QObject*>(ptr));

 void couldBeQObject(void*);

 template<typename S> constexpr bool could_be_qobject = std::is_same_v<decltype(couldBeQObject(static_cast<S*>(nullptr))),QObject*>;

 template<typename S> auto hasServiceFactory(S*) -> std::integral_constant<int,sizeof(service_factory<S>)>;

 void hasServiceFactory(void*);


 template<typename S> constexpr bool has_service_factory = std::negation_v<std::is_same<decltype(hasServiceFactory(static_cast<S*>(nullptr))),void>>;


///
/// \brief Wraps a QList into a QObject.
/// The type of the QObject needs not be known to the caller, as it will only be passed to unwrapList(QObject*) again!
/// \param list will be wrapped by the QObject.
/// \param parent will become the returned object's parent.
/// \return a QObject that wraps the supplied list.
///
QObject *wrapList(const QObjectList &list, QObject *parent = nullptr);

///
/// \brief Un-wraps the QList that is wrapped b the supplied object.
/// \param obj the object. Must be obtained using wrapList(const QObjectList&).
/// \return a reference to the list wrapped by the obj.
///
const QObjectList &unwrapList(QObject *obj);




template<typename S,typename I,typename F> service_descriptor* create_descriptor(F creator, const std::vector<dependency_info>& dependencies = {}, const service_config& config = service_config{}) {
    static_assert(std::is_base_of_v<QObject,I>, "Impl-type must be a sub-class of QObject");
    return new service_descriptor{typeid(S), typeid(I), creator, dependencies, config};
}

template<typename S> constructor_t get_default_constructor() {
    if constexpr(std::conjunction_v<std::is_base_of<QObject,S>,std::is_default_constructible<S>>) {
        return [](const QObjectList&) { return new S;};
    }
    return nullptr;
}



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

template <typename S,Cardinality card> struct dependency_helper_base {

    using type = S;


    static S* convert(QObject *arg) {
        return dynamic_cast<S*>(arg);
    }

    static dependency_info info() {
        return { &typeid(S), card, get_default_constructor<S>() };
    }

    static dependency_info info(Dependency<S,card> dep) {
        return { &typeid(S), card, get_default_constructor<S>(), dep.requiredName };
    }


};

template <typename S>
struct dependency_helper : dependency_helper_base<S,Cardinality::MANDATORY> {
};

template <typename S>
struct dependency_helper<Dependency<S, Cardinality::N>> {
    using type = S;


    static QList<S*> convert(QObject *arg) {
        return detail::convertQList<S>(detail::unwrapList(arg));
    }

    static dependency_info info() {
        return { &typeid(S), Cardinality::N };
    }

    static dependency_info info(Dependency<S,Cardinality::N> dep) {
        return { &typeid(S), Cardinality::N, nullptr, dep.requiredName };
    }
};

template <typename S>
struct dependency_helper<Dependency<S, Cardinality::OPTIONAL>> : dependency_helper_base<S,Cardinality::OPTIONAL>  {
};

template <typename S>
struct dependency_helper<Dependency<S, Cardinality::MANDATORY>> : dependency_helper_base<S,Cardinality::MANDATORY> {
};

template <typename S>
struct dependency_helper<Dependency<S, Cardinality::PRIVATE_COPY>> : dependency_helper_base<S,Cardinality::PRIVATE_COPY> {
};



template<typename S> auto convert_arg(QObject* arg) {
    return dependency_helper<S>::convert(arg);
}




template<typename D,typename...Dep> struct contains_type_traits;

template<typename D,typename First,typename...Tail> struct contains_type_traits<D,First,Tail...> {
    static constexpr bool value = std::is_same_v<typename dependency_helper<D>::type,typename dependency_helper<First>::type> || contains_type_traits<D,Tail...>::value;
};

template<typename D> struct contains_type_traits<D> {
    static constexpr bool value = false;
};



template<typename First, typename...Tail> void make_dependencies(std::vector<dependency_info>& target) {
    target.push_back(dependency_helper<First>::info());
    if constexpr(sizeof...(Tail) > 0) {
        make_dependencies<Tail...>(target);
    }
}

template<typename First, typename...Tail> void make_dependencies(std::vector<dependency_info>& target, First first, Tail...tail) {
    target.push_back(dependency_helper<First>::info(first));
    if constexpr(sizeof...(Tail) > 0) {
        make_dependencies<Tail...>(target, tail...);
    }
}


template<typename... D> struct descriptor_helper_base {

    static std::vector<dependency_info> dependencies() {
        std::vector<dependency_info> result;
        make_dependencies<D...>(result);
        return result;
    }

    static std::vector<dependency_info> dependencies(D...dep) {
        std::vector<dependency_info> result;
        make_dependencies<D...>(result, dep...);
        return result;
    }


};

template <typename T, typename... D> struct descriptor_helper;

template <typename T>
struct descriptor_helper<T> {
    static constexpr auto creator() {
        return [](const QObjectList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}();
            } else {
                return new T;
            }
        };
    }

    static std::vector<dependency_info> dependencies() {
        return {};
    }
};

template <typename T, typename D1>
struct descriptor_helper<T, D1> : descriptor_helper_base<D1> {
    static constexpr auto creator() {
        return [](const QObjectList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(convert_arg<D1>(dependencies[0]));
            } else {
                return new T{ convert_arg<D1>(dependencies[0]) };
            }
        };
    }

};

template <typename T, typename D1, typename D2>
struct descriptor_helper<T, D1, D2>  : descriptor_helper_base<D1,D2> {
    static constexpr auto creator() {
        return [](const QObjectList &dependencies) {
            if constexpr(detail::has_service_factory<T>) {
                return service_factory<T>{}(convert_arg<D1>(dependencies[0]), convert_arg<D2>(dependencies[1]));
            } else {
                return new T{ convert_arg<D1>(dependencies[0]), convert_arg<D2>(dependencies[1]) };
            }
        };
    }
};

template <typename T, typename D1, typename D2, typename D3>
struct descriptor_helper<T, D1, D2, D3> :  descriptor_helper_base<D1,D2,D3> {
    static constexpr auto creator() {
        return [](const QObjectList &dependencies) {
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
};

template <typename T, typename D1, typename D2, typename D3, typename D4>
struct descriptor_helper<T, D1, D2, D3, D4>  : descriptor_helper_base<D1,D2,D3,D4> {
    static constexpr auto creator() {
        return [](const QObjectList &dependencies) {
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
};

template <typename T, typename D1, typename D2, typename D3, typename D4, typename D5>
struct descriptor_helper<T, D1, D2, D3, D4, D5>  : descriptor_helper_base<D1,D2,D3,D4,D5> {
    static constexpr auto creator() {
        return [](const QObjectList &dependencies) {
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
};

template <typename T>
struct service_traits {
    static_assert(std::is_base_of_v<QObject,T>, "Service-type must be a subclass of QObject");

    using service_type = T;

    using impl_type = T;
};

template <typename Srv, typename Impl>
struct service_traits<Service<Srv, Impl>> {

    using service_type = typename Service<Srv, Impl>::service_type;

    using impl_type = typename Service<Srv, Impl>::impl_type;
};

template<typename...Dep> void getRequiredName(Dep...dependencies) {

}



} // namespace detail






///
/// \brief A DI-Container for Qt-based applications.
///
class QApplicationContext : public QObject
{
    Q_OBJECT

public:

    ///
    /// \brief Have all registered services been published?
    /// This property will initially yield `false`, until publish() is invoked.
    /// If that was successul, this property will yield `true`.
    /// It will stay `true` as long as no more services are registered but not yet published.
    /// **Note:** This property will **not** transition back to `false` upon destruction of this ApplicationContext!
    /// \return `true` if all registered services have been published.
    ///
    Q_PROPERTY(bool published READ published NOTIFY publishedChanged)


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
    /// \see registerService(const QString&,std::initializer_list<service_config::entry_type>,bool,const QString&).
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename D1,Cardinality c1> auto registerService(const QString& objectName, const service_config& config, Dependency<D1,c1> dep1) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        return registerServiceWithDependencies<S>(objectName, config, dep1);
    }

    ///
    /// \brief Registers a service with this ApplicationContext.
    /// \see registerService(const QString&,std::initializer_list<service_config::entry_type>,bool,const QString&).
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename D1,Cardinality c1,typename D2,Cardinality c2> auto registerService(const QString& objectName, const service_config& config, Dependency<D1,c1> dep1, Dependency<D2,c2> dep2) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        return registerServiceWithDependencies<S>(objectName, config, dep1, dep2);
    }

    ///
    /// \brief Registers a service with this ApplicationContext.
    /// \see registerService(const QString&,std::initializer_list<service_config::entry_type>,bool,const QString&).
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename D1,Cardinality c1,typename D2,Cardinality c2,typename D3,Cardinality c3> auto registerService(const QString& objectName, const service_config& config, Dependency<D1,c1> dep1, Dependency<D2,c2> dep2, Dependency<D3,c3> dep3) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        return registerServiceWithDependencies<S>(objectName, config, dep1, dep2, dep3);
    }

    ///
    /// \brief Registers a service with this ApplicationContext.
    /// \see registerService(const QString&,std::initializer_list<service_config::entry_type>,bool,const QString&).
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename D1,Cardinality c1,typename D2,Cardinality c2,typename D3,Cardinality c3,typename D4,Cardinality c4> auto registerService(const QString& objectName, const service_config& config, Dependency<D1,c1> dep1, Dependency<D2,c2> dep2, Dependency<D3,c3> dep3, Dependency<D4,c4> dep4) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        return registerServiceWithDependencies<S>(objectName, config, dep1, dep2, dep3, dep4);
    }

    ///
    /// \brief Registers a service with this ApplicationContext.
    /// \see registerService(const QString&,std::initializer_list<service_config::entry_type>,bool,const QString&).
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the Configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename D1,Cardinality c1,typename D2,Cardinality c2,typename D3,Cardinality c3,typename D4,Cardinality c4,typename D5,Cardinality c5> auto registerService(const QString& objectName, const service_config& config, Dependency<D1,c1> dep1, Dependency<D2,c2> dep2, Dependency<D3,c3> dep3, Dependency<D4,c4> dep4, Dependency<D5,c5> dep5) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        return registerServiceWithDependencies<S>(objectName, config, dep1, dep2, dep3, dep4, dep5);
    }


    ///
    /// \brief Registers a service with this ApplicationContext.
    ///
    /// There are two alternatives for specifying the service-type:
    /// 1. Use a QObject-derived class directly as the service-type:
    ///
    ///     context -> registerService<QNetworkAccessManager>();
    ///
    /// 2. Use the template Service, which helps separate the service-interface from its implementation:
    ///
    ///     context -> registerService<Service<QAbstractItemModel,QStringListModel>>();
    ///
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param properties the configuration-properties for the service.
    /// \param autowire determines whether the service's properties shall be autowired.
    /// \param initMethod the Q_INVOKABLE method to call before publishing this service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename...Dep> auto registerService(const QString& objectName = "", std::initializer_list<service_config::entry_type> properties = {}, bool autowire = false, const QString& initMethod = "") -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        return registerService<S,Dep...>(objectName, service_config{properties, autowire, initMethod});
    }

    ///
    /// \brief Registers a service with this ApplicationContext.
    ///
    /// There are two alternatives for specifying the service-type:
    /// 1. Use a QObject-derived class directly as the service-type:
    ///
    ///     context -> registerService<QNetworkAccessManager>();
    ///
    /// 2. Use the template Service, which helps separate the service-interface from its implementation:
    ///
    ///     context -> registerService<Service<QAbstractItemModel,QStringListModel>>();
    ///
    /// \param objectName the name that the service shall have. If empty, a name will be auto-generated.
    /// The instantiated service will get this name as its QObject::objectName(), if it does not set a name itself in
    /// its constructor.
    /// \param config the configuration for the service.
    /// \tparam S the service-type.
    /// \return a ServiceRegistration for the registered service, or `nullptr` if it could not be registered.
    ///
    template<typename S,typename...Dep> auto registerService(const QString& objectName, const service_config& config) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        using service_type = typename detail::service_traits<S>::service_type;
        using impl_type = typename detail::service_traits<S>::impl_type;
        using descriptor_helper = detail::descriptor_helper<impl_type,Dep...>;
        auto dependencies = descriptor_helper::dependencies();
        auto result = registerService(objectName, detail::create_descriptor<service_type,impl_type>(descriptor_helper::creator(), dependencies, config));
        return ServiceRegistration<service_type>::wrap(result);
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
    /// \return a ServiceRegistration for the registered object, or `nullptr` if it could not be registered.
    ///
    template<typename S> ServiceRegistration<S>* registerObject(S* obj, const QString& objName = "") {
        static_assert(detail::could_be_qobject<S>, "Object is not convertible to QObject");
        return ServiceRegistration<S>::wrap(registerObject(objName, dynamic_cast<QObject*>(obj), new service_descriptor{typeid(S), typeid(*obj)}));
    }

    ///
    /// \brief Obtains a ServiceRegistration for a service-type.
    /// In contrast to the ServiceRegistration that is returned by registerService(),
    /// the ServiceRegistration returned by this function manages all Services of the requested type.
    /// This means that if you subscribe to it using ServiceRegistration::subscribe(), you will be notified
    /// about all those published services.
    /// \return a ServiceRegistration that manages all Services of the requested type.
    ///
    template<typename S> [[nodiscard]] ServiceRegistration<S>* getRegistration() const {
        return ServiceRegistration<S>::wrap(getRegistration(typeid(S)));
    }



    ///
    /// \brief Publishes this ApplicationContext.
    /// This method may be invoked multiple times.
    /// Each time it is invoked, it will attempt to instantiate all yet-unpublished services that have been registered with this ApplicationContext.
    /// \return `true` if this ApplicationContext could be successfully published.
    ///
    virtual bool publish() = 0;

    ///
    /// \brief Have all registered services been published?
    /// This property will initially yield `false`, until publish() is invoked.
    /// If that was successul, this property will yield `true`.
    /// It will stay `true` as long as no more services are registered but not yet published.
    /// **Note:** This property will **not** transition back to `false` upon destruction of this ApplicationContext!
    /// \return `true` if all registered services have been published.
    ///
    virtual bool published() const = 0;

signals:

    ///
    /// \brief Signals that the published() property has changed.
    /// This signal will be emitted with a `true`value after a successful invocatin of publich().
    /// It will be emitted with a `false` value when a new service has been registered after publication.
    /// **Note:** the signal will not be emitted on destruction of this ApplicationContext!
    ///
    void publishedChanged(bool);



protected:


    ///
    /// \brief Registers a service with this QApplicationContext.
    /// \param objectName
    /// \param config
    /// \param dependencyInfos
    ///
    template<typename S,typename...Dep> auto registerServiceWithDependencies(const QString& objectName, const service_config& config, Dep... dependencyInfos) -> ServiceRegistration<typename detail::service_traits<S>::service_type>* {
        using service_type = typename detail::service_traits<S>::service_type;
        using impl_type = typename detail::service_traits<S>::impl_type;
        using descriptor_helper = detail::descriptor_helper<impl_type,Dep...>;
        auto dependencies = descriptor_helper::dependencies(dependencyInfos...);
        auto result = registerService(objectName, detail::create_descriptor<service_type,impl_type>(descriptor_helper::creator(), dependencies, config));
        return ServiceRegistration<service_type>::wrap(result);
    }


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
    virtual Registration* registerService(const QString& name, service_descriptor* descriptor) = 0;

    ///
    /// \brief Registers an Object with this QApplicationContext.
    /// \param name
    /// \param obj
    /// \param descriptor
    /// \return a Registration for the object, or `nullptr` if it could not be registered.
    ///
    virtual Registration* registerObject(const QString& name, QObject* obj, service_descriptor* descriptor) = 0;

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
    static Registration* delegateRegisterService(QApplicationContext& appContext, const QString& name, service_descriptor* descriptor) {
        return appContext.registerService(name, descriptor);
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
    static Registration* delegateRegisterObject(QApplicationContext& appContext, const QString& name, QObject* obj, service_descriptor* descriptor) {
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



Q_DECLARE_LOGGING_CATEGORY(loggingCategory)

}
