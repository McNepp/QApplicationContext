#include <QMetaProperty>
#include <QCoreApplication>
#include "qapplicationcontext.h"


namespace mcnepp::qtdi {




std::atomic<QApplicationContext*> QApplicationContext::theInstance = nullptr;


Q_LOGGING_CATEGORY(defaultLoggingCategory, "qtdi")

bool QApplicationContext::setInstance(QApplicationContext* context) {
    QApplicationContext* expected = nullptr;
    return theInstance.compare_exchange_weak(expected, context);
}

bool QApplicationContext::unsetInstance(QApplicationContext* context) {
    QApplicationContext* expected = context;
    return theInstance.compare_exchange_weak(expected, nullptr);
}



QApplicationContext::QApplicationContext(QObject* parent) :
    QObject(parent) {
}

QApplicationContext* QApplicationContext::instance() {
    return theInstance.load();
}



QApplicationContext::~QApplicationContext() {
    //This is a the last resort: a derived class has forgotten to un-set itself while it was still alive. Better late than sorry!
    if(unsetInstance(this)) {
        //We have to use defaultLoggingCategory() here, as the pure virtual function QApplicationContext::loggingCategory() is not available in the destructor.
        qCWarning(defaultLoggingCategory()).noquote().nospace() << "Removed destroyed QApplicationContext " << this << " as global instance";
    }
}

bool QApplicationContext::isGlobalInstance() const
{
    return theInstance.load() == this;
}

const QLoggingCategory& loggingCategory(registration_handle_t handle) {
    if(handle) {
        return handle->applicationContext()->loggingCategory();
    }
    return defaultLoggingCategory();
}


const service_config& serviceConfig(service_registration_handle_t handle) {
    static service_config defaultConfig;
    return handle ? handle->config() : defaultConfig;
}


namespace detail {

QMetaProperty findPropertyBySignal(const QMetaMethod &signalFunction, const QMetaObject* metaObject, const QLoggingCategory& loggingCategory)
{
    auto owner = signalFunction.enclosingMetaObject();
    if(owner && metaObject && owner == metaObject) {
        for(int index = 0; index < owner->propertyCount(); ++index) {
            auto prop = owner->property(index);
            if(prop.hasNotifySignal() && prop.notifySignal() == signalFunction) {
                return prop;
            }
        }
    }
    if(metaObject) {
        qCritical(loggingCategory).noquote().nospace() << "Signal " << signalFunction.name() << " does not correspond to a property of " << metaObject->className();
    } else {
        qCritical(loggingCategory).noquote().nospace() << "Signal " << signalFunction.name() << " cannot be validated to correspond to any property";
    }
    return QMetaProperty{};
}

const QMetaObject *ServiceRegistration::serviceMetaObject() const
{
    return descriptor().meta_object;
}

inline QString kindToString(int kind) {
    switch(kind) {
    case static_cast<int>(Kind::N):
        return "N";
    case static_cast<int>(Kind::OPTIONAL):
        return "optional";
    case static_cast<int>(Kind::MANDATORY):
        return "mandatory";
    case VALUE_KIND:
        return "value";
    case RESOLVABLE_KIND:
        return "resolvable";
    case PARENT_PLACEHOLDER_KIND:
        return "parent placeholder";
    case INVALID_KIND:
        return "invalid";
    default:
        return "unknown";

    }
}


QDebug operator<<(QDebug out, const dependency_info& info) {
    QDebugStateSaver state{out};
    out.noquote().nospace() << "Dependency<" << info.type.name() << "> [" << kindToString(info.kind) << ']';
    switch(info.kind) {
    case detail::VALUE_KIND:
        out << " with value " << info.value;
        break;
    case detail::RESOLVABLE_KIND:
        out << " with expression '" << info.expression << "'";
        break;
    default:
        if(!info.expression.isEmpty()) {
            out << " with required name '" << info.expression << "'";
        }
    }
    return out;
}




QDebug operator << (QDebug out, const service_descriptor& descriptor) {
    QDebugStateSaver state{out};
    out.nospace().noquote() << "Descriptor [service-types=";
    const char* del = "";
    for(auto& t : descriptor.service_types) {
        out << del << t.name();
        del = ", ";
    }
    out << "]";
    if(!descriptor.dependencies.empty()) {
        out << " with " << descriptor.dependencies.size() << " dependencies ";
        const char* sep = "";
        for(auto& dep : descriptor.dependencies) {
            out << sep << dep;
            sep = ", ";
        }
    }
    return out;
}



void BasicSubscription::cancel() {
    QObject::disconnect(out_connection);
    QObject::disconnect(in_connection);
}

void BasicSubscription::connectTo(registration_handle_t source) {
    in_connection = detail::connect(source, this);
}

SourceTargetSubscription::SourceTargetSubscription(registration_handle_t target, QObject* boundSource, QObject * parent) :
    BasicSubscription{parent},
    m_target{target},
    m_boundSource{boundSource} {
    connectOut(this, &SourceTargetSubscription::onPublished);
}


void SourceTargetSubscription::cancel() {
    for(auto child : children()) {
        if(auto subscr = dynamic_cast<SourceTargetSubscription*>(child)) {
            subscr->cancel();
        }
    }
    BasicSubscription::cancel();
}

void SourceTargetSubscription::onPublished(QObject* obj) {
    if(m_boundSource) {
        notify(m_boundSource, obj);
    } else {
        auto subscr = createForSource(obj);
        m_target->subscribe(subscr);
    }
}


}

}//mcnepp::qtdi
