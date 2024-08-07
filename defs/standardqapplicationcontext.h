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

    struct delegate_tag_t {

    };

public:


    ///
    /// \brief Determines that a StandardApplicationContext is used as a delegate by another ApplicationContext.
    /// \sa StandardApplicationContext(const QLoggingCategory&, QApplicationContext*, delegate_tag_t);
    ///
    static constexpr delegate_tag_t delegate_tag{};


    /**
     * @brief Creates a StandardApplicationContext using an explicit LoggingCategory.
     * @param loggingCategory a reference to the QLoggingCategory that will be used by this ApplicationContext.
     * @param parent will become the QObject::parent().
     */
    explicit StandardApplicationContext(const QLoggingCategory& loggingCategory, QObject *parent = nullptr):
        StandardApplicationContext{loggingCategory, this, parent} {
    }

    /**
     * @brief Creates a StandardApplicationContext with the default LoggingCategory.
     * The default LoggingCategory can be obtained via mcnepp::qtdi::defaultLoggingCategory().
     * @param parent will become the QObject::parent().
     */
    explicit StandardApplicationContext(QObject *parent = nullptr) : StandardApplicationContext{defaultLoggingCategory(), this, parent} {

    }

    /**
     * @brief Creates a StandardApplicationContext using an explicit LoggingCategory and a delegating context.
     * <br>The delegating context comes into play when another implementor or QApplicationContext wants to use
     * the class StandardApplicationContext as its *delegate*. When the delegating context invokes StandardApplicationContext::publish(),
     * itself (and not the *delegate*) must be injected into the *init-methods* of services, as well as into QApplicationContextPostProcessor::process()
     * methods.
     * @param loggingCategory a reference to the QLoggingCategory that will be used by this ApplicationContext.
     * @param delegatingContext the ApplicationContext that delegates its calls to this ApplicationContext. Will also become the QObject::parent().
     */
    StandardApplicationContext(const QLoggingCategory& loggingCategory, QApplicationContext* delegatingContext, delegate_tag_t):
        StandardApplicationContext{loggingCategory, delegatingContext, delegatingContext} {
    }

    /**
     * @brief Creates a StandardApplicationContext using a delegating context.
     * <br>The delegating context comes into play when another implementor or QApplicationContext wants to use
     * the class StandardApplicationContext as its *delegate*. When the delegating context invokes StandardApplicationContext::publish(),
     * itself (and not the *delegate*) must be injected into the *init-methods* of services, as well as into QApplicationContextPostProcessor::process()
     * methods.
     * @param delegatingContext the ApplicationContext that delegates its calls to this ApplicationContext. Will also become the QObject::parent().
     */
    StandardApplicationContext(QApplicationContext* delegatingContext, delegate_tag_t):
        StandardApplicationContext{defaultLoggingCategory(), delegatingContext, delegatingContext} {
    }



    ~StandardApplicationContext();

    virtual bool publish(bool allowPartial = false) final override;

    virtual unsigned published() const final override;

    virtual unsigned pendingPublication() const override;

    virtual QVariant getConfigurationValue(const QString& key, bool searchParentSections) const override;

    virtual const QLoggingCategory& loggingCategory() const override;

    using QApplicationContext::registerObject;

    using QApplicationContext::registerService;

    using QApplicationContext::registerPrototype;

protected:

    virtual service_registration_handle_t registerService(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, QObject* baseObj) override;

    virtual service_registration_handle_t getRegistrationHandle(const QString& name) const override;

    virtual proxy_registration_handle_t getRegistrationHandle(const std::type_info& service_type, const QMetaObject* metaObject) const override;

    virtual QList<service_registration_handle_t> getRegistrationHandles() const override;


private:

    StandardApplicationContext(const QLoggingCategory& loggingCategory, QApplicationContext* injectedContext, QObject* parent);

    bool registerAlias(service_registration_handle_t reg, const QString& alias);


    using descriptor_set = std::unordered_set<DescriptorRegistration*>;

    using descriptor_list = std::deque<DescriptorRegistration*>;


    static constexpr int STATE_INIT = 0;
    static constexpr int STATE_CREATED = 1;
    static constexpr int STATE_PUBLISHED = 3;
    //The state reported by a Service-Template
    static constexpr int STATE_IGNORE = 4;

    class DescriptorRegistration : public detail::ServiceRegistration {
        friend class StandardApplicationContext;

        virtual subscription_handle_t createBindingTo(const char* sourcePropertyName, registration_handle_t target, const detail::property_descriptor& targetProperty) override;


        virtual subscription_handle_t createAutowiring(const std::type_info& type, detail::q_inject_t injector, registration_handle_t source) override;


        QString registeredName() const override {
            return m_name;
        }


        virtual QApplicationContext* applicationContext() const final override {
            return m_context->m_injectedContext;
        }

        const QLoggingCategory& loggingCategory() const {
            return applicationContext()->loggingCategory();
        }


        virtual QObject* getObject() const = 0;

        virtual int state() const = 0;

        bool isPublished() const {
            return state() == STATE_PUBLISHED;
        }

        bool isManaged() const {
            switch(scope()) {
            case ServiceScope::PROTOTYPE:
            case ServiceScope::SINGLETON:
                return true;
            default:
                return false;
            }
        }

        virtual const service_descriptor& descriptor() const final override {
            return m_descriptor;
        }


        virtual QStringList getBeanRefs() const = 0;

        virtual void notifyPublished() = 0;

        virtual bool registerAlias(const QString& alias) override {
            return m_context->registerAlias(this, alias);
        }

        bool matches(const std::type_info& type) const override {
            if(descriptor().matches(type) || type == typeid(QObject)) {
                return true;
            }
            return m_base && m_base->matches(type);
        }

        bool matches(const dependency_info& info) const {
            return info.isValid() && matches(info.type) && (!info.has_required_name() || info.expression == registeredName());
        }


        unsigned index() const {
            return m_index;
        }



        static auto matcher(const dependency_info& info) {
            return [&info](DescriptorRegistration* reg) { return reg->matches(info); };
        }


        DescriptorRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, StandardApplicationContext* context, QObject* parent);

        DescriptorRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, StandardApplicationContext* parent) :
        DescriptorRegistration{base, index, name, desc, parent, parent} {

        }

        virtual QObject* createService(const QVariantList& dependencies, descriptor_list& created) = 0;

        virtual int unpublish() = 0;

        virtual void resolveProperty(const QString& key, const QVariant& value) = 0;

        virtual const QVariantMap& resolvedProperties() const = 0;

        DescriptorRegistration* base() const {
            return m_base;
        }
    protected:

        service_descriptor m_descriptor;
        QString m_name;
        std::vector<QPropertyNotifier> bindings;
        std::unordered_set<std::type_index> autowirings;
        unsigned const m_index;
        StandardApplicationContext* const m_context;
        DescriptorRegistration* const m_base;
    };



    class ServiceRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        ServiceRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent) :
            DescriptorRegistration{base, index, name, desc, context, parent},
            theService(nullptr),
            m_config(config),
            m_resolvedProperties{config.properties},
            m_state(STATE_INIT)        {

        }

        ServiceRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
        ServiceRegistration{base, index, name, desc, config, parent, parent} {

        }


        virtual ServiceScope scope() const override {
            return ServiceScope::SINGLETON;
        }


        virtual const QVariantMap& resolvedProperties() const override {
            return m_resolvedProperties;
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

        virtual void resolveProperty(const QString& key, const QVariant& value) override {
            m_resolvedProperties.insert(key, value);
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



        virtual int unpublish() override;



    private:
        QObject* theService;
        service_config m_config;
        QMetaObject::Connection onDestroyed;
        QVariantMap m_resolvedProperties;
        int m_state;
        mutable std::optional<QStringList> beanRefsCache;
    };

    class ServiceTemplateRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        ServiceTemplateRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent);

        ServiceTemplateRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
            ServiceTemplateRegistration{base, index, name, desc, config, parent, parent} {

        }


        virtual ServiceScope scope() const override {
            return ServiceScope::TEMPLATE;
        }


        void add(DescriptorRegistration* handle) {
            derivedServices.push_back(handle);
            handle->subscribe(proxySubscription);
        }

        virtual subscription_handle_t createBindingTo(const char* sourcePropertyName, registration_handle_t target, const detail::property_descriptor& targetProperty) override;

        void notifyPublished() override {
        }

        virtual int state() const override {
            return STATE_IGNORE;
        }

        virtual QObject* getObject() const override {
            return nullptr;
        }





        virtual void print(QDebug out) const override;

        virtual const service_config& config() const override {
            return m_config;
        }


        virtual void resolveProperty(const QString& key, const QVariant& value) override {
            m_resolvedProperties.insert(key, value);
        }


        virtual const QVariantMap& resolvedProperties() const override {
            return m_resolvedProperties;
        }


        virtual QStringList getBeanRefs() const override;



        virtual QObject* createService(const QVariantList& dependencies, descriptor_list& created) override;


        virtual void onSubscription(subscription_handle_t) override;





        virtual int unpublish() override {
            return 0;
        }



    private:
        service_config m_config;
        QVariantMap m_resolvedProperties;
        mutable std::optional<QStringList> beanRefsCache;
        subscription_handle_t proxySubscription;
        descriptor_list derivedServices;
    };


    class PrototypeRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        PrototypeRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent);

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

        virtual void resolveProperty(const QString&, const QVariant&) override {
        }

        virtual const QVariantMap& resolvedProperties() const override {
            return m_config.properties;
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
            DescriptorRegistration{nullptr, index, name, desc, parent},
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

        virtual const QVariantMap& resolvedProperties() const override {
            return defaultConfig.properties;
        }




        virtual QObject* getObject() const override {
            return theObj;
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

        virtual QApplicationContext* applicationContext() const final override {
            return m_context->m_injectedContext;
        }

        virtual QList<service_registration_handle_t> registeredServices() const override;

        virtual const QMetaObject* serviceMetaObject() const override {
            return m_meta;
        }


        virtual const std::type_info& serviceType() const override {
            return m_type;
        }

        bool add(service_registration_handle_t reg) {
            if(reg->scope() != ServiceScope::TEMPLATE && reg->matches(m_type)) {
                reg->subscribe(proxySubscription);
                return true;
            }
            return false;
        }

        virtual void onSubscription(subscription_handle_t subscription) override;



        virtual void print(QDebug out) const final override {
            out.nospace().noquote() << "Services [" << registeredServices().size() << "] with service-type '" << m_type.name() << "'";
        }

        const QLoggingCategory& loggingCategory() const {
            return applicationContext()->loggingCategory();
        }


        const std::type_info& m_type;
        const QMetaObject* m_meta;
        std::unordered_set<std::type_index> autowirings;
        subscription_handle_t proxySubscription;
        StandardApplicationContext* const m_context;
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

    bool findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<dependency_info>& dependents) const;

    void unpublish();

    void contextObjectDestroyed(QObject*);

    DescriptorRegistration* getRegistrationByName(const QString& name) const;


    std::pair<QVariant,Status> resolveDependency(const descriptor_list& published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial);

    static QVariantList resolveDependencies(const QVariantList& dependencies, descriptor_list& created);

    static QVariant resolveDependency(const QVariant& arg, descriptor_list& created);

    Status configure(DescriptorRegistration*, const service_config& config, QObject*, descriptor_list& toBePublished, bool allowPartial);

    Status init(DescriptorRegistration*, const QList<QApplicationContextPostProcessor*>& postProcessors);

    std::pair<Status,bool> resolveBeanRef(QVariant& value, descriptor_list& toBePublished, bool allowPartial);

    std::pair<QVariant,Status> resolvePlaceholders(const QString& key, const service_config& config);

    DescriptorRegistration* findAutowiringCandidate(service_registration_handle_t, const QMetaProperty&);

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
    const QLoggingCategory& m_loggingCategory;
    QApplicationContext* const m_injectedContext;
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

