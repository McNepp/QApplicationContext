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


QApplicationContext::QApplicationContext(QObject* parent) :
    QObject(parent),
    m_isInstance(setInstance(this)) {
    if(m_isInstance)    {
        qCInfo(loggingCategory()).noquote().nospace() << "Installed QApplicationContext " << this << " as global instance";
    }
}

QApplicationContext* QApplicationContext::instance() {
    return theInstance.load();
}



QApplicationContext::~QApplicationContext() {
    QApplicationContext* instance = this;
    if(m_isInstance && theInstance.compare_exchange_weak(instance, nullptr)) {
        qCInfo(loggingCategory()).noquote().nospace() << "Removed QApplicationContext " << this << " as global instance";
    }
}

bool QApplicationContext::isGlobalInstance() const
{
    return m_isInstance;
}



}//mcnepp::qtdi
