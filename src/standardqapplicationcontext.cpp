#include <QThread>
#include <QEvent>
#include <QMetaMethod>
#include <QLoggingCategory>
#include <QUuid>
#include <QRegularExpression>
#include <QSettings>
#include <QCoreApplication>
#include "standardqapplicationcontext.h"

namespace mcnepp::qtdi {



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
    QDebug tmp = out.nospace().noquote();
    tmp << "Descriptor [service-types=";
    const char* del = "";
    for(auto& t : descriptor.service_types) {
        tmp << del << t.name();
        del = ", ";
    }
    tmp << "]";
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





}

namespace {

const QRegularExpression beanRefPattern{"^&([^.]+)(\\.([^.]+))?"};






QMetaMethod methodByName(const QMetaObject* metaObject, const QString& name) {
    for(int i = 0; i < metaObject->methodCount(); ++i) {
        auto method = metaObject->method(i);
            if(method.name() == name.toLatin1()) {
                return method;
            }
        }
        return {};
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

QString makeConfigPath(const QString& group, const QString& key) {
        if(group.isEmpty()) {
            return key;
        }
        return group+"/"+key;
}

QString makeName(const std::type_index& type) {
    QString typeName{type.name()};
    typeName.replace(' ', '-');
    return typeName+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
}



}

struct StandardApplicationContext::CreateRegistrationHandleEvent : public QEvent {
    static QEvent::Type eventId() {
        static int eventId = QEvent::registerEventType();
        return static_cast<QEvent::Type>(eventId);
    }


    CreateRegistrationHandleEvent(const type_info &service_type, const QMetaObject* metaObject) :
        QEvent(eventId()),
        m_service_type(service_type),
        m_metaObject(metaObject),
        m_result(QSharedPointer<std::optional<registration_handle_t>>::create())
    {
    }

    void createHandle(StandardApplicationContext* context) {
        *m_result = new StandardApplicationContext::ProxyRegistration{m_service_type, m_metaObject, context, context->registrations};
    }

    const type_info &m_service_type;
    const QMetaObject* m_metaObject;
    QSharedPointer<std::optional<registration_handle_t>> m_result;
};










StandardApplicationContext::StandardApplicationContext(QObject* parent) :
QApplicationContext(parent)
{
}


StandardApplicationContext::~StandardApplicationContext() {
    unpublish();
}

template<typename C> StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::find_by_type(const C& regs, const std::type_info& type) {
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
            for(auto& beanRef : reg->getBeanRefs()) {
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


std::pair<QVariant,StandardApplicationContext::Status> StandardApplicationContext::resolveDependency(const descriptor_list &published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial, bool publish, QObject* temporaryPrivateParent)
{
    const std::type_info& type = d.type;

    QList<DescriptorRegistration*> depRegs;

    for(auto pub : published) {
        if(pub->matches(type)) {
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
            auto resolved = resolveProperty(reg->config().group, d.expression, d.value, allowPartial);
            if(resolved.second == Status::ok) {
                qCInfo(loggingCategory()).noquote().nospace() << "Resolved " << d << " with " << resolved.first;
            }
            return resolved;
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
            std::transform(depRegs.begin(), depRegs.end(), std::back_insert_iterator(dep), std::mem_fn(&DescriptorRegistration::getObject));
            return {QVariant::fromValue(dep), Status::ok};
        }

    case static_cast<int>(Kind::PRIVATE_COPY):
        DescriptorRegistration* depReg = nullptr;
        switch(depRegs.size()) {
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
        if(!(depReg && depReg->isManaged())) {
            if(allowPartial) {
                qWarning(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                //Unresolvable dependencies may be fixable by registering more services:
                return {QVariant{}, Status::fixable};
            } else {
                qCritical(loggingCategory()).noquote().nospace() << "Could not resolve " << d;
                return {QVariant{}, Status::fatal};
            }
        }
        QVariantList subDep;
        QObject subTemporaryPrivateParent;
        for(auto& dd : depReg->descriptor.dependencies) {
            auto result = resolveDependency(published, depReg, dd, allowPartial, publish, &subTemporaryPrivateParent);
            if(result.second != Status::ok) {
                return result;
            }
            subDep.push_back(result.first);
        }
        QObject* service = nullptr;
        if(publish) {
            service = depReg->createPrivateObject(subDep);
            if(!service) {
                //If creation fails, this is always a non-fixable error:
                qCCritical(loggingCategory()).noquote().nospace() << "Could not create private copy of " << d;
                return {QVariant{}, Status::fatal};
            }
            qCInfo(loggingCategory()).noquote().nospace() << "Created private copy of " << *depReg;
            service->setParent(temporaryPrivateParent);
            QObjectList subChildren = subTemporaryPrivateParent.children(); //We must make a copy of the chilren, as we'll modify it indirectly in the loop.
            for(auto child : subChildren) {
                child->setParent(service);
            }
            qCInfo(loggingCategory()).noquote().nospace() << "Resolved dependency " << d << " with " << service;
        }
        return {QVariant::fromValue(service), Status::ok};
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
    qCCritical(loggingCategory()).noquote().nospace() << "Could not find a Registration for name '" << name;
    return nullptr;
}



detail::ProxyRegistration *StandardApplicationContext::getRegistrationHandle(const type_info &service_type, const QMetaObject* metaObject) const
{
    QMutexLocker<QMutex> locker{&mutex};

    auto found = proxyRegistrationCache.find(service_type);
    if(found != proxyRegistrationCache.end()) {
        return found->second;
    }
    ProxyRegistration* proxyReg;
    if(QThread::currentThread() == thread()) {
        proxyReg = new ProxyRegistration{service_type, metaObject, const_cast<StandardApplicationContext*>(this), registrations};
    } else {
        //We are in a different Thread than the QApplicationContext's. Let's post an Event that will create the ProxyRegistration asynchronously:
        auto event = new CreateRegistrationHandleEvent{service_type, metaObject};
        auto result = event->m_result; //Pin member on Stack to prevent asynchronous deletion.
        QCoreApplication::postEvent(const_cast<StandardApplicationContext*>(this), event);
        QDeadlineTimer timer{1000};
        while(!result->has_value()) {
            condition.wait(&mutex, timer);
        }
        if(!result->has_value()) {
            qCCritical(loggingCategory()).noquote().nospace() << "Could not obtain Registration-handle from another thread in time";
            return nullptr;
        }

        proxyReg = dynamic_cast<ProxyRegistration*>(result->value());
    }
    proxyRegistrationCache.insert({service_type, proxyReg});
    return proxyReg;
}


bool StandardApplicationContext::registerAlias(detail::ServiceRegistration *reg, const QString &alias)
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
            for(auto& entry : proxyRegistrationCache) {
                entry.second->remove(regPtr.get());
            }
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

        auto& dependencyInfos = reg->descriptor.dependencies;
        for(auto& d : dependencyInfos) {
            //If we find an unpublished dependency, we continue with that:
            auto foundReg = eraseIf(unpublished, DescriptorRegistration::matcher(d));
            if(foundReg) {
                unpublished.push_front(reg); //Put the current Registration back where it came from. Will be processed after the dependency.
                reg = foundReg;
                goto next_unpublished;
            }
        }
        if(!dependencyInfos.empty()) {
            QObject temporaryParent;
            qCInfo(loggingCategory()).noquote().nospace() << "Resolving " << dependencyInfos.size() << " dependencies of " << *reg << ":";
            for(auto& d : dependencyInfos) {
                auto result = resolveDependency(allPublished, reg, d, allowPartial, false, &temporaryParent);
                switch(result.second) {
                case Status::fixable:
                    if(allowPartial) {
                        status = Status::fixable;
                        goto fetch_next;
                    }
                case Status::fatal:
                    return Status::fatal;
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


bool StandardApplicationContext::publish(bool allowPartial)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot publish ApplicationContext in different thread";
        return false;
    }

    unsigned managed = 0;
    qsizetype publishedCount = 0;
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
        for(auto reg : toBePublished) {
            QVariantList dependencies;
            QObject temporaryParent;
            auto& dependencyInfos = reg->descriptor.dependencies;
            if(!dependencyInfos.empty()) {
                qCInfo(loggingCategory()).noquote().nospace() << "Resolving " << dependencyInfos.size() << " dependencies of " << *reg << ":";
                for(auto& d : dependencyInfos) {
                    auto result = resolveDependency(allCreated, reg, d, allowPartial, true, &temporaryParent);
                    dependencies.push_back(result.first);
                }
            }
            auto service = reg->publish(dependencies);
            if(!service) {
                qCCritical(loggingCategory()).nospace().noquote() << "Could not publish " << *reg;
                return false;
            }
            QObjectList tempChildren = temporaryParent.children(); //We must make a copy of the chilren, as we'll modify it indirectly in the loop.
            for(auto child : tempChildren) {
                child->setParent(service);
            }
            if(service->objectName().isEmpty()) {
                service->setObjectName(reg->registeredName());
            }
            qCInfo(loggingCategory()).nospace().noquote() << "Published " << *reg;
            allCreated.push_back(reg);
        }
        managed = std::count_if(allCreated.begin(), allCreated.end(), std::mem_fn(&DescriptorRegistration::isManaged));

    }

    QList<QApplicationContextPostProcessor*> postProcessors;
    for(auto reg : allCreated) {
        if(auto processor = dynamic_cast<QApplicationContextPostProcessor*>(reg->getObject())) {
            postProcessors.push_back(processor);
            qCInfo(loggingCategory()).noquote().nospace() << "Detected PostProcessor " << *reg;
        }
    }
    //Move PostProcessors to the front, so that they will be configured before they process other Services:
    for(unsigned moved = 0, pos = 1; pos < toBePublished.size(); ++pos) {
        if(dynamic_cast<QApplicationContextPostProcessor*>(toBePublished[pos]->getObject())) {
            std::swap(toBePublished[moved++], toBePublished[pos]);
        }
    }
    //Add the services that need configuration to those that have been been created in this round:
    toBePublished.insert(toBePublished.end(), needConfiguration.begin(), needConfiguration.end());
    //The services that have been instantiated during this methd-invocation will be configured in the order they have have been
    //instantiated. However, if their configuration has bean-refs, this order may need to be modified.
    while(!toBePublished.empty()) {
        DescriptorRegistration* reg = pop_front(toBePublished);
        next_published:
        for(auto& beanRef : reg->getBeanRefs()) {
            auto foundReg = eraseIf(toBePublished, [&beanRef](DescriptorRegistration* r) { return r->registeredName() == beanRef;});
            if(foundReg) {
                toBePublished.push_front(reg);//Put the current Registration back where it came from. Will be processed after the dependency.
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
                    validationResult = Status::fixable;
                    qCWarning(loggingCategory()).nospace().noquote() << "Could not configure private copy of " << *reg;
                    goto next_published;
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
                validationResult = Status::fixable;
                continue;

            case Status::ok:
                qCInfo(loggingCategory()).noquote().nospace() << "Configured " << *reg;
                ++publishedCount;
                reg->notifyPublished();
            }
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

StandardApplicationContext::DescriptorRegistration* StandardApplicationContext::registerDescriptor(QString name, const service_descriptor& descriptor, const service_config& config, QObject* obj) {
    if(name.isEmpty()) {
        name = makeName(*descriptor.service_types.begin());
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
    for(auto& entry : proxyRegistrationCache) {
        entry.second->add(registration);
    }
    qCInfo(loggingCategory()).noquote().nospace() << "Registered " << *registration;
    return registration;
}

detail::ServiceRegistration* StandardApplicationContext::registerService(const QString& name, const service_descriptor& descriptor, const service_config& config)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot register service in different thread";
        return nullptr;
    }
    DescriptorRegistration* reg;
    {
        QMutexLocker<QMutex> locker{&mutex};
        if(!name.isEmpty()) {
            reg = getRegistrationByName(name);
            //If we have a registration under the same name, we'll return it only if it has the same descriptor and config:
            if(reg) {
                //With isManaged() we test whether reg is also a ServiceRegistration (no ObjectRegistration)
                if(reg->isManaged() && descriptor == reg->descriptor && reg->config() == config) {
                    return reg;
                }
                //Otherwise, we have a conflicting registration
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Service " << descriptor << " as '" << name << "'. Has already been registered as " << *reg;
                return nullptr;
            }
        } else {
            //For an anonymous registration, we have to loop over all registrations:
            for(auto reg : registrations) {
                //With isManaged() we test whether reg is also a ServiceRegistration (no ObjectRegistration)
                if(reg->isManaged() && reg->config() == config) {
                    switch(detail::match(descriptor, reg->descriptor)) {
                    case detail::DESCRIPTOR_IDENTICAL:
                        return reg;
                    case detail::DESCRIPTOR_INTERSECTS:
                        //Otherwise, we have a conflicting registration
                        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Service " << descriptor << ". Has already been registered as " << *reg;
                        return nullptr;
                    default:
                        continue;
                    }
                }
            }
        }

        reg = registerDescriptor(name, descriptor, config, nullptr);
    }
    // Emit signal after mutex has been released:
    emit pendingPublicationChanged();
    return reg;
}

detail::ServiceRegistration * StandardApplicationContext::registerObject(const QString &name, QObject *obj, const service_descriptor& descriptor)
{
    if(!obj) {
        qCCritical(loggingCategory()).noquote().nospace() << "Cannot register null-object for " << descriptor;
        return nullptr;
    }
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot register service in different thread";
        return nullptr;
    }

    DescriptorRegistration* reg;
    {// A scope for the QMutexLocker
        QMutexLocker<QMutex> locker{&mutex};
        QString objName = name.isEmpty() ? obj->objectName() : name;
        if(!objName.isEmpty()) {
            auto reg = getRegistrationByName(objName);
            //If we have a registration under the same name, we'll return it only if it's for the same object and it has the same descriptor:
            if(reg) {
                if(!reg->isManaged() && reg->getObject() == obj && descriptor == reg->descriptor) {
                    return reg;
                }
                //Otherwise, we have a conflicting registration
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << obj << " as '" << objName << "'. Has already been registered as " << *reg;
                return nullptr;
            }
        }
        //For object-registrations, even if we supply an explicit name, we still have to loop over all registrations,
        //as we need to check whether the same object has been registered before.
        for(auto reg : registrations) {
            //With isManaged() we test whether reg is also an ObjectRegistration (no ServiceRegistration)
            if(!reg->isManaged() && obj == reg->getObject()) {
                //An identical anonymous registration is allowed:
                if(descriptor == reg->descriptor && objName.isEmpty()) {
                    return reg;
                }
                //Otherwise, we have a conflicting registration
                qCCritical(loggingCategory()).noquote().nospace() << "Cannot register Object " << obj << " as '" << objName << "'. Has already been registered as " << *reg;
                return nullptr;
            }
        }

        reg = registerDescriptor(objName, descriptor, ObjectRegistration::defaultConfig, obj);
    }
    // Emit signal after mutex has been released:
    emit publishedChanged();
    return reg;


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
            reg->resolveProperty(key, resolvedValue);
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
            processor->process(this, target, reg->registeredProperties());
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


const service_config StandardApplicationContext::ObjectRegistration::defaultConfig;


detail::Subscription* StandardApplicationContext::DescriptorRegistration::createBindingTo(const char* sourcePropertyName, detail::Registration *target, const detail::property_descriptor& targetProperty)
{
    if(QThread::currentThread() != this->thread()) {
        qCritical(loggingCategory()).noquote().nospace() << "Cannot create binding in different thread";
        return nullptr;
    }

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


    auto subscription = new PropertyBindingSubscription{target, sourceProperty, setter};
    qCInfo(loggingCategory()).noquote().nospace() << "Created Subscription for binding property '" << sourceProperty.name() << "' of " << *this << " to " << setter << " of " << *target;
    return subscribe(subscription);
}

detail::Subscription *StandardApplicationContext::DescriptorRegistration::createAutowiring(const type_info &type, detail::q_inject_t injector, Registration *source)
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
           m_state = STATE_INIT;
       }
    }

    void StandardApplicationContext::ObjectRegistration::print(QDebug out) const {
       out.nospace().noquote() << "Object '" << registeredName() << "' with " << this->descriptor;
    }







    void StandardApplicationContext::DescriptorRegistration::PropertyBindingSubscription::notify(QObject* obj) {
       auto subscr = new PropertyInjector{obj, m_sourceProperty, m_setter};
       m_target->subscribe(subscr);
       subscriptions.push_back(subscr);
    }

    void StandardApplicationContext::DescriptorRegistration::PropertyBindingSubscription::cancel() {
       for(auto iter = subscriptions.begin(); iter != subscriptions.end(); iter = subscriptions.erase(iter)) {
           auto subscription = *iter;
           if(subscription) {
              subscription->cancel();
           }
       }
       QObject::disconnect(out_Connection);
       QObject::disconnect(in_Connection);
    }

    void StandardApplicationContext::DescriptorRegistration::PropertyBindingSubscription::connectTo(Registration *source)
    {
        in_Connection = connect(source, this);
    }


    void StandardApplicationContext::DescriptorRegistration::PropertyInjector::connectTo(Registration *source)
    {
        in_Connection = connect(source, this);
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
       for(auto iter = connections.begin(); iter != connections.end(); iter = connections.erase(iter)) {
           QObject::disconnect(*iter);
       }
       //QPropertyNotifier will remove the binding in its destructor:
       bindings.clear();
       QObject::disconnect(out_Connection);
       QObject::disconnect(in_Connection);
    }

    QStringList StandardApplicationContext::ServiceRegistration::getBeanRefs() const
    {
        if(beanRefsCache.has_value()) {
            return beanRefsCache.value();
        }
        QStringList result;
        for(auto entry : config().properties.asKeyValueRange()) {
            auto key = entry.second.toString();
            if(key.startsWith('&')) {
                int dot = key.indexOf('.');
                if(dot < 0) {
                    dot = key.size();
                }
                result.push_back(key.mid(1, dot-1));
            }
        }
        beanRefsCache = result;
        return result;
    }


    void StandardApplicationContext::AutowireSubscription::notify(QObject *obj) {
        if(auto sourceReg = dynamic_cast<registration_handle_t>(m_bound)) {
            auto subscr = new AutowireSubscription{m_injector, obj};
            sourceReg->subscribe(subscr);
            subscriptions.push_back(subscr);
        } else {
            m_injector(m_bound, obj);
        }
    }

    void StandardApplicationContext::AutowireSubscription::cancel() {
        for(auto iter = subscriptions.begin(); iter != subscriptions.end(); iter = subscriptions.erase(iter)) {
            auto subscr = *iter;
            if(subscr) {
                subscr->cancel();
            }
        }
        QObject::disconnect(out_Connection);
        QObject::disconnect(in_Connection);
    }

    void StandardApplicationContext::AutowireSubscription::connectTo(detail::Registration *source)
    {
        in_Connection = connect(source, this);
    }

    detail::Subscription *StandardApplicationContext::ProxyRegistration::createAutowiring(const type_info &type, detail::q_inject_t injector, Registration *source)
    {
        if(!autowirings.insert(type).second) {
            qCCritical(loggingCategory()).noquote().nospace() << "Cannot register autowiring for type " << type.name() << " in " << *this;
            return nullptr;
        }

        return subscribe(new AutowireSubscription{injector, source});
    }



    }//mcnepp::qtdi
