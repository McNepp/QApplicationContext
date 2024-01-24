#include <QThread>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QUuid>
#include <QRegularExpression>
#include <QSettings>
#include "standardqapplicationcontext.h"

namespace mcnepp::qtdi {



namespace detail {

BindingProxy::BindingProxy(QMetaProperty sourceProp, QObject* source, const detail::property_descriptor& setter, QObject* target) : QObject(source),
    m_source(source),
    m_sourceProp(sourceProp),
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

inline QString kindToString(int kind) {
    switch(kind) {
    case static_cast<int>(Kind::N):
        return "N";
    case static_cast<int>(Kind::OPTIONAL):
        return "optional";
    case static_cast<int>(Kind::MANDATORY):
        return "mandatory";
    case static_cast<int>(Kind::PRIVATE_COPY):
        return "private copy";
    case VALUE_KIND:
        return "value";
    case RESOLVABLE_KIND:
        return "resolvable";
    default:
        return "unknown";

    }
}


inline QDebug operator<<(QDebug out, const dependency_info& info) {
    QDebug tmp = out.noquote().nospace() << "Dependency<" << info.type.name() << "> [" << kindToString(info.kind) << ']';
    switch(info.kind) {
    case detail::VALUE_KIND:
        tmp << " with value " << info.value;
        break;
    case detail::RESOLVABLE_KIND:
        tmp << " with expression '" << info.expression << "'";
        break;
    default:
        if(!info.expression.isEmpty()) {
            tmp << " with required name '" << info.expression << "'";
        }
    }
    return out;
}

inline QDebug operator << (QDebug out, const service_descriptor& descriptor) {
    QDebug tmp = out.nospace().noquote() << "Descriptor [service-type=" << descriptor.service_type.name() << "] [impl-type=" << descriptor.impl_type.name() << "]";
    if(!descriptor.dependencies.empty()) {
        tmp << " with " << descriptor.dependencies.size() << " dependencies ";
        const char* sep = "";
        for(auto& dep : descriptor.dependencies) {
            tmp << sep << dep;
            sep = ", ";
        }
    }
    return out;
}

bool isBindable(const QMetaProperty& sourceProperty) {
    return sourceProperty.hasNotifySignal() || sourceProperty.isBindable();
}


std::variant<std::nullptr_t,QMetaObject::Connection,QPropertyNotifier> bindProperty(QObject* source, const QMetaProperty& sourceProperty, QObject* target, const detail::property_descriptor& setter) {
    if(sourceProperty.hasNotifySignal()) {
        detail::BindingProxy* proxy = new detail::BindingProxy{sourceProperty, source, setter, target};
        auto connection = QObject::connect(source, sourceProperty.notifySignal(), proxy, detail::BindingProxy::notifySlot());
        qCDebug(loggingCategory()).nospace().noquote() << "Bound property '" << sourceProperty.name() << "' of " << source << " to " << setter <<" of " << target;
        return std::move(connection);
    }
    if(sourceProperty.isBindable()) {
        auto sourceBindable = sourceProperty.bindable(source);
        auto notifier = sourceBindable.addNotifier([sourceProperty,source,setter,target]{
            setter.setter(target, sourceProperty.read(source));
        });
        qCDebug(loggingCategory()).nospace().noquote() << "Bound property '" << sourceProperty.name() << "' of " << source << " to " << setter << " of " << target;
        return std::move(notifier);
    }
    qCInfo(loggingCategory()).nospace().noquote() << "Could not bind property '" << sourceProperty.name() << "' of " << source << " to " << setter << " of " << target;
    return nullptr;
}

inline QDebug operator << (QDebug out, const Registration& reg) {
    reg.print(out);
    return out;
}
}

namespace {

const QRegularExpression beanRefPattern{"^&([^.]+)(\\.([^.]+))?"};


struct QOwningList {
    QObjectList list;

    QOwningList() = default;

    QOwningList(const QOwningList&) = delete;

    ~QOwningList() {
        for(auto iter = list.begin(); iter != list.end(); iter = list.erase(iter)) {
            delete *iter;
        }
    }

    void push_back(QObject* ptr) {
        if(ptr) {
            list.push_back(ptr);
        }
    }

    void moveTo(QObject* newParent) {
        for(auto iter = list.begin(); iter != list.end(); iter = list.erase(iter)) {
            (*iter)->setParent(newParent);
        }

    }
};

QMetaMethod methodByName(const QMetaObject* metaObject, const QString& name) {
    for(int i = 0; i < metaObject->methodCount(); ++i) {
        auto method = metaObject->method(i);
            if(method.name() == name.toLatin1()) {
                return method;
            }
        }
        return {};
    }


    template<typename C,typename P> auto erase_if(C& container, P predicate) -> std::enable_if_t<std::is_pointer_v<typename C::value_type>,typename C::value_type> {
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

QString makeConfigPath(const QString& group, const QString& key) {
        if(group.isEmpty()) {
            return key;
        }
        return group+"/"+key;
}

QString makeName(const std::type_info& type) {
    QString typeName{type.name()};
    typeName.replace(' ', '-');
    return typeName+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
}



}









StandardApplicationContext::StandardApplicationContext(QObject* parent) :
QApplicationContext(parent)
{
}


StandardApplicationContext::~StandardApplicationContext() {
    unpublish();
}

template<typename C> StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::find_by_type(const C& regs, const std::type_index& type) {
    auto found = std::find_if(regs.begin(), regs.end(), DescriptorRegistration::matcher(type));
    return found != regs.end() ? *found : nullptr;
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
            for(auto& t : dep->descriptor.dependencies) {
                if(reg->descriptor.matches(t.type)) {
                    published.erase(depend);
                    published.push_front(reg);
                    reg = dep;
                    goto next_published;
                }
            }
            for(auto& beanRef : getBeanRefs(reg->config())) {
                if(beanRef == reg->registeredName()) {
                    published.erase(depend);
                    published.push_front(reg);
                    reg = dep;
                    goto next_published;
                }
            }
        }
        if(reg->unpublish()) {
            ++unpublished;
            qCInfo(loggingCategory()).nospace().noquote() << "Un-published " << *reg;
        }
    }
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has been un-published. " << unpublished << " Objects have been successfully destroyed.";
    QStringList remainingNames;
    for(auto reg : registrations) {
        if(reg->isPublished() && !reg->isManaged()) {
            remainingNames.push_back(reg->registeredName());
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
    QObjectList dep;

    for(auto pub : published) {
        if(pub->matches(type)) {
            if(d.has_required_name()) {
                auto byName = getRegistrationByName(d.expression);
                if(!byName || byName->getObject() != pub->getObject()) {
                    continue;
                }
            }
            depRegs.push_back(pub);
            dep.push_back(pub->getObject());
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
            auto resolved = resolveProperty(reg->config().group, d.expression, d.value, allowPartial);
            if(resolved.second == Status::ok) {
                qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << resolved.first;
            }
            return resolved;
        }

    case static_cast<int>(Kind::MANDATORY):
        if(dep.empty()) {
            if(allowPartial) {
                qWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fixable};
            } else {
                qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fatal};
            }

        }
    case static_cast<int>(Kind::OPTIONAL):
        switch(dep.size()) {
        case 0:
            qCInfo(loggingCategory()).noquote().nospace() << "Skipped " << d;
            return {QVariant{}, Status::ok};
        case 1:
            qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << dep[0];
            return {QVariant::fromValue(dep[0]), Status::ok};
        default:
            //Ambiguity is always a non-fixable error:
            qCritical(loggingCategory()).noquote().nospace() << d << " is ambiguous";
            return {QVariant{}, Status::fatal};
        }
    case static_cast<int>(Kind::N):
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << dep.size() << " objects.";
        return {QVariant::fromValue(dep), Status::ok};

    case static_cast<int>(Kind::PRIVATE_COPY):
        DescriptorRegistration* depReg = nullptr;
        switch(dep.size()) {
        case 0:
            break;
        case 1:
            depReg = depRegs[0];
            break;
        default:
            if(d.has_required_name()) {
                for(auto r : depRegs) {
                    if(r->registeredName() == d.expression) {
                        depReg = r;
                        break;
                    }
                }
            }
            if(!depReg) {
                //Ambiguity is always a non-fixable error:
                qCritical(loggingCategory()).noquote().nospace() << d << " is ambiguous";
                return {QVariant{}, Status::fatal};
            }
        }
        if(!depReg) {
            if(!d.defaultConstructor) {
                if(allowPartial) {
                    qWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                    //Unresolvable dependencies may be fixable by registering more services:
                    return {QVariant{}, Status::fixable};
                } else {
                    qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                    return {QVariant{}, Status::fatal};
                }
            }
            depReg = new ServiceRegistration{"", service_descriptor{type, type, nullptr, d.defaultConstructor}, service_config{}, this};
        }
        QVariantList subDep;
        QOwningList privateSubDep;
        for(auto& dd : depReg->descriptor.dependencies) {
            auto result = resolveDependency(published, depReg, dd, allowPartial);
            if(result.second != Status::ok) {
                return result;
            }
            subDep.push_back(result.first);
            if(dd.kind == static_cast<int>(Kind::PRIVATE_COPY)) {
                privateSubDep.push_back(result.first.value<QObject*>());
            }
        }
        QObject* service = depReg->createPrivateObject(subDep);
        if(!service) {
            //If creation fails, this is always a non-fixable error:
            qCCritical(loggingCategory()).noquote().nospace() << "Could not create private copy of " << d;
            return {QVariant{}, Status::fatal};
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Created private copy of " << *depReg;
        privateSubDep.moveTo(service);
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved dependency " << d << " with " << service;
        return {QVariant::fromValue(service), Status::ok};
    }

    return {QVariant{}, Status::fatal};
}





detail::ServiceRegistration *StandardApplicationContext::getRegistration(const type_info &service_type, const QString& name) const
{
    DescriptorRegistration* reg = getRegistrationByName(name);
    if(reg && reg->matches(service_type)) {
        return reg;
    }
    qCCritical(loggingCategory()).noquote().nospace() << "Could not find a Registration for name '" << name << "' and service-type " << service_type.name();
    return nullptr;
}

detail::ProxyRegistration *StandardApplicationContext::getRegistration(const type_info &service_type, LookupKind lookup, q_predicate_t dynamicCheck, const QMetaObject* metaObject) const
{
    ProxyRegistration* proxyReg;
    auto found = proxyRegistrationCache.find({service_type, lookup});
    if(found != proxyRegistrationCache.end()) {
        return found->second;
    }
    switch(lookup) {
    case LookupKind::STATIC:
         proxyReg = new StaticProxyRegistration{service_type, metaObject, const_cast<StandardApplicationContext*>(this)};
        break;
    case LookupKind::DYNAMIC:
        proxyReg = new DynamicProxyRegistration{service_type, dynamicCheck, metaObject, const_cast<StandardApplicationContext*>(this)};
        break;
    }
    for(auto reg : registrations) {
        proxyReg->add(reg);
    }
    proxyRegistrationCache.insert({{service_type, lookup}, proxyReg});
    return proxyReg;
}






QStringList StandardApplicationContext::getBeanRefs(const service_config& config)
{
    QStringList result;
    for(auto entry : config.properties.asKeyValueRange()) {
        auto key = entry.second.toString();
        if(key.startsWith('&')) {
            int dot = key.indexOf('.');
            if(dot < 0) {
                dot = key.size();
            }
            result.push_back(key.mid(1, dot-1));
        }
    }
    return result;
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
            for(LookupKind lookup : {LookupKind::STATIC, LookupKind::DYNAMIC}) {
                auto proxy = proxyRegistrationCache.find({regPtr->service_type(), lookup});
                if(proxy != proxyRegistrationCache.end()) {
                    proxy -> second->remove(regPtr.get());
                }
            }
        } else {
            ++iter;
        }
    }
}



bool StandardApplicationContext::publish(bool allowPartial)
{
    descriptor_list allPublished;
    descriptor_list publishedNow;//Keep order of publication

    descriptor_list unpublished;
    descriptor_set unresolvable;
    for(auto reg : registrations) {
        if(reg->isPublished()) {
            allPublished.push_back(reg);
        } else {
            unpublished.push_back(reg);
        }
    }

    qCInfo(loggingCategory()).noquote().nospace() << "Publish ApplicationContext with " << unpublished.size() << " unpublished Objects";
    std::vector<std::size_t> posStack;
    DescriptorRegistration* reg = nullptr;
    //Do several rounds and publish those services whose dependencies have already been published.
    //For a service with an empty set of dependencies, this means that it will be published first.
    while(!unpublished.empty()) {
        reg = pop_front(unpublished);

        next_unpublished:
        for(auto& beanRef : getBeanRefs(reg->config())) {
            if(!getRegistrationByName(beanRef)) {
                if(!allowPartial) {
                    qCCritical(loggingCategory()).noquote().nospace() << *reg << " is unresolvable. References Object '" << beanRef << "', but no such Object has been registered.";
                    return false;
                }
                qCWarning(loggingCategory()).noquote().nospace() << *reg << " is unresolvable. References Object '" << beanRef << "', but no such Object has been registered.";
                unresolvable.insert(reg);
                reg = pop_front(unpublished);
                goto next_unpublished;
            }
        }

        QVariantList dependencies;
        QOwningList privateDependencies;
        auto& dependencyInfos = reg->descriptor.dependencies;
        for(auto& d : dependencyInfos) {
            //If we find an unpublished dependency, we continue with that:
            auto foundReg = erase_if(unpublished, DescriptorRegistration::matcher(d.type));
            if(foundReg) {
                unpublished.push_front(reg); //Put the current Registration back where it came from. Will be processed after the dependency.
                reg = foundReg;
                goto next_unpublished;
            }
            //If we find a mandatory dependency for which there is a default-constructor, we continue with that:
            if(!find_by_type(allPublished, d.type) && d.kind == static_cast<int>(Kind::MANDATORY) && d.defaultConstructor) {
                auto def = registerDescriptor("", service_descriptor{d.type, d.type, nullptr, d.defaultConstructor}, service_config{}, nullptr);
                if(def) {
                    unpublished.push_front(reg);
                    reg = def;
                    qCInfo(loggingCategory()).noquote().nospace() << "Creating default-instance of " << d << " for " << *reg;
                    goto next_unpublished;
                }
            }
        }
        if(!dependencyInfos.empty()) {
            qCInfo(loggingCategory()).noquote().nospace() << "Resolving " << dependencyInfos.size() << " dependencies of " << *reg << ":";
            for(auto& d : dependencyInfos) {
                auto result = resolveDependency(allPublished, reg, d, allowPartial);
                switch(result.second) {
                case Status::fatal:
                    return false;
                case Status::fixable:
                    if(!allowPartial) {
                        return false;
                    }
                    unresolvable.insert(reg);
                    reg = pop_front(unpublished);
                    goto next_unpublished;
                }
                dependencies.push_back(result.first);
                if(d.kind == static_cast<int>(Kind::PRIVATE_COPY)) {
                    privateDependencies.push_back(result.first.value<QObject*>());
                }
            }
        }
        auto service = reg->publish(dependencies);
        if(!service) {
            qCCritical(loggingCategory()).nospace().noquote() << "Could not publish " << *reg;
            return false;
        }
        privateDependencies.moveTo(service);
        if(service->objectName().isEmpty()) {
            service->setObjectName(reg->registeredName());
        }
        qCInfo(loggingCategory()).nospace().noquote() << "Published " << *reg;
        publishedNow.push_back(reg);
        //By building the list of published services from scratch, we guarantee that they'll end up in Kind::N-dependencies in the right order:
        allPublished.clear();
        std::copy_if(registrations.begin(), registrations.end(), std::inserter(allPublished, allPublished.begin()), std::mem_fn(&DescriptorRegistration::isPublished));
    }
    qsizetype publishedCount = 0;
    QList<QApplicationContextPostProcessor*> postProcessors;
    for(auto reg : allPublished) {
        if(auto processor = dynamic_cast<QApplicationContextPostProcessor*>(reg->getObject())) {
            postProcessors.push_back(processor);
            qCInfo(loggingCategory()).noquote().nospace() << "Detected PostProcessor " << *reg;
        }
    }
    //Move PostProcessors to the front, so that they will be configured before they process other Services:
    for(unsigned moved = 0, pos = 1; pos < publishedNow.size(); ++pos) {
        if(dynamic_cast<QApplicationContextPostProcessor*>(publishedNow[pos]->getObject())) {
            std::swap(publishedNow[moved++], publishedNow[pos]);
        }
    }
    //The services that have been instantiated during this methd-invocation will be configured in the order they have have been
    //instantiated. However, if their configuration has bean-refs, this order may need to be modified.
    while(!publishedNow.empty()) {
        reg = pop_front(publishedNow);
        next_published:
        for(auto& beanRef : getBeanRefs(reg->config())) {
            auto foundReg = erase_if(publishedNow, [&beanRef](DescriptorRegistration* r) { return r->registeredName() == beanRef;});
            if(foundReg) {
                publishedNow.push_front(reg);//Put the current Registration back where it came from. Will be processed after the dependency.
                reg = foundReg;
                goto next_published;
            }
        }

        auto service = reg->getObject();
        if(service) {
            for(auto privateObj : reg->privateObjects()) {
                auto configResult = configure(reg, privateObj, postProcessors, allowPartial);
                switch(configResult) {
                case Status::fatal:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not configure private copy of " << *reg;
                    return false;
                case Status::fixable:
                    qCWarning(loggingCategory()).nospace().noquote() << "Could not configure private copy of " << *reg;
                    break;
                case Status::ok:
                    qCInfo(loggingCategory()).noquote().nospace() << "Configured private copy of " << *reg;
                }

            }
            auto configResult = configure(reg, service, postProcessors, allowPartial);
            switch(configResult) {
            case Status::fatal:
                qCCritical(loggingCategory()).nospace().noquote() << "Could not configure " << *reg;
                return false;
            case Status::fixable:
                qCWarning(loggingCategory()).nospace().noquote() << "Could not configure " << *reg;
                unresolvable.insert(reg);
                erase_if(allPublished, [reg](DescriptorRegistration* arg) { return arg == reg; });
                continue;

            case Status::ok:
                qCInfo(loggingCategory()).noquote().nospace() << "Configured " << *reg;
                ++publishedCount;
                reg->notifyPublished();
            }
        }
    }
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has published " << publishedCount << " objects";
    unsigned managed = std::count_if(registrations.begin(), registrations.end(), std::mem_fn(&DescriptorRegistration::isManaged));
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has a total number of " << allPublished.size() << " published objects of which " << managed << " are managed.";
    if(!unpublished.empty()) {
        qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has " << unpublished.size() << " unpublished objects";
    }

    if(publishedCount) {
        emit publishedChanged();
        emit pendingPublicationChanged();
    }
    if(allowPartial) {
        return publishedCount != 0;
    }
    return unresolvable.empty();
}

unsigned StandardApplicationContext::published() const
{
    return std::count_if(registrations.begin(), registrations.end(), std::mem_fn(&DescriptorRegistration::isPublished));
}

unsigned int StandardApplicationContext::pendingPublication() const
{
    return std::count_if(registrations.begin(), registrations.end(), std::not_fn(std::mem_fn(&DescriptorRegistration::isPublished)));
}

StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::registerDescriptor(QString name, const service_descriptor& descriptor, const service_config& config, QObject* obj) {
    bool isAnonymous = name.isEmpty();
    if(isAnonymous) {
        name = makeName(descriptor.service_type);
    } else {
        auto found = getRegistrationByName(name);
        if(found) {
            if(found->isEqual(descriptor, config, obj)) {
                qCInfo(loggingCategory()).nospace().noquote() << "A Service with an equivalent " << descriptor << " has already been registered as " << *found;
                return found;
            } else {
                qCCritical(loggingCategory()).nospace().noquote() << "Cannot register " << descriptor << " as '" << name << "'. That name has already been taken by " << *found;
                return nullptr;
            }
        }
    }

    for(auto reg : registrations) {
        if(reg->isEqual(descriptor, config, obj)) {
            if(isAnonymous) {
                qCInfo(loggingCategory()).nospace().noquote() << "A Service with an equivalent " << descriptor << " has already been registered as " << *reg;
                return reg;
            }
            registrationsByName.insert({name, reg});
            qCInfo(loggingCategory()).nospace().noquote() << "Created alias '" << name << "' for " << *reg;
            return reg;
        }
    }

    std::unordered_set<std::type_index> dependencies;

    findTransitiveDependenciesOf(descriptor, dependencies);

    if(!checkTransitiveDependentsOn(descriptor, dependencies)) {
        qCCritical(loggingCategory()).nospace().noquote() <<  "Cannot register '" << name << "'. Cyclic dependency in dependency-chain of " << descriptor;
        return nullptr;

    }

    if(descriptor.meta_object) {
        for(auto& key : config.properties.keys()) {
            if(!key.startsWith('.') && descriptor.meta_object->indexOfProperty(key.toLatin1()) < 0) {
                qCCritical(loggingCategory()).nospace().noquote() << "Cannot register " << descriptor << " as '" << name << "'. Service-type has no property '" << key << "'";
                return nullptr;
            }
        }
        if(!config.initMethod.isEmpty()) {
            auto initMethod = methodByName(descriptor.meta_object, config.initMethod);
            if(!initMethod.isValid() || initMethod.parameterCount() > 1) {
                qCCritical(loggingCategory()).nospace().noquote() << "Cannot register " << descriptor << " as '" << name << "'. Service-type has no invokable method '" << config.initMethod << "'";
                return nullptr;
            }
        }
    }

    DescriptorRegistration* registration;
    if(obj) {
        registration = new ObjectRegistration{name, descriptor, obj, this};
    } else {
        registration = new ServiceRegistration{name, descriptor, config, this};
    }
    registrationsByName.insert({name, registration});
    registrations.push_back(registration);
    for(LookupKind lookup : {LookupKind::STATIC, LookupKind::DYNAMIC}) {
        auto proxy = proxyRegistrationCache.find({registration->service_type(), lookup});
        if(proxy != proxyRegistrationCache.end()) {
            proxy->second->add(registration);
        }
    }
    qCInfo(loggingCategory()).noquote().nospace() << "Registered " << *registration;
    emit pendingPublicationChanged();
    return registration;


}

detail::ServiceRegistration* StandardApplicationContext::registerService(const QString& name, const service_descriptor& descriptor, const service_config& config)
{
    return registerDescriptor(name, descriptor, config, nullptr);
}

detail::ServiceRegistration * StandardApplicationContext::registerObject(const QString &name, QObject *obj, const service_descriptor& descriptor)
{
    if(!obj) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register null-object for " << descriptor.service_type.name();
        return nullptr;
    }

    return registerDescriptor(name.isEmpty() ? obj->objectName() : name, descriptor, ObjectRegistration::defaultConfig, obj);
}




void StandardApplicationContext::findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<std::type_index>& result) const
{
    for(auto& t : descriptor.dependencies) {
        if(find_by_type(registrations, t.type)) {
            result.insert(t.type);
            for(auto reg : registrations) {
                if(reg->descriptor.matches(t.type)) {
                    findTransitiveDependenciesOf(reg->descriptor, result);
                }
            }
        }
    }

}



bool StandardApplicationContext::checkTransitiveDependentsOn(const service_descriptor& descriptor, const std::unordered_set<std::type_index>& dependencies) const
{
    for(auto reg : registrations) {
        for(auto& t : reg->descriptor.dependencies) {
            if(descriptor.matches(t.type)) {
                if(std::find_if(dependencies.begin(), dependencies.end(), [reg](auto dep) { return reg->matches(dep);}) != dependencies.end()) {
                   return false;
                }
                if(!checkTransitiveDependentsOn(reg->descriptor, dependencies)) {
                    return false;
                }
            }
        }
    }
    return true;
}


StandardApplicationContext::ResolvedBeanRef StandardApplicationContext::resolveBeanRef(const QVariant &value, bool allowPartial)
{
    if(!value.isValid()) {
        return {value, Status::fatal, false};
    }
    QString key = value.toString();
    auto match = beanRefPattern.match(key);
    if(match.hasMatch()) {
        key = match.captured(1);
        auto bean = getRegistrationByName(key);
        if(!(bean && bean->getObject())) {
            if(allowPartial) {
                qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
                return {QVariant{}, Status::fixable, false};
            }
            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << key << "'";
            return {QVariant{}, Status::fatal, false};
        }
        QMetaProperty sourceProp;
        QObject* parent = nullptr;
        QVariant resultValue = QVariant::fromValue(bean->getObject());
        if(match.hasCaptured(3)) {
            QString propName = match.captured(3);
            parent = bean->getObject();
            if(!parent) {
                if(allowPartial) {
                    qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                    return {QVariant{}, Status::fixable, false};
                }
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                return {QVariant{}, Status::fatal, false};
            }
            sourceProp = bean->getProperty(propName.toLatin1());
            if(!sourceProp.isValid()) {
                //Refering to a non-existing Q_PROPERTY is always non-fixable:
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << parent;
                return {QVariant{}, Status::fatal, false};
            }
            resultValue = sourceProp.read(parent);
        }

        qCInfo(loggingCategory()).nospace().noquote() << "Resolved reference '" << key << "' to " << resultValue;
        return {resultValue, Status::ok, true, sourceProp, parent};
    }
    return {value, Status::ok, false};

}


std::pair<QVariant,StandardApplicationContext::Status> StandardApplicationContext::resolveProperty(const QString& group, const QVariant &valueOrPlaceholder, const QVariant& defaultValue, bool allowPartial)
{
    constexpr int STATE_INIT = 0;
    constexpr int STATE_FOUND_DOLLAR = 1;
    constexpr int STATE_FOUND_PLACEHOLDER = 2;
    constexpr int STATE_FOUND_DEFAULT_VALUE = 3;
    constexpr int STATE_ESCAPED = 4;
    if(!valueOrPlaceholder.isValid()) {
        return {valueOrPlaceholder, Status::fatal};
    }
    QString key = valueOrPlaceholder.toString();
    QVariant lastResolvedValue;
    QString resolvedString;
    QString token;
    QString defaultValueToken;
    QVariant currentDefault;

    int lastStateBeforeEscape = STATE_INIT;
    int state = STATE_INIT;
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
            case STATE_INIT:
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
                state = STATE_INIT;
                resolvedString += ch;
                continue;
            }

        case '}':
            currentDefault = defaultValue;
            switch(state) {
            case STATE_ESCAPED:
                resolvedString += '}';
                state = lastStateBeforeEscape;
                continue;
            case STATE_FOUND_DEFAULT_VALUE:
                currentDefault = defaultValueToken;

            case STATE_FOUND_PLACEHOLDER:
                if(!token.isEmpty()) {
                    QString path = makeConfigPath(group, token);
                    lastResolvedValue = getConfigurationValue(path, currentDefault);
                    if(!lastResolvedValue.isValid()) {
                        if(allowPartial) {
                            qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve configuration-key '" << path << "'";
                            return {QVariant{}, Status::fixable};
                        }
                        qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve configuration-key '" << path << "'";
                        return {QVariant{}, Status::fatal};
                    }
                    if(resolvedString.isEmpty() && pos + 1 == key.length()) {
                        return {lastResolvedValue, Status::ok};
                    }
                    resolvedString += lastResolvedValue.toString();
                    token.clear();
                    defaultValueToken.clear();
                }
                state = STATE_INIT;
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
                state = STATE_INIT;
            case STATE_INIT:
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
    case STATE_INIT:
        if(resolvedString == key) {
            return {valueOrPlaceholder, Status::ok};
        }
        return {resolvedString, Status::ok};
    case STATE_ESCAPED:
        resolvedString += '\\';
        return {resolvedString, Status::ok};
    default:
        qCCritical(loggingCategory()).nospace().noquote() << "Unbalanced placeholder '" << key << "'";
        return {QVariant{}, Status::fatal};
    }
}


StandardApplicationContext::Status StandardApplicationContext::configure(DescriptorRegistration* reg, QObject* target, const QList<QApplicationContextPostProcessor*>& postProcessors, bool allowPartial) {
    if(!target) {
        return Status::fatal;
    }
    auto metaObject = target->metaObject();
    auto& config = reg->config();
    if(metaObject) {
        std::unordered_set<QString> usedProperties;
        for(auto[key,value] : config.properties.asKeyValueRange()) {
            auto result = resolveBeanRef(value, allowPartial);
            if(result.status != Status::ok) {
                return result.status;
            }
            QVariant resolvedValue;
            if(result.resolved) {
                resolvedValue = result.resolvedValue;
            } else {
                auto propertyResult = resolveProperty(config.group, value, QVariant{}, allowPartial);
                if(propertyResult.second != Status::ok) {
                    return propertyResult.second;
                }
                resolvedValue = propertyResult.first;
            }
            reg->resolvedProperties.insert(key, resolvedValue);
            if(!key.startsWith('.')) {
                auto targetProperty = metaObject->property(metaObject->indexOfProperty(key.toLatin1()));
                if(!targetProperty.isValid() || !targetProperty.isWritable()) {
                    //Refering to a non-existing Q_PROPERTY by name is always non-fixable:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not find writable property " << key << " of '" << metaObject->className() << "'";
                    return Status::fatal;
                }
                if(targetProperty.write(target, resolvedValue)) {
                    qCDebug(loggingCategory()).nospace().noquote() << "Set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    usedProperties.insert(key);
                    if(result.sourceProperty.isValid() && result.source) {
                        auto notifier = detail::bindProperty(result.source, result.sourceProperty, target, detail::propertySetter(targetProperty));
                        if(std::holds_alternative<QPropertyNotifier>(notifier)) {
                            reg->bindings.push_back(std::get<QPropertyNotifier>(std::move(notifier)));
                        }
                    }
                } else {
                    //An error while setting a Q_PROPERTY is always non-fixable:
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    return Status::fatal;
                }
            }
        }
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
                QString propTypeName = propType.name();
                auto propObjectType = QMetaType::fromName(propTypeName.first(propTypeName.length()-1).toUtf8()); //Remove the '*'
                DescriptorRegistration* candidate = getRegistrationByName(prop.name()); //First, try by name
                if(!(candidate && QMetaType::canConvert(candidate->getObject()->metaObject()->metaType(), propObjectType))) {
                    //No matching name found, now we iterate over all registrations:
                    for(auto reg : registrations) {
                        auto obj = reg->getObject();
                        if(!obj || obj == target) {
                            continue;
                        }
                        if(QMetaType::canConvert(obj->metaObject()->metaType(), propObjectType)) {
                            candidate = reg;
                            break;
                        }
                    }
                }
                if(candidate) {
                    if(prop.write(target, QVariant::fromValue(candidate->getObject()))) {
                        qCInfo(loggingCategory()).nospace() << "Autowired property '" << prop.name() << "' of " << *reg << " to " << *candidate;
                        break;
                    } else {
                        qCInfo(loggingCategory()).nospace().noquote() << "Could not autowire property '" << prop.name()  << "' of " << *reg << " to " << *candidate;
                    }
                }

            }
        }

    }


    for(auto processor : postProcessors) {
        if(processor != dynamic_cast<QApplicationContextPostProcessor*>(target)) {
            //Don't process yourself!
            processor->process(this, target, reg->resolvedProperties);
        }
    }

    if(!config.initMethod.isEmpty()) {
        QMetaMethod method = methodByName(metaObject, config.initMethod);
        if(!method.isValid()) {
            //Referingi to a non-existing init-method is always non-fixable:
            qCCritical(loggingCategory()).nospace().noquote() << "Could not find init-method '" << config.initMethod << "'";
            return Status::fatal;

        }
        switch(method.parameterCount()) {
        case 0:
            if(method.invoke(target)) {
                qCInfo(loggingCategory()).nospace().noquote() << "Invoked init-method '" << config.initMethod << "' of " << *reg;
                return Status::ok;
            }
            break;
        case 1:
            if(method.invoke(target, Q_ARG(QApplicationContext*,this))) {
                qCInfo(loggingCategory()).nospace().noquote() << "Invoked init-method '" << config.initMethod << "' of " << *reg << ", passing the ApplicationContext";
                return Status::ok;
            }
            break;
       }
       qCCritical(loggingCategory()).nospace().noquote() << "Could not invoke init-method '" << method.methodSignature() << "' of " << *reg;
       return Status::fatal;
    }
    return Status::ok;
}





QVariant StandardApplicationContext::getConfigurationValue(const QString& key, const QVariant& defaultValue) const {
    for(auto reg : registrations) {
        if(QSettings* settings = dynamic_cast<QSettings*>(reg->getObject())) {
            auto value = settings->value(key);
            if(value.isValid()) {
                qCDebug(loggingCategory()).noquote().nospace() << "Obtained configuration-entry: " << key << " = " << value << " from " << settings->fileName();
                return value;
            }
        }
    }
    qCDebug(loggingCategory()).noquote().nospace() << "Use default-value for configuration-entry: " << key << " = " << defaultValue;
    return defaultValue;
}

const service_config StandardApplicationContext::ObjectRegistration::defaultConfig;


detail::Subscription* StandardApplicationContext::DescriptorRegistration::createBindingTo(const char* sourcePropertyName, detail::Registration *target, const detail::property_descriptor& targetProperty)
{
    detail::property_descriptor setter = targetProperty;
    auto targetReg = dynamic_cast<StandardRegistrationImpl*>(target);
    if(!targetReg) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to " << *target;
        return nullptr;
    }
    if(this == target && QString{sourcePropertyName} == setter.name) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to self";
        return nullptr;
    }

    if(target->applicationContext() != applicationContext()) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to " << *target << " from different ApplicationContext";
        return nullptr;
    }

    auto sourceProperty = getProperty(sourcePropertyName);
    if(!detail::isBindable(sourceProperty)) {
        qCCritical(loggingCategory()).noquote().nospace() << "Property '" << sourcePropertyName << "' in " << *this << " is not bindable";
        return nullptr;
    }
    if(!setter.setter) {
        auto targetProperty = targetReg->getProperty(setter.name);
        if(!targetProperty.isValid() || !targetProperty.isWritable()) {
            qCCritical(loggingCategory()).noquote().nospace() << setter << " is not a writable property for " << *target;
            return nullptr;
        }
        if(!QMetaType::canConvert(sourceProperty.metaType(), targetProperty.metaType())) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot bind property '" << sourcePropertyName << "' of " << *this << " to " << setter << " of " << *target << " with incompatible types";
            return nullptr;
        }
        setter = detail::propertySetter(targetProperty);
    }
    if(!targetReg->registerBoundProperty(setter.name)) {
        qCCritical(loggingCategory()).noquote().nospace() << setter << " has already been bound to " << *target;
        return nullptr;

    }


    auto subscription = new PropertyBindingSubscription{this, target, sourceProperty, setter};
    qCInfo(loggingCategory()).noquote().nospace() << "Created Subscription for binding property '" << sourceProperty.name() << "' of " << *this << " to " << setter << " of " << *target;
    return subscription;
}





StandardApplicationContext::DescriptorRegistration::DescriptorRegistration(const QString& name, const service_descriptor& desc, StandardApplicationContext* parent) :
    detail::ServiceRegistration(parent),
    descriptor{desc},
    m_name(name)
{
}







    void StandardApplicationContext::ServiceRegistration::print(QDebug out) const {
       out.nospace().noquote() << "Service '" << registeredName() << "' with " << this->descriptor;
    }

    void StandardApplicationContext::ServiceRegistration::serviceDestroyed(QObject *srv) {
       if(srv == theService) {
           //Somebody has destroyed a Service that is managed by this ApplicationContext.
           //All we can do is log an error and set theService to nullptr.
           //Yet, it might still be in use somewhere as a dependency.
           qCritical(loggingCategory()).noquote().nospace() << *this << " has been destroyed externally";
           theService = nullptr;
       }
    }

    void StandardApplicationContext::ObjectRegistration::print(QDebug out) const {
       out.nospace().noquote() << "Object '" << registeredName() << "' with " << this->descriptor;
    }







    void StandardApplicationContext::DescriptorRegistration::PropertyBindingSubscription::notify(QObject* obj) {
       subscriptions.push_back(subscribe( new PropertyInjector{m_target, obj, m_sourceProperty, m_setter}));
    }

    void StandardApplicationContext::DescriptorRegistration::PropertyBindingSubscription::cancel() {
       Subscription::cancel();
       for(auto iter = subscriptions.begin(); iter != subscriptions.end(); iter = subscriptions.erase(iter)) {
           auto subscription = *iter;
           if(subscription) {
              subscription->cancel();
           }
       }
    }

    void StandardApplicationContext::DescriptorRegistration::PropertyInjector::notify(QObject* target) {
       m_setter.setter(target, m_sourceProperty.read(m_boundSource));
       auto notifier = detail::bindProperty(m_boundSource, m_sourceProperty, target, m_setter);
       if(std::holds_alternative<QPropertyNotifier>(notifier)) {
           bindings.push_back(std::get<QPropertyNotifier>(std::move(notifier)));
       }
       if(std::holds_alternative<QMetaObject::Connection>(notifier)) {
           connections.push_back(std::get<QMetaObject::Connection>(notifier));
       }

    }

    void StandardApplicationContext::DescriptorRegistration::PropertyInjector::cancel() {
       Subscription::cancel();
       for(auto iter = connections.begin(); iter != connections.end(); iter = connections.erase(iter)) {
           QObject::disconnect(*iter);
       }
       //QPropertyNotifier will remove the binding in its destructor:
       bindings.clear();
    }

    }//mcnepp::qtdi
