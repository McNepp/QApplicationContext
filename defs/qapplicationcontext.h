#pragma once

#include <utility>
#include <typeindex>
#include <unordered_set>
#include <QObject>
#include <QVariant>
#include <QLoggingCategory>

namespace com::neppert::context {

class QApplicationContext;

///
/// This template can be used to force the QApplicationContext to use a static factory-function instead of a constructor.
/// You may specialize this template for your own component-types.
/// If you do so, it must be a Callable object with a pointer to your component as its return-type
/// and as many arguments as are needed to construct an instance.
///
template<typename S> struct service_factory;


///
/// \brief Describes a Service by its interface and implementation.
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
/// \brief Specifies the cardinality of a Service-dependency.
/// Will be used as a non-type argument to Dependency, when registering a Service.
///
enum class Cardinality {
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
/// \brief Specifies a dependency of a Service.
/// Can by used as a type-argument for QApplicationContext::registerService(const QString&, const QApplicationContext::Config&).
/// In the standard-case of a mandatory relationship, the use of `Dependency` is optional.
/// Suppose you have a service-type `Reader` that needs a mandatory pointer to a `DatabaseAccess` in its constructor:

///    class Reader : public QObject {
///      public:
///        explicit Update(DatabaseAccess* dao, QObject* parent = nullptr);
///    };
///
/// In that case, the following two lines would be completely equivalent:
///
///    context->registerService<Reader,DatabaseAccess>("reader");
///
///    context->registerService<Reader,Dependency<DatabaseAccess,Cardinality::MANDATORY>>("reader");
///
/// However, if your service can do without a `DatabaseAccess`, you should register it like this:
///
///    context->registerService<Reader,Dependency<DatabaseAccess,Cardinality::OPTIONAL>>("reader");
///
/// Consider the case where your `Reader` takes a List of `DatabaseAccess` instances:
///
///    class Reader : public QObject {
///      public:
///        explicit Update(const QList<DatabaseAccess>& daos, QObject* parent = nullptr);
///    };
///
/// In that case, it would be registered in an ApplicationContext using the following line:
///
///    context->registerService<Reader,Dependency<DatabaseAccess,Cardinality::N>>("reader");
///
///
template<typename S,Cardinality c> struct Dependency {
};




struct config_data final {
    using entry_type = std::pair<QString,QVariant>;


    friend inline bool operator==(const config_data& left, const config_data& right) {
        return left.properties == right.properties && left.autowire == right.autowire && left.initMethod == right.initMethod;
    }


    QVariantMap properties;
    bool autowire = false;
    QString initMethod;
};



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
    service_descriptor(const std::type_info& service_type, const std::type_info& impl_type,
            constructor_t constructor = nullptr,
            const std::vector<dependency_info>& dependencies = {},
            const config_data& config = config_data{}) :
            m_service_type(service_type),
            m_impl_type(impl_type),
            m_constructor(constructor),
            m_dependencies(dependencies),
            m_config(config)
    {
    }

    const std::type_info& service_type() const {
        return m_service_type;
    }

    const std::type_info& impl_type() const {
        return m_impl_type;
    }



    const std::vector<dependency_info>& dependencies() const {
        return m_dependencies;
    }


    QObject* create(const QObjectList& dependencies) const {
        return m_constructor ? m_constructor(dependencies) : nullptr;
    }

    bool matches(const std::type_index& type) const {
        return type == m_service_type || type == m_impl_type;
    }

    const config_data& config() const {
        return m_config;
    }

    const std::type_info& m_service_type;
    const std::type_info& m_impl_type;
    constructor_t m_constructor;
    std::vector<dependency_info> m_dependencies;
    config_data m_config;
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
    return left.service_type() == right.service_type() &&
           left.impl_type() == right.impl_type() &&
           left.dependencies() == right.dependencies() &&
           left.config() == right.config();
 }



namespace detail {

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




template<typename S,typename I,typename F> service_descriptor* create_descriptor(F creator, const std::vector<dependency_info>& dependencies = {}, const config_data& config = config_data{}) {
    static_assert(std::is_base_of_v<QObject,I>, "Impl-type must be a sub-class of QObject");
    return new service_descriptor(typeid(S), typeid(I), creator, dependencies, config);
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

template<typename... D> struct descriptor_helper_base {

    static std::vector<dependency_info> dependencies() {
        std::vector<dependency_info> result;
        make_dependencies<D...>(result);
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
    static_assert(std::is_base_of_v<QObject, T>, "Service-type must be a subclass of QObject");

    using service_type = T;

    using impl_type = T;
};

template <typename Srv, typename Impl>
struct service_traits<Service<Srv, Impl>> {

    using service_type = typename Service<Srv, Impl>::service_type;

    using impl_type = typename Service<Srv, Impl>::impl_type;
};



} // namespace detail

template<typename S,typename...Dep> class ServiceConfig {
public:

    ServiceConfig() = default;


    static ServiceConfig autowired()
    {
        ServiceConfig cfg;
        cfg.m_config.autowire = true;
        return cfg;
    }



           ///
           /// \brief Specifies that the dependency of the supplied type must have a specific name.
           /// If the supplied type `<D>` is not one of the Dependency-types of the ExtendedServiceConfig,
           /// compilation will fail with a diagnostic.
           /// \param name the name of the object that shall be used as dependency.
           ///
    template<typename D> void setRequiredName(const QString& name) {
        static_assert(detail::contains_type_traits<D,Dep...>::value, "Type must be one of the dependent-types");
        m_requiredNames.insert({typeid(D), name});
    }

           ///
           /// \brief Specifies that the dependency of the supplied type must have a specific name.
           /// If the supplied type `<D>` is not one of the Dependency-types of the ExtendedServiceConfig,
           /// compilation will fail with a diagnostic.
           /// \param name the name of the object that shall be used as dependency.
           /// \return an ServiceConfig with that requirement.
           ///
    template <typename D>
    ServiceConfig withRequiredName(const QString &name) && {
        static_assert(detail::contains_type_traits<D,Dep...>::value, "Type must be one of the dependent-types");
        ServiceConfig cfg{std::move(*this)};
        cfg.setRequiredName<D>(name);
        return cfg;
    }

           ///
           /// \brief Specifies that the dependency of the supplied type must have a specific name.
           /// If the supplied type `<D>` is not one of the Dependency-types of the ExtendedServiceConfig,
           /// compilation will fail with a diagnostic.
           /// \param name the name of the object that shall be used as dependency.
           /// \return an ServiceConfig with that requirement.
           ///
    template <typename D>
    ServiceConfig withRequiredName(const QString &name) const& {
        static_assert(detail::contains_type_traits<D,Dep...>::value, "Type must be one of the dependent-types");
        ServiceConfig cfg{*this};
        cfg.setRequiredName<D>(name);
        return cfg;
    }

    void setProperties(std::initializer_list<config_data::entry_type> properties) {
        for(auto& entry : properties) {
            m_config.properties.insert(entry.first, entry.second);
        }
    }

    ServiceConfig withProperties(std::initializer_list<config_data::entry_type> properties) const& {
        ServiceConfig cfg{*this};
        cfg.setProperties(properties);
        return cfg;
    }

    ServiceConfig withProperties(std::initializer_list<config_data::entry_type> properties) && {
        ServiceConfig cfg{std::move(*this)};
        cfg.setProperties(properties);
        return cfg;
    }


    ServiceConfig withInitMethod(const QString& initMethod) const& {
        ServiceConfig cfg{*this};
        cfg.setInitMethod(initMethod);
        return cfg;
    }

    ServiceConfig withInitMethod(const QString& initMethod) && {
        ServiceConfig cfg{std::move(*this)};
        cfg.setInitMethod(initMethod);
        return cfg;
    }

    void setInitMethod(const QString& initMethod) {
        m_config.initMethod = initMethod;
    }

    const config_data& data() const {
        return m_config;
    }




    QString requiredName(const std::type_info& type) const {
        auto found = m_requiredNames.find(type);
        return found != m_requiredNames.end() ? found->second : QString{};
    }

private:

    ServiceConfig(const config_data& config, const std::unordered_map<std::type_index,QString>& requiredNames) :
            m_config{config},
            m_requiredNames{requiredNames}
    {
    }

    ServiceConfig(config_data&& config, std::unordered_map<std::type_index,QString>&& requiredNames) :
            m_config{std::move(config)},
            m_requiredNames{std::move(requiredNames)}
    {
    }
    std::unordered_map<std::type_index,QString> m_requiredNames;
    config_data m_config;

};




///
/// An  type that serves as a "handle" for registrations.
///
class Registration : public QObject {
    Q_OBJECT

    Q_PROPERTY(QObjectList publishedObjects READ getPublishedObjects NOTIFY publishedObjectsChanged)

public:
    [[nodiscard]] virtual const std::type_info& service_type() const = 0;

    [[nodiscard]] virtual QObjectList getPublishedObjects() const = 0;

signals:

    void publishedObjectsChanged();

protected:

    explicit Registration(QObject* parent = nullptr) : QObject(parent) {

    }

    virtual ~Registration() = default;
};

///
/// \brief A type-safe wrapper for a Registration.
/// Instances of this class are being returned by the public function-templates QApplicationContext::registerService(),
/// QApplicationContext::registerObject(QObject*) and IApplicationService::getRegistration().
///
template<typename S> class TRegistration : public Registration {
    friend class QApplicationContext;
public:
    [[nodiscard]] virtual const std::type_info& service_type() const override {
        return unwrap()->service_type();
    }



    [[nodiscard]] virtual QObjectList getPublishedObjects() const override {
        return unwrap()->getPublishedObjects();
    }



           ///
           /// \brief Receive all published QObjects in a type-safe way.
           /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
           /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
           /// If the ApplicationContext has already been published, this method
           /// will invoke the callable immediately with the current getPublishedObjects().
           /// \param context
           /// \param callable
           ///
    template<typename F> void onPublished(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        connect(this, &Registration::publishedObjectsChanged, context, Notifier<F>{callable, this}, connectionType);
        emit publishedObjectsChanged();
    }

           /// \brief Receive all published QObjects in a type-safe way.
           /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
           /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
           /// If the ApplicationContext has already been published, this method
           /// will invoke the setter immediately with the current getPublishedObjects().
           /// \param target
           /// \param setter
           ///
    template<typename T,typename A,typename R> void onPublished(T* target, R (T::*setter)(A*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        onPublished(target, std::bind(std::mem_fn(setter), target, std::placeholders::_1), connectionType);
    }



    Registration* unwrap() const {
        return static_cast<Registration*>(parent());
    }




private:
    explicit TRegistration(Registration* reg) : Registration(reg)
    {
        connect(reg, &Registration::publishedObjectsChanged, this, &Registration::publishedObjectsChanged);
    }



    static TRegistration* wrap(Registration* reg) {
        return reg ? new TRegistration(reg) : nullptr;
    }

    template<typename F> struct Notifier {
        F callable;

        Registration* source;

        std::unordered_set<S*> publishedObjects;

        void operator()() {
            for(auto obj : source->getPublishedObjects()) {
                S* ptr = dynamic_cast<S*>(obj);
                if(ptr && publishedObjects.insert(ptr).second) {
                    callable(ptr);
                }
            }
        }
    };
};


///
/// \brief Extended Service-Configuration.
/// Allows to add additional constraints regarding the Service-dependencies.
///







class QApplicationContext : public QObject
{
    Q_OBJECT

public:

    Q_PROPERTY(bool published READ published NOTIFY publishedChanged)

    template<typename S,typename...Dep> auto registerService(const QString& objectName, const ServiceConfig<S,Dep...>& config) -> TRegistration<typename detail::service_traits<S>::service_type>* {
        using service_type = typename detail::service_traits<S>::service_type;
        using impl_type = typename detail::service_traits<S>::impl_type;
        using descriptor_helper = detail::descriptor_helper<impl_type,Dep...>;
        auto dependencies = descriptor_helper::dependencies();
        for(auto& dep : dependencies) {
            dep.requiredName = config.requiredName(dep.type());
        }
        auto result = registerService(objectName, detail::create_descriptor<service_type,impl_type>(descriptor_helper::creator(), dependencies, config.data()));
        return TRegistration<service_type>::wrap(result);
    }

    template<typename S,typename...Dep> auto registerService(const QString& objectName = "", std::initializer_list<config_data::entry_type> properties = {}) -> TRegistration<typename detail::service_traits<S>::service_type>* {
        using service_type = typename detail::service_traits<S>::service_type;
        using impl_type = typename detail::service_traits<S>::impl_type;
        using descriptor_helper = detail::descriptor_helper<impl_type,Dep...>;
        auto dependencies = descriptor_helper::dependencies();
        auto result = registerService(objectName, detail::create_descriptor<service_type,impl_type>(descriptor_helper::creator(), dependencies, config_data{properties}));
        return TRegistration<service_type>::wrap(result);
    }





    template<typename S> TRegistration<S>* registerObject(S* obj, const QString& objName = {}) {
        static_assert(detail::could_be_qobject<S>, "Object is not convertible to QObject");
        return TRegistration<S>::wrap(registerObject(objName, dynamic_cast<QObject*>(obj), new service_descriptor(typeid(S), typeid(*obj))));
    }

    template<typename S> [[nodiscard]] TRegistration<S>* getRegistration() const {
        return TRegistration<S>::wrap(getRegistration(typeid(S)));
    }



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

    void publishedChanged(bool);



protected:

    explicit QApplicationContext(QObject* parent = nullptr);


    virtual Registration* registerService(const QString& name, service_descriptor* descriptor) = 0;

    virtual Registration* registerObject(const QString& name, QObject* obj, service_descriptor* descriptor) = 0;

    virtual Registration* getRegistration(const std::type_info& service_type) const = 0;

    static Registration* delegateRegisterService(QApplicationContext& appContext, const QString& name, service_descriptor* descriptor) {
        return appContext.registerService(name, descriptor);
    }

    static Registration* delegateRegisterObject(QApplicationContext& appContext, const QString& name, QObject* obj, service_descriptor* descriptor) {
        return appContext.registerObject(name, obj, descriptor);
    }

    static Registration* delegateGetRegistration(const QApplicationContext& appContext, const std::type_info& service_type) {
        return appContext.getRegistration(service_type);
    }


};


///
/// \brief A mix-in interface for classes that may modify services before publication.
/// The process(QObject*) method will be invoked for each service after its properties have been set, but
/// before an *init-method* is invoked.
///
class QApplicationContextPostProcessor {
public:
    virtual void process(QApplicationContext* appContext, QObject* service) = 0;

    virtual ~QApplicationContextPostProcessor() = default;
};

Q_DECLARE_LOGGING_CATEGORY(loggingCategory)

}
