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

    virtual detail::ServiceRegistration* registerService(const QString& name, const service_descriptor& descriptor, const service_config& config) override;

    virtual detail::ServiceRegistration* registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) override;

    virtual detail::ServiceRegistration* getRegistration(const std::type_info& service_type, const QString& name) const override;

    virtual detail::ProxyRegistration* getRegistration(const std::type_info& service_type, LookupKind lookup, q_predicate_t dynamicCheck, const QMetaObject* metaObject) const override;


private:



    class StandardRegistrationImpl {


    public:

        virtual QMetaProperty getProperty(const char* name) const = 0;

        virtual bool registerBoundProperty(const char* name) = 0;


    };

    class DescriptorRegistration : public detail::ServiceRegistration, public StandardRegistrationImpl {
    private:



        class PropertyBindingSubscription : public detail::Subscription {
            friend class DescriptorRegistration;
        public:

            void notify(QObject* obj) override;

            void cancel() override;
        private:
            PropertyBindingSubscription(detail::Registration* source, detail::Registration* target, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter) : detail::Subscription(target, Qt::AutoConnection),
                m_sourceProperty(sourceProperty),
                m_setter(setter),
                m_source(source) {

            }
            detail::Registration* m_source;
            QMetaProperty m_sourceProperty;
            detail::property_descriptor m_setter;
            std::vector<QPointer<Subscription>> subscriptions;
        };

        class PropertyInjector : public detail::Subscription {
            friend class PropertyBindingSubscription;
        public:

            void notify(QObject* obj) override;

            void cancel() override;
        private:

            PropertyInjector(QObject* boundTarget, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter) : detail::Subscription(boundTarget, Qt::AutoConnection),
                m_sourceProperty(sourceProperty),
                m_setter(setter),
                m_boundTarget(boundTarget) {

            }
            QMetaProperty m_sourceProperty;
            detail::property_descriptor m_setter;
            QObject* m_boundTarget;
            std::vector<QPropertyNotifier> bindings;
            std::vector<QMetaObject::Connection> connections;
        };

    public:
        virtual detail::Subscription* createBindingTo(const char* sourcePropertyName, Registration* target, const detail::property_descriptor& targetProperty) override;


        const std::type_info& service_type() const override {
            return descriptor.service_type;
        }


        QString registeredName() const override {
            return m_name;
        }

        QVariantMap registeredProperties() const override {
            return resolvedProperties;
        }

        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }

        virtual QObject* getObject() const = 0;

        virtual bool isPublished() const = 0;

        virtual bool isManaged() const = 0;

        virtual bool isEqual(const service_descriptor& descriptor, const service_config& config, QObject* obj) const = 0;

        virtual const service_config& config() const = 0;



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

        virtual bool registerBoundProperty(const char* name) override {
            return boundProperties.insert(name).second;
        }


        service_descriptor descriptor;
        QString m_name;
        QVariantMap resolvedProperties;
        std::vector<QPropertyNotifier> bindings;
        std::unordered_set<QString> boundProperties;
    };

    using descriptor_set = std::unordered_set<DescriptorRegistration*>;

    using descriptor_list = std::deque<DescriptorRegistration*>;


    struct ServiceRegistration : public DescriptorRegistration {



        ServiceRegistration(const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            theService(nullptr),
            m_config(config) {
            resolvedProperties = config.properties;

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

        virtual void subscribe(detail::Subscription* subscription) override {
            //If the Service is already present, there is no need to connect to the signal:
            if(theService) {
                emit subscription->objectPublished(theService);
                return;
            }
            Registration::subscribe(subscription);
        }

        virtual QMetaProperty getProperty(const char* name) const override {
            if(descriptor.meta_object) {
                return descriptor.meta_object->property(descriptor.meta_object->indexOfProperty(name));
            }
            return QMetaProperty{};
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

        virtual void subscribe(detail::Subscription* subscription) override {
            //There is no need to connect to the signal, as the Object is already present
            emit subscription->objectPublished(theObj);
        }

        virtual QMetaProperty getProperty(const char* name) const override {
            auto meta = theObj->metaObject();
            return meta->property(meta->indexOfProperty(name));
        }


        static const service_config defaultConfig;


    private:
        QObject* const theObj;
    };

    struct ProxyRegistration : public detail::ProxyRegistration, public StandardRegistrationImpl {




        ProxyRegistration(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent) :
            detail::ProxyRegistration{parent},
                m_type(type),
            m_meta(metaObject) {
        }



        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }

        virtual QList<detail::ServiceRegistration*> registeredServices() const override {
            return QList<detail::ServiceRegistration*>{registrations.begin(), registrations.end()};
        }




        virtual QMetaProperty getProperty(const char* name) const override {
            if(m_meta) {
                return m_meta->property(m_meta->indexOfProperty(name));
            }
            return QMetaProperty{};
        }

        virtual bool add(DescriptorRegistration* reg) = 0;


        void remove(DescriptorRegistration* reg) {
            auto found = std::find(registrations.begin(), registrations.end(), reg);
            if(found != registrations.end()) {
                registrations.erase(found);
            }
        }


        const type_info &service_type() const final override {
            return m_type;
        }



        virtual void print(QDebug out) const final override {
            out.nospace().noquote() << "Services [" << registrations.size() << "] with service-type '" << service_type().name() << "'";
        }

        virtual bool registerBoundProperty(const char* name) override {
            return boundProperties.insert(name).second;
        }


        const std::type_info& m_type;
        descriptor_list registrations;
        const QMetaObject* m_meta;
        std::unordered_set<QString> boundProperties;

    };



    struct StaticProxyRegistration : public ProxyRegistration {



        StaticProxyRegistration(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent) :
            ProxyRegistration{type, metaObject, parent} {
        }




        bool add(DescriptorRegistration* reg) override {
            if(reg->matches(service_type()) && std::find(registrations.begin(), registrations.end(), reg) == registrations.end()) {
                registrations.push_back(reg);
                if(reg->isPublished()) {
                    emit objectPublished(reg->getObject());
                } else {
                    connect(reg, &Registration::objectPublished, this, &ProxyRegistration::objectPublished);
                }
            }
            return false;
        }



        virtual void subscribe(detail::Subscription* subscription) override {
            Registration::subscribe(subscription);
            for(auto reg : registrations) {
                auto obj = reg->getObject();
                if(obj) {
                    emit subscription->objectPublished(obj);
                }
            }
        }


    };


    struct DynamicProxyRegistration : public ProxyRegistration {



        DynamicProxyRegistration(const std::type_info& type, q_predicate_t check, const QMetaObject* metaObject, StandardApplicationContext* parent) :
            ProxyRegistration{type, metaObject, parent},
            dynamicCheck(check) {
        }








        virtual bool add(DescriptorRegistration* reg) override {
            if(std::find(registrations.begin(), registrations.end(), reg) == registrations.end()) {
                registrations.push_back(reg);
                if(reg->isPublished() && dynamicCheck(reg->getObject())) {
                    emit objectPublished(reg->getObject());
                } else {
                    QObject::connect(reg, &Registration::objectPublished, this, &DynamicProxyRegistration::objectPublishedSlot);
                }
                return true;
            }
            return false;
        }


        virtual void subscribe(detail::Subscription* subscription) override {
            Registration::subscribe(subscription);
            for(auto reg : registrations) {
                auto obj = reg->getObject();
                if(dynamicCheck(obj)) {
                    emit subscription->objectPublished(obj);
                }
            }
        }





    private:
        void objectPublishedSlot(QObject* obj) {
            if(dynamicCheck(obj)) {
                emit objectPublished(obj);
            }
        }

        q_predicate_t dynamicCheck;
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

    using proxy_key_t = std::pair<std::type_index,LookupKind>;

    struct proxy_hash {
        std::size_t operator()(const proxy_key_t& arg) const {
            return arg.first.hash_code() ^ static_cast<std::size_t>(arg.second);
        }
    };

    mutable std::unordered_map<proxy_key_t,ProxyRegistration*,proxy_hash> proxyRegistrationCache;
};


namespace detail {

class BindingProxy : public QObject {
    Q_OBJECT

public:
    BindingProxy(QMetaProperty sourceProp, QObject* source, const detail::property_descriptor& setter, QObject* target);

    static const QMetaMethod& notifySlot();


private slots:
    void notify();
private:
    QMetaProperty m_sourceProp;
    QObject* m_source;
    QObject* m_target;
    detail::property_descriptor m_setter;
};
}


}

