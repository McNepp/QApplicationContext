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

//Forward-declarations of nested class:
    class CreateRegistrationHandleEvent;

    class DescriptorRegistration;

public:
    explicit StandardApplicationContext(QObject *parent = nullptr);

    ~StandardApplicationContext();

    virtual bool publish(bool allowPartial = false) final override;

    virtual unsigned published() const final override;

    virtual unsigned pendingPublication() const override;

    virtual QVariant getConfigurationValue(const QString& key) const override;

    using QApplicationContext::registerObject;

    using QApplicationContext::registerService;

    using QApplicationContext::registerPrototype;

protected:

    virtual service_registration_handle_t registerService(const QString& name, const service_descriptor& descriptor, const service_config& config, bool prototype) override;

    virtual service_registration_handle_t registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) override;

    virtual service_registration_handle_t getRegistrationHandle(const QString& name) const override;

    virtual proxy_registration_handle_t getRegistrationHandle(const std::type_info& service_type, const QMetaObject* metaObject) const override;

    virtual QList<service_registration_handle_t> getRegistrationHandles() const override;


private:


    bool registerAlias(service_registration_handle_t reg, const QString& alias);



    using descriptor_set = std::unordered_set<DescriptorRegistration*>;

    using descriptor_list = std::deque<DescriptorRegistration*>;


    static constexpr int STATE_INIT = 0;
    static constexpr int STATE_CREATED = 1;
    static constexpr int STATE_PUBLISHED = 3;

    class DescriptorRegistration : public detail::ServiceRegistration {
        friend class StandardApplicationContext;

        virtual subscription_handle_t createBindingTo(const char* sourcePropertyName, registration_handle_t target, const detail::property_descriptor& targetProperty) override;


        virtual subscription_handle_t createAutowiring(const std::type_info& type, detail::q_inject_t injector, registration_handle_t source) override;


        QString registeredName() const override {
            return m_name;
        }


        virtual StandardApplicationContext* applicationContext() const final override {
            return m_context;
        }


        virtual QObject* getObject() const = 0;

        virtual int state() const = 0;

        bool isPublished() const {
            return state() == STATE_PUBLISHED;
        }

        bool isManaged() const {
            return scope() != ServiceScope::EXTERNAL;
        }

        virtual const service_descriptor& descriptor() const final override {
            return m_descriptor;
        }


        virtual const service_config& config() const = 0;


        virtual QStringList getBeanRefs() const = 0;

        virtual void notifyPublished() = 0;

        virtual bool registerAlias(const QString& alias) override {
            return applicationContext()->registerAlias(this, alias);
        }

        bool matches(const std::type_info& type) const override {
            return descriptor().matches(type) || type == typeid(QObject);
        }

        bool matches(const dependency_info& info) const {
            return matches(info.type) && (!info.has_required_name() || info.expression == registeredName());
        }


        unsigned index() const {
            return m_index;
        }



        static auto matcher(const dependency_info& info) {
            return [&info](DescriptorRegistration* reg) { return reg->matches(info); };
        }


        DescriptorRegistration(unsigned index, const QString& name, const service_descriptor& desc, StandardApplicationContext* context, QObject* parent);

        DescriptorRegistration(unsigned index, const QString& name, const service_descriptor& desc, StandardApplicationContext* parent) :
        DescriptorRegistration{index, name, desc, parent, parent} {

        }

        virtual QObject* createService(const QVariantList& dependencies, descriptor_list& created) = 0;

        virtual int unpublish() = 0;

        virtual void resolveProperty(const QString& key, const QVariant& value) = 0;
    protected:

        service_descriptor m_descriptor;
        QString m_name;
        std::vector<QPropertyNotifier> bindings;
        std::unordered_set<std::type_index> autowirings;
        unsigned const m_index;
        StandardApplicationContext* const m_context;
    };



    class ServiceRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        ServiceRegistration(unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent) :
            DescriptorRegistration{index, name, desc, context, parent},
            theService(nullptr),
            m_config(config),
            resolvedProperties{config.properties},
            m_state(STATE_INIT)        {

        }

        ServiceRegistration(unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
        ServiceRegistration{index, name, desc, config, parent, parent} {

        }


        virtual ServiceScope scope() const override {
            return ServiceScope::SINGLETON;
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



        virtual QStringList getBeanRefs() const override;



        virtual QObject* createService(const QVariantList& dependencies, descriptor_list& created) override;


        virtual void onSubscription(subscription_handle_t subscription) override {
            //If the Service is already present, there is no need to connect to the signal:
            if(isPublished()) {
                emit subscription->objectPublished(theService);
            } else {
                subscription->connectTo(this);
            }
        }



        void serviceDestroyed(QObject* srv);



        virtual int unpublish() override {
            if(theService) {
                std::unique_ptr<QObject> srv{theService};
                QObject::disconnect(onDestroyed);
                theService = nullptr;
                m_state = STATE_INIT;
                return 1;
            }
            return 0;
        }



    private:
        QObject* theService;
        service_config m_config;
        QMetaObject::Connection onDestroyed;
        QVariantMap resolvedProperties;
        int m_state;
        mutable std::optional<QStringList> beanRefsCache;
    };



    class PrototypeRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        PrototypeRegistration(unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent);

        virtual ServiceScope scope() const override {
            return ServiceScope::PROTOTYPE;
        }


        void notifyPublished() override {
        }

        virtual int state() const override {
            return m_state;
        }

        virtual QObject* getObject() const override {
            //PrototypeRegistration returns this here. It will be resolved later in QApplicationContext::resolveDependencies().
            return const_cast<PrototypeRegistration*>(this);
        }

        virtual void print(QDebug out) const override;

        virtual const service_config& config() const override {
            return m_config;
        }

        QVariantMap registeredProperties() const override {
            return QVariantMap{};
        }

        virtual void resolveProperty(const QString&, const QVariant&) override {
        }


        virtual subscription_handle_t createBindingTo(const char* sourcePropertyName, registration_handle_t target, const detail::property_descriptor& targetProperty) override;

        virtual QStringList getBeanRefs() const override;




        virtual QObject* createService(const QVariantList& dependencies, descriptor_list& created) override;

        virtual void onSubscription(subscription_handle_t subscription) override;





        virtual int unpublish() override;


        QVariantList m_dependencies;


    private:
        int m_state;
        service_config m_config;
        mutable std::optional<QStringList> beanRefsCache;
        subscription_handle_t proxySubscription;
    };

    class ObjectRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        ObjectRegistration(unsigned index, const QString& name, const service_descriptor& desc, QObject* obj, StandardApplicationContext* parent) :
            DescriptorRegistration{index, name, desc, parent},
            theObj(obj){
            //Do not connect the signal QObject::destroyed if obj is the ApplicationContext itself:
            if(obj != parent) {
                connect(obj, &QObject::destroyed, parent, &StandardApplicationContext::contextObjectDestroyed);
            }
        }

        void notifyPublished() override {
        }

        virtual ServiceScope scope() const override {
            return ServiceScope::EXTERNAL;
        }


        virtual int state() const override {
            return STATE_PUBLISHED;
        }

        virtual int unpublish() override {
            return 0;
        }




        virtual QObject* getObject() const override {
            return theObj;
        }

        QVariantMap registeredProperties() const override {
            return QVariantMap{};
        }

        virtual void resolveProperty(const QString&, const QVariant&) override {
        }



        virtual QObject* createService(const QVariantList&, descriptor_list&) override {
            return theObj;
        }


        virtual QStringList getBeanRefs() const override {
            return QStringList{};
        }

        virtual void print(QDebug out) const override;

        virtual const service_config& config() const override {
            return defaultConfig;
        }

        virtual void onSubscription(subscription_handle_t subscription) override {
            emit subscription->objectPublished(theObj);
        }






        static const service_config defaultConfig;


    private:
        QObject* const theObj;
    };


    class ProxyRegistrationImpl : public detail::ProxyRegistration {


        friend class StandardApplicationContext;

        ProxyRegistrationImpl(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent);


        virtual subscription_handle_t createAutowiring(const std::type_info& type, detail::q_inject_t injector, registration_handle_t source) override;

        bool matches(const std::type_info& type) const override {
            return m_type == type || type == typeid(QObject);
        }

        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }

        virtual QList<service_registration_handle_t> registeredServices() const override;

        virtual const QMetaObject* serviceMetaObject() const override {
            return m_meta;
        }



        bool add(service_registration_handle_t reg) {
            if(reg->matches(m_type)) {
                reg->subscribe(proxySubscription);
                return true;
            }
            return false;
        }

        virtual void onSubscription(subscription_handle_t subscription) override;



        virtual void print(QDebug out) const final override {
            out.nospace().noquote() << "Services [" << registeredServices().size() << "] with service-type '" << m_type.name() << "'";
        }




        const std::type_info& m_type;
        const QMetaObject* m_meta;
        std::unordered_set<std::type_index> autowirings;
        subscription_handle_t proxySubscription;
    };




    static QMetaProperty getProperty(registration_handle_t reg, const char* name) {
        if(auto meta = reg->serviceMetaObject()) {
            return meta->property(meta->indexOfProperty(name));
        }
        return QMetaProperty{};
    }




    enum class Status {
        ok,
        fixable,
        fatal
    };


    Status validate(bool allowPartial, const descriptor_list& published, descriptor_list& unpublished);

    bool checkTransitiveDependentsOn(const service_descriptor& descriptor, const QString& name, const std::unordered_set<dependency_info>& dependencies) const;

    void findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<dependency_info>& dependents) const;

    void unpublish();

    void contextObjectDestroyed(QObject*);

    DescriptorRegistration* getRegistrationByName(const QString& name) const;


    std::pair<QVariant,Status> resolveDependency(const descriptor_list& published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial);

    static QVariantList resolveDependencies(const QVariantList& dependencies, descriptor_list& created);

    static QVariant resolveDependency(const QVariant& arg, descriptor_list& created);

    DescriptorRegistration* registerDescriptor(QString name, const service_descriptor& descriptor, const service_config& config, QObject* obj, ServiceScope scope);

    Status configure(DescriptorRegistration*, descriptor_list& toBePublished, bool allowPartial);

    Status init(DescriptorRegistration*, const QList<QApplicationContextPostProcessor*>& postProcessors);

    std::pair<Status,bool> resolveBeanRef(QVariant& value, descriptor_list& toBePublished, bool allowPartial);

    std::pair<QVariant,Status> resolvePlaceholders(const QString& key, const QString& group);

    DescriptorRegistration* findAutowiringCandidate(DescriptorRegistration*, const QMetaProperty&);

    bool registerBoundProperty(registration_handle_t target, const char* propName);

    // QObject interface
public:
    bool event(QEvent *event) override;

private:

    descriptor_list registrations;

    std::unordered_map<QString,DescriptorRegistration*> registrationsByName;



    mutable std::unordered_map<std::type_index,ProxyRegistrationImpl*> proxyRegistrationCache;
    mutable QMutex mutex;
    mutable QWaitCondition condition;
    std::unordered_map<registration_handle_t,std::unordered_set<QString>> m_boundProperties;
    std::atomic<unsigned> nextIndex;
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

