#include <QMetaProperty>
#include <QCoreApplication>
#include "qapplicationcontext.h"


namespace mcnepp::qtdi {




std::atomic<QApplicationContext*> QApplicationContext::theInstance = nullptr;


Q_LOGGING_CATEGORY(loggingCategory, "qtdi")

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
    //This is a the last resort: a derived class has forgotten to un-set itsefl while it was still alive. Better late than sorry!
    if(unsetInstance(this)) {
        qCWarning(loggingCategory()).noquote().nospace() << "Removed destroyed QApplicationContext " << this << " as global instance";
    }
}

bool QApplicationContext::isGlobalInstance() const
{
    return theInstance.load() == this;
}

namespace detail {

QMetaProperty findPropertyBySignal(const QMetaMethod &signalFunction, const QMetaObject* metaObject)
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
        qCritical(loggingCategory()).noquote().nospace() << "Signal " << signalFunction.name() << " does not correspond to a property of " << metaObject->className();
    } else {
        qCritical(loggingCategory()).noquote().nospace() << "Signal " << signalFunction.name() << " cannot be validated to correspond to any property";
    }
    return QMetaProperty{};
}

const QMetaObject *ServiceRegistration::serviceMetaObject() const
{
    return descriptor().meta_object;
}

}

}//mcnepp::qtdi
