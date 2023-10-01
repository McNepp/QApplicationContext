#include <QThread>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QUuid>
#include <QRegularExpression>
#include <QSettings>
#include "standardqapplicationcontext.h"

namespace mcnepp::qtdi {




namespace {
struct QOwningList : QObjectList {
    QOwningList() = default;

    QOwningList(const QOwningList&) = delete;

    ~QOwningList() {
        for(auto iter = begin(); iter != end(); iter = erase(iter)) {
            delete *iter;
        }
    }

    void moveTo(QObject* newParent) {
        for(auto iter = begin(); iter != end(); iter = erase(iter)) {
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


}

inline QString cardinalityToString(Cardinality card) {
    switch(card) {
    case Cardinality::N:
        return "N";
    case Cardinality::OPTIONAL:
        return "optional";
    case Cardinality::MANDATORY:
        return "mandatory";
    case Cardinality::PRIVATE_COPY:
        return "private copy";
    default:
        return "unknown";

    }
}

inline QDebug operator<<(QDebug out, const QApplicationContext::dependency_info& info) {
    QDebug tmp = out.noquote().nospace() << "Dependency '" << info.type.name() << "' [" << cardinalityToString(info.cardinality) << ']';
    if(!info.requiredName.isEmpty()) {
        return tmp << "] with required name '" << info.requiredName << "'";
    }
    return tmp << ']';
}

inline QDebug operator << (QDebug out, const detail::service_descriptor& descriptor) {
    return out.nospace().noquote() << "Object with service-type '" << descriptor.service_type.name() << "' and impl-type '" << descriptor.impl_type.name() << "'";
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
    std::copy_if(registrations.rbegin(), registrations.rend(), std::inserter(published, published.begin()), std::mem_fn(&DescriptorRegistration::isPublished));

    qCInfo(loggingCategory()).noquote().nospace() << "Un-publish ApplicationContext with " << published.size() << " published Objects";

    DescriptorRegistration* reg = nullptr;
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
            for(auto& beanRef : getBeanRefs(reg->descriptor.config)) {
                if(beanRef == reg->name()) {
                    published.erase(depend);
                    published.push_front(reg);
                    reg = dep;
                    goto next_published;
                }
            }
        }
        if(reg->unpublish()) {
            qCInfo(loggingCategory()).nospace().noquote() << "Un-published " << *reg;
        }
    }
    qCInfo(loggingCategory()) << "ApplicationContext has been un-published";
}

StandardApplicationContext::DescriptorRegistration *StandardApplicationContext::getRegistrationByName(const QString &name) const
{

    auto found = registrationsByName.find(name);
    return found != registrationsByName.end() ? found->second : nullptr;
}


std::pair<QObject*,StandardApplicationContext::Status> StandardApplicationContext::resolveDependency(const descriptor_list &published, descriptor_list& publishedNow, DescriptorRegistration* reg, const dependency_info& d, QObject* temporaryParent, bool allowPartial)
{
    const std::type_info& type = d.type;

    QObjectList dep;

    for(auto pub : published) {
        if(pub->matches(type)) {
            if(!d.requiredName.isEmpty()) {
                auto byName = getRegistrationByName(d.requiredName);
                if(!byName || byName->getObject() != pub->getObject()) {
                    continue;
                }
            }
            dep.push_back(pub->getObject());
        }
    }

    switch(d.cardinality) {
    case Cardinality::MANDATORY:
        if(dep.empty()) {
            if(allowPartial) {
                qWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d << " of " << *reg;
                return {nullptr, Status::fixable};
            } else {
                qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d << " of " << *reg;
                return {nullptr, Status::fatal};
            }

        }
    case Cardinality::OPTIONAL:
        switch(dep.size()) {
        case 0:
            return {nullptr, Status::ok};
        case 1:
            qCInfo(loggingCategory()).noquote().nospace() << "Resolved dependency " << d << " of " << *reg;
            return {dep[0], Status::ok};
        default:
            //Ambiguity is always a non-fixable error:
            qCritical(loggingCategory()).noquote().nospace() << d << "' of " << *reg << " is ambiguous";
            return {nullptr, Status::fatal};
        }
    case Cardinality::N:
        return {detail::wrapList(dep, temporaryParent), Status::ok};

    case Cardinality::PRIVATE_COPY:
        DescriptorRegistration* depReg;
        switch(dep.size()) {
        case 0:
            depReg = find_by_type(publishedNow, type);
            break;
        case 1:
            depReg = find_by_type(published, type);
            break;
        default:
            //Ambiguity is always a non-fixable error:
            qCritical(loggingCategory()).noquote().nospace() << d << "' of " << *reg << " is ambiguous";
            return {nullptr, Status::fatal};
        }
        if(!depReg) {
            if(!d.defaultConstructor) {
                if(allowPartial) {
                    qWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d << " of " << *reg;
                    //Unresolvable dependencies may be fixable by registering more services:
                    return {nullptr, Status::fixable};
                } else {
                    qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d << " of " << *reg;
                    return {nullptr, Status::fatal};
                }
            }
            depReg = new ServiceRegistration{"", service_descriptor{type, type, d.defaultConstructor}, this};
            publishedNow.push_back(depReg);
        }
        QObjectList subDep;
        QOwningList privateSubDep;
        for(auto& dd : depReg->descriptor.dependencies) {
            auto result = resolveDependency(published, publishedNow, depReg, dd, temporaryParent, allowPartial);
            if(result.second != Status::ok) {
                return result;
            }
            subDep.push_back(result.first);
            if(dd.cardinality == Cardinality::PRIVATE_COPY) {
                privateSubDep.push_back(result.first);
            }
        }
        QObject* service = depReg->createPrivateObject(subDep);
        if(!service) {
            //If creation fails, this is always a non-fixable error:
            qCCritical(loggingCategory()).noquote().nospace() << "Could not create private copy of " << d << " for " << *reg;
            return {nullptr, Status::fatal};
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Created private copy of " << *depReg << " for " << *reg;
        privateSubDep.moveTo(service);
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved dependency " << d << " of " << *reg;
        return {service, Status::ok};
    }

    return {nullptr, Status::fatal};
}





Registration *StandardApplicationContext::getRegistration(const type_info &service_type) const
{
    auto found = proxyRegistrationCache.find(service_type);
    ProxyRegistration* multiReg;
    if(found != proxyRegistrationCache.end()) {
        multiReg = found->second;
    } else {
        multiReg = new ProxyRegistration{service_type, const_cast<StandardApplicationContext*>(this)};
        for(auto reg : registrations) {
            if(reg->matches(service_type)) {
                multiReg->add(reg);
            }
        }
        proxyRegistrationCache.insert({service_type, multiReg});
    }
    return multiReg;

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
        auto reg = *iter;
        if(reg->getObject() == obj) {
            iter = registrations.erase(iter);
            delete reg;
        } else {
            ++iter;
        }
    }
}



bool StandardApplicationContext::publish(bool allowPartial)
{
    QObject temporaryParent;//Will manage temporary QObjects obtained by detail::wrapList(const QObjectList&).

    descriptor_list allPublished;
    descriptor_list publishedNow;//Keep order of publication

    std::copy_if(registrations.begin(), registrations.end(), std::inserter(allPublished, allPublished.begin()), std::mem_fn(&DescriptorRegistration::isPublished));

    descriptor_list unpublished;
    descriptor_set unresolvable;
    std::copy_if(registrations.begin(), registrations.end(), std::inserter(unpublished, unpublished.begin()), std::not_fn(std::mem_fn(&DescriptorRegistration::isPublished)));

    qCInfo(loggingCategory()).noquote().nospace() << "Publish ApplicationContext with " << unpublished.size() << " unpublished Objects";
    std::vector<std::size_t> posStack;
    DescriptorRegistration* reg = nullptr;
    //Do several rounds and publish those services whose dependencies have already been published.
    //For a service with an empty set of dependencies, this means that it will be published first.
    while(!unpublished.empty()) {
        reg = pop_front(unpublished);

        next_unpublished:
        for(auto& beanRef : getBeanRefs(reg->descriptor.config)) {
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

        QObjectList dependencies;
        QOwningList privateDependencies;
        for(auto& d : reg->descriptor.dependencies) {
            //If we find an unpublished dependency, we continue with that:
            auto foundReg = erase_if(unpublished, DescriptorRegistration::matcher(d.type));
            if(foundReg) {
                unpublished.push_front(reg); //Put the current Registration back where it came from. Will be processed after the dependency.
                reg = foundReg;
                goto next_unpublished;
            }
            //If we find a mandatory dependency for which there is a default-constructor, we continue with that:
            if(!find_by_type(allPublished, d.type) && d.cardinality == Cardinality::MANDATORY && d.defaultConstructor) {
                auto def = registerDescriptor("", service_descriptor{d.type, d.type, d.defaultConstructor}, nullptr);
                if(def.second) {
                    unpublished.push_front(reg);
                    reg = def.first;
                    qCInfo(loggingCategory()).noquote().nospace() << "Creating default-instance of " << d << " for " << *reg;
                    goto next_unpublished;
                }
            }
        }
        for(auto& d : reg->descriptor.dependencies) {
            auto result = resolveDependency(allPublished, publishedNow, reg, d, &temporaryParent, allowPartial);
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
            if(d.cardinality == Cardinality::PRIVATE_COPY) {
                privateDependencies.push_back(result.first);
            }
        }
        auto service = reg->publish(dependencies);
        if(!service) {
            qCCritical(loggingCategory()).nospace().noquote() << "Could not publish " << *reg;
            return false;
        }
        privateDependencies.moveTo(service);
        if(service->objectName().isEmpty()) {
            service->setObjectName(reg->name());
        }
        qCInfo(loggingCategory()).nospace().noquote() << "Published " << *reg;
        publishedNow.push_back(reg);
        //By building the list of published services from scratch, we guarantee that they'll end up in Cardinality::N-dependencies in the right order:
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
        for(auto& beanRef : getBeanRefs(reg->descriptor.config)) {
            auto foundReg = erase_if(publishedNow, [&beanRef](DescriptorRegistration* r) { return r->name() == beanRef;});
            if(foundReg) {
                publishedNow.push_front(reg);//Put the current Registration back where it came from. Will be processed after the dependency.
                reg = foundReg;
                goto next_published;
            }
        }

        auto service = reg->getObject();
        if(service) {
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
    }
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has published " << publishedCount << " objects";
    if(publishedCount != allPublished.size()) {
        qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has a total number of " << allPublished.size() << " published objects.";
    }
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

std::pair<StandardApplicationContext::DescriptorRegistration*,bool> StandardApplicationContext::registerDescriptor(QString name, const service_descriptor& descriptor, QObject* obj) {
    bool isAnonymous = name.isEmpty();
    if(isAnonymous) {
        name = QString{descriptor.service_type.name()}+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    } else {
        auto found = getRegistrationByName(name);
        if(found) {
            if(found->isEqual(descriptor, obj)) {
                qCInfo(loggingCategory()).nospace().noquote() << descriptor << " has already been registered";
                return {found, false};
            } else {
                qCCritical(loggingCategory()).nospace().noquote() << descriptor << " has already been registered as " << *found;
                return {nullptr, false};
            }
        }
    }

    for(auto reg : registrations) {
        if(reg->isEqual(descriptor, obj)) {
            if(isAnonymous) {
                qCInfo(loggingCategory()).nospace().noquote() << descriptor << " has already been registered";
                return {reg, false};
            }
            registrationsByName.insert({name, reg});
            qCInfo(loggingCategory()).nospace().noquote() << "Created alias '" << name << "' for " << *reg;
            return {reg, false};
        }
    }

    std::unordered_set<std::type_index> dependencies;

    findTransitiveDependenciesOf(descriptor, dependencies);

    if(!checkTransitiveDependentsOn(descriptor, dependencies)) {
        qCCritical(loggingCategory()).nospace() << "Cyclic dependency in dependency-chain of " << descriptor;
        return {nullptr, false};

    }

    DescriptorRegistration* registration;
    if(obj) {
        registration = new ObjectRegistration{name, descriptor, obj, this};
    } else {
        registration = new ServiceRegistration{name, descriptor, this};
    }
    registrationsByName.insert({name, registration});
    registrations.push_back(registration);
    auto proxy = proxyRegistrationCache.find(registration->service_type());
    if(proxy != proxyRegistrationCache.end()) {
        proxy->second->add(registration);
    }
    emit pendingPublicationChanged();
    return {registration, true};


}

Registration* StandardApplicationContext::registerService(const QString& name, const service_descriptor& descriptor)
{
    auto result = registerDescriptor(name, descriptor, nullptr);
    return result.first;
}

Registration * StandardApplicationContext::registerObject(const QString &name, QObject *obj, const service_descriptor& descriptor)
{
    if(!obj) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register null-object for " << descriptor.service_type.name();
        return nullptr;
    }

    auto result = registerDescriptor(name.isEmpty() ? obj->objectName() : name, descriptor, obj);
    if(result.second) {
        connect(obj, &QObject::destroyed, this, &StandardApplicationContext::contextObjectDestroyed);
    }
    return result.first;

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

std::pair<QVariant,StandardApplicationContext::Status> StandardApplicationContext::resolveValue(const QVariant &value, bool allowPartial)
{
    if(!value.isValid()) {
        return {value, Status::fatal};
    }
    QString key = value.toString();
    if(key.startsWith('&')) {
        key = key.last(key.size()-1);
        auto components = key.split('.');
        auto bean = getRegistrationByName(components[0]);
        if(!(bean && bean->getObject())) {
            if(allowPartial) {
                qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << components[0] << "'";
                return {QVariant{}, Status::fixable};
            }
            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << components[0] << "'";
            return {QVariant{}, Status::fatal};
        }

        QVariant resultValue = QVariant::fromValue(bean->getObject());
        for(unsigned pos = 1; pos < components.size(); ++pos) {
            QString propName = components[pos];
            auto parent = resultValue.value<QObject*>();
            if(!parent) {
                if(allowPartial) {
                    qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                    return {QVariant{}, Status::fixable};
                }
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                return {QVariant{}, Status::fatal};
            }
            int propIndex = parent->metaObject()->indexOfProperty(propName.toLatin1());
            if(propIndex < 0) {
                //Refering to a non-existing Q_PROPERTY is always non-fixable:
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << parent;
                return {QVariant{}, Status::fatal};
            }
            resultValue = parent->metaObject()->property(propIndex).read(parent);

        }

        qCInfo(loggingCategory()).nospace().noquote() << "Resolved reference '" << key << "' to " << resultValue;
        return {resultValue, Status::ok};
    }



    if(key.contains("${")) {
        QVariant lastResolvedValue;
        QString resolvedString;
        QString token;
        int state = 0;
        for(int pos = 0; pos < key.length(); ++pos) {
            auto ch = key[pos];
            switch(ch.toLatin1()) {
            case '$':
                switch(state) {
                case 1:
                    resolvedString += '$';
                case 0:
                    state = 1;
                    continue;
                default:
                    qCCritical(loggingCategory()).nospace().noquote() << "Invalid placeholder '" << key << "'";
                    return {QVariant{}, Status::fatal};
                 }
            case '{':
                switch(state) {
                case 1:
                    state = 2;
                    continue;
                default:
                    state = 0;
                    resolvedString += ch;
                    continue;
                }

            case '}':
                switch(state) {
                case 2:
                    if(!token.isEmpty()) {
                        lastResolvedValue = getConfigurationValue(token);
                        if(!lastResolvedValue.isValid()) {
                            if(allowPartial) {
                                qCWarning(loggingCategory()).nospace().noquote() << "Could not resolve config-value '" << token << "'";
                                return {QVariant{}, Status::fixable};
                            }
                            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve config-value '" << token << "'";
                            return {QVariant{}, Status::fatal};
                        }
                        qCInfo(loggingCategory()).nospace().noquote() << "Resolved variable '" << token << "' to " << lastResolvedValue;
                        if(resolvedString.isEmpty() && pos + 1 == key.length()) {
                            return {lastResolvedValue, Status::ok};
                        }
                        resolvedString += lastResolvedValue.toString();
                        token.clear();
                    }
                    state = 0;
                    continue;
                default:
                    resolvedString += ch;
                    continue;
                }

            default:
                switch(state) {
                case 1:
                    resolvedString += '$';
                    state = 0;
                case 0:
                    resolvedString += ch;
                    continue;
                default:
                    token += ch;
                    continue;
                }
            }
        }
        switch(state) {
        case 1:
            resolvedString += '$';
        case 0:
            return {resolvedString, Status::ok};
        default:
            qCCritical(loggingCategory()).nospace().noquote() << "Unbalanced placeholder '" << key << "'";
            return {QVariant{}, Status::fatal};
        }
    }
    return {value, Status::ok};
}




StandardApplicationContext::Status StandardApplicationContext::configure(DescriptorRegistration* reg, QObject* target, const QList<QApplicationContextPostProcessor*>& postProcessors, bool allowPartial) {
    if(!target) {
        return Status::fatal;
    }
    auto metaObject = target->metaObject();
    auto& config = reg->descriptor.config;
    if(metaObject) {
        std::unordered_set<QString> usedProperties;
        for(auto[key,value] : config.properties.asKeyValueRange()) {
            auto result = resolveValue(value, allowPartial);
            if(result.second != Status::ok) {
                return result.second;
            }
            auto resolvedValue = result.first;
            reg->resolvedProperties.insert(key, resolvedValue);
            if(!key.startsWith('.')) {
                int index = metaObject->indexOfProperty(key.toUtf8());
                if(index < 0) {
                    //Refering to a non-existing Q_PROPERTY by name is always non-fixable:
                    qCCritical(loggingCategory()).nospace() << "Could not find property " << key << " of '" << metaObject->className() << "'";
                    return Status::fatal;
                }
                auto property = metaObject->property(index);
                if(property.write(target, resolvedValue)) {
                    qCDebug(loggingCategory()).nospace() << "Set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    usedProperties.insert(key);
                } else {
                    //An error while setting a Q_PROPERTY is always non-fixable:
                    qCCritical(loggingCategory()).nospace() << "Could not set property '" << key << "' of " << *reg << " to value " << resolvedValue;
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





QVariant StandardApplicationContext::getConfigurationValue(const QString& key) const {
    for(auto reg : registrations) {
        if(QSettings* settings = dynamic_cast<QSettings*>(reg->getObject())) {
           auto value = settings->value(key);
           if(value.isValid()) {
                return value;
           }
        }
    }
    return {};
}


StandardApplicationContext::DescriptorRegistration::DescriptorRegistration(const QString& name, const service_descriptor& desc, StandardApplicationContext* parent) :
    StandardRegistration(parent),
    descriptor{desc},
    m_name(name)
    {
    }

    bool StandardApplicationContext::StandardRegistration::registerAutoWiring(const type_info &type, binder_t binder)
    {

       auto result = autowirings.insert({type, binder});
       if(result.second) {
           connect(this, &Registration::publishedObjectsChanged, this, TargetBinder{this, binder, applicationContext()->getRegistration(type)});
       }
       return result.second;
    }













}//mcnepp::qtdi
