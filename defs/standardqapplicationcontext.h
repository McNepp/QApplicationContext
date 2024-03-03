#pragma once

#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <typeindex>
#include <QMetaProperty>
#include <QMutex>
#include <QWaitCondition>
#include <QBindable>
#include "qapplicationcontext.h"

namespace mcnepp::qtdi {


///
/// \brief A ready-to use implementation of the QApplicationContext.
///
class StandardApplicationContext final : public QApplicationContext
{
    Q_OBJECT

    class CreateRegistrationHandleEvent;

public:
    explicit StandardApplicationContext(QObject *parent = nullptr);

    ~StandardApplicationContext();

    virtual bool publish(bool allowPartial = false) final override;

    virtual unsigned published() const final override;

    virtual unsigned pendingPublication() const override;

    virtual QList<service_registration_handle_t> getRegistrationHandles() const override;


protected:

    virtual detail::ServiceRegistration* registerService(const QString& name, const service_descriptor& descriptor, const service_config& config) override;

    virtual detail::ServiceRegistration* registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) override;

    virtual detail::ServiceRegistration* getRegistrationHandle(const QString& name) const override;

    virtual detail::ProxyRegistration* getRegistrationHandle(const std::type_info& service_type, const QMetaObject* metaObject) const override;


private:


    bool registerAlias(detail::ServiceRegistration* reg, const QString& alias);


    class StandardRegistrationImpl {


    public:

        virtual QMetaProperty getProperty(const char* name) const = 0;

        virtual bool registerBoundProperty(const char* name) = 0;


    };

    class AutowireSubscription : public detail::Subscription {
    public:
        AutowireSubscription(detail::q_inject_t injector, QObject* bound) : Subscription(bound),
            m_injector(injector),
            m_bound(bound)
        {
            out_Connection = QObject::connect(this, &Subscription::objectPublished, this, &AutowireSubscription::notify);
        }

        void notify(QObject* obj);

        void cancel() override;

        void connectTo(detail::Registration* source) override;


    private:
        detail::q_inject_t m_injector;
        QObject* m_bound;
        std::vector<QPointer<detail::Subscription>> subscriptions;
        QMetaObject::Connection out_Connection;
        QMetaObject::Connection in_Connection;
    };


    static constexpr int STATE_INIT = 0;
    static constexpr int STATE_CREATED = 1;
    static constexpr int STATE_PUBLISHED = 3;

    class DescriptorRegistration : public detail::ServiceRegistration, public StandardRegistrationImpl {
    private:



        class PropertyBindingSubscription : public detail::Subscription {
            friend class DescriptorRegistration;
        public:

            void notify(QObject* obj);

            void cancel() override;

            void connectTo(Registration* source) override;

        private:
            PropertyBindingSubscription(detail::Registration* target, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter) : detail::Subscription(target),
                m_sourceProperty(sourceProperty),
                m_setter(setter),
                m_target(target) {
                out_Connection = QObject::connect(this, &Subscription::objectPublished, this, &PropertyBindingSubscription::notify);
            }
            detail::Registration* m_target;
            QMetaProperty m_sourceProperty;
            detail::property_descriptor m_setter;
            std::vector<QPointer<Subscription>> subscriptions;
            QMetaObject::Connection out_Connection;
            QMetaObject::Connection in_Connection;
        };

        class PropertyInjector : public detail::Subscription {
            friend class PropertyBindingSubscription;
        public:

            void notify(QObject* obj);

            void cancel() override;

            void connectTo(Registration* source) override;
        private:

            PropertyInjector(QObject* boundSource, const QMetaProperty& sourceProperty, const detail::property_descriptor& setter) : detail::Subscription(boundSource),
                m_sourceProperty(sourceProperty),
                m_setter(setter),
                m_boundSource(boundSource) {
                out_Connection = QObject::connect(this, &Subscription::objectPublished, this, &PropertyInjector::notify);
            }
            QMetaProperty m_sourceProperty;
            detail::property_descriptor m_setter;
            QObject* m_boundSource;
            std::vector<QPropertyNotifier> bindings;
            std::vector<QMetaObject::Connection> connections;
            QMetaObject::Connection out_Connection;
            QMetaObject::Connection in_Connection;
        };

    public:
        virtual detail::Subscription* createBindingTo(const char* sourcePropertyName, Registration* target, const detail::property_descriptor& targetProperty) override;


        virtual detail::Subscription* createAutowiring(const std::type_info& type, detail::q_inject_t injector, Registration* source) override;


        QString registeredName() const override {
            return m_name;
        }


        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }

        virtual QObject* getObject() const = 0;

        virtual int state() const = 0;

        bool isPublished() const {
            return state() == STATE_PUBLISHED;
        }

        virtual bool isManaged() const = 0;


        virtual const service_config& config() const = 0;


        virtual QStringList getBeanRefs() const = 0;

        virtual void notifyPublished() = 0;

        virtual bool registerAlias(const QString& alias) override {
            return applicationContext()->registerAlias(this, alias);
        }

        bool matches(const std::type_info& type) const override {
            return descriptor.matches(type);
        }

        bool matches(const std::type_index& type) const {
            return descriptor.matches(type);
        }





        static auto matcher(const std::type_info& type) {
            return [&type](DescriptorRegistration* reg) { return reg->matches(type); };
        }

        static auto matcher(const dependency_info& info) {
            return [&info](DescriptorRegistration* reg) {
                if(!reg->matches(info.type)) {
                    return false;
                }
                return !info.has_required_name() || reg->registeredName() == info.expression;
            };
        }


        DescriptorRegistration(const QString& name, const service_descriptor& desc, StandardApplicationContext* parent);


        virtual QObject* publish(const QVariantList& dependencies) = 0;

        virtual bool unpublish() = 0;

        virtual QObjectList privateObjects() const = 0;

        virtual QObject* createPrivateObject(const QVariantList& dependencies) = 0;

        virtual bool registerBoundProperty(const char* name) override {
            return boundProperties.insert(name).second;
        }

        virtual void resolveProperty(const QString& key, const QVariant& value) = 0;


        service_descriptor descriptor;
        QString m_name;
        std::vector<QPropertyNotifier> bindings;
        std::unordered_set<QString> boundProperties;
        std::unordered_set<std::type_index> autowirings;
    };

    using descriptor_set = std::unordered_set<DescriptorRegistration*>;

    using descriptor_list = std::deque<DescriptorRegistration*>;


    struct ServiceRegistration : public DescriptorRegistration {



        ServiceRegistration(const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            theService(nullptr),
            m_config(config),
            resolvedProperties{config.properties},
            m_state(STATE_INIT)        {

        }

        void notifyPublished() override {
            if(theService) {
                m_state = STATE_PUBLISHED;
                emit objectPublished(theService);
            }
        }

        virtual int state() const override {
            return m_state;
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

        QVariantMap registeredProperties() const override {
            return resolvedProperties;
        }

        virtual void resolveProperty(const QString& key, const QVariant& value) override {
            resolvedProperties.insert(key, value);
        }


        virtual QObject* createPrivateObject(const QVariantList& dependencies) override {
            QObject* obj = descriptor.create(dependencies);
            if(obj) {
                m_privateObjects.push_back(obj);
            }
            return obj;
        }

        virtual QStringList getBeanRefs() const override;

        virtual QObjectList privateObjects() const override {
            return m_privateObjects;
        }


        virtual QObject* publish(const QVariantList& dependencies) override {
            if(!theService) {
                theService = descriptor.create(dependencies);
                if(theService) {
                    onDestroyed = connect(theService, &QObject::destroyed, this, &ServiceRegistration::serviceDestroyed);
                    m_state = STATE_CREATED;
                }
            }
            return theService;
        }

        virtual void onSubscription(detail::Subscription* subscription) override {
            //If the Service is already present, there is no need to connect to the signal:
            if(isPublished()) {
                emit subscription->objectPublished(theService);
            } else {
                subscription->connectTo(this);
            }
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
                m_state = STATE_INIT;
                return true;
            }
            return false;
        }



    private:
        QObject* theService;
        QObjectList m_privateObjects;
        service_config m_config;
        QMetaObject::Connection onDestroyed;
        QVariantMap resolvedProperties;
        int m_state;
        mutable std::optional<QStringList> beanRefsCache;
    };

    struct ObjectRegistration : public DescriptorRegistration {



        ObjectRegistration(const QString& name, const service_descriptor& desc, QObject* obj, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            theObj(obj){
            connect(obj, &QObject::destroyed, parent, &StandardApplicationContext::contextObjectDestroyed);
        }

        void notifyPublished() override {
        }



        virtual int state() const override {
            return STATE_PUBLISHED;
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

        QVariantMap registeredProperties() const override {
            return QVariantMap{};
        }

        virtual void resolveProperty(const QString& key, const QVariant& value) override {
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

        virtual QStringList getBeanRefs() const override {
            return QStringList{};
        }

        virtual void print(QDebug out) const override;

        virtual const service_config& config() const override {
            return defaultConfig;
        }

        virtual void onSubscription(detail::Subscription* subscription) override {
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

        class ProxySubscription : public detail::Subscription {
        public:
            explicit ProxySubscription(ProxyRegistration* target) :
                detail::Subscription{target} {
                out_connection = QObject::connect(this, &Subscription::objectPublished, target, &Registration::objectPublished);
            }

            void connectTo(Registration* source) override {
                in_connections.push_back(connect(source, this));
            }

            void cancel() override {
                QObject::disconnect(out_connection);
                for(auto& connection : in_connections) {
                    QObject::disconnect(connection);
                }
            }


            QMetaObject::Connection out_connection;
            QList<QMetaObject::Connection> in_connections;
        };

        ///
        /// \brief Passes the signal through, but does not accept connections from a source-Registration.
        ///
        class TemporarySubscriptionProxy : public detail::Subscription {
        public:
            explicit TemporarySubscriptionProxy(Subscription* target) :
                detail::Subscription{target} {
                QObject::connect(this, &Subscription::objectPublished, target, &Subscription::objectPublished);
            }

            void connectTo(Registration* source) override {
                //Does nothing intentionally
            }

            void cancel() override {
            }
        };


        ProxyRegistration(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent, const descriptor_list& registrations) :
            detail::ProxyRegistration{parent},
                m_type(type),
            m_meta(metaObject)
        {
            proxySubscription = new ProxySubscription{this};
            for(auto reg : registrations) {
                add(reg);
            }
        }

        virtual detail::Subscription* createAutowiring(const std::type_info& type, detail::q_inject_t injector, Registration* source) override;

        bool matches(const std::type_info& type) const override {
            return m_type == type;
        }

        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }

        virtual QList<detail::ServiceRegistration*> registeredServices() const override {
            QMutexLocker locker{&mutex};
            return QList<detail::ServiceRegistration*>{registrations.begin(), registrations.end()};
        }



        bool add(DescriptorRegistration* reg) {
            {
                QMutexLocker locker{&mutex};
                if(!reg->matches(m_type) || std::find(registrations.begin(), registrations.end(), reg) != registrations.end()) {
                    return false;
                }
                registrations.push_back(reg);
            }
            //May emit a signal, therefore do it after releasing the Mutex:
            reg->subscribe(proxySubscription);
            return true;
        }



        virtual void onSubscription(detail::Subscription* subscription) override {
            detail::Subscription::connect(this, subscription);
            TemporarySubscriptionProxy tempProxy{subscription};
            //By subscribing to a TemporarySubscriptionProxy, we force exisiting objects to be signalled immediately, while not creating any new Connections:
            for(auto reg : registeredServices()) {
                reg->subscribe(&tempProxy);
            }
        }

        virtual QMetaProperty getProperty(const char* name) const override {
            if(m_meta) {
                return m_meta->property(m_meta->indexOfProperty(name));
            }
            return QMetaProperty{};
        }


        void remove(DescriptorRegistration* reg) {
            QMutexLocker locker{&mutex};
            auto found = std::find(registrations.begin(), registrations.end(), reg);
            if(found != registrations.end()) {
                registrations.erase(found);
            }
        }




        virtual void print(QDebug out) const final override {
            out.nospace().noquote() << "Services [" << registrations.size() << "] with service-type '" << m_type.name() << "'";
        }

        virtual bool registerBoundProperty(const char* name) override {
            //We do not need to lock the mutex, as the member 'boundProperties' is only ever read and written to from the ApplicationContext's thread.
            //See: DescriptorRegistration::createBindingTo()
            return boundProperties.insert(name).second;
        }


        const std::type_info& m_type;
        descriptor_list registrations;
        const QMetaObject* m_meta;
        std::unordered_set<QString> boundProperties;
        std::unordered_set<std::type_index> autowirings;
        ProxySubscription* proxySubscription;
        mutable QMutex mutex;
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

    Status validate(bool allowPartial, const descriptor_list& published, descriptor_list& unpublished);

    template<typename C> static DescriptorRegistration* find_by_type(const C& regs, const std::type_info& type);

    bool checkTransitiveDependentsOn(const service_descriptor& descriptor, const std::unordered_set<std::type_index>& dependencies) const;

    void findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<std::type_index>& dependents) const;

    void unpublish();

    QVariant getConfigurationValue(const QString& key, const QVariant& defaultValue) const;

    void contextObjectDestroyed(QObject*);

    DescriptorRegistration* getRegistrationByName(const QString& name) const;


    std::pair<QVariant,Status> resolveDependency(const descriptor_list& published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial, bool publish, QObject* temporaryPrivateParent);

    DescriptorRegistration* registerDescriptor(QString name, const service_descriptor& descriptor, const service_config& config, QObject* obj);

    Status configure(DescriptorRegistration*,QObject*, const QList<QApplicationContextPostProcessor*>& postProcessors, bool allowPartial);


    ResolvedBeanRef resolveBeanRef(const QVariant& value, bool allowPartial);

    std::pair<QVariant,Status> resolveProperty(const QString& group, const QVariant& valueOrPlaceholder, const QVariant& defaultValue, bool allowPartial);

    // QObject interface
public:
    bool event(QEvent *event) override;

private:

    descriptor_list registrations;

    std::unordered_map<QString,DescriptorRegistration*> registrationsByName;



    mutable std::unordered_map<std::type_index,ProxyRegistration*> proxyRegistrationCache;
    mutable QMutex mutex;
    mutable QWaitCondition condition;
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

