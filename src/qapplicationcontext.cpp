#include <QMetaProperty>
#include <QUuid>
#include <QCoreApplication>
#include "qapplicationcontext.h"
#ifdef __GNUG__
#include <cxxabi.h>
#endif

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
    QObject{parent} {
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


QString QConfigurationResolver::makePath(const QString& section, const QString& path) {
    if(section.isEmpty() || path.startsWith('/')) {
        return path;
    }
    if(section.endsWith('/')) {
        return section + path;
    }
    return section + '/' + path;
}

bool QConfigurationResolver::removeLastPath(QString& s) {
    int lastSlash = s.lastIndexOf('/');
    if(lastSlash <= 0) {
        return false;
    }
    int nextSlash = s.lastIndexOf('/', lastSlash - 1);
    //lastIndexOf will return -1 if not found.
    //Thus, the following code will remove either the part after the nextSlash or from the beginning of the String:
    s.remove(nextSlash + 1, lastSlash - nextSlash);
    return true;
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
    out.noquote().nospace() << "Dependency<" << detail::type_name(info.type) << "> [" << kindToString(info.kind) << ']';
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
        out << del << detail::type_name(t);
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
    if((boundSource != nullptr) == (target != nullptr)) {
        qCCritical(defaultLoggingCategory()).nospace() << "Invalid source " << boundSource << " and target " << target << " supplied";
        return;
    }
    if(boundSource) {
        connectOut(this, &SourceTargetSubscription::onPublishedTarget);
    } else if(target) {
       connectOut(this, &SourceTargetSubscription::onPublishedSource);
    }
}


void SourceTargetSubscription::cancel() {
    for(auto subscr : m_children) {
        if(subscr) {
            subscr->cancel();
        }
    }
    QObject::disconnect(m_objectsPublishedConnection);
    BasicSubscription::cancel();
}

void SourceTargetSubscription::onPublishedSource(QObject* obj) {
    auto subscr = createForSource(obj);
    if(subscr) {
        m_children.push_back(subscr);
        m_target->subscribe(subscr);
    }
}

void SourceTargetSubscription::onPublishedTarget(QObject* obj) {
   emit objectsPublished(m_boundSource, obj);
}

QString uniquePropertyName(const void* data, std::size_t size)
{
    QString str{"."}; // property starts with a dot -> "private property"
    for(const unsigned char* iter = static_cast<const unsigned char*>(data), *end = iter + size; iter < end; ++iter) {
        str.append(QString::number(*iter, 16));
    }
    return str;
}



#ifdef __GNUG__

QString demangle(const char* name) {
    int status = 0;
    std::unique_ptr<char,decltype(&std::free)> demangledName{abi::__cxa_demangle(name, nullptr, nullptr, &status), std::free};
    return status == 0 ? demangledName.get() : name;
}
#endif

}

}//mcnepp::qtdi
