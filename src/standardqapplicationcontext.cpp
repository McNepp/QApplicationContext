#include <QThread>
#include <QSettings>
#include <QEvent>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QUuid>
#include <QRegularExpression>
#include <QCoreApplication>
#include "standardqapplicationcontext.h"



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

const QRegularExpression beanRefPattern{"^&([^.]+)(\\.([^.]+))?"};


inline bool isPrivateProperty(const QString& key) {
    return key.startsWith('.');
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


QStringList determineBeanRefs(const QVariantMap& properties) {
    QStringList result;
    for(auto entry : properties.asKeyValueRange()) {
        auto key = entry.second.toString();
        if(key.length() > 1 && key.startsWith('&')) {
            int dot = key.indexOf('.');
            if(dot < 0) {
                dot = key.size();
            }
            result.push_back(key.mid(1, dot-1));
        }
    }
    return result;
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
    QString typeName{type.name()};
    typeName.replace(' ', '-');
    return typeName+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
}






class AutowireSubscription : public detail::CallableSubscription {
public:
    AutowireSubscription(detail::q_inject_t injector, QObject* bound) : CallableSubscription(bound),
        m_injector(injector),
        m_bound(bound)
    {
    }

     void notify(QObject *obj) override {
        if(auto sourceReg = dynamic_cast<registration_handle_t>(m_bound)) {
            auto subscr = new AutowireSubscription{m_injector, obj};
            sourceReg->subscribe(subscr);
            subscriptions.push_back(subscr);
        } else {
            m_injector(m_bound, obj);
        }
    }

    void cancel() override{
        for(auto iter = subscriptions.begin(); iter != subscriptions.end(); iter = subscriptions.erase(iter)) {
            auto subscr = *iter;
            if(subscr) {
                subscr->cancel();
            }
        }
        detail::CallableSubscription::cancel();
    }




private:
    detail::q_inject_t m_injector;
    QObject* m_bound;
    std::vector<QPointer<detail::Subscription>> subscriptions;
};


class PropertyInjector : public detail::CallableSubscription {
    friend class PropertyBindingSubscription;
public:


    void notify(QObject* target) override {
        m_setter.setter(target, m_sourceProperty.read(m_boundSource));
        if(m_sourceProperty.hasNotifySignal()) {
            detail::BindingProxy* proxy = new detail::BindingProxy{m_sourceProperty, m_boundSource, m_setter, target};
            auto connection = QObject::connect(m_boundSource, m_sourceProperty.notifySignal(), proxy, detail::BindingProxy::notifySlot());
            qCDebug(loggingCategory()).nospace().noquote() << "Bound property '" << m_sourceProperty.name() << "' of " << m_boundSource << " to " << m_setter <<" of " << target;
            connections.push_back(std::move(connection));
            return;
        }
        if(m_sourceProperty.isBindable()) {
            auto sourceBindable = m_sourceProperty.bindable(m_boundSource);
            auto notifier = sourceBindable.addNotifier([this,target]{
                m_setter.setter(target, m_sourceProperty.read(m_boundSource));
            });
            qCDebug(loggingCategory()).nospace().noquote() << "Bound property '" << m_sourceProperty.name() << "' of " << m_boundSource << " to " << m_setter << " of " << target;
            bindings.push_back(std::move(notifier));
            return;
        }
        qCWarning(loggingCategory()).nospace().noquote() << "Could not bind property '" << m_sourceProperty.name() << "' of " << m_boundSource << " to " << m_setter << " of " << target;

    }

    void cancel() override {
        for(auto iter = connections.begin(); iter != connections.end(); iter = connections.erase(iter)) {
            QObject::disconnect(*iter);
        }
        //QPropertyNotifier will remove the binding in its destructor:
        bindings.clear();
        detail::CallableSubscription::cancel();
    }

private:

    PropertyInjector(QObject* boundSource, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter) : detail::CallableSubscription(boundSource),
        m_sourceProperty(sourceProperty),
        m_setter(setter),
        m_boundSource(boundSource) {
    }
    QMetaProperty m_sourceProperty;
    detail::property_descriptor m_setter;
    QObject* m_boundSource;
    std::vector<QPropertyNotifier> bindings;
    std::vector<QMetaObject::Connection> connections;
};

class PropertyBindingSubscription : public detail::CallableSubscription {
public:


    void notify(QObject* obj) override {
        auto subscr = new PropertyInjector{obj, m_sourceProperty, m_setter};
        m_target->subscribe(subscr);
        subscriptions.push_back(subscr);
    }

    void cancel() override {
        for(auto iter = subscriptions.begin(); iter != subscriptions.end(); iter = subscriptions.erase(iter)) {
            auto subscription = *iter;
            if(subscription) {
                subscription->cancel();
            }
        }
        detail::CallableSubscription::cancel();
    }

    PropertyBindingSubscription(registration_handle_t target, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter) : detail::CallableSubscription(target),
        m_target(target),
        m_sourceProperty(sourceProperty),
        m_setter(setter) {
    }
private:
    registration_handle_t m_target;
    QMetaProperty m_sourceProperty;
    detail::property_descriptor m_setter;
    std::vector<QPointer<Subscription>> subscriptions;
};


class ProxySubscription : public detail::Subscription {
public:
    explicit ProxySubscription(registration_handle_t target) :
        detail::Subscription{target} {
        out_connection = QObject::connect(this, &Subscription::objectPublished, target, &detail::Registration::objectPublished);
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

private:
    QMetaObject::Connection out_connection;
    QList<QMetaObject::Connection> in_connections;
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



class StandardApplicationContext::CreateRegistrationHandleEvent : public QEvent {
public:
    static QEvent::Type eventId() {
        static int eventId = QEvent::registerEventType();
        return static_cast<QEvent::Type>(eventId);
    }


    CreateRegistrationHandleEvent(const std::type_info &service_type, const QMetaObject* metaObject) :
        QEvent(eventId()),
        m_service_type(service_type),
        m_metaObject(metaObject),
        m_result(QSharedPointer<std::optional<ProxyRegistrationImpl*>>::create())
    {
    }

    void createHandle(StandardApplicationContext* context) {
        *m_result = new ProxyRegistrationImpl{m_service_type, m_metaObject, context};
    }

    QSharedPointer<std::optional<ProxyRegistrationImpl*>> result() const {
        return m_result;
    }

private:
    const std::type_info &m_service_type;
    const QMetaObject* m_metaObject;
    QSharedPointer<std::optional<ProxyRegistrationImpl*>> m_result;
};


StandardApplicationContext::ProxyRegistrationImpl::ProxyRegistrationImpl(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent) :
    detail::ProxyRegistration{parent},
    m_type(type),
    m_meta(metaObject)
{
    proxySubscription = new ProxySubscription{this};
    for(auto reg : parent->registrations) {
        add(reg);
    }
}

QList<service_registration_handle_t> StandardApplicationContext::ProxyRegistrationImpl::registeredServices() const {
    QList<service_registration_handle_t> result;
    for(auto handle : applicationContext() -> getRegistrationHandles()) {
        if(auto reg = dynamic_cast<DescriptorRegistration*>(handle); reg && reg->matches(m_type)) {
            result.push_back(reg);
        }
    }
    return result;
}



void StandardApplicationContext::ProxyRegistrationImpl::onSubscription(subscription_handle_t subscription) {
    detail::connect(this, subscription);
    TemporarySubscriptionProxy tempProxy{subscription};
    //By subscribing to a TemporarySubscriptionProxy, we force existing objects to be signalled immediately, while not creating any new Connections:
    for(auto reg : registeredServices()) {
        reg->subscribe(&tempProxy);
    }
}

detail::Subscription *StandardApplicationContext::ProxyRegistrationImpl::createAutowiring(const std::type_info &type, detail::q_inject_t injector, Registration *source)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot create autowiring in different thread";
        return nullptr;
    }
    if(!autowirings.insert(type).second) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register autowiring for type " << type.name() << " in " << *this;
        return nullptr;
    }

    return subscribe(new AutowireSubscription{injector, source});
}



const service_config StandardApplicationContext::ObjectRegistration::defaultConfig;


subscription_handle_t StandardApplicationContext::DescriptorRegistration::createBindingTo(const char* sourcePropertyName, detail::Registration *target, const detail::property_descriptor& targetProperty)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot create binding in different thread";
        return nullptr;
    }

    detail::property_descriptor setter = targetProperty;
    if(this == target && QString{sourcePropertyName} == setter.name) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to self";
        return nullptr;
    }

    if(target->applicationContext() != applicationContext()) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to " << *target << " from different ApplicationContext";
        return nullptr;
    }

    auto sourceProperty = getProperty(this, sourcePropertyName);
    if(!detail::isBindable(sourceProperty)) {
        qCWarning(loggingCategory()).noquote().nospace() << "Property '" << sourcePropertyName << "' in " << *this << " is not bindable";
    }
    if(!setter.setter) {
        auto targetProp = getProperty(target, setter.name);
        if(!targetProp.isValid() || !targetProp.isWritable()) {
            qCCritical(loggingCategory()).noquote().nospace() << setter << " is not a writable property for " << *target;
            return nullptr;
        }
        if(!QMetaType::canConvert(sourceProperty.metaType(), targetProp.metaType())) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to " << setter << " of " << *target << " with incompatible types";
            return nullptr;
        }
        setter = detail::propertySetter(targetProp);
    }
    if(!applicationContext()->registerBoundProperty(target, setter.name)) {
        qCCritical(loggingCategory()).noquote().nospace() << setter << " has already been bound to " << *target;
        return nullptr;
    }

    auto subscription = new PropertyBindingSubscription{target, sourceProperty, setter};
    qCInfo(loggingCategory()).noquote().nospace() << "Created Subscription for binding property '" << sourceProperty.name() << "' of " << *this << " to " << setter << " of " << *target;
    return subscribe(subscription);
}

detail::Subscription *StandardApplicationContext::DescriptorRegistration::createAutowiring(const std::type_info &type, detail::q_inject_t injector, Registration *source)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot create autowiring in different thread";
        return nullptr;
    }

    if(!autowirings.insert(type).second) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register autowiring for type " << type.name() << " in " << *this;
        return nullptr;
    }
    return subscribe(new AutowireSubscription{injector, source});
}





StandardApplicationContext::DescriptorRegistration::DescriptorRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, StandardApplicationContext* context, QObject* parent) :
    detail::ServiceRegistration(parent),
    m_descriptor{desc},
    m_name(name),
    m_index(index),
    m_context(context),
    m_base(base)
{
}






void StandardApplicationContext::ServiceRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Service '" << registeredName() << "' with " << this->descriptor();
}

void StandardApplicationContext::ServiceRegistration::serviceDestroyed(QObject *srv) {
    if(srv == theService) {
        if(auto parentReg = dynamic_cast<service_registration_handle_t>(parent()); parentReg && parentReg -> scope() == ServiceScope::PROTOTYPE) {
            qInfo(loggingCategory()).noquote().nospace() << "Instance of Prototype " << *this << " has been destroyed";
        } else {
            //Somebody has destroyed a Service that is managed by this ApplicationContext.
            //All we can do is log an error and set theService to nullptr.
            //Yet, it might still be in use somewhere as a dependency.
            qCritical(loggingCategory()).noquote().nospace() << *this << " has been destroyed externally";
        }
        theService = nullptr;
        m_state = STATE_INIT;
    }
}

void StandardApplicationContext::ObjectRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Object '" << registeredName() << "' with " << this->descriptor();
}




QStringList StandardApplicationContext::ServiceRegistration::getBeanRefs() const
{
    if(!beanRefsCache.has_value()) {
        beanRefsCache = determineBeanRefs(config().properties);
    }
    return beanRefsCache.value();

}

QObject* StandardApplicationContext::ServiceRegistration::createService(const QVariantList &dependencies, descriptor_list &created)
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
                onDestroyed = connect(theService, &QObject::destroyed, this, &ServiceRegistration::serviceDestroyed);
                m_state = STATE_CREATED;
            }
        }
    }
    return theService;
}

StandardApplicationContext::ServiceTemplateRegistration::ServiceTemplateRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent) :
    DescriptorRegistration{base, index, name, desc, context, parent},
    m_config(config),
    resolvedProperties{config.properties} {
    proxySubscription = new ProxySubscription{this};
}


QStringList StandardApplicationContext::ServiceTemplateRegistration::getBeanRefs() const
{
    if(!beanRefsCache.has_value()) {
        beanRefsCache = determineBeanRefs(config().properties);
    }
    return beanRefsCache.value();

}

subscription_handle_t StandardApplicationContext::ServiceTemplateRegistration::createBindingTo(const char*, registration_handle_t, const detail::property_descriptor&)
{
    qCritical(loggingCategory()).noquote().nospace() << "Cannot create binding from " << *this;
    return nullptr;
}


QObject* StandardApplicationContext::ServiceTemplateRegistration::createService(const QVariantList&, descriptor_list&) {
    return nullptr;
}

void StandardApplicationContext::ServiceTemplateRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Service-template '" << registeredName() << "' of type " << descriptor().impl_type.name();
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
    DescriptorRegistration{base, index, name, desc, parent},
    m_state(STATE_INIT),
    m_config(config)
{
    proxySubscription = new ProxySubscription{this};
}


int StandardApplicationContext::PrototypeRegistration::unpublish()
{
    return 0;
}

QStringList StandardApplicationContext::PrototypeRegistration::getBeanRefs() const
{
    if(!beanRefsCache.has_value()) {
        beanRefsCache = determineBeanRefs(config().properties);
    }
    return beanRefsCache.value();
}

QObject* StandardApplicationContext::PrototypeRegistration::createService(const QVariantList& dependencies, descriptor_list& created) {
    switch(state()) {
    case STATE_INIT:
        //Store dependencies for deferred creation of service-instances:
        m_dependencies = dependencies;
        m_state = STATE_PUBLISHED;
        return this;
    case STATE_PUBLISHED:
        {
        std::unique_ptr<DescriptorRegistration> instanceReg{ new StandardApplicationContext::ServiceRegistration{base(), ++applicationContext()->nextIndex, registeredName(), descriptor(), config(), applicationContext(), this}};
            QObject* instance = instanceReg->createService(m_dependencies, created);
            if(!instance) {
                qCCritical(loggingCategory()).noquote().nospace() << "Could not create instancef of " << *this;
                return nullptr;
            }
            qCInfo(loggingCategory()).noquote().nospace() << "Created instance of " << *this;

            instanceReg->subscribe(proxySubscription);
            created.push_back(instanceReg.release());
            return instance;
        }
    default:
        qCCritical(loggingCategory()).noquote().nospace() << "Invalid state! Cannot create instance of " << *this;
        return nullptr;
    }
}

void StandardApplicationContext::PrototypeRegistration::print(QDebug out) const {
    out.nospace().noquote() << "Prototype '" << registeredName() << "' with " << this->descriptor();
}

subscription_handle_t StandardApplicationContext::PrototypeRegistration::createBindingTo(const char*, registration_handle_t, const detail::property_descriptor&)
{
    qCritical(loggingCategory()).noquote().nospace() << "Cannot create binding from " << *this;
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




void registerAppInGlobalContext() {
    auto globalContext = QApplicationContext::instance();
    if(globalContext && !globalContext->getRegistration("application")) {
        globalContext->registerObject(QCoreApplication::instance(), "application");
    }
}

Q_COREAPP_STARTUP_FUNCTION(registerAppInGlobalContext)





StandardApplicationContext::StandardApplicationContext(QObject* parent) :
QApplicationContext(parent)
{
    if(auto app = QCoreApplication::instance()) {
        registerObject(app, "application");
    }

    registerObject<QApplicationContext>(this, "context");


    if(setInstance(this)) {
        qCInfo(loggingCategory()).noquote().nospace() << "Installed QApplicationContext " << this << " as global instance";
    }
}


StandardApplicationContext::~StandardApplicationContext() {
    //Before we un-publish, we unset this instance as the global instance:
    if(unsetInstance(this)) {
        qCInfo(loggingCategory()).noquote().nospace() << "Removed QApplicationContext " << this << " as global instance";
    }
    unpublish();
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
                if(getRegistrationByName(beanRef) == reg) {
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

StandardApplicationContext::DescriptorRegistration *StandardApplicationContext::getRegistrationByName(const QString &name) const
{

    auto found = registrationsByName.find(name);
    return found != registrationsByName.end() ? found->second : nullptr;
}


std::pair<QVariant,StandardApplicationContext::Status> StandardApplicationContext::resolveDependency(const descriptor_list &published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial)
{
    const std::type_info& type = d.type;

    QList<DescriptorRegistration*> depRegs;

    for(auto pub : published) {
        if(pub->matches(type) && pub->scope() != ServiceScope::TEMPLATE) {
            if(d.has_required_name()) {
                auto byName = getRegistrationByName(d.expression);
                if(!byName || byName != pub) {
                    continue;
                }
            }
            depRegs.push_back(pub);
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
            auto resolved = resolvePlaceholders(d.expression, reg->config());
            switch(resolved.second) {
            case Status::ok:
                qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << resolved.first;
                return resolved;
            case Status::fixable:
                if(d.value.isValid()) {
                    return {d.value, Status::ok};
                }
                [[fallthrough]];
            default:
                return resolved;
            }
        }

    case static_cast<int>(Kind::MANDATORY):
        if(depRegs.empty()) {
            if(allowPartial) {
                qWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fixable};
            } else {
                qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fatal};
            }

        }
    case static_cast<int>(Kind::OPTIONAL):
        switch(depRegs.size()) {
        case 0:
            qCInfo(loggingCategory()).noquote().nospace() << "Skipped " << d;
            return {QVariant{}, Status::ok};
        case 1:
            qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << depRegs[0];
            return {QVariant::fromValue(depRegs[0]->getObject()), Status::ok};
        default:
            //Ambiguity is always a non-fixable error:
            qCritical(loggingCategory()).noquote().nospace() << d << " is ambiguous";
            return {QVariant{}, Status::fatal};
        }
    case static_cast<int>(Kind::N):
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << depRegs.size() << " objects.";
        {
            QObjectList dep;
            //Sort the dependencies by their index(), which is the order of registration:
            std::sort(depRegs.begin(), depRegs.end(), [](auto left, auto right) { return left->index() < right->index();});
            std::transform(depRegs.begin(), depRegs.end(), std::back_insert_iterator(dep), std::mem_fn(&DescriptorRegistration::getObject));
            return {QVariant::fromValue(dep), Status::ok};
        }
    }

    return {QVariant{}, Status::fatal};
}





detail::ServiceRegistration *StandardApplicationContext::getRegistrationHandle(const QString& name) const
{
    QMutexLocker<QMutex> locker{&mutex};

    DescriptorRegistration* reg = getRegistrationByName(name);
    if(reg) {
        return reg;
    }
    qCCritical(loggingCategory()).noquote().nospace() << "Could not find a Registration for name '" << name << "'";
    return nullptr;
}



detail::ProxyRegistration *StandardApplicationContext::getRegistrationHandle(const std::type_info &service_type, const QMetaObject* metaObject) const
{
    QMutexLocker<QMutex> locker{&mutex};

    auto found = proxyRegistrationCache.find(service_type);
    if(found != proxyRegistrationCache.end()) {
        return found->second;
    }
    ProxyRegistrationImpl* proxyReg;
    if(QThread::currentThread() == thread()) {
        proxyReg = new ProxyRegistrationImpl{service_type, metaObject, const_cast<StandardApplicationContext*>(this)};
    } else {
        //We are in a different Thread than the QApplicationContext's. Let's post an Event that will create the ProxyRegistration asynchronously:
        auto event = new CreateRegistrationHandleEvent{service_type, metaObject};
        auto result = event->result(); //Pin member on Stack to prevent asynchronous deletion.
        QCoreApplication::postEvent(const_cast<StandardApplicationContext*>(this), event);
        QDeadlineTimer timer{1000};
        while(!result->has_value()) {
            condition.wait(&mutex, timer);
        }
        if(!result->has_value()) {
            qCCritical(loggingCategory()).noquote().nospace() << "Could not obtain Registration-handle from another thread in time";
            return nullptr;
        }

        proxyReg = result->value();
    }
    proxyRegistrationCache.insert({service_type, proxyReg});
    return proxyReg;
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
    auto found = getRegistrationByName(alias);
    if(found && found != reg) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register alias '" << alias << "' for " << *reg << ". Another Service has been registered under this name: " << *found;
        return false;
    }
    //At this point we know for sure that reg
    registrationsByName.insert({alias, *foundIter});
    qCInfo(loggingCategory()).noquote().nospace() << "Registered alias '" << alias << "' for " << *reg;
    return true;

}







void StandardApplicationContext::contextObjectDestroyed(QObject* obj)
{
    for(auto iter = registrationsByName.begin(); iter != registrationsByName.end();) {
        auto reg = iter->second;
        if(reg->getObject() == obj) {
            iter = registrationsByName.erase(iter);
        } else {
            ++iter;
        }
    }


    for(auto iter = registrations.begin(); iter != registrations.end();) {
        if((*iter)->getObject() == obj) {
            std::unique_ptr<DescriptorRegistration> regPtr{*iter};
            iter = registrations.erase(iter);
            qCInfo(loggingCategory()).noquote().nospace() << *regPtr << " has been destroyed externally";
        } else {
            ++iter;
        }
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
            if(!getRegistrationByName(beanRef)) {
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
    if(auto proto = arg.value<DescriptorRegistration*>(); proto && proto->scope() == ServiceScope::PROTOTYPE) {
        auto instance = proto->createService(QVariantList{}, created);
        if(!instance) {
            return QVariant{};
        }
        return QVariant::fromValue(instance);
    }
    return arg;
}


bool StandardApplicationContext::publish(bool allowPartial)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot publish ApplicationContext in different thread";
        return false;
    }

    descriptor_list allCreated;
    descriptor_list toBePublished;
    descriptor_list needConfiguration;
    Status validationResult = Status::ok;
    {
        QMutexLocker<QMutex> locker{&mutex};

        for(auto reg : registrations) {
            switch (reg->state()) {
            case STATE_INIT:
                toBePublished.push_back(reg);
                break;
            case STATE_CREATED:
                needConfiguration.push_back(reg);
                [[fallthrough]];
            case STATE_PUBLISHED:
                allCreated.push_back(reg);
            }
        }
    }
    if(toBePublished.empty() && needConfiguration.empty()) {
        return true;
    }
    validationResult = validate(allowPartial, allCreated, toBePublished);
    if(validationResult == Status::fatal) {
        return false;
    }

    qCInfo(loggingCategory()).noquote().nospace() << "Publish ApplicationContext with " << toBePublished.size() << " unpublished Objects";
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

        reg->createService(dependencies, needConfiguration);

        switch(reg->state()) {
        case STATE_INIT:
            qCCritical(loggingCategory()).nospace().noquote() << "Could not create service " << *reg;
            return false;

        case STATE_CREATED:
            qCInfo(loggingCategory()).nospace().noquote() << "Created service " << *reg;
            needConfiguration.push_back(reg);
            [[fallthrough]];
        case STATE_PUBLISHED:
            allCreated.push_back(reg);

        }
    }


    unsigned managed = std::count_if(allCreated.begin(), allCreated.end(), std::mem_fn(&DescriptorRegistration::isManaged));

    //The services that have been instantiated during this methd-invocation will be configured in the order they have have been
    //instantiated.
    while(!needConfiguration.empty()) {
        auto reg = pop_front(needConfiguration);
        auto configResult = configure(reg, reg->config(), reg->getObject(), needConfiguration, allowPartial);
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
    for(auto reg : toBePublished) {
        auto initResult = init(reg, postProcessors);
        switch(initResult) {
        case Status::fatal:
            qCCritical(loggingCategory()).nospace().noquote() << "Could not initialize " << *reg;
            return false;
        case Status::fixable:
            qCWarning(loggingCategory()).nospace().noquote() << "Could not initialize " << *reg;
            validationResult = Status::fixable;
            continue;

        case Status::ok:
            ++publishedCount;
            reg->notifyPublished();
            qCInfo(loggingCategory()).noquote().nospace() << "Published " << *reg;
        }
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
    return std::count_if(registrations.begin(), registrations.end(), std::not_fn(std::mem_fn(&DescriptorRegistration::isPublished)));
}

QList<service_registration_handle_t> StandardApplicationContext::getRegistrationHandles() const
{
    QMutexLocker<QMutex> locker{&mutex};

    QList<service_registration_handle_t> result;
    std::copy(registrations.begin(), registrations.end(), std::back_inserter(result));
    return result;
}



service_registration_handle_t StandardApplicationContext::registerService(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, QObject* baseObj)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot register service in different thread";
        return nullptr;
    }
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
            if(objName.isEmpty()) {
                objName = baseObj->objectName();
            }
            if(!objName.isEmpty()) {
                reg = getRegistrationByName(objName);
                //If we have a registration under the same name, we'll return it only if it's for the same object and it has the same descriptor:
                if(reg) {
                    if(!reg->isManaged() && reg->getObject() == baseObj && descriptor == reg->descriptor()) {
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
                //With isManaged() we test whether reg is also an ObjectRegistration (no ServiceRegistration)
                if(!regist->isManaged() && baseObj == regist->getObject()) {
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
                reg = getRegistrationByName(name);
                //If we have a registration under the same name, we'll return it only if it has the same descriptor and config:
                if(reg) {
                    //With isManaged() we test whether reg is also a ServiceRegistration (no ObjectRegistration)
                    if(reg->isManaged() && descriptor == reg->descriptor() && reg->config() == config) {
                        return reg;
                    }
                    //Otherwise, we have a conflicting registration
                    qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Service " << descriptor << " as '" << name << "'. Has already been registered as " << *reg;
                    return nullptr;
                }
            } else {
                //For an anonymous registration, we have to loop over all registrations:
                for(auto regist : registrations) {
                    //With isManaged() we test whether reg is also a ServiceRegistration (no ObjectRegistration)
                    if(regist->isManaged() && regist->config() == config) {
                        switch(detail::match(descriptor, regist->descriptor())) {
                        case detail::DESCRIPTOR_IDENTICAL:
                            return regist;
                        case detail::DESCRIPTOR_INTERSECTS:
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

            if(descriptor.meta_object) {
                for(auto& key : config.properties.keys()) {
                    if(!isPrivateProperty(key) && descriptor.meta_object->indexOfProperty(key.toLatin1()) < 0) {
                        qCCritical(loggingCategory()).nospace().noquote() << "Cannot register " << descriptor << " as '" << name << "'. Service-type has no property '" << key << "'";
                        return nullptr;
                    }
                }
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
            switch(scope) {
            case ServiceScope::PROTOTYPE:
                reg = new PrototypeRegistration{base, ++nextIndex, objName, descriptor, config, this};
                break;
            case ServiceScope::SINGLETON:
                reg = new ServiceRegistration{base, ++nextIndex, objName, descriptor, config, this};
                break;
            case ServiceScope::TEMPLATE:
                reg = new ServiceTemplateRegistration{base, ++nextIndex, objName, descriptor, config, this};
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

        registrationsByName.insert({objName, reg});
        registrations.push_back(reg);
        for(auto& entry : proxyRegistrationCache) {
            entry.second->add(reg);
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Registered " << *reg;
    }

    // Emit signal after mutex has been released:
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
    auto match = beanRefPattern.match(key);
    if(match.hasMatch()) {
        key = match.captured(1);
        auto bean = getRegistrationByName(key);
        if(!(bean && bean->getObject())) {
            if(allowPartial) {
                qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
                return { Status::fixable, false};
            }
            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
            return {Status::fatal, false};
        }
        QVariant resultValue = resolveDependency(QVariant::fromValue(bean->getObject()), toBePublished);
        if(match.hasCaptured(3)) {
            QString propName = match.captured(3);
            if(!resultValue.isValid()) {
                if(allowPartial) {
                    qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                    return { Status::fixable, false};
                }
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                return {Status::fatal, false};
            }
            QMetaProperty sourceProp = getProperty(bean, propName.toLatin1());
            if(!sourceProp.isValid()) {
                //Refering to a non-existing Q_PROPERTY is always non-fixable:
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                return {Status::fatal, false};
            }
            resultValue = sourceProp.read(resultValue.value<QObject*>());
        }

        qCInfo(loggingCategory()).nospace().noquote() << "Resolved reference '" << key << "' to " << resultValue;
        value = resultValue;
        return {Status::ok, true};
    }
    return {Status::ok, false};

}


std::pair<QVariant,StandardApplicationContext::Status> StandardApplicationContext::resolvePlaceholders(const QString& key, const service_config& config)
{
    constexpr int STATE_START = 0;
    constexpr int STATE_FOUND_DOLLAR = 1;
    constexpr int STATE_FOUND_PLACEHOLDER = 2;
    constexpr int STATE_FOUND_DEFAULT_VALUE = 3;
    constexpr int STATE_ESCAPED = 4;
    QVariant lastResolvedValue;
    QString resolvedString;
    QString token;
    QString defaultValueToken;
    const QString& group = config.group;

    int lastStateBeforeEscape = STATE_START;
    int state = STATE_START;
    for(int pos = 0; pos < key.length(); ++pos) {
        auto ch = key[pos];
        switch(ch.toLatin1()) {

        case '\\':
            switch(state) {
            case STATE_ESCAPED:
                resolvedString += '\\';
                state = lastStateBeforeEscape;
                continue;
            default:
                lastStateBeforeEscape = state;
                state = STATE_ESCAPED;
                continue;
            }

        case '$':
            switch(state) {
            case STATE_ESCAPED:
                resolvedString += '$';
                state = lastStateBeforeEscape;
                continue;
            case STATE_FOUND_DOLLAR:
                resolvedString += '$';
                [[fallthrough]];
            case STATE_START:
                state = STATE_FOUND_DOLLAR;
                continue;
            default:
                qCCritical(loggingCategory()).nospace().noquote() << "Invalid placeholder '" << key << "'";
                return {QVariant{}, Status::fatal};
            }


        case '{':
            switch(state) {
            case STATE_ESCAPED:
                resolvedString += '{';
                state = lastStateBeforeEscape;
                continue;
            case STATE_FOUND_DOLLAR:
                state = STATE_FOUND_PLACEHOLDER;
                continue;
            default:
                state = STATE_START;
                resolvedString += ch;
                continue;
            }

        case '}':
            switch(state) {
            case STATE_ESCAPED:
                resolvedString += '}';
                state = lastStateBeforeEscape;
                continue;
            case STATE_FOUND_DEFAULT_VALUE:
            case STATE_FOUND_PLACEHOLDER:
                if(!token.isEmpty()) {
                    lastResolvedValue = getConfigurationValue(group.isEmpty() ? token : group + "/" + token);
                    if(!lastResolvedValue.isValid()) {
                        //If not found in ApplicationContext's configuration, look in the "private properties":
                        lastResolvedValue = config.properties["." + token];
                        if(!lastResolvedValue.isValid()) {
                            if(state == STATE_FOUND_DEFAULT_VALUE) {
                                lastResolvedValue = defaultValueToken;
                            } else {
                                if(!lastResolvedValue.isValid()) {
                                    qCInfo(loggingCategory()).nospace().noquote() << "Could not resolve configuration-key '" << token << "'";
                                    return {QVariant{}, Status::fixable};
                                }
                            }
                        }
                    }
                    if(resolvedString.isEmpty() && pos + 1 == key.length()) {
                        return {lastResolvedValue, Status::ok};
                    }
                    resolvedString += lastResolvedValue.toString();
                    token.clear();
                    defaultValueToken.clear();
                }
                state = STATE_START;
                continue;
            default:
                resolvedString += ch;
                continue;
            }
        case ':':
            switch(state) {
            case STATE_ESCAPED:
                resolvedString += ':';
                state = lastStateBeforeEscape;
                continue;
            case STATE_FOUND_PLACEHOLDER:
                state = STATE_FOUND_DEFAULT_VALUE;
                continue;
            }

        default:
            switch(state) {
            case STATE_FOUND_DOLLAR:
                resolvedString += '$';
                state = STATE_START;
                [[fallthrough]];
            case STATE_START:
                resolvedString += ch;
                continue;
            case STATE_FOUND_PLACEHOLDER:
                token += ch;
                continue;
            case STATE_FOUND_DEFAULT_VALUE:
                defaultValueToken += ch;
                continue;
            case STATE_ESCAPED:
                resolvedString += ch;
                state = lastStateBeforeEscape;
                continue;
            default:
                token += ch;
                continue;
            }
        }
    }
    switch(state) {
    case STATE_FOUND_DOLLAR:
        resolvedString += '$';
        [[fallthrough]];
    case STATE_START:
        return {resolvedString, Status::ok};
    case STATE_ESCAPED:
        resolvedString += '\\';
        return {resolvedString, Status::ok};
    default:
        qCCritical(loggingCategory()).nospace().noquote() << "Unbalanced placeholder '" << key << "'";
        return {QVariant{}, Status::fatal};
    }
}

StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::findAutowiringCandidate(service_registration_handle_t target, const QMetaProperty& prop) {
    auto propMetaType = prop.metaType().metaObject();
    DescriptorRegistration* candidate = getRegistrationByName(prop.name()); //First, try by name
    //If the candidate is assignable to the property, return it, unless it is the target. (We never autowire a property with a pointer to the same service)
    if(candidate && candidate != target && candidate -> getObject() && candidate->getObject()->metaObject()->inherits(propMetaType)) {
        return candidate;
    }
    candidate = nullptr;
    //No matching name found, now we iterate over all registrations:
    for(auto regist : registrations) {
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


StandardApplicationContext::Status StandardApplicationContext::configure(DescriptorRegistration* reg, const service_config& config, QObject* target, descriptor_list& toBePublished, bool allowPartial) {
    if(!target) {
        return Status::fatal;
    }
    if(target->objectName().isEmpty()) {
        target->setObjectName(reg->registeredName());
    }

    if(reg->base()) {
        service_config mergedConfig{reg->base()->config()};
        //Add the 'private properties' from the current Reg to the properties from the base. Current values will overwrite inherited values:
        for(auto[key,value] : config.properties.asKeyValueRange()) {
            if(isPrivateProperty(key)) {
                mergedConfig.properties.insert(key, value);
            }
        }
        auto baseStatus = configure(reg->base(), mergedConfig, target, toBePublished, allowPartial);
        if(baseStatus != Status::ok) {
            return baseStatus;
        }
    }

    auto metaObject = target->metaObject();
    if(metaObject) {
        std::unordered_set<QString> usedProperties;
        descriptor_list createdForThis;
        for(auto[key,value] : config.properties.asKeyValueRange()) {
            QVariant resolvedValue = value;
            auto result = resolveBeanRef(resolvedValue, createdForThis, allowPartial);
            if(result.first != Status::ok) {
                return result.first;
            }
            if(!result.second && value.userType() == QMetaType::QString) {
                auto propertyResult = resolvePlaceholders(value.toString(), config);
                if(propertyResult.second != Status::ok) {
                    return propertyResult.second;
                }
                resolvedValue = propertyResult.first;
            }
            reg->resolveProperty(key, resolvedValue);
            if(!isPrivateProperty(key)) {
                auto targetProperty = metaObject->property(metaObject->indexOfProperty(key.toLatin1()));
                if(!targetProperty.isValid() || !targetProperty.isWritable()) {
                    //Refering to a non-existing Q_PROPERTY by name is always non-fixable:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not find writable property " << key << " of '" << metaObject->className() << "'";
                    return Status::fatal;
                }
                if(targetProperty.write(target, resolvedValue)) {
                    qCDebug(loggingCategory()).nospace().noquote() << "Set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    usedProperties.insert(key);
                } else {
                    //An error while setting a Q_PROPERTY is always non-fixable:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    return Status::fatal;
                }
            }
        }
        //If any instances of prototypes have been created while configuring the properties, make them children of the target:
        for(auto child : createdForThis) {
            setParentIfNotSet(child->getObject(), target);
        }
        toBePublished.insert(toBePublished.end(), createdForThis.begin(), createdForThis.end());
        if(config.autowire) {
            for(int p = 0; p < metaObject->propertyCount(); ++p) {
                auto prop = metaObject->property(p);
                if(usedProperties.find(prop.name()) != usedProperties.end()) {
                    qCDebug(loggingCategory()).nospace() << "Skip Autowiring property '" << prop.name() << "' of " << *reg << " because it has been explicitly set";
                    continue; //Already set this property explicitly
                }
                auto propType = prop.metaType();
                if(!(propType.flags() & QMetaType::PointerToQObject)) {
                    continue;
                }
                DescriptorRegistration* candidate = findAutowiringCandidate(reg, prop);
                if(candidate) {
                    if(prop.write(target, QVariant::fromValue(candidate->getObject()))) {
                        qCInfo(loggingCategory()).nospace() << "Autowired property '" << prop.name() << "' of " << *reg << " to " << *candidate;
                    } else {
                        qCWarning(loggingCategory()).nospace().noquote() << "Autowiring property '" << prop.name()  << "' of " << *reg << " to " << *candidate << " failed.";
                    }
                } else {
                    qCInfo(loggingCategory()).nospace().noquote() << "Could not autowire property '" << prop.name()  << "' of " << *reg;
                }
            }
        }

    }
    return Status::ok;
}

StandardApplicationContext::Status StandardApplicationContext::init(DescriptorRegistration* reg, const QList<QApplicationContextPostProcessor*>& postProcessors) {
    QObject* target = reg->getObject();
    if(!target) {
        return Status::fatal;
    }

    for(auto processor : postProcessors) {
        if(processor != dynamic_cast<QApplicationContextPostProcessor*>(target)) {
            //Don't process yourself!
            processor->process(this, target, reg->registeredProperties());
        }
    }

    for(DescriptorRegistration* self = reg; self; self = self->base()) {
        if(self->descriptor().init_method) {
            self->descriptor().init_method(target, this);
            qCInfo(loggingCategory()).nospace().noquote() << "Invoked init-method of " << *reg;
            break;
       }
    }
    //If the service has no parent, make it a child of this ApplicationContext:
    //Note: It will be deleted in StandardApplicationContext's destructor explicitly, to maintain the correct order of dependencies!
    setParentIfNotSet(target, this);
    return Status::ok;
}




QVariant StandardApplicationContext::getConfigurationValue(const QString& key) const {
    if(auto bytes = QString{key}.replace('/', '.').toLocal8Bit(); qEnvironmentVariableIsSet(bytes)) {
        auto value = qEnvironmentVariable(bytes);
        qCDebug(loggingCategory()).noquote().nospace() << "Obtained configuration-entry: " << bytes << " = '" << value << "' from enviroment";
        return value;
    }

    Collector<QSettings> collector;
    for(auto reg : getRegistrationHandles()) {
        reg->subscribe(&collector);
    }
    for(QSettings* settings : collector.collected) {
        auto value = settings->value(key);
        if(value.isValid()) {
            qCDebug(loggingCategory()).noquote().nospace() << "Obtained configuration-entry: " << key << " = " << value << " from " << settings->fileName();
            return value;
        }
    }

    qCDebug(loggingCategory()).noquote().nospace() << "No value found for configuration-entry: " << key;
    return QVariant{};
}




bool StandardApplicationContext::event(QEvent *event)
{
    if(event->type() == CreateRegistrationHandleEvent::eventId()) {
        auto createEvent = static_cast<CreateRegistrationHandleEvent*>(event);
        QMutexLocker<QMutex> locker{&mutex};
        createEvent->createHandle(this);
        condition.notify_all();
        return true;
    }
    return QObject::event(event);

}





    }//mcnepp::qtdi
