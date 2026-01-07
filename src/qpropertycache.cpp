#include "qpropertycache.h"

namespace mcnepp::qtdi::detail {

class PropertyFinder : public QPropertyProvider {
    friend class QPropertyCache;

    explicit PropertyFinder(QObject* source)
    {
        auto meta = source->metaObject();
        auto notifySlot = PropertyFinder::notifySlot();
        for(int p = 0; p < meta->propertyCount(); ++p) {
            auto prop = meta->property(p);
            if(prop.isBindable()) {
                auto bindable = prop.bindable(source);
                m_bindings.push_back(bindable.addNotifier([prop,this] {
                    m_prop = prop;
                }));
                continue;
            }
            if(prop.hasNotifySignal()) {
                auto notifySignal = prop.notifySignal();
                connect(source, notifySignal, this, notifySlot);
                m_propsBySignalIndex.insert({notifySignal.methodIndex(), prop});
            }
        }
    }

    void notify() override {
        m_prop = m_propsBySignalIndex[senderSignalIndex()];
    }

    static QMetaMethod notifySlot() {
        static QMetaMethod notifySlot = QPropertyProvider::staticMetaObject.method(QPropertyProvider::staticMetaObject.indexOfSlot("notify()"));
        return notifySlot;
    }


    QMetaProperty m_prop;
    std::vector<QPropertyNotifier> m_bindings;
    std::unordered_map<int,QMetaProperty> m_propsBySignalIndex;

};



QMetaProperty QPropertyCache::tryToFindPropertyBySetter(QObject* target, q_setter_t setter, const QVariant &propValue)
{
    if(!setter) {
        return {};
    }
    QMetaProperty& prop = m_propertyNameCache[{target->metaObject(), setter}];
    if(!prop.isValid()) {
        PropertyFinder finder{target};
        setter(target, propValue);
        prop = finder.m_prop; //Store in m_propertyNameCache
    } else {
        setter(target, propValue);
    }
    return prop;
}

void QPropertyCache::setProperty(property_descriptor &descriptor, QObject *target, const QVariant &propValue)
{
    if(!descriptor.setter) {
        return;
    }
    if(descriptor.name.isEmpty()) {
        QMetaProperty& prop = m_propertyNameCache[{target->metaObject(),  descriptor.setter}];
        if(!prop.isValid()) {
            PropertyFinder finder{target};
            descriptor.setter(target, propValue);
            if(finder.m_prop.isValid()) {
                prop = finder.m_prop; //Store in m_propertyNameCache
                descriptor.name = prop.name();
            }
            return;
        }
    }
    descriptor.setter(target, propValue);
}



}
