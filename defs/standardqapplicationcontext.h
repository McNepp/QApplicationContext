#pragma once

#include <mutex>
#include <memory>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <typeindex>
#include <QMetaProperty>
#include <QBindable>
#include "qapplicationcontext.h"

namespace mcnepp::qtdi {



///
/// \brief A ready-to use implementation of the QApplicationContext.
///
class StandardApplicationContext final : public QApplicationContext
{
    Q_OBJECT
public:
    explicit StandardApplicationContext(QObject *parent = nullptr);

    ~StandardApplicationContext();

    virtual bool publish(bool allowPartial = false) final override;

    virtual unsigned published() const final override;

    virtual unsigned pendingPublication() const override;


protected:

    virtual Registration* registerService(const QString& name, const service_descriptor& descriptor, const service_config& config) override;

    virtual Registration* registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) override;

    virtual Registration* getRegistration(const std::type_info& service_type, const QString& name) const override;


private:

    struct StandardRegistration : public Registration {
        StandardRegistration(StandardApplicationContext* parent) : Registration(parent) {

        }

        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }



    };


    struct DescriptorRegistration : public StandardRegistration {
        const std::type_info& service_type() const override {
            return descriptor.service_type;
        }


        QString registeredName() const override {
            return m_name;
        }



        virtual QObject* getObject() const = 0;

        virtual bool isPublished() const = 0;

        virtual bool isManaged() const = 0;

        virtual bool isEqual(const service_descriptor& descriptor, const service_config& config, QObject* obj) const = 0;

        virtual const service_config& config() const = 0;

        virtual QObjectList publishedObjects() const override {
            QObjectList result;
            if(isPublished()) {
                    result.push_back(getObject());
            }
            return result;
        }

        virtual void notifyPublished() = 0;


        bool matches(const std::type_index& type) const {
            return descriptor.matches(type);
        }

        static auto matcher(const std::type_index& type) {
            return [type](DescriptorRegistration* reg) { return reg->matches(type); };
        }


        DescriptorRegistration(const QString& name, const service_descriptor& desc, StandardApplicationContext* parent);


        virtual QObject* publish(const QVariantList& dependencies) = 0;

        virtual bool unpublish() = 0;

        virtual QObjectList privateObjects() const = 0;

        virtual QObject* createPrivateObject(const QVariantList& dependencies) = 0;


        service_descriptor descriptor;
        QString m_name;
        QVariantMap resolvedProperties;
        std::vector<QPropertyNotifier> bindings;
    };

    using descriptor_set = std::unordered_set<DescriptorRegistration*>;

    using descriptor_list = std::deque<DescriptorRegistration*>;


    struct ServiceRegistration : public DescriptorRegistration {

        ServiceRegistration(const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            theService(nullptr),
            m_config(config) {

        }

        void notifyPublished() override {
            if(theService) {
                    emit objectPublished(theService);
            }
        }

        virtual bool isPublished() const override {
            return theService != nullptr;
        }

        virtual QObject* getObject() const override {
            return theService;
        }

        virtual bool isManaged() const override {
            return true;
        }

        virtual void print(QDebug out) const override;

        virtual const service_config& config() const override {
            return m_config;
        }

        virtual QObject* createPrivateObject(const QVariantList& dependencies) override {
            QObject* obj = descriptor.create(dependencies);
            if(obj) {
                m_privateObjects.push_back(obj);
            }
            return obj;
        }

        virtual bool isEqual(const service_descriptor& descriptor, const service_config& config, QObject* obj) const override {
            return descriptor == this->descriptor && m_config == config;
        }

        virtual QObjectList privateObjects() const override {
            return m_privateObjects;
        }


        virtual QObject* publish(const QVariantList& dependencies) override {
            if(!theService) {
                theService = descriptor.create(dependencies);
                if(theService) {
                    onDestroyed = connect(theService, &QObject::destroyed, this, &ServiceRegistration::serviceDestroyed);
                }
            }
            return theService;
        }

        virtual QMetaObject::Connection subscribe(detail::Subscription* subscription) override {
            //If the Service is already present, there is no need to connect to the signal:
            if(theService) {
                emit subscription->objectPublished(theService);
                return QMetaObject::Connection{};
            }
            return connect(this, &Registration::objectPublished, subscription, &detail::Subscription::objectPublished);
        }

        void serviceDestroyed(QObject* srv);



        virtual bool unpublish() override {
            if(theService) {
                std::unique_ptr<QObject> srv{theService};
                QObject::disconnect(onDestroyed);
                theService = nullptr;
                return true;
            }
            return false;
        }

    private:
        QObject* theService;
        QObjectList m_privateObjects;
        service_config m_config;
        QMetaObject::Connection onDestroyed;
    };

    struct ObjectRegistration : public DescriptorRegistration {



        ObjectRegistration(const QString& name, const service_descriptor& desc, QObject* obj, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            theObj(obj){
            connect(obj, &QObject::destroyed, parent, &StandardApplicationContext::contextObjectDestroyed);
        }

        void notifyPublished() override {
        }

        virtual bool isEqual(const service_descriptor& descriptor, const service_config&, QObject* obj) const override {
            return descriptor == this->descriptor && theObj == obj;
        }

        virtual bool isPublished() const override {
            return true;
        }

        virtual bool unpublish() override {
            return false;
        }

        virtual bool isManaged() const override {
            return false;
        }


        virtual QObject* getObject() const override {
            return theObj;
        }

        virtual QObject* publish(const QVariantList& dependencies) override {
            return theObj;
        }

        virtual QObject* createPrivateObject(const QVariantList& dependencies) override {
            return nullptr;
        }

        virtual QObjectList privateObjects() const override {
            return {};
        }

        virtual void print(QDebug out) const override;

        virtual const service_config& config() const override {
            return defaultConfig;
        }

        virtual QMetaObject::Connection subscribe(detail::Subscription* subscription) override {
            //There is no need to connect to the signal, as the Object is already present
            emit subscription->objectPublished(theObj);
            return QMetaObject::Connection{};
        }

        static const service_config defaultConfig;


    private:
        QObject* const theObj;
    };


    struct ProxyRegistration : public StandardRegistration {



        ProxyRegistration(const std::type_info& type, StandardApplicationContext* parent) :
                StandardRegistration{parent},
                m_type(type){
        }



        const std::type_info& m_type;


        const type_info &service_type() const override {
            return m_type;
        }

        QString registeredName() const override {
            return QString{};
        }

        virtual QObjectList publishedObjects() const override {
            QObjectList result;
            for(auto reg : registrations) {
                if(reg->getObject()) {
                    result.push_back(reg->getObject());
                }
            }
            return result;
        }


        virtual void print(QDebug out) const override {
            out.nospace().noquote() << "Services [" << registrations.size() << "] with service-type '" << service_type().name() << "'";
        }


        void add(DescriptorRegistration* reg) {
            if(std::find(registrations.begin(), registrations.end(), reg) == registrations.end()) {
                registrations.push_back(reg);
                connect(reg, &Registration::objectPublished, this, &ProxyRegistration::objectPublished);
            }
        }

        void remove(DescriptorRegistration* reg) {
            auto found = std::find(registrations.begin(), registrations.end(), reg);
            if(found != registrations.end()) {
                registrations.erase(found);
            }
        }

        virtual QMetaObject::Connection subscribe(detail::Subscription* subscription) override {
            auto connection = connect(this, &Registration::objectPublished, subscription, &detail::Subscription::objectPublished);
            for(auto reg : registrations) {
                auto obj = reg->getObject();
                if(obj) {
                    emit subscription->objectPublished(obj);
                }
            }
            return connection;
        }




    private:

        descriptor_list registrations;
    };



    enum class Status {
        ok,
        fixable,
        fatal
    };

    struct ResolvedBeanRef {
        QVariant resolvedValue;
        Status status;
        bool resolved;
        QMetaProperty sourceProperty;
        QObject* source;
    };


    template<typename C> static DescriptorRegistration* find_by_type(const C& regs, const std::type_index& type);

    bool checkTransitiveDependentsOn(const service_descriptor& descriptor, const std::unordered_set<std::type_index>& dependencies) const;

    void findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<std::type_index>& dependents) const;

    void unpublish();

    QVariant getConfigurationValue(const QString& key, const QVariant& defaultValue) const;

    static QStringList getBeanRefs(const service_config&);

    void contextObjectDestroyed(QObject*);

    DescriptorRegistration* getRegistrationByName(const QString& name) const;


    std::pair<QVariant,Status> resolveDependency(const descriptor_list& published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial);

    DescriptorRegistration* registerDescriptor(QString name, const service_descriptor& descriptor, const service_config& config, QObject* obj);

    Status configure(DescriptorRegistration*,QObject*, const QList<QApplicationContextPostProcessor*>& postProcessors, bool allowPartial);


    ResolvedBeanRef resolveBeanRef(const QVariant& value, bool allowPartial);

    std::pair<QVariant,Status> resolveProperty(const QString& group, const QVariant& valueOrPlaceholder, const QVariant& defaultValue, bool allowPartial);


    descriptor_list registrations;

    std::unordered_map<QString,DescriptorRegistration*> registrationsByName;



    mutable std::unordered_map<std::type_index,ProxyRegistration*> proxyRegistrationCache;
};


namespace detail {

class BindingProxy : public QObject {
    Q_OBJECT

public:
    BindingProxy(QMetaProperty sourceProp, QObject* source, QMetaProperty targetProp, QObject* target);

    static const QMetaMethod& notifySlot();

private slots:
    void notify();
private:
    QMetaProperty m_sourceProp;
    QObject* m_source;
    QMetaProperty m_targetProp;
    QObject* m_target;

};
}


}

