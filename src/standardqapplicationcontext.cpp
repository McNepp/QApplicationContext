#include <QThread>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QUuid>
#include <QRegularExpression>
#include <QSettings>
#include "standardqapplicationcontext.h"

namespace com::neppert::context {




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
    QDebug tmp = out.noquote().nospace() << "Dependency '" << info.type().name() << "' [" << cardinalityToString(info.cardinality);
    if(!info.requiredName.isEmpty()) {
        return tmp << "] with required name '" << info.requiredName << "'";
    }
    return tmp << ']';
}

StandardApplicationContext::StandardApplicationContext(QObject* parent) :
QApplicationContext(parent),
successfullyPublished(false)
{
}


StandardApplicationContext::~StandardApplicationContext() {
    unpublish();
}

template<typename C> StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::find_by_type(const C& regs, const std::type_index& type) {
    auto found = std::find_if(regs.begin(), regs.end(), [&type](DescriptorRegistration* reg) {return reg->matches(type);} );
    return found != regs.end() ? *found : nullptr;
}


void StandardApplicationContext::unpublish()
{
    std::unordered_set<DescriptorRegistration*> published;
    std::copy_if(registrations.begin(), registrations.end(), std::inserter(published, published.begin()), std::mem_fn(&DescriptorRegistration::isPublished));

    qCInfo(loggingCategory()).noquote().nospace() << "Un-publish ApplicationContext with " << published.size() << " published Objects";

    //Do several rounds and delete those services on which no other published Services depend:
    while(!published.empty()) {

        for(auto iter = published.begin();;) {
            loop_head:
            if(iter == published.end()) {
                break;
            }
            auto reg = *iter;
            for(auto& depend : published) {
                for(auto& t : depend->descriptor->dependencies()) {
                    if(reg->descriptor->matches(t.type())) {
                        ++iter;
                        goto loop_head;
                    }
                }
            }
            if(reg->unpublish()) {
                qCInfo(loggingCategory()).nospace().noquote() << "Un-published " << *reg;
            }
            iter = published.erase(iter);
        }
    }
    qCInfo(loggingCategory()) << "ApplicationContext has been un-published";
}

StandardApplicationContext::DescriptorRegistration *StandardApplicationContext::getRegistrationByName(const QString &name) const
{

    auto found = registrationsByName.find(name);
    return found != registrationsByName.end() ? found->second : nullptr;
}


QObject* StandardApplicationContext::resolveDependency(const descriptor_set &published, std::vector<DescriptorRegistration*>& publishedNow, DescriptorRegistration* reg, const dependency_info& d, QObject* temporaryParent)
{
    const std::type_info& type = d.type();

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
            qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d << " of " << *reg;
            return nullptr;
        }
    case Cardinality::OPTIONAL:
        switch(dep.size()) {
        case 0:
            return nullptr;
        case 1:
            qCInfo(loggingCategory()).noquote().nospace() << "Resolved dependency " << d << " of " << *reg;
            return dep[0];
        default:
            qCritical(loggingCategory()).noquote().nospace() << d << "' of " << *reg << " is ambiguous";
            return nullptr;
        }
    case Cardinality::N:
        return detail::wrapList(dep, temporaryParent);

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
            qCritical(loggingCategory()).noquote().nospace() << d << "' of " << *reg << " is ambiguous";
            return nullptr;
        }
        if(!depReg) {
            if(!d.defaultConstructor) {
                qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d << " of " << *reg;
                return nullptr;
            }
            depReg = new ServiceRegistration{"", new service_descriptor{d.type(), type, d.defaultConstructor}, this};
            publishedNow.push_back(depReg);
        }
        QObjectList subDep;
        QOwningList privateSubDep;
        for(auto& dd : depReg->descriptor->dependencies()) {
            auto result = resolveDependency(published, publishedNow, depReg, dd, temporaryParent);
            if(!result && dd.cardinality != Cardinality::OPTIONAL) {
                return nullptr;
            }
            subDep.push_back(result);
            if(dd.cardinality == Cardinality::PRIVATE_COPY) {
                privateSubDep.push_back(result);
            }
        }
        QObject* service = depReg->createPrivateObject(subDep);
        if(!service) {
            qCCritical(loggingCategory()).noquote().nospace() << "Could not create private copy of " << d << " for " << *reg;
            return nullptr;
        }
        qCInfo(loggingCategory()).noquote().nospace() << "Created private copy of " << *depReg << " for " << *reg;
        privateSubDep.moveTo(service);
        qCInfo(loggingCategory()).noquote().nospace() << "Resolved dependency " << d << " of " << *reg;
        return service;
    }

    return nullptr;
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


bool StandardApplicationContext::isResolvable() const
{
    for(auto& reg : registrations) {
        if(reg->isPublished()) {
            continue;
        }
        for(auto& t : reg->descriptor->dependencies()) {
            bool foundMatch = false;

            switch(t.cardinality) {
            case Cardinality::MANDATORY:
            case Cardinality::OPTIONAL:
            case Cardinality::PRIVATE_COPY:
                //Ambiguity-check is necessary for MANDATORY, OPTIONAL and PRIVATE_COPY
                if(t.requiredName.isEmpty()) {
                    for(auto r : registrations) {
                        if(r->matches(t.type())) {
                            if(foundMatch) {
                                qCritical(loggingCategory()).noquote().nospace() << t << " of " << *reg << " is ambiguous. You could try a named dependency!";
                                return false;
                            }
                            foundMatch = true;
                        }
                    }
                } else {
                    auto byName = getRegistrationByName(t.requiredName);
                    foundMatch = byName && byName->matches(t.type());
                }
                if(!(foundMatch || t.defaultConstructor || t.cardinality == Cardinality::OPTIONAL)) {
                    qCCritical(loggingCategory()).noquote().nospace() << *reg << " is unresolvable. " << t << " has not been registered";
                    return false;
                }
            default:

                continue;
            }
        }
        for(auto& beanRef : getBeanRefs(reg->descriptor->config())) {

            if(!getRegistrationByName(beanRef)) {
                qCCritical(loggingCategory()).noquote().nospace() << *reg << " is unresolvable. References Object '" << beanRef << "', but no such Object has been registered.";
                return false;
            }
        }
    }

    return true;
}


bool StandardApplicationContext::publish()
{
    if(!isResolvable()) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot publish ApplicationContext.";
        return false;
    }
    QObject temporaryParent;//Will manage temporary QObjects obtained by detail::wrapList(const QObjectList&).

    descriptor_set allPublished;
    std::vector<DescriptorRegistration*> publishedNow;//Keep order of publication

    std::copy_if(registrations.begin(), registrations.end(), std::inserter(allPublished, allPublished.begin()), std::mem_fn(&DescriptorRegistration::isPublished));

    std::unordered_set<DescriptorRegistration*> unpublished;
    std::copy_if(registrations.begin(), registrations.end(), std::inserter(unpublished, unpublished.begin()), std::not_fn(std::mem_fn(&DescriptorRegistration::isPublished)));

    qCInfo(loggingCategory()).noquote().nospace() << "Publish ApplicationContext with " << unpublished.size() << " unpublished Objects";

    //Do several rounds and publish those Services whose dependencies have already been published.
    //For a Service with an empty set of dependencies, this means that it will be published first.
    while(!unpublished.empty()) {

        for(auto iter = unpublished.begin();;) {
            loop_head:
            if(iter == unpublished.end()) {
                break;
            }
            auto reg = *iter;
            QObjectList dependencies;
            QOwningList privateDependencies;
            for(auto& d : reg->descriptor->dependencies()) {
                //If there are unpublished dependencies, skip this service:
                if(find_by_type(unpublished, d.type())) {
                    ++iter;
                    goto loop_head;
                }
                if(!find_by_type(allPublished, d.type()) && d.cardinality == Cardinality::MANDATORY) {
                    auto defaultReg = std::make_unique<ServiceRegistration>("", new service_descriptor{d.type(), d.type(), d.defaultConstructor}, this);
                    auto def = registerDescriptor(defaultReg.get());
                    if(def.first == defaultReg.get()) {
                        iter = unpublished.insert(unpublished.begin(), defaultReg.release());
                        qCInfo(loggingCategory()).noquote().nospace() << "Creating default-instance of " << d << " for " << *reg;
                        goto loop_head;
                    }
                }
            }
            for(auto& d : reg->descriptor->dependencies()) {
                auto result = resolveDependency(allPublished, publishedNow, reg, d, &temporaryParent);
                if(!result && d.cardinality != Cardinality::OPTIONAL) {
                    return false;
                }
                dependencies.push_back(result);
                if(d.cardinality == Cardinality::PRIVATE_COPY) {
                    privateDependencies.push_back(result);
                }
            }
            auto service = reg->publish(dependencies);
            if(!service) {
                qCCritical(loggingCategory()).nospace().noquote() << "Could not publish " << *reg;
                unpublish();
                return false;
            }
            privateDependencies.moveTo(service);
            if(service->objectName().isEmpty()) {
                service->setObjectName(reg->name());
            }
            qCInfo(loggingCategory()).nospace().noquote() << "Published " << *reg;
            allPublished.insert(reg);
            publishedNow.push_back(reg);
            iter = unpublished.erase(iter);
        }
    }
    qsizetype publishedCount = publishedNow.size();
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
        for(auto iter = publishedNow.begin();;) {
            next_published:
            if(iter == publishedNow.end()) {
                break;
            }
            auto reg = *iter;
            for(auto& beanRef : getBeanRefs(reg->descriptor->config())) {
                auto foundByName = std::find_if(iter, publishedNow.end(), [&beanRef](DescriptorRegistration* r) { return r->name() == beanRef;});
                if(foundByName != publishedNow.end()) {
                    ++iter;
                    goto next_published;
                }
            }

            auto service = reg->theService;
            if(service) {
                if(!configure(reg, service, postProcessors)) {
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not configure " << *reg;
                    unpublish();
                    return false;
                }
                qCInfo(loggingCategory()).noquote().nospace() << "Configured " << *reg;
                reg->notifyPublished();
            }
            for(auto privateObj : reg->privateObjects()) {
                if(!configure(reg, privateObj, postProcessors)) {
                    qCCritical(loggingCategory()).nospace().noquote() << "Could not configure private copy of " << *reg;
                    unpublish();
                    return false;
                }
                qCInfo(loggingCategory()).noquote().nospace() << "Configured private copy of " << *reg;
            }
            iter = publishedNow.erase(iter);
        }
    }
    qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has published " << publishedCount << " objects";
    if(publishedCount != allPublished.size()) {
        qCInfo(loggingCategory()).noquote().nospace() << "ApplicationContext has a total number of " << allPublished.size() << " objects.";
    }

    successfullyPublished = true;
    emit publishedChanged(true);

    return true;
}

bool StandardApplicationContext::published() const
{
    return successfullyPublished;
}


std::pair<Registration*,bool> StandardApplicationContext::registerDescriptor(StandardApplicationContext::DescriptorRegistration* registration) {

    auto found = getRegistrationByName(registration->name());
    if(found) {
        if(*registration == *found) {
            qCInfo(loggingCategory()).nospace().noquote() << *registration << " has already been registered";
        } else {
            qCCritical(loggingCategory()).nospace().noquote() << *registration << " has already been registered as " << *found;
        }
        return {found, false};
    }

    for(auto reg : registrations) {
        if(*registration == *reg) {
            if(registration->isAnonymous) {
                qCInfo(loggingCategory()).nospace().noquote() << *registration << " has already been registered";
                return {reg, false};
            }
            registrationsByName.insert({registration->name(), reg});
            qCInfo(loggingCategory()).nospace().noquote() << "Created alias '" << registration->name() << "' for " << *reg;
            return {reg, false};
        }
    }

    std::unordered_set<std::type_index> dependencies;

    findTransitiveDependenciesOf(registration->descriptor.get(), dependencies);

    if(!checkTransitiveDependentsOn(registration->descriptor.get(), dependencies)) {
        qCCritical(loggingCategory()).nospace() << "Cyclic dependency in dependency-chain of " << *registration;
        return {nullptr, false};

    }

    bool wasPublished = published();
    registrationsByName.insert({registration->name(), registration});
    registrations.insert(registration);
    auto proxy = proxyRegistrationCache.find(registration->service_type());
    if(proxy != proxyRegistrationCache.end()) {
        proxy->second->add(registration);
    }
    if(wasPublished) {
        successfullyPublished = false;
        emit publishedChanged(false);
    }
    return {registration, true};

}

Registration* StandardApplicationContext::registerService(const QString& name, service_descriptor *descriptor)
{
    auto registration = std::make_unique<ServiceRegistration>(name, descriptor, this);

    if(!descriptor) {
        return nullptr;
    }


    auto result = registerDescriptor(registration.get());
    if(result.second) {
        return registration.release();
    }
    return result.first;
}

Registration * StandardApplicationContext::registerObject(const QString &name, QObject *obj, service_descriptor* descriptor)
{
    auto registration = std::make_unique<ObjectRegistration>(name.isEmpty() ? obj->objectName() : name, obj, descriptor, this);

    if(!obj) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register null-object for " << *registration;
        return nullptr;
    }

    auto result = registerDescriptor(registration.get());
    if(result.second) {
        connect(obj, &QObject::destroyed, this, &StandardApplicationContext::contextObjectDestroyed);
        return registration.release();
    }
    return result.first;

}




void StandardApplicationContext::findTransitiveDependenciesOf(service_descriptor* descriptor, std::unordered_set<std::type_index>& result) const
{
    for(auto& t : descriptor->dependencies()) {
        if(find_by_type(registrations, t.type())) {
            result.insert(t.type());
            for(auto reg : registrations) {
                if(reg->descriptor->matches(t.type())) {
                    findTransitiveDependenciesOf(reg->descriptor.get(), result);
                }
            }
        }
    }

}



bool StandardApplicationContext::checkTransitiveDependentsOn(service_descriptor* descriptor, const std::unordered_set<std::type_index>& dependencies) const
{
    for(auto reg : registrations) {
        for(auto& t : reg->descriptor->dependencies()) {
            if(descriptor->matches(t.type())) {
                if(std::find_if(dependencies.begin(), dependencies.end(), [reg](auto dep) { return reg->matches(dep);}) != dependencies.end()) {
                   return false;
                }
                if(!checkTransitiveDependentsOn(reg->descriptor.get(), dependencies)) {
                    return false;
                }
            }
        }
    }
    return true;
}

QVariant StandardApplicationContext::resolveValue(const QVariant &value)
{
    if(!value.isValid()) {
        return value;
    }
    QString key = value.toString();
    if(key.startsWith('&')) {
        key = key.last(key.size()-1);
        auto components = key.split('.');
        auto bean = getRegistrationByName(components[0]);
        if(!(bean && bean->getObject())) {
            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve reference '" << components[0] << "'";
            return {};
        }
        QVariant resultValue = QVariant::fromValue(bean->getObject());
        for(unsigned pos = 1; pos < components.size(); ++pos) {
            QString propName = components[pos];
            auto parent = resultValue.value<QObject*>();
            if(!parent) {
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << resultValue;
                return {};
            }
            int propIndex = parent->metaObject()->indexOfProperty(propName.toLatin1());
            if(propIndex < 0) {
                qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve property '" << propName << "' of " << parent;
                return {};
            }
            resultValue = parent->metaObject()->property(propIndex).read(parent);

        }

        qCInfo(loggingCategory()).nospace().noquote() << "Resolved reference '" << key << "' to " << resultValue;
        return resultValue;
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
                    return {};
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
                            qCCritical(loggingCategory()).nospace().noquote() << "Could not resolve '" << token << "'";
                            return {};
                        }
                        qCInfo(loggingCategory()).nospace().noquote() << "Resolved variable '" << token << "' to " << lastResolvedValue;
                        if(resolvedString.isEmpty() && pos + 1 == key.length()) {
                            return lastResolvedValue;
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
            return resolvedString;
        default:
            qCCritical(loggingCategory()).nospace().noquote() << "Unbalanced placeholder '" << key << "'";
            return {};
        }
    }
    return value;
}




bool StandardApplicationContext::configure(DescriptorRegistration* reg, QObject* target, const QList<QApplicationContextPostProcessor*>& postProcessors) {
    if(!target) {
        return false;
    }
    auto metaObject = target->metaObject();
    auto config = reg->descriptor->config();
    if(metaObject) {
        std::unordered_set<QString> usedProperties;
        for(auto[key,value] : config.properties.asKeyValueRange()) {
            auto resolvedValue = resolveValue(value);
            if(!resolvedValue.isValid()) {
                return false;
            }
            reg->resolvedProperties.insert(key, resolvedValue);
            if(!key.startsWith('.')) {
                int index = metaObject->indexOfProperty(key.toUtf8());
                if(index < 0) {
                    qCCritical(loggingCategory()).nospace() << "Could not find property " << key << " of '" << metaObject->className() << "'";
                    return false;
                }
                auto property = metaObject->property(index);
                if(property.write(target, resolvedValue)) {
                    qCDebug(loggingCategory()).nospace() << "Set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    usedProperties.insert(key);
                } else {
                    qCCritical(loggingCategory()).nospace() << "Could not set property '" << key << "' of " << *reg << " to value " << resolvedValue;
                    return false;
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
        for(int index = 0, methodCount = metaObject->methodCount(); index < methodCount; ++index) {
            auto method = metaObject->method(index);
            qCInfo(loggingCategory()) << "method: " << method.name();
            if(method.name() == config.initMethod) {
                switch(method.parameterCount()) {
                case 0:
                    if(metaObject->method(index).invoke(target)) {
                        qCInfo(loggingCategory()).nospace().noquote() << "Invoked init-method '" << config.initMethod << "' of " << *reg;
                        return true;
                    }
                    break;
                case 1:
                    if(metaObject->method(index).invoke(target, Q_ARG(QApplicationContext*,this))) {
                        qCInfo(loggingCategory()).nospace().noquote() << "Invoked init-method '" << config.initMethod << "' of " << *reg << ", passing the ApplicationContext";
                        return true;
                    }
                    break;
               }
               qCCritical(loggingCategory()).nospace().noquote() << "Could not invoke init-method '" << method.methodSignature() << "' of " << *reg;
               return false;
            }
        }
        qCCritical(loggingCategory()).nospace().noquote() << "Could not find init-method '" << config.initMethod << "'";
        return false;
    }
    return true;
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

StandardApplicationContext::DescriptorRegistration::DescriptorRegistration(const QString& name, service_descriptor* desc, StandardApplicationContext* parent) :
    Registration(parent),
    descriptor{desc},
    theService{nullptr},
    isAnonymous(name.isEmpty()),
    m_name(name)
    {
       if(isAnonymous) {
           m_name = QString{descriptor->service_type().name()}+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
       }
    }











}//com::neppert::context
