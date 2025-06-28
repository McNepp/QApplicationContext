#include <QThread>
#include <QEvent>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QUuid>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include "standardapplicationcontext.h"
#include "qsettingswatcher.h"



namespace mcnepp::qtdi {


inline QDebug operator<<(QDebug out, ServiceScope scope) {
    switch(scope) {
    case ServiceScope::EXTERNAL:
        return out.noquote().nospace() << "EXTERNAL";
    case ServiceScope::SINGLETON:
        return out.noquote().nospace() << "SINGLETON";
    case ServiceScope::PROTOTYPE:
        return out.noquote().nospace() << "PROTOTYPE";
    case ServiceScope::TEMPLATE:
        return out.noquote().nospace() << "TEMPLATE";
    case ServiceScope::UNKNOWN:
        return out.noquote().nospace() << "UNKNOWN";
    default:
        return out.noquote().nospace() << "Invalid ServiceScope";

    }

}

namespace detail {

constexpr int DESCRIPTOR_NO_MATCH = 0;
constexpr int DESCRIPTOR_INTERSECTS = 1;
constexpr int DESCRIPTOR_IDENTICAL = 2;



/**
     * @brief Is a service_descriptor compatible with another one?
     *  -# If the left descriptor has a different impl_type than the right, will return DESCRIPTOR_NO_MATCH.
     *  -# If the left descriptor has different dependencies than the right, will return DESCRIPTOR_NO_MATCH.
     *  -# If the service_types are equal, will return DESCRIPTOR_IDENTICAL.
     *  -# If the left descriptor's service_types are a full sub-set of the other's, or vice versa, will return DESCRIPTOR_INTERSECTS.
     *  -# Otherwise, will return DESCRIPTOR_NO_MATCH.
     * @param other
     * @return The kind of match between the two descriptors.
     */
int match(const service_descriptor& left, const service_descriptor& right) {
    if(left.impl_type != right.impl_type || left.dependencies != right.dependencies) {
        return DESCRIPTOR_NO_MATCH;
    }
    //The straight-forward case: both sets are equal.
    if(left.service_types == right.service_types) {
        return DESCRIPTOR_IDENTICAL;
    }
    //Otherwise, if the sets have the same size, one cannot be a sub-set of the other:
    if(left.service_types.size() == right.service_types.size()) {
        return DESCRIPTOR_NO_MATCH;
    }
    const auto& larger = left.service_types.size() > right.service_types.size() ? left.service_types : right.service_types;
    const auto& smaller = left.service_types.size() < right.service_types.size() ? left.service_types : right.service_types;
    for(auto& type : smaller) {
        //If at least one item of the smaller set cannot be found in the larger set
        if(larger.find(type) == larger.end()) {
            return DESCRIPTOR_NO_MATCH;
        }
    }
    return DESCRIPTOR_INTERSECTS;
}


BindingProxy::BindingProxy(QMetaProperty sourceProp, QObject* source, const detail::property_descriptor& setter, QObject* target) : QObject(source),
    m_sourceProp(sourceProp),
    m_source(source),
    m_target(target),
    m_setter(setter) {

}

const QMetaMethod &BindingProxy::notifySlot()
{
    static QMetaMethod theSlot = staticMetaObject.method(staticMetaObject.indexOfSlot("notify()"));
    return theSlot;
}

void BindingProxy::notify()
{
    m_setter.setter(m_target, m_sourceProp.read(m_source));
}


inline detail::property_descriptor propertySetter(const QMetaProperty& property) {
    return {property.name(), [property](QObject* target, QVariant value) {property.write(target, value);}};
}




bool isBindable(const QMetaProperty& sourceProperty) {
    return sourceProperty.hasNotifySignal() || sourceProperty.isBindable();
}




}

namespace {

bool getBeanRef(QString& str) {
    static QRegularExpression regEx{"^&([^.]+)"};
    auto match = regEx.match(str);
    if(match.hasMatch()) {
        str = match.captured(1);
        return true;
    }
    return false;
}

Profiles& defaultProfiles() {
    static Profiles profiles{"default"};
    return profiles;
}

QString profilesAsString(const Profiles& profiles) {
    QString result{"[profiles="};
    const char* del = "";
    for(const QString& str : profiles) {
        result += del;
        result += str;
        del = ", ";
    }
    result += "]";
    return result;
}


const QStringList splitList(const QString& str) {
    static QRegularExpression regEx{",\\s*"};
    return str.split(regEx, Qt::SkipEmptyParts);
}

inline void setParentIfNotSet(QObject* obj, QObject* newParent) {
    if(!obj->parent()) {
        obj->setParent(newParent);
    }
}




template<typename T> struct Collector : public detail::Subscription {

    Collector() {
        QObject::connect(this, &detail::Subscription::objectPublished, this, &Collector::collect);
    }

    template<typename Cont> auto subscribeAll(const Cont& container) ->
        std::enable_if_t<std::conjunction_v<std::is_assignable<registration_handle_t&,decltype(*container.begin())>,std::is_assignable<registration_handle_t&,decltype(*container.end())>>,void>
    {
        for(auto& reg : container) {
            reg->subscribe(this);
        }
    }

    QList<T*> collected;

    void cancel() override {

    }

    void connectTo(registration_handle_t) override {

    }

    void collect(QObject* obj) {
        if(auto ptr = dynamic_cast<T*>(obj)) {
            collected.push_back(ptr);
        }
    }
};




QStringList determineBeanRefs(const detail::service_config::map_type& properties) {
    QStringList result;
    for(auto entry : properties.asKeyValueRange()) {
        switch(entry.second.configType) {

        case detail::ConfigValueType::AUTO_REFRESH_EXPRESSION:
        case detail::ConfigValueType::SERVICE:
            continue;
        default:

            if(QString key = entry.second.expression.toString(); getBeanRef(key)) {
                result.push_back(key);
            }
        }
    }
    return result;
}

QVariantMap initPlaceholders(const detail::service_config::map_type& properties)
{
    QVariantMap resolved;
    for(const auto& entry : properties.asKeyValueRange()) {
        if(entry.second.configType == detail::ConfigValueType::PRIVATE) {
            resolved[entry.first] = entry.second.expression;
        }
    }
    return resolved;
}




template<typename C,typename P> auto eraseIf(C& container, P predicate) -> std::enable_if_t<std::is_pointer_v<typename C::value_type>,typename C::value_type> {
        auto iterator = std::find_if(container.begin(), container.end(), predicate);
        if(iterator != container.end()) {
            auto value = *iterator;
            container.erase(iterator);
            return value;
        }
        return nullptr;
}



template<typename C> auto pop_front(C& container) -> typename C::value_type {
        auto value = container.front();
        container.pop_front();
        return value;
}


QString makeName(const std::type_index& type) {
    QString typeName{detail::type_name(type)};
    typeName.replace(' ', '-');
    return typeName+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
}







class PropertyInjector : public detail::MultiServiceSubscription {
public:




    QMetaObject::Connection connectObjectsPublished() override
    {
        return connect(this, &detail::MultiServiceSubscription::objectsPublished, this, &PropertyInjector::notify);
    }


    MultiServiceSubscription* newChild(const MultiServiceSubscription::target_list_t& targets) override {
        return new PropertyInjector{targets, m_sourceProperty, m_setter, m_loggingCategory, this};
    }

    void cancel() override {
        for(auto& conn : connections) {
            QObject::disconnect(conn);
        }
        //QPropertyNotifier will remove the binding in its destructor:
        bindings.clear();
        MultiServiceSubscription::cancel();
    }

    PropertyInjector(const MultiServiceSubscription::target_list_t& targets, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter, const QLoggingCategory& loggingCategory, QObject* parent) :
        MultiServiceSubscription(targets, parent),
        m_sourceProperty(sourceProperty),
        m_setter(setter),
        m_loggingCategory(loggingCategory)    {
    }



private:

    void notify(const QObjectList& objs) {
        auto source = objs[0];
        auto target = objs[1];
        m_setter.setter(target, m_sourceProperty.read(source));
        if(m_sourceProperty.hasNotifySignal()) {
            detail::BindingProxy* proxy = new detail::BindingProxy{m_sourceProperty, source, m_setter, target};
            auto connection = QObject::connect(source, m_sourceProperty.notifySignal(), proxy, detail::BindingProxy::notifySlot());
            qCDebug(m_loggingCategory).nospace().noquote() << "Bound property '" << m_sourceProperty.name() << "' of " << source << " to " << m_setter <<" of " << target;
            connections.push_back(std::move(connection));
            return;
        }
        if(m_sourceProperty.isBindable()) {
            auto sourceBindable = m_sourceProperty.bindable(source);
            auto notifier = sourceBindable.addNotifier([this,source,target]{
                m_setter.setter(target, m_sourceProperty.read(source));
            });
            qCDebug(m_loggingCategory).nospace().noquote() << "Bound property '" << m_sourceProperty.name() << "' of " << source << " to " << m_setter << " of " << target;
            bindings.push_back(std::move(notifier));
            return;
        }
        qCWarning(m_loggingCategory).nospace().noquote() << "Could not bind property '" << m_sourceProperty.name() << "' of " << source << " to " << m_setter << " of " << target;

    }

    QMetaProperty m_sourceProperty;
    detail::property_descriptor m_setter;
    std::vector<QPropertyNotifier> bindings;
    std::vector<QMetaObject::Connection> connections;
    const QLoggingCategory& m_loggingCategory;

};





///
/// \brief Passes the signal through, but does not accept connections from a source-Registration.
///
class TemporarySubscriptionProxy : public detail::Subscription {
public:
    explicit TemporarySubscriptionProxy(Subscription* target) :
        detail::Subscription{target} {
        QObject::connect(this, &Subscription::objectPublished, target, &Subscription::objectPublished);
    }

    void connectTo(registration_handle_t) override {
        //Does nothing intentionally
    }

    void cancel() override {
    }
};


} // End of anonymous namespace



class StandardApplicationContext::CreateHandleEvent : public QEvent {
public:
    using create_handle_t = std::function<QObject*()>;

    static QEvent::Type eventId() {
        static int eventId = QEvent::registerEventType();
        return static_cast<QEvent::Type>(eventId);
    }


    explicit CreateHandleEvent(create_handle_t func) :
        QEvent(eventId()),
        m_result(QSharedPointer<std::optional<QObject*>>::create()),
        m_func{func}
    {
    }

    void createHandle() {
        *m_result = m_func();
    }

    QSharedPointer<std::optional<QObject*>> result() const {
        return m_result;
    }

private:
    QSharedPointer<std::optional<QObject*>> m_result;
    create_handle_t m_func;
};



class StandardApplicationContext::ProxySubscription : public detail::Subscription {
public:
    explicit ProxySubscription(registration_handle_t target, bool initiallyEnabled) :
        detail::Subscription{target},
        m_target{target}    {
        if(initiallyEnabled) {
            enableSignal();
        }
    }

    void connectTo(registration_handle_t source) override {
        in_connections.push_back(detail::connect(source, this));
    }

    void cancel() override {
        QObject::disconnect(out_connection);
        for(auto& connection : in_connections) {
            QObject::disconnect(connection);
        }
    }

    void enableSignal() {
        out_connection = QObject::connect(this, &Subscription::objectPublished, m_target, &detail::Registration::objectPublished);
    }

private:
    registration_handle_t const m_target;
    QMetaObject::Connection out_connection;
    QList<QMetaObject::Connection> in_connections;
};



StandardApplicationContext::ProxyRegistrationImpl::ProxyRegistrationImpl(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent) :
    detail::ProxyRegistration{parent},
    m_type(type),
    m_meta(metaObject),
    m_context(parent)
{
    proxySubscription = new ProxySubscription{this, false};
    for(auto reg : parent->registrations) {
         add(reg);
    }
    proxySubscription->enableSignal();
}

QObjectList StandardApplicationContext::ProxyRegistrationImpl::obtainServices(descriptor_list& created) {
    QObjectList result;
    for(auto reg : m_context -> registrations) {
        if(canAdd(reg)) {
            result.append(reg->obtainServices(created));
        }
    }
    return result;
}

QList<service_registration_handle_t> StandardApplicationContext::ProxyRegistrationImpl::registeredServices() const {
    QList<service_registration_handle_t> result;
    for(auto reg : m_context -> registrations) {
        if(canAdd(reg)) {
            result.push_back(reg);
        }
    }
    return result;
}

bool StandardApplicationContext::ProxyRegistrationImpl::add(
    DescriptorRegistration* reg) {
    if (canAdd(reg)) {
        reg->subscribe(proxySubscription);
        return true;
    }
    return false;
}

bool StandardApplicationContext::ProxyRegistrationImpl::canAdd(
    DescriptorRegistration* reg) const {
    return reg->scope() != ServiceScope::TEMPLATE && reg->matches(m_type);
}



void StandardApplicationContext::ProxyRegistrationImpl::onSubscription(subscription_handle_t subscription) {
    detail::connect(this, subscription);
    TemporarySubscriptionProxy tempProxy{subscription};
    //By subscribing to a TemporarySubscriptionProxy, we force existing objects to be signalled immediately, while not creating any new Connections:
    for(auto reg : m_context -> registrations) {
        if(canAdd(reg)) {
            reg->subscribe(&tempProxy);
        }
    }
}






subscription_handle_t StandardApplicationContext::DescriptorRegistration::createBindingTo(const detail::source_property_descriptor& sourcePropertyDescriptor, detail::Registration *target, const detail::property_descriptor& targetProperty)
{
    if(!detail::hasCurrentThreadAffinity(this)) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot create binding in different thread";
        return nullptr;
    }

    detail::property_descriptor setter = targetProperty;
    auto sourceProperty = getProperty(this, sourcePropertyDescriptor);
    if(!sourceProperty.isValid()) {
        qCCritical(loggingCategory()).noquote().nospace() << sourcePropertyDescriptor << " not found for " << *this;
        return nullptr;

    }

    if(this == target && sourceProperty.name() == setter.name) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourceProperty.name() << "' of " << *this << " to self";
        return nullptr;
    }

    if(target->applicationContext() != applicationContext()) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourceProperty.name() << "' of " << *this << " to " << *target << " from different ApplicationContext";
        return nullptr;
    }

    if(!detail::isBindable(sourceProperty)) {
        qCWarning(loggingCategory()).noquote().nospace() << "Property '" << sourceProperty.name() << "' in " << *this << " is not bindable";
    }
    if(!setter.setter) {
        auto targetProp = getProperty(target, {setter.name});
        if(!targetProp.isValid() || !targetProp.isWritable()) {
            qCCritical(loggingCategory()).noquote().nospace() << setter << " is not a writable property for " << *target;
            return nullptr;
        }
        if(!QMetaType::canConvert(sourceProperty.metaType(), targetProp.metaType())) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourceProperty.name() << "' of " << *this << " to " << setter << " of " << *target << " with incompatible types";
            return nullptr;
        }
        setter = detail::propertySetter(targetProp);
    }
    if(!m_context->registerBoundProperty(target, setter.name)) {
        qCCritical(loggingCategory()).noquote().nospace() << setter << " has already been bound to " << *target;
        return nullptr;
    }

    auto subscription = new PropertyInjector{QList<registration_handle_t>{target}, sourceProperty, setter, loggingCategory(), target};
    qCInfo(loggingCategory()).noquote().nospace() << "Created Subscription for binding property '" << sourceProperty.name() << "' of " << *this << " to " << setter << " of " << *target;
    return subscribe(subscription);
}






StandardApplicationContext::DescriptorRegistration::DescriptorRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent) :
    detail::ServiceRegistration(parent),
    m_descriptor{desc},
    m_name(name),
    m_index(index),
    m_context(context),
    m_base(base),
    m_condition{Condition::always()},
    m_resolvedPlaceholders{initPlaceholders(config.properties)},
    m_config{config},
    m_beanRefsCache{determineBeanRefs(config.properties)}
{
}


bool StandardApplicationContext::DescriptorRegistration::isActiveInProfile() const
{
    return m_condition.matches(m_context);
}




StandardApplicationContext::ServiceRegistrationImpl::ServiceRegistrationImpl(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent) :
    DescriptorRegistration{base, index, name, desc, config, context, parent},
    theService(nullptr),
    m_state(STATE_INIT)
{
}



void StandardApplicationContext::ServiceRegistrationImpl::print(QDebug out) const {
    out.nospace().noquote() << "Service '" << registeredName() << "' with " << this->descriptor() << ' ' << m_condition;
}

void StandardApplicationContext::ServiceRegistrationImpl::serviceDestroyed(QObject *srv) {
    if(srv == theService) {
        if(auto parentReg = dynamic_cast<service_registration_handle_t>(parent()); parentReg && parentReg -> scope() == ServiceScope::PROTOTYPE) {
            qCInfo(loggingCategory()).noquote().nospace() << "Instance of Prototype " << *this << " has been destroyed";
        } else {
            //Somebody has destroyed a Service that is managed by this ApplicationContext.
            //All we can do is log an error and set theService to nullptr.
            //Yet, it might still be in use somewhere as a dependency.
            qCCritical(loggingCategory()).noquote().nospace() << *this << " has been destroyed externally";
        }
        theService = nullptr;
        m_state = STATE_INIT;
    }
}

void StandardApplicationContext::ObjectRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Object '" << registeredName() << "' with " << this->descriptor();
}





bool StandardApplicationContext::ServiceRegistrationImpl::prepareService(const QVariantList &dependencies, descriptor_list &created)
{
    switch(state()) {
        case STATE_INIT:
        if(!theService) {
                descriptor_list createdForThis;
                theService = descriptor().create(resolveDependencies(dependencies, createdForThis));
                //If any instances of prototypes have been created while resolving dependencies, make them children of the newly created service:
                for(auto child : createdForThis) {
                    setParentIfNotSet(child->getObject(), theService);
                }
                created.insert(created.end(), createdForThis.begin(), createdForThis.end());
            if(theService) {
                onDestroyed = connect(theService, &QObject::destroyed, this, &ServiceRegistrationImpl::serviceDestroyed);
                if(provideConfig()) {
                    m_state = STATE_PUBLISHED;
                    emit objectPublished(theService);
                } else {
                    m_state = STATE_NEEDS_CONFIGURATION;
                }
            }
        }
    }
    return true;
}

int StandardApplicationContext::ServiceRegistrationImpl::unpublish() {
    if(theService) {
        std::unique_ptr<QObject> srv{theService};
        QObject::disconnect(onDestroyed);
        if(theService->parent() != applicationContext()) {
            //Do not delete service if it has an external parent!
            srv.release();
        }
        theService = nullptr;
        m_state = STATE_INIT;
        return 1;
    }
    return 0;
}

StandardApplicationContext::ServiceTemplateRegistration::ServiceTemplateRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent) :
    DescriptorRegistration{base, index, name, desc, config, context, parent} {
    proxySubscription = new ProxySubscription{this, true};
}



subscription_handle_t StandardApplicationContext::ServiceTemplateRegistration::createBindingTo(const detail::source_property_descriptor&, registration_handle_t, const detail::property_descriptor&)
{
    qCCritical(loggingCategory()).noquote().nospace() << "Cannot create binding from " << *this;
    return nullptr;
}




bool StandardApplicationContext::ServiceTemplateRegistration::prepareService(const QVariantList&, descriptor_list&) {
    return false;
}

void StandardApplicationContext::ServiceTemplateRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Service-template '" << registeredName() << "' of type " << detail::type_name(descriptor().impl_type);
}

void StandardApplicationContext::ServiceTemplateRegistration::onSubscription(subscription_handle_t subscription) {
    detail::connect(this, subscription);
    TemporarySubscriptionProxy tempProxy{subscription};
    //By subscribing to a TemporarySubscriptionProxy, we force existing objects to be signalled immediately, while not creating any new Connections:
    for(auto reg : derivedServices) {
        reg->subscribe(&tempProxy);
    }
}


StandardApplicationContext::PrototypeRegistration::PrototypeRegistration(DescriptorRegistration* base, unsigned index, const QString &name, const service_descriptor &desc, const service_config &config, StandardApplicationContext *parent) :
    DescriptorRegistration{base, index, name, desc, config, parent}
{
    proxySubscription = new ProxySubscription{this, true};
}


int StandardApplicationContext::PrototypeRegistration::unpublish()
{
    return 0;
}


bool StandardApplicationContext::PrototypeRegistration::prepareService(const QVariantList& dependencies, descriptor_list&) {
        //Store dependencies for deferred creation of service-instances:
        m_dependencies = dependencies;
        return true;
}

QObjectList StandardApplicationContext::PrototypeRegistration::obtainServices(descriptor_list& created) {
        std::unique_ptr<DescriptorRegistration> instanceReg{ new ServiceRegistrationImpl{base(), ++m_context->nextIndex, registeredName(), descriptor(), config(), m_context, this}};
        if(!instanceReg->prepareService(m_dependencies, created)) {
            qCCritical(loggingCategory()).noquote().nospace() << "Could not create instancef of " << *this;
            return QObjectList{};
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Created instance of " << *this;

        instanceReg->subscribe(proxySubscription);
        created.push_back(instanceReg.get());
        return instanceReg.release()->obtainServices(created);
}

void StandardApplicationContext::PrototypeRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Prototype '" << registeredName() << "' with " << this->descriptor();
}

subscription_handle_t StandardApplicationContext::PrototypeRegistration::createBindingTo(const detail::source_property_descriptor&, registration_handle_t, const detail::property_descriptor&)
{
    qCCritical(loggingCategory()).noquote().nospace() << "Cannot create binding from " << *this;
    return nullptr;
}


void StandardApplicationContext::PrototypeRegistration::onSubscription(subscription_handle_t subscription) {
    detail::connect(this, subscription);
    TemporarySubscriptionProxy tempProxy{subscription};
    //By subscribing to a TemporarySubscriptionProxy, we force existing objects to be signalled immediately, while not creating any new Connections:
    for(auto child : children()) {
        if(auto reg = dynamic_cast<DescriptorRegistration*>(child)) {
            reg->subscribe(&tempProxy);
        }
    }
}

class StandardApplicationContext::ServiceGroupRegistration : public DescriptorRegistration {
    friend class StandardApplicationContext;
    ServiceGroupRegistration(DescriptorRegistration* base, unsigned index, const QString &name, const service_descriptor &desc, const service_config &config, StandardApplicationContext *parent) :
        DescriptorRegistration{base, index, name, desc, config, parent}
    {
        proxySubscription = new ProxySubscription{this, true};
    }

    virtual ServiceScope scope() const override {
        return ServiceScope::SERVICE_GROUP;
    }


    void notifyPublished() override {
        for(auto reg : serviceRegistrations) {
            reg->notifyPublished();
        }
    }

    virtual int state() const override {
        return m_state;
    }

    virtual QObject* getObject() const override {
        return nullptr;
    }

    virtual void print(QDebug out) const override {
        out.nospace().noquote() << "Service-group [" << serviceRegistrations.size() << "] '" << registeredName() << "' with " << this->descriptor() << ' ' << m_condition;
    }

    virtual subscription_handle_t createBindingTo(const detail::source_property_descriptor&, registration_handle_t, const detail::property_descriptor&) override
    {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot create binding from " << *this;
        return nullptr;
    }

    virtual bool prepareService(const QVariantList& dependencies, descriptor_list& created) override {
        const QString namePattern{"%1:%2"};
        QStringList services = splitList(m_context->resolveConfigValue(m_resolvedPlaceholders[m_config.serviceGroupPlaceholder].toString(), m_config.group, m_resolvedPlaceholders).toString());
        if(services.empty()) {
            qCWarning(loggingCategory()).nospace().noquote() << "Expression for Service-group placeholder '" << m_config.serviceGroupPlaceholder << "' resolved to an empty List";
        } else {
            for(const QString& service : services) {
                std::unique_ptr<DescriptorRegistration> instanceReg{ new ServiceRegistrationImpl{base(), ++m_context->nextIndex, namePattern.arg(registeredName()).arg(service), descriptor(), config(), m_context, this}};
                instanceReg->resolvedPlaceholders()[m_config.serviceGroupPlaceholder] = service;
                if(!instanceReg->prepareService(dependencies, created)) {
                    qCCritical(loggingCategory()).noquote().nospace() << "Could not create instancef of " << *this;
                    return false;
                }
                qCInfo(loggingCategory()).noquote().nospace() << "Created instance for " << *this << ": " << *instanceReg;

                instanceReg->subscribe(proxySubscription);
                created.push_back(instanceReg.get());
                serviceRegistrations.push_back(instanceReg.release());
            }
        }
        m_state = STATE_PUBLISHED;
        return true;
    }

    virtual void onSubscription(subscription_handle_t subscription) override {
        detail::connect(this, subscription);
        TemporarySubscriptionProxy tempProxy{subscription};
        //By subscribing to a TemporarySubscriptionProxy, we force existing objects to be signalled immediately, while not creating any new Connections:
        for(auto reg : serviceRegistrations) {
            reg->subscribe(&tempProxy);
        }
    }


    virtual int unpublish() override {
        return std::transform_reduce(serviceRegistrations.begin(), serviceRegistrations.end(), 0, std::plus<int>{}, std::mem_fn(&DescriptorRegistration::unpublish));
    }

    virtual QObjectList obtainServices(descriptor_list& created) override {
        QObjectList result;
        for(auto reg : serviceRegistrations) {
            result.append(reg->obtainServices(created));
        }
        return result;
    }

    subscription_handle_t proxySubscription;
    descriptor_list serviceRegistrations;
    int m_state = STATE_INIT;
};


void registerAppInGlobalContext() {
    auto globalContext = QApplicationContext::instance();
    if(globalContext && !globalContext->getRegistration("application")) {
        globalContext->registerObject(QCoreApplication::instance(), "application");
    }
}

Q_COREAPP_STARTUP_FUNCTION(registerAppInGlobalContext)






StandardApplicationContext::StandardApplicationContext(const QLoggingCategory& loggingCategory, QApplicationContext* delegatingContext, QObject* parent) :
    QApplicationContext(parent),
    m_loggingCategory(loggingCategory),
    m_injectedContext(delegatingContext),
    m_activeProfiles{&defaultProfiles()}
{

    if(qEnvironmentVariableIsSet("QTDI_ACTIVE_PROFILES")) {
        auto profiles = splitList(qEnvironmentVariable("QTDI_ACTIVE_PROFILES"));
        m_activeProfiles = new Profiles{profiles.begin(), profiles.end()};
    }

    if(auto app = QCoreApplication::instance()) {
        registerObject(app, "application");
    }


    getRegistration<QSettings>().subscribe(this, &StandardApplicationContext::onSettingsAdded);


    registerObject<QApplicationContext>(delegatingContext, "context");

    setAsGlobalInstance();
}




StandardApplicationContext::~StandardApplicationContext() {
    //Before we un-publish, we unset this instance as the global instance:
    if(unsetInstance(this)) {
        qCInfo(loggingCategory()).noquote().nospace() << "Removed " << this << " as global instance";
    }
    unpublish();
    if(m_activeProfiles != &defaultProfiles()) {
        delete m_activeProfiles;
    }
}





void StandardApplicationContext::unpublish()
{
    descriptor_list published;
    //Unpublish in revers order:
    std::copy_if(registrations.rbegin(), registrations.rend(), std::inserter(published, published.begin()), [](DescriptorRegistration* reg) { return reg->isPublished() && reg->isManaged();});


    qCInfo(loggingCategory()).noquote().nospace() << "Un-publish ApplicationContext with " << published.size() << " managed published Objects";

    DescriptorRegistration* reg = nullptr;
    unsigned unpublished = 0;
    //Do several rounds and delete those services on which no other published Services depend:
    while(!published.empty()) {
        reg = pop_front(published);
    next_published:
        for(auto depend = published.begin(); depend != published.end(); ++depend) {
            auto dep = *depend;
            for(auto& t : dep->descriptor().dependencies) {
                if(reg->matches(t)) {
                    published.erase(depend);
                    published.push_front(reg);
                    reg = dep;
                    goto next_published;
                }
            }
            for(auto& beanRef : reg->getBeanRefs()) {
                if(getActiveRegistrationByName(beanRef) == reg) {
                    published.erase(depend);
                    published.push_front(reg);
                    reg = dep;
                    goto next_published;
                }
            }
        }
        int u = reg->unpublish();
        if(u)        {
            unpublished += u;
            qCInfo(loggingCategory()).nospace().noquote() << "Un-published " << *reg;
        }
    }
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has been un-published. " << unpublished << " Objects have been successfully destroyed.";
    QStringList remainingNames;
    for(auto regist : registrations) {
        if(regist->isPublished() && !regist->isManaged()) {
            remainingNames.push_back(regist->registeredName());
        }
    }
    if(!remainingNames.isEmpty()) {
        qCInfo(loggingCategory()).noquote().nospace() << "Remaining un-managed Objects: " << remainingNames.join(',');
    }
}

StandardApplicationContext::DescriptorRegistration *StandardApplicationContext::getActiveRegistrationByName(const QString &name) const
{
    auto found = registrationsByName.find(name);
    DescriptorRegistration* reg = nullptr;
    if(found != registrationsByName.end()) {
        for(auto candidate : found->second) {
            if(candidate->registeredCondition().matches(const_cast<StandardApplicationContext*>(this))) {
                if(reg) {
                    qCCritical(loggingCategory()).noquote().nospace() << "Ambiguous registrations for name '" << name << "'" << ": " << *reg << " and " << *candidate;
                    return nullptr;
                }
                reg = candidate;
            }
        }
    }
    return reg;
}


std::pair<QVariant,StandardApplicationContext::Status> StandardApplicationContext::resolveDependency(const descriptor_list &published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial)
{
    const std::type_info& type = d.type;

    QList<DescriptorRegistration*> depRegs;
    auto requiredNames = d.expression.split(',', Qt::SkipEmptyParts);
    if(!requiredNames.isEmpty()) {
        for(const auto& name : requiredNames) {
            auto byName = getActiveRegistrationByName(name);
            if(byName && byName->matches(type) && byName->scope() != ServiceScope::TEMPLATE) {
                if(std::find(published.begin(), published.end(), byName) != published.end()) {
                    depRegs.push_back(byName);
                }
            }
        }
    } else {
        for(auto pub : published) {
            if(pub->matches(type) && pub->scope() != ServiceScope::TEMPLATE) {
                depRegs.push_back(pub);
            }
        }
    }

    switch(d.kind) {
    case detail::VALUE_KIND:
        if(!d.value.isValid()) {
            qCCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
            return {d.value, Status::fatal};
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d;
        return {d.value, Status::ok};

    case detail::RESOLVABLE_KIND:
        {
            auto resolver = getResolver(d.expression);
            QVariant resolved;
            if(!resolver) {
                return {resolved, Status::fatal};
            }
            resolved = resolver->resolve(reg->config().group, reg->resolvedPlaceholders());
            if(resolved.isValid()) {
                detail::convertVariant(resolved, d.variantConverter);
                qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << resolved;
                return {resolved, Status::ok};
            }
            if(d.value.isValid()) {
                return {d.value, Status::ok};
            }
            return {resolved, Status::fatal};
        }

    case detail::PARENT_PLACEHOLDER_KIND:
        return {QVariant::fromValue(static_cast<QObject*>(m_injectedContext)), Status::ok};

    case static_cast<int>(DependencyKind::MANDATORY):
        if(depRegs.empty()) {
            if(allowPartial) {
                qCWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fixable};
            } else {
                qCCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fatal};
            }

        }
    case static_cast<int>(DependencyKind::OPTIONAL):
        switch(depRegs.size()) {
        case 0:
            qCInfo(loggingCategory()).noquote().nospace() << "Skipped " << d;
            return {QVariant::fromValue<QObject*>(nullptr), Status::ok};
        case 1:
            qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << depRegs[0];
            return {QVariant::fromValue(depRegs[0]), Status::ok};
        default:
            //Ambiguity is always a non-fixable error:
            qCCritical(loggingCategory()).noquote().nospace() << d << " is ambiguous";
            return {QVariant{}, Status::fatal};
        }
    case static_cast<int>(DependencyKind::N):
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << depRegs.size() << " objects.";
        {
            //Sort the dependencies by their index(), which is the order of registration:
            std::sort(depRegs.begin(), depRegs.end(), [](auto left, auto right) { return left->index() < right->index();});
            return {QVariant::fromValue(QObjectList{depRegs.begin(), depRegs.end()}), Status::ok};
        }
    }

    return {QVariant{}, Status::fatal};
}





detail::ServiceRegistration *StandardApplicationContext::getRegistrationHandle(const QString& name) const
{
    QMutexLocker<QMutex> locker{&mutex};

    DescriptorRegistration* reg = getActiveRegistrationByName(name);
    if(reg) {
        return reg;
    }
    qCWarning(loggingCategory()).noquote().nospace() << "Could not find a Registration for name '" << name << "'";
    return nullptr;
}

/* Creates and returns some QObject with a thread-affinity to the application-thread.
 * If called from the QApplicationContext's thread, it will simply return the result of the supplied func.
 * Otherwise, posts an Event that creates some QObject and waits for it.
   Note: Before invoking this function, the mutex MUST be locked!
*/
QObject* StandardApplicationContext::obtainHandleFromApplicationThread(CreateHandleEvent::create_handle_t func) {
    if(detail::hasCurrentThreadAffinity(this)) {
        return func();
    }
    CreateHandleEvent* event = new CreateHandleEvent{func};
    auto result = event->result(); //Pin member on Stack to prevent asynchronous deletion.
    QCoreApplication::postEvent(this, event);
    QDeadlineTimer timer{1000}; //Wait at most one second.
    while(!timer.hasExpired() && !result->has_value()) {
         m_condition.wait(&mutex, timer);
    }
    if(!result->has_value()) {
        qCCritical(loggingCategory()).noquote().nospace() << "Could not obtain handle from another thread in time";
        return nullptr;
    }
    return result->value();
}


detail::ProxyRegistration *StandardApplicationContext::getRegistrationHandle(const std::type_info &service_type, const QMetaObject* metaObject) const
{
    QMutexLocker<QMutex> locker{&mutex};

    auto found = proxyRegistrationCache.find(service_type);
    if(found != proxyRegistrationCache.end()) {
        return found->second;
    }
    auto context = const_cast<StandardApplicationContext*>(this);
    ProxyRegistrationImpl* proxyReg = dynamic_cast<ProxyRegistrationImpl*>(context->obtainHandleFromApplicationThread([&service_type,metaObject,context] {
        return new ProxyRegistrationImpl{service_type, metaObject, context};
    }));

    if(proxyReg) {
        proxyRegistrationCache.insert({service_type, proxyReg});
    }
    return proxyReg;
}

void StandardApplicationContext::insertByName(const QString &name, DescriptorRegistration *reg)
{
    registrationsByName[name].insert(reg);
}

QSettings* StandardApplicationContext::settingsForProfile(QSettings* settings, const QString& profile) {
    QSettings* forProfile;
    //If the applicationName is empty, we create a new QSettings using the fileName of the existing one:
    if(settings -> applicationName().isEmpty()) {
        QFileInfo info{settings -> fileName()};
        QString withProfile = QDir{info.path()}.filePath(info.completeBaseName()+"-"+profile+"."+info.suffix());
        forProfile = new QSettings{withProfile, settings->format(), m_injectedContext};
    } else {
        forProfile = new QSettings{settings->format(), settings->scope(), settings->organizationName(), settings->applicationName()+"-"+profile, m_injectedContext};
    }
    return forProfile;
}

void StandardApplicationContext::initSettingsForActiveProfiles() {
    if(m_activeProfiles != &defaultProfiles()) {
        Collector<QSettings> collector;
        collector.subscribeAll(registrations);
        for(QSettings* settings : collector.collected) {
            //Only create profile-specific QSettings if this has been explicitly requested:
            if(!settings->value("qtdi/enableProfileSpecificSettings").toBool()) {
                continue;
            }
            for(const QString& profile : *m_activeProfiles) {
                if(profile == "default") {
                    continue; //No need to create an extra QSettings for profile "default"
                }
                QSettings*& forProfile = m_profileSettings[{profile, settings->fileName()}];
                if(!forProfile) {
                    forProfile = settingsForProfile(settings, profile);
                    if(m_SettingsWatcher) {
                        m_SettingsWatcher->add(forProfile);
                    }
                    qCInfo(loggingCategory()).nospace() << "Lookup profile-specific configuration-entries in Settings-path " << forProfile->fileName();
                }
            }
        }
    }
}

bool StandardApplicationContext::canChangeActiveProfiles()
{
    for(auto reg : registrations) {
        if(reg->isPublished() && reg->registeredCondition().hasProfiles()) {
            qCWarning(loggingCategory()).nospace() << "Cannot change active profiles, as a profile-dependent Service has already been published: " << *reg;
            return false;
        }
    }
    if(m_activeProfiles == &defaultProfiles()) {
        m_activeProfiles = new Profiles{};
    }
    return true;
}


bool StandardApplicationContext::registerAlias(service_registration_handle_t reg, const QString &alias)
{
    QMutexLocker<QMutex> locker{&mutex};
    if(!reg) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register alias '" << alias << "' for null";
        return false;
    }
    auto foundIter = std::find(registrations.begin(), registrations.end(), reg);
    if(foundIter == registrations.end()) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register alias '" << alias << "' for " << *reg << ". Not found in ApplicationContext";
        return false;
    }
    auto found = getActiveRegistrationByName(alias);
    if(found && found != reg) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register alias '" << alias << "' for " << *reg << ". Another Service has been registered under this name: " << *found;
        return false;
    }
    insertByName(alias, *foundIter);
    qCInfo(loggingCategory()).noquote().nospace() << "Registered alias '" << alias << "' for " << *reg;
    return true;

}

QMetaProperty StandardApplicationContext::getProperty(registration_handle_t reg, const detail::source_property_descriptor& sourceProperty) {
    if(auto meta = reg->serviceMetaObject()) {
        if(sourceProperty.name.isEmpty()) {
            if(sourceProperty.signalMethod.isValid()) {
                for(int index = 0; index < meta->propertyCount(); ++index) {
                    auto prop = meta->property(index);
                    if(prop.hasNotifySignal() && prop.notifySignal() == sourceProperty.signalMethod) {
                        return prop;
                    }
                }
            }
        } else {
            return meta->property(meta->indexOfProperty(sourceProperty.name));
        }
    }
    return QMetaProperty{};
}







void StandardApplicationContext::contextObjectDestroyed(DescriptorRegistration* objectRegistration)
{
    qCInfo(loggingCategory()).noquote().nospace() << "Object for " << *objectRegistration << " has been destroyed externally";

    for(auto& regs : registrationsByName) {
        for(auto iter = regs.second.begin(); iter != regs.second.end();) {
            if(*iter == objectRegistration) {
                iter = regs.second.erase(iter);
            } else {
                ++iter;
            }
        }
    }


    auto found = std::find(registrations.begin(), registrations.end(), objectRegistration);
    if(found != registrations.end()) {
        registrations.erase(found);
        delete objectRegistration;
    }
}

///
/// \brief Validates this ApplicationContext before publishing.
/// \param allowPartial if true, will succeed even if not all service-dependencies can be resolved.
/// \param published The services that have already been published (used for dependency-resolution).
/// \param unpublished Upon entry: the yet unpublished services, in no particular order.
/// Upon exit: the yet unpublished services, in the correct order for publication.
/// \return Status::Ok if all services can be published. Status::Fixable if some services can be published.
/// Status::Fatal if there are non-fixable errors.<br>
/// If allowPartial == true, the result can only be Status::Ok or Status::Fatal!
///
StandardApplicationContext::Status StandardApplicationContext::validate(bool allowPartial, const descriptor_list& published, descriptor_list& unpublished)
{
    descriptor_list allPublished{published.begin(), published.end()};
    descriptor_list validated; //validated contains the yet-to-be-published services in the correct order. Will be copied back to unpublished upon exit.

    qCDebug(loggingCategory()).noquote().nospace() << "Validating ApplicationContext with " << unpublished.size() << " unpublished Objects";
    DescriptorRegistration* reg = nullptr;
    Status status = Status::ok;
    //Do several rounds and validate those services whose dependencies have already been published.
    //For a service with an empty set of dependencies, this means that it will be validated first.
    for(;;) {
    fetch_next:
        if(unpublished.empty()) {
            break;
        }
        reg = pop_front(unpublished);
    next_unpublished:

        auto& dependencyInfos = reg->descriptor().dependencies;
        for(auto& d : dependencyInfos) {
            //If we find an unpublished dependency, we continue with that:
            auto foundReg = eraseIf(unpublished, DescriptorRegistration::matcher(d));
            if(foundReg) {
                unpublished.push_front(reg); //Put the current Registration back where it came from. Will be processed after the dependency.
                reg = foundReg;
                goto next_unpublished;
            }
        }
        for(auto& beanRef : reg->getBeanRefs()) {
            if(!getActiveRegistrationByName(beanRef)) {
                if(allowPartial) {
                    status = Status::fixable;
                    qCWarning(loggingCategory()).noquote().nospace() << "Cannot resolve reference '" << beanRef << "' from " << *reg;
                    goto fetch_next;
                }
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot resolve reference '" << beanRef << "' from " << *reg;
                return Status::fatal;
            }
        }
        if(!dependencyInfos.empty()) {
            QObject temporaryParent;
            qCInfo(loggingCategory()).noquote().nospace() << "Resolving " << dependencyInfos.size() << " dependencies of " << *reg << ":";
            for(auto& d : dependencyInfos) {
                auto result = resolveDependency(allPublished, reg, d, allowPartial);
                switch(result.second) {
                case Status::fixable:
                    if(allowPartial) {
                        status = Status::fixable;
                        goto fetch_next;
                    }
                    [[fallthrough]];
                case Status::fatal:
                    return Status::fatal;
                default: break;
                }
            }
        }
        allPublished.push_back(reg);
        validated.push_back(reg);
    }
    // Copy validated yet-to-be-published services back to unpublished, now in the correct order for publication:
    unpublished.insert(unpublished.begin(), validated.begin(), validated.end());
    return status;
}


QVariantList StandardApplicationContext::resolveDependencies(const QVariantList& dependencies, descriptor_list& created) {
    QVariantList result;
    for(auto& arg : dependencies) {
        result.push_back(resolveDependency(arg, created));
    }
    return result;
}

QVariant StandardApplicationContext::resolveDependency(const QVariant &arg, descriptor_list &created)
{
    QObjectList objectList = arg.value<QObjectList>();
    if(!objectList.empty()) {
        QObjectList resultList;
        for(QObject* ptr : objectList) {
            if(auto reg = dynamic_cast<DescriptorRegistration*>(ptr)) {
                resultList.append(reg->obtainServices(created));
            }
        }
        return QVariant::fromValue(resultList);
    }
    if(auto reg = arg.value<DescriptorRegistration*>()) {
        auto instances = reg->obtainServices(created);
        switch(reg->scope()) {
        case ServiceScope::SERVICE_GROUP:
            return QVariant::fromValue(instances);
        default:
            return instances.empty() ? QVariant{} : QVariant::fromValue(instances[0]);
        }
    }
    return arg;
}


bool StandardApplicationContext::publish(bool allowPartial)
{
    if(!detail::hasCurrentThreadAffinity(this)) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot publish ApplicationContext in different thread";
        return false;
    }


    descriptor_list allCreated;
    descriptor_list toBePublished;
    descriptor_list needConfiguration;
    descriptor_list allRegistrations;
    Status validationResult = Status::ok;
    {
        QMutexLocker<QMutex> locker{&mutex};
        allRegistrations = registrations;
    }

    std::unordered_set<QString> distinctNames;
    for(auto reg : allRegistrations) {
        switch (reg->state()) {
        case STATE_INIT:
            if(!reg->isActiveInProfile()) {
                qCInfo(loggingCategory()).noquote().nospace() << "Service " << *reg << " is not active in " << profilesAsString(*m_activeProfiles);
                continue;
            }
            // getActiveRegistrationByName(name) yields nullptr if no registration under the supplied name exists, or if the registration is ambiguous.
            // At this point, however - supplying a valid name - ambiguity must be the cause of the failure:
            if(!getActiveRegistrationByName(reg->registeredName())) {
                return false;
            }

            toBePublished.push_back(reg);
            break;
        case STATE_NEEDS_CONFIGURATION:
            needConfiguration.push_back(reg);
            [[fallthrough]];
        case STATE_PUBLISHED:
            allCreated.push_back(reg);
        }
    }
    if(toBePublished.empty() && needConfiguration.empty()) {
        return true;
    }
    validationResult = validate(allowPartial, allCreated, toBePublished);
    if(validationResult == Status::fatal) {
        return false;
    }

    qCInfo(loggingCategory()).noquote().nospace() << "Publish ApplicationContext " << profilesAsString(*m_activeProfiles) << " with " << toBePublished.size() << " unpublished Objects";

    //Move QSettings to the beginning, so that they will be available for configuration of other services:
    std::stable_sort(needConfiguration.begin(), needConfiguration.end(), [](const DescriptorRegistration* left, const DescriptorRegistration* right) { return left->provideConfig() && !right->provideConfig();});

    //Do several rounds and publish those services whose dependencies have already been published.
    //For a service with an empty set of dependencies, this means that it will be published first.
    while(!toBePublished.empty()) {
        auto reg = pop_front(toBePublished);
        QVariantList dependencies;
        auto& dependencyInfos = reg->descriptor().dependencies;
        if(!dependencyInfos.empty()) {
            qCInfo(loggingCategory()).noquote().nospace() << "Resolving " << dependencyInfos.size() << " dependencies of " << *reg << ":";
            for(auto& d : dependencyInfos) {
                auto result = resolveDependency(allCreated, reg, d, allowPartial);
                dependencies.push_back(result.first);
            }
        }

        if(!reg->prepareService(dependencies, needConfiguration)) {
            qCCritical(loggingCategory()).nospace().noquote() << "Could not create service " << *reg;
            return false;
        }

        switch(reg->state()) {
        case STATE_NEEDS_CONFIGURATION:
            needConfiguration.push_back(reg);
            [[fallthrough]];
        case STATE_PUBLISHED:
            qCInfo(loggingCategory()).nospace().noquote() << "Created service " << *reg;
            [[fallthrough]];
        default:
            allCreated.push_back(reg);
        }
    }


    unsigned managed = std::count_if(allCreated.begin(), allCreated.end(), std::mem_fn(&DescriptorRegistration::isManaged));

    //The services that have been instantiated during this methd-invocation will be configured in the order they have have been
    //instantiated.
    while(!needConfiguration.empty()) {
        auto reg = pop_front(needConfiguration);
        auto configResult = configure(reg, reg->resolvedPlaceholders(), reg->getObject(), needConfiguration, allowPartial);
        switch(configResult) {
        case Status::fatal:
            qCCritical(loggingCategory()).nospace().noquote() << "Could not configure " << *reg;
            return false;
        case Status::fixable:
            qCWarning(loggingCategory()).nospace().noquote() << "Could not configure " << *reg;
            validationResult = Status::fixable;
            continue;

        case Status::ok:
            qCInfo(loggingCategory()).noquote().nospace() << "Configured " << *reg;
            toBePublished.push_back(reg);
        }
    }
    qsizetype publishedCount = 0;
    QList<QApplicationContextPostProcessor*> postProcessors;
    for(auto reg : allCreated) {
        if(auto processor = dynamic_cast<QApplicationContextPostProcessor*>(reg->getObject())) {
            postProcessors.push_back(processor);
            qCInfo(loggingCategory()).noquote().nospace() << "Detected PostProcessor " << *reg;
        }
    }

    //Move PostProcessors to the front, so that they will be initialized before they process other Services:
    for(unsigned moved = 0, pos = 1; pos < toBePublished.size(); ++pos) {
        if(dynamic_cast<QApplicationContextPostProcessor*>(toBePublished[pos]->getObject())) {
            std::swap(toBePublished[moved++], toBePublished[pos]);
        }
    }
    while(!toBePublished.empty()) {
        auto reg = toBePublished.front();
        QObject* target = reg->getObject();
        if(!target) {
            qCCritical(loggingCategory()).nospace().noquote() << "Could not initialize " << *reg;
            return false;
        }
        bool initialized = init(reg, ServiceInitializationPolicy::DEFAULT);
        runPostProcessors(reg, postProcessors);
        reg->notifyPublished();
        if(!initialized) {
            init(reg, ServiceInitializationPolicy::AFTER_PUBLICATION);
        }
        //If the service has no parent, make it a child of this ApplicationContext.
        //Note: It will be deleted in StandardApplicationContext's destructor explicitly, to maintain the correct order of dependencies!
        setParentIfNotSet(target, m_injectedContext);
        toBePublished.pop_front();
        ++publishedCount;
        qCInfo(loggingCategory()).noquote().nospace() << "Published " << *reg;
    }
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has published " << publishedCount << " objects";
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has a total number of " << allCreated.size() << " published objects of which " << managed << " are managed.";
    if(!toBePublished.empty()) {
        qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has " << toBePublished.size() << " unpublished objects";
    }

    if(publishedCount) {
        emit publishedChanged();
        emit pendingPublicationChanged();
    }
    return validationResult == Status::ok;
}

unsigned StandardApplicationContext::published() const
{
    QMutexLocker<QMutex> locker{&mutex};
    return std::count_if(registrations.begin(), registrations.end(), std::mem_fn(&DescriptorRegistration::isPublished));
}

unsigned int StandardApplicationContext::pendingPublication() const
{
    QMutexLocker<QMutex> locker{&mutex};
    return std::count_if(registrations.begin(), registrations.end(), [](DescriptorRegistration* reg) { return !reg->isPublished() && reg->isActiveInProfile(); });
}

QList<service_registration_handle_t> StandardApplicationContext::getRegistrationHandles() const
{
    QMutexLocker<QMutex> locker{&mutex};

    QList<service_registration_handle_t> result;
    std::copy(registrations.begin(), registrations.end(), std::back_inserter(result));
    return result;
}



service_registration_handle_t StandardApplicationContext::registerServiceHandle(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, const Condition& condition, QObject* baseObj)
{
    if(!detail::hasCurrentThreadAffinity(this)) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register service in different thread";
        return nullptr;
    }
    std::vector<ProxyRegistrationImpl*> matchingProxies;
    DescriptorRegistration* reg;
    {
        QMutexLocker<QMutex> locker{&mutex};
        QString objName = name;

        ServiceTemplateRegistration* base = nullptr;
        switch(scope) {
        case ServiceScope::EXTERNAL:
            if(!baseObj) {
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register null-object for " << descriptor;
                return nullptr;
            }
            if(!condition.isAlways()) {
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot specify" << condition << " for external object " << descriptor;
                return nullptr;
            }
            if(objName.isEmpty()) {
                objName = baseObj->objectName();
            }
            if(!objName.isEmpty()) {
                reg = getActiveRegistrationByName(objName);
                //If we have a registration under the same name, we'll return it only if it's for the same object and it has the same descriptor:
                if(reg) {
                    if(reg->getObject() == baseObj && descriptor == reg->descriptor()) {
                        return reg;
                    }
                    //Otherwise, we have a conflicting registration
                    qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << baseObj << " as '" << objName << "'. Has already been registered as " << *reg;
                    return nullptr;
                }
            }
            //For object-registrations, even if we supply an explicit name, we still have to loop over all registrations,
            //as we need to check whether the same object has been registered before.
            for(auto regist : registrations) {
                if(baseObj == regist->getObject()) {
                    //An identical anonymous registration is allowed:
                    if(descriptor == regist->descriptor() && objName.isEmpty()) {
                        return regist;
                    }
                    //Otherwise, we have a conflicting registration
                    qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << baseObj << " as '" << objName << "'. Has already been registered as " << *regist;
                    return nullptr;
                }
            }
            if(objName.isEmpty()) {
                objName = makeName(*descriptor.service_types.begin());
            }
            reg = new ObjectRegistration{++nextIndex, objName, descriptor, baseObj, this};

            break;

        case ServiceScope::SERVICE_GROUP:
            if(config.serviceGroupPlaceholder.isEmpty() || !config.properties[config.serviceGroupPlaceholder].expression.isValid()) {
                qCCritical(loggingCategory()).nospace().noquote() <<  "Cannot register Service-group for " << descriptor << " with no service-group-expression";
                return nullptr;
            }
        case ServiceScope::SINGLETON:
        case ServiceScope::PROTOTYPE:
            {
                std::unordered_set<dependency_info> dependencies{};

                if(!findTransitiveDependenciesOf(descriptor, dependencies)) {
                    qCCritical(loggingCategory()).nospace().noquote() <<  "Cannot register " << descriptor << ". Found invalid dependency";
                    return nullptr;
                }

                if(!checkTransitiveDependentsOn(descriptor, name, dependencies)) {
                    qCCritical(loggingCategory()).nospace().noquote() <<  "Cannot register '" << name << "'. Cyclic dependency in dependency-chain of " << descriptor;
                    return nullptr;

                }
            }
            [[fallthrough]];

        case ServiceScope::TEMPLATE:
            if(!name.isEmpty()) {
                auto found = registrationsByName.find(objName);
                if(found != registrationsByName.end()) {
                    for(auto regForName : found->second) {
                        reg = regForName;
                        //With isManaged() we test whether reg is also a ServiceRegistration (no ObjectRegistration)
                        //If we have a registration under the same name, we'll return it only if it has the same descriptor, config and profiles:
                        if(reg->isManaged()) {
                            if(descriptor == reg->descriptor() && reg->config() == config && reg->m_condition == condition) {
                                return reg;
                            }
                        }
                        if(reg->m_condition.overlaps(condition)) {
                            //Otherwise, we have a conflicting registration
                            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Service " << descriptor << " as '" << name << "'. Has already been registered as " << *reg;
                            return nullptr;
                        }
                        //If the conditions do not overlap, we will create a new registration. We assume that only one of the registrations will be active at a time.
                    }
                }
            } else {
                //For an anonymous registration, we have to loop over all registrations:
                for(auto regist : registrations) {
                    //With isManaged() we test whether reg is also a ServiceRegistration (no ObjectRegistration)
                    if(regist->isManaged() && regist->config() == config) {
                        switch(detail::match(descriptor, regist->descriptor())) {
                        case detail::DESCRIPTOR_IDENTICAL:
                            if(regist->m_condition == condition) {
                                return regist;
                            }
                            [[fallthrough]];
                        case detail::DESCRIPTOR_INTERSECTS:
                            if(!regist->m_condition.overlaps(condition)) {
                                continue;
                            }
                            //Otherwise, we have a conflicting registration
                            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Service " << descriptor << ". Has already been registered as " << *regist;
                            return nullptr;
                        default:
                            continue;
                        }
                    }
                }
                objName = makeName(*descriptor.service_types.begin());
            }

            if(auto baseRegistration = dynamic_cast<service_registration_handle_t>(baseObj)) {
                if(baseRegistration->scope() != ServiceScope::TEMPLATE) {
                    qCCritical(loggingCategory()).noquote().nospace() << "Template-Registration " << *baseRegistration << " must have scope TEMPLATE, but has scope " << baseRegistration->scope();
                    return nullptr;

                }
                if(baseRegistration->applicationContext() != this) {
                    qCCritical(loggingCategory()).noquote().nospace() << "Template-Registration " << *baseRegistration << " not registered in this ApplicationContext";
                    return nullptr;
                }
                if(descriptor.meta_object && baseRegistration->descriptor().meta_object) {
                    if(!descriptor.meta_object->inherits(baseRegistration->descriptor().meta_object)) {
                        qCCritical(loggingCategory()).noquote().nospace() << "Registration " << descriptor << " does not inherit Base-Registration " << *baseRegistration;
                        return nullptr;
                    }
                }
                base = dynamic_cast<ServiceTemplateRegistration*>(baseRegistration);
            }

            if(descriptor.meta_object && scope != ServiceScope::TEMPLATE) {
                const service_config::map_type* props = &config.properties;
                for(DescriptorRegistration* handle = base;;handle = handle->base() ){
                    for(const auto& entry : props->asKeyValueRange()) {
                        if(entry.second.configType != detail::ConfigValueType::PRIVATE && !entry.second.propertySetter && descriptor.meta_object->indexOfProperty(entry.first.toLatin1()) < 0) {
                            qCCritical(loggingCategory()).nospace().noquote() << "Cannot register " << descriptor << " as '" << name << "'. Service-type has no property '" << entry.first << "'";
                            return nullptr;
                        }
                    }
                    if(!handle) {
                        break;
                    }
                    props = &handle->config().properties;
                }
            }

            if(!validateResolvers(descriptor, config)) {
                return nullptr;
            }
            switch(scope) {
            case ServiceScope::PROTOTYPE:
                reg = new PrototypeRegistration{base, ++nextIndex, objName, descriptor, config, this};
                break;
            case ServiceScope::SINGLETON:
                reg = new ServiceRegistrationImpl{base, ++nextIndex, objName, descriptor, config, this};
                break;
            case ServiceScope::TEMPLATE:
                reg = new ServiceTemplateRegistration{base, ++nextIndex, objName, descriptor, config, this};
                break;
            case ServiceScope::SERVICE_GROUP:
                reg = new ServiceGroupRegistration{base, ++nextIndex, objName, descriptor, config, this};
                break;
            default:
                reg = nullptr;
                break;
            }

            if(base) {
                base->add(reg);
            }

            break;
        default:
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register " << descriptor << "with scope " << scope;
            return nullptr;
        }

        reg->m_condition = condition;
        insertByName(objName, reg);

        registrations.push_back(reg);
        for(auto& entry : proxyRegistrationCache) {
            if(entry.second->canAdd(reg)) {
                matchingProxies.push_back(entry.second);
            }
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Registered " << *reg;
    }

    // Emit signal(s) after mutex has been released:

    for(auto proxy : matchingProxies) {
        proxy->add(reg);
    }

    emit pendingPublicationChanged();
    return reg;
}






bool StandardApplicationContext::findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<dependency_info>& result) const
{
    for(auto& t : descriptor.dependencies) {
        if(!t.isValid()) {
            return false;
        }
        for(auto reg : registrations) {
            if(reg->matches(t)) {
                result.insert(t);
                if(!findTransitiveDependenciesOf(reg->descriptor(), result)) {
                    return false;
                }
            }
        }
    }
    return true;
}



bool StandardApplicationContext::checkTransitiveDependentsOn(const service_descriptor& descriptor, const QString& name, const std::unordered_set<dependency_info>& dependencies) const
{
    for(auto reg : registrations) {
        for(auto& t : reg->descriptor().dependencies) {
            if(descriptor.matches(t.type) && (!t.has_required_name() || t.expression == name))  {
                if(std::find_if(dependencies.begin(), dependencies.end(), [reg](auto dep) { return reg->matches(dep);}) != dependencies.end()) {
                   return false;
                }
                if(!checkTransitiveDependentsOn(reg->descriptor(), reg->registeredName(), dependencies)) {
                    return false;
                }
            }
        }
    }
    return true;
}


std::pair<StandardApplicationContext::Status,bool> StandardApplicationContext::resolveBeanRef(QVariant &value, descriptor_list& toBePublished, bool allowPartial)
{
    if(!value.isValid()) {
        return {Status::fatal, false};
    }
    QString key = value.toString();
    if(getBeanRef(key)) {
        auto beanReg = getActiveRegistrationByName(key);
        if(!beanReg) {
            if(allowPartial) {
                qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
                return { Status::fixable, false};
            }
            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
            return {Status::fatal, false};
        }
        QObjectList beans = beanReg->obtainServices(toBePublished);
        switch(beans.size()) {
        case 1:
            qCInfo(loggingCategory()).nospace().noquote() << "Resolved reference '" << key << "' to " << beans[0];
            value.setValue(beans[0]);
            return {Status::ok, true};
        case 0:
            qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
            return { Status::fixable, false};
        default:
            qCritical(loggingCategory()).nospace().noquote() << "Reference '" << key << "' references multiple services";
            return { Status::fatal, false};
        }
    }
    return {Status::ok, false};

}



StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::findAutowiringCandidate(service_registration_handle_t target, const QMetaProperty& prop) {
    auto propType = prop.metaType();
    if(!(propType.flags() & QMetaType::PointerToQObject)) {
        return nullptr;
    }
    auto propMetaType = propType.metaObject();
    DescriptorRegistration* candidate = getActiveRegistrationByName(prop.name()); //First, try by name
    //If the candidate is assignable to the property, return it, unless it is the target. (We never autowire a property with a pointer to the same service)
    if(candidate && candidate != target && candidate -> getObject() && candidate->getObject()->metaObject()->inherits(propMetaType)) {
        return candidate;
    }
    candidate = nullptr;
    //No matching name found, now we iterate over all registrations:
    for(auto regist : registrations) {
        if(!regist->isActiveInProfile()) {
            continue;
        }
        if(regist == target) {
            //We never autowire a property with a pointer to the same service
            continue;
        }
        auto obj = regist->getObject();
        if(obj && obj->metaObject()->inherits(propMetaType)) {
            if(candidate) {
                return nullptr; //Ambiguous candidate => return immediately
            }
            candidate = regist;
        }
    }
    return candidate;
}

bool StandardApplicationContext::registerBoundProperty(registration_handle_t target, const char *propName)
{
    return m_boundProperties[target].insert(propName).second;
}


StandardApplicationContext::Status StandardApplicationContext::configure(DescriptorRegistration* reg, QVariantMap& resolvedPlaceholders, QObject* target, descriptor_list& toBePublished, bool allowPartial) {
    if(!target) {
        return Status::fatal;
    }
    if(target->objectName().isEmpty()) {
        target->setObjectName(reg->registeredName());
    }

    const service_config& config = reg->config();

    auto metaObject = target->metaObject();
    if(metaObject) {
        std::unordered_set<QString> usedProperties;
        descriptor_list createdForThis;
        for(auto[key,cv] : config.properties.asKeyValueRange()) {
            QVariant resolvedValue = cv.expression;
            detail::PlaceholderResolver* resolver = nullptr;
            bool isAutoRefreshProperty = config.autoRefresh; // If config.autoRefresh is false, we might still find a detail::ConfigValue below
            switch(cv.configType) {
            case detail::ConfigValueType::SERVICE:
                if(auto srvReg = dynamic_cast<DescriptorRegistration*>(cv.expression.value<service_registration_handle_t>())) {
                    auto instances = srvReg->obtainServices(toBePublished);
                    switch(srvReg->scope()) {
                    case ServiceScope::SERVICE_GROUP:
                        resolvedValue.setValue(instances);
                        break;
                    default:
                        if(instances.size() != 1) {
                            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << key << "' of " << *reg;
                            return Status::fatal;
                        }
                        resolvedValue.setValue(instances[0]);
                    }
                }
                if(auto proxyReg = dynamic_cast<ProxyRegistrationImpl*>(cv.expression.value<proxy_registration_handle_t>())) {
                    resolvedValue.setValue(proxyReg->obtainServices(toBePublished));
                }
                break;

            case detail::ConfigValueType::AUTO_REFRESH_EXPRESSION:
                isAutoRefreshProperty = true;
                [[fallthrough]];
            case detail::ConfigValueType::DEFAULT:
                {
                    auto[status, resolved] = resolveBeanRef(resolvedValue, createdForThis, allowPartial);
                    if(status != Status::ok) {
                        return status;
                    }
                    if(!resolved) {
                        if(cv.expression.typeId() == QMetaType::QString) {
                            resolver = getResolver(cv.expression.toString());
                            if(!resolver) {
                                return Status::fatal;
                            }
                            // We only need to watch this property if it does contain placeholders:
                            isAutoRefreshProperty = isAutoRefreshProperty && resolver && resolver->hasPlaceholders();
                            resolvedValue = resolver->resolve(config.group, resolvedPlaceholders);
                            if(resolvedValue.isValid()) {
                                detail::convertVariant(resolvedValue, cv.variantConverter);
                            }
                        } else {
                            resolvedValue = cv.expression;
                        }
                    }
                }
                break;
            default:
                continue;
            }
            if(!resolvedValue.isValid()) {
                return Status::fatal;
            }
            detail::property_descriptor propertyDescriptor;
            if(cv.propertySetter) {
                cv.propertySetter(target, resolvedValue);
                propertyDescriptor.setter = cv.propertySetter;
                propertyDescriptor.name = key.toLatin1();
            } else {
                auto targetProperty = metaObject->property(metaObject->indexOfProperty(key.toLatin1()));
                if(!targetProperty.isValid() || !targetProperty.isWritable()) {
                    //Refering to a non-existing Q_PROPERTY by name is always non-fixable:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not find writable property " << key << " of '" << metaObject->className() << "'";
                    return Status::fatal;
                }
                if(!targetProperty.write(target, resolvedValue)) {
                    //An error while setting a Q_PROPERTY is always non-fixable:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    return Status::fatal;
                }
                propertyDescriptor = detail::propertySetter(targetProperty);
            }
            qCDebug(loggingCategory()).nospace().noquote() << "Set property '" << key << "' of " << *reg << " to value " << resolvedValue;
            usedProperties.insert(key);

            if(isAutoRefreshProperty && resolver) {
                if(autoRefreshEnabled()) {
                    m_SettingsWatcher->addWatchedProperty(resolver, cv.variantConverter, propertyDescriptor, target, config.group, resolvedPlaceholders);
                } else {
                    qCWarning(loggingCategory()).nospace() << "Cannot watch property '" << key << "' of " << target << ", as auto-refresh has not been enabled.";
                }
            }

        }
        //If any instances of prototypes have been created while configuring the properties, make them children of the target:
        for(auto child : createdForThis) {
            setParentIfNotSet(child->getObject(), target);
        }
        toBePublished.insert(toBePublished.end(), createdForThis.begin(), createdForThis.end());
        if(config.autowire) {
            QString group = config.group;
            if(group.isEmpty()) {
                group = reg->registeredName();
            }
            QStringList keys = configurationKeys(group);

            for(int p = 0; p < metaObject->propertyCount(); ++p) {
                auto prop = metaObject->property(p);
                if(usedProperties.find(prop.name()) != usedProperties.end()) {
                    qCDebug(loggingCategory()).nospace() << "Skip Autowiring property '" << prop.name() << "' of " << *reg << " because it has been explicitly set";
                    continue; //Already set this property explicitly
                }
                QVariant propValue;
                if(QString path = detail::makeConfigPath(group, prop.name()); keys.contains(path)) {
                    propValue = getConfigurationValue(path);
                    if(QString beanRef = propValue.toString(); getBeanRef(beanRef)) {
                        if(auto candidate = getActiveRegistrationByName(beanRef)) {
                            propValue.setValue(candidate->getObject());
                        }
                    }
                } else
                if(DescriptorRegistration* candidate = findAutowiringCandidate(reg, prop)) {
                    propValue.setValue(candidate->getObject());
                }
                if(!propValue.isValid()) {
                    qCInfo(loggingCategory()).nospace().noquote() << "Could not autowire property '" << prop.name()  << "' of " << *reg;
                    continue;
                }

                if(prop.write(target, propValue)) {
                    qCInfo(loggingCategory()).nospace() << "Autowired property '" << prop.name() << "' of " << *reg << " to " << propValue;
                    usedProperties.insert(prop.name());
                } else {
                    qCWarning(loggingCategory()).nospace().noquote() << "Autowiring property '" << prop.name()  << "' of " << *reg << " to " << propValue << " failed.";
                }

            }
        }

    }
    if(reg->base()) {
        QVariantMap resolvedBasePlaceholder{resolvedPlaceholders};
        resolvedBasePlaceholder.insert(reg->base()->resolvedPlaceholders());
        auto baseStatus = configure(reg->base(), resolvedBasePlaceholder, target, toBePublished, allowPartial);
        if(baseStatus != Status::ok) {
            return baseStatus;
        }
    }
    return Status::ok;
}



bool StandardApplicationContext::init(DescriptorRegistration* reg, ServiceInitializationPolicy policy) {
    QObject* target = reg->getObject();

    for(DescriptorRegistration* self = reg; self; self = self->base()) {
        if(auto initMethod = self->descriptor().init_method; initMethod && self->descriptor().initialization_policy == policy) {
            initMethod(target, m_injectedContext);
            qCInfo(loggingCategory()).nospace().noquote() << "Invoked init-method of " << *reg;
            return true;
       }
    }
    return false;
}

void StandardApplicationContext::runPostProcessors(DescriptorRegistration* reg, const QList<QApplicationContextPostProcessor*>& postProcessors) {
    QObject* target = reg->getObject();

    for(auto processor : postProcessors) {
        if(processor != dynamic_cast<QApplicationContextPostProcessor*>(target)) {
            //Don't process yourself!
            processor->process(reg, target, reg->resolvedPlaceholders());
        }
    }
}


bool StandardApplicationContext::validateResolvers(const service_descriptor& descriptor, const service_config& config) {
    for(auto[key,cv] : config.properties.asKeyValueRange()) {
        if(!cv.expression.isValid()) {
            return false;
        }
        bool isAutoRefreshProperty = config.autoRefresh;
        QString asString;
        switch(cv.configType) {
        case detail::ConfigValueType::SERVICE:
            if(!cv.expression.value<registration_handle_t>()) {
                qCritical(loggingCategory()).nospace().noquote() << "Invalid value for property '" << key << "'";
                return false;
            }
            continue;
        case detail::ConfigValueType::AUTO_REFRESH_EXPRESSION:
            isAutoRefreshProperty = true;
            [[fallthrough]];
        default:
            if(cv.expression.typeId() != QMetaType::QString) {
                continue;
            }
            asString = cv.expression.toString();
        }
        detail::PlaceholderResolver* configResolver = getResolver(asString);
        if(!configResolver) {
            return false;
        }
        if(isAutoRefreshProperty && !configResolver->hasPlaceholders()) {
            qCInfo(loggingCategory()).noquote().nospace() << "Property '" << key << "' of " << descriptor << " will not be watched, as expression '" << asString << "' contains no placeholders";
        }
    }
    return true;
}




detail::PlaceholderResolver *StandardApplicationContext::getResolver(const QString& placeholderText)
{
    auto& configResolver = resolverCache[placeholderText];
    if(!configResolver) {
        configResolver = detail::PlaceholderResolver::parse(placeholderText, m_injectedContext);
    }
    return configResolver.get();
}

QStringList StandardApplicationContext::configurationKeys(const QString &section) const
{
    Collector<QSettings> collector;
    collector.subscribeAll(registrations);

    QStringList orderedKeys;
    QSet<QString> keySet;
    QString prefix = detail::makeConfigPath(section, "");
    for(QSettings* settings : collector.collected) {
        for(const QString& key : settings->allKeys()) {
            if(key.startsWith(prefix) && !keySet.contains(key))             {
                keySet.insert(key);
                orderedKeys.push_back(key);
            }
        }
    }
    return orderedKeys;
}


QVariant StandardApplicationContext::getConfigurationValue(const QString& key, bool searchParentSections) const {
    if(auto bytes = QString{key}.replace('/', '.').toLocal8Bit(); qEnvironmentVariableIsSet(bytes)) {
        auto value = qEnvironmentVariable(bytes);
        qCDebug(loggingCategory()).noquote().nospace() << "Obtained configuration-entry: " << bytes << " = '" << value << "' from enviroment";
        return value;
    }

    Collector<QSettings> collector;
    //The profile-specific QSettings shall be searched before the others:
    for(auto& entry : m_profileSettings) {
        collector.collected.push_back(entry.second);
    }

    collector.subscribeAll(registrations);

    QString searchKey = key;
    do {
        for(QSettings* settings : collector.collected) {
            auto value = settings->value(searchKey);
            if(value.isValid()) {
                qCDebug(loggingCategory()).noquote().nospace() << "Obtained configuration-entry: " << searchKey << " = " << value << " from " << settings->fileName();
                return value;
            }
        }
    } while(searchParentSections && detail::removeLastConfigPath(searchKey));

    qCDebug(loggingCategory()).noquote().nospace() << "No value found for configuration-entry: " << key;
    return QVariant{};
}


const QLoggingCategory &StandardApplicationContext::loggingCategory() const
{
    return m_loggingCategory;
}




bool StandardApplicationContext::event(QEvent *event)
{
    if(event->type() == CreateHandleEvent::eventId()) {
        auto createEvent = static_cast<CreateHandleEvent*>(event);
        QMutexLocker<QMutex> locker{&mutex};
        createEvent->createHandle();
        m_condition.notify_all();
        return true;
    }
    return QObject::event(event);

}

void StandardApplicationContext::onSettingsAdded(QSettings * settings)
{
    auto profilesSetting = settings->value("qtdi/activeProfiles");
    QStringList profiles;
    if(profilesSetting.typeId() == QMetaType::QStringList) {
        profiles = profilesSetting.toStringList();
    } else {
        profiles = splitList(profilesSetting.toString());
    }
    if(!m_SettingsWatcher) {
        bool enabled = settings->value("qtdi/enableAutoRefresh").toBool();
        if(enabled) {
            m_SettingsWatcher = new detail::QSettingsWatcher{this};
            connect(m_SettingsWatcher, &detail::QSettingsWatcher::autoRefreshMillisChanged, this, &StandardApplicationContext::autoRefreshMillisChanged);
            m_SettingsWatcher->setAutoRefreshMillis(settings->value("qtdi/autoRefreshMillis", detail::QSettingsWatcher::DEFAULT_REFRESH_MILLIS).toInt());

            qCInfo(loggingCategory()) << "Auto-refresh has been enabled.";
        }
    }
    if(!profiles.empty() && canChangeActiveProfiles()) {
        Profiles profilesToAdd{profiles.begin(), profiles.end()};
        if(!m_activeProfiles -> contains(profilesToAdd)) {
            *m_activeProfiles += profilesToAdd;
            initSettingsForActiveProfiles();
            emit activeProfilesChanged(*m_activeProfiles);
        }
    }
}

int StandardApplicationContext::autoRefreshMillis() const
{
    return m_SettingsWatcher ? m_SettingsWatcher->autoRefreshMillis() : detail::QSettingsWatcher::DEFAULT_REFRESH_MILLIS;
}

void StandardApplicationContext::setAutoRefreshMillis(int newRefreshMillis)
{
    if(!m_SettingsWatcher) {
        qCWarning(loggingCategory()) << "Setting autoRefreshMillis has no effect, as auto-refresh has not been enabled!";
        return;
    }
    m_SettingsWatcher->setAutoRefreshMillis(newRefreshMillis);
}

void StandardApplicationContext::setActiveProfiles(const Profiles &profiles)
{
    if(profiles.empty()) {
        qCCritical(loggingCategory()).nospace() << "Cannot set the active profiles to an empty set";
        return;
    }
    if(canChangeActiveProfiles() && *m_activeProfiles != profiles) {
        *m_activeProfiles = profiles;
        initSettingsForActiveProfiles();
        emit activeProfilesChanged(profiles);
    }
}

bool StandardApplicationContext::autoRefreshEnabled() const
{
    return m_SettingsWatcher != nullptr;
}

Profiles StandardApplicationContext::activeProfiles() const
{
    return *m_activeProfiles;
}

QConfigurationWatcher *StandardApplicationContext::watchConfigValue(const QString &expression)
{
    QMutexLocker<QMutex> locker{&mutex};
    if(!autoRefreshEnabled()) {
        qCWarning(loggingCategory()).nospace().noquote() << "Expression '" << expression << "' will not be watched, as auto-refresh has not been enabled";
        return nullptr;
    }
    return dynamic_cast<QConfigurationWatcher*>(obtainHandleFromApplicationThread([expression,this] {
        return m_SettingsWatcher->watchConfigValue(getResolver(expression));
    }));
}

QVariant StandardApplicationContext::resolveConfigValue(const QString &expression, const QString& group, QVariantMap& resolvedPlaceholders)
{
    if(detail::PlaceholderResolver::isLiteral(expression)) {
        return expression;
    }
    detail::PlaceholderResolver* resolver;
    {
        QMutexLocker<QMutex> locker{&mutex};
        resolver = dynamic_cast<detail::PlaceholderResolver*>(obtainHandleFromApplicationThread([this,expression] { return getResolver(expression);}));
    }
    if(resolver) {
        return resolver->resolve(group, resolvedPlaceholders);
    }
    return QVariant{};
}

QApplicationContext *newDelegate(const QLoggingCategory &loggingCategory, QApplicationContext *delegatingContext)
{
    return new StandardApplicationContext{loggingCategory, delegatingContext, delegatingContext};
}



}//mcnepp::qtdi
