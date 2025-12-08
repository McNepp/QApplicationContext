#pragma once
#include "qapplicationcontext.h"
#include <QObject>
#include <QMetaProperty>
#include <QPropertyNotifier>

namespace mcnepp::qtdi::detail {


class QPropertyCache : public QObject {
public:
    explicit QPropertyCache(QObject* parent = nullptr) : QObject{parent} {

    }



    ///
    /// \brief Attempts to find a property by the signal that is emitted.
    /// <br>First, attempts to lookup the property via the supplied setter in the internal cache.
    /// If the property can be found, that is returned at the end of the function.
    /// <br>Otherwise, we connect to the signales emitted by **all properties** of the target.
    /// Then, the setter is invoked, and we assume that the emitted signal belongs to
    /// that property, store it in the internal Cache and return it.
    /// <br>**Note:** Naturally, any signal will only be emitted if the property's value does actually changes!
    /// This can not be guaranteed.
    /// \param target the QObject to connect to.
    /// \param setter
    /// \param propValue
    /// \return the property that the setter modified, or an invalid QMetaProperty if it could not be determined.
    ///
    QMetaProperty tryToFindPropertyBySetter(QObject* target, q_setter_t setter, const QVariant& propValue);

    ///
    /// \brief Sets a property on a target and attempts to record the property's name in the descriptor.
    /// \param descriptor
    /// \param target
    /// \param propValue
    ///
    void setProperty(property_descriptor& descriptor, QObject* target, const QVariant& propValue);

private:

    struct key_t {
        const QMetaObject* meta;
        detail::q_setter_t setter;

        friend bool operator==(const key_t& left, const key_t& right) {
            return left.meta == right.meta && left.setter == right.setter;
        }
    };

    struct key_hash {
        std::size_t operator()(const key_t& key) const {
            return std::hash<const void*>{}(key.meta) ^ hashCode(key.setter);
        }
    };

    std::unordered_map<key_t,QMetaProperty,key_hash> m_propertyNameCache;

};

class QPropertyProvider : public QObject {
    Q_OBJECT


private Q_SLOTS:

    virtual void notify() = 0;
};

}
