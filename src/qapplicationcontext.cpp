#include <QMetaProperty>
#include "qapplicationcontext.h"
#include <unordered_set>

namespace com::neppert::context {










Q_LOGGING_CATEGORY(loggingCategory, "qapplicationcontext");

QApplicationContext::QApplicationContext(QObject* parent) :
    QObject(parent) {

}





namespace detail {


class QListWrapper : public QObject {
public:
    explicit QListWrapper(const QObjectList& list, QObject* parent = nullptr) :
        QObject(parent),
        m_list(list) {

    }

    QObjectList m_list;
};

QObject *wrapList(const QObjectList& list, QObject* parent)
{
    return new QListWrapper{list, parent};
}

const QObjectList &unwrapList(QObject *obj)
{
    static const QObjectList EMPTY_LIST;
    if(QListWrapper* wrapper = dynamic_cast<QListWrapper*>(obj)) {
        return wrapper->m_list;
    }
    qCCritical(loggingCategory()) << "Object" << obj << "passed to unwrapList was not obtained by wrapList";
    return EMPTY_LIST;
}

}//detail

}//com::neppert::context
