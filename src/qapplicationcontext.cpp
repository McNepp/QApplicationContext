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


}//mcnepp::qtdi
