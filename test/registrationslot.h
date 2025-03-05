#pragma once

#include "qapplicationcontext.h"

namespace mcnepp::qtdi {
    
template<typename S> class RegistrationSlot {
public:

    RegistrationSlot(const Registration<S>& registration, QObject* context)
    {
        m_subscription = const_cast<Registration<S>&>(registration).subscribe(context, [this](S* srv) { m_obj.push_back(srv);});
    }


    S* operator->() const {
        return m_obj.empty() ? nullptr : m_obj.back();
    }

    S* last() const {
        return m_obj.empty() ? nullptr : m_obj.back();
    }

    explicit operator bool() const {
        return !m_obj.empty();
    }





    bool operator ==(const RegistrationSlot& other) const {
        return m_obj == other.m_obj;
    }

    bool operator !=(const RegistrationSlot& other) const {
        return m_obj != other.m_obj;
    }

    int invocationCount() const {
        return m_obj.size();
    }

    int size() const {
        return m_obj.size();
    }

    S* operator[](std::size_t index) const {
        return m_obj[index];
    }

    Subscription& subscription() {
        return m_subscription;
    }

private:
    QList<S*> m_obj;
    Subscription m_subscription;
};

}
