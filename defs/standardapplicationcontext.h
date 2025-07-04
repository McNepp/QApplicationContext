#pragma once

#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <typeindex>
#include <QMetaProperty>
#include <QMutex>
#include <QWaitCondition>
#include <QBindable>
#include <QSettings>
#include "qapplicationcontext.h"
#include "placeholderresolver.h"

namespace mcnepp::qtdi {

namespace detail {
    class QSettingsWatcher;
}





///
/// \brief A ready-to use implementation of the QApplicationContext.
/// Using this class is the "canonical way" of instantiating a QApplicationContext.
/// <br>It should be used in all places where you rely on what the interface QApplicationContext offers, without
/// the need to add more functionality.
/// <br>Should you want to provide a customized version of QApplicationContext, or should you want to augment
/// your class with additional functionaly, you may want to resort to the helper-class ApplicationContextImplBase.
///
class StandardApplicationContext final : public QApplicationContext
{
    Q_OBJECT

    Q_PROPERTY(int autoRefreshMillis READ autoRefreshMillis WRITE setAutoRefreshMillis NOTIFY autoRefreshMillisChanged)
    Q_PROPERTY(Profiles activeProfiles READ activeProfiles WRITE setActiveProfiles NOTIFY activeProfilesChanged)

//Forward-declarations of nested class:
    class CreateHandleEvent;


    class DescriptorRegistration;

    class ServiceGroupRegistration;

    friend QApplicationContext* newDelegate(const QLoggingCategory& loggingCategory, QApplicationContext* delegatingContext);

signals:

    void autoRefreshMillisChanged(int);

    void activeProfilesChanged(const Profiles&);

public:



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




    ~StandardApplicationContext();

    virtual bool publish(bool allowPartial = false) final override;

    virtual unsigned published() const final override;

    virtual unsigned pendingPublication() const override;

    virtual QVariant getConfigurationValue(const QString& key, bool searchParentSections = false) const override;

    virtual QStringList configurationKeys(const QString& section = {}) const override;

    virtual const QLoggingCategory& loggingCategory() const override;

    ///
    /// \brief Determines the maximum delay for 'auto-refreshable' configuration-values.
    /// <br>This value will influence the behaiour of configuration-values created with mcnepp::qtdi::autoRefresh(const Qstring&).
    /// \return the current number of milliseconds between refreshes of the configuration.
    ///
    int autoRefreshMillis() const;

    ///
    /// \brief Determines the maximum delay for 'auto-refreshable' configuration-values.
    /// <br>This value will influence the behaiour of configuration-values created with mcnepp::qtdi::autoRefresh(const Qstring&).
    /// \param newRefreshMillis the new number of milliseconds between refreshes of the configuration.
    ///
    void setAutoRefreshMillis(int newRefreshMillis);

    ///
    /// \brief Sets the active Profiles.
    /// <br>Invoking this method will overrule any profiles determined by the configuration-entry `"qtdi/activeProfiles"`.
    /// <br>**Note:** Changing the active profiles is only possible when no profile-dependent %Service has been published yet.
    /// \param profiles the profiles to activate. Must not be empty.
    ///
    void setActiveProfiles(const Profiles& profiles);

    ///
    /// \brief Has auto-refresh been enabled?
    /// <br>If enabled, it is possible to use mcnepp::qtdi::autoRefresh(const QString&) to force automatic updates of service-properties,
    /// whenever the corresponding configuration-values is modified.
    /// <br>Auto-refresh can be enabled by putting a configuration-entry into one of the QSettings-objects registered with the context:
    ///
    ///     [qtdi]
    ///     enableAutoRefresh=true
    ///     ; Optionally, specify the refresh-period:
    ///     autoRefreshMillis=2000
    ///
    /// \return `true` if auto-refresh has been enabled.
    ///
    bool autoRefreshEnabled() const override;

    virtual Profiles activeProfiles() const override;

    virtual QConfigurationWatcher* watchConfigValue(const QString& expression) override;

    virtual QVariant resolveConfigValue(const QString& expression, const QString& group, QVariantMap& resolvedPlaceholders) override;


    using QApplicationContext::registerObject;

    using QApplicationContext::registerService;

    using QApplicationContext::registerPrototype;

protected:

    virtual service_registration_handle_t registerServiceHandle(const QString& name, const service_descriptor& descriptor, const service_config& config, ServiceScope scope, const Condition& condition, QObject* baseObj) override;

    virtual service_registration_handle_t getRegistrationHandle(const QString& name) const override;

    virtual proxy_registration_handle_t getRegistrationHandle(const std::type_info& service_type, const QMetaObject* metaObject) const override;

    virtual QList<service_registration_handle_t> getRegistrationHandles() const override;


private:

    StandardApplicationContext(const QLoggingCategory& loggingCategory, QApplicationContext* delegatingContext, QObject* parent);

    bool registerAlias(service_registration_handle_t reg, const QString& alias);

    using descriptor_set = std::unordered_set<DescriptorRegistration*>;

    using descriptor_list = std::deque<DescriptorRegistration*>;



    static constexpr int STATE_INIT = 0;
    static constexpr int STATE_NEEDS_CONFIGURATION = 1;
    static constexpr int STATE_PUBLISHED = 3;
    //The state reported by a Service-Template
    static constexpr int STATE_IGNORE = 4;

    class DescriptorRegistration : public detail::ServiceRegistration {
        friend class StandardApplicationContext;

        virtual subscription_handle_t createBindingTo(const detail::source_property_descriptor& sourceProperty, registration_handle_t target, const detail::property_descriptor& targetProperty) override;



        QString registeredName() const override {
            return m_name;
        }



        virtual QApplicationContext* applicationContext() const final override {
            return m_context->m_injectedContext;
        }

        const QLoggingCategory& loggingCategory() const {
            return applicationContext()->loggingCategory();
        }

        // Returns the single service-object.
        // If scope is one of SINGLETON or EXTERNAL, will return the service-object.
        // Othwerise, will return nullptr
        virtual QObject* getObject() const = 0;

        // Obtains the service(s).
        // If scope is one of SINGLETON or EXTERNAL, will return the service-object.
        // If scope is SERVICE_GROUP, will return multiple service-objects.
        // If scope == PROTOTYPE, it will create a new prototype-instance for each invocation.
        virtual QObjectList obtainServices(descriptor_list& created) = 0;

        virtual int state() const = 0;

        bool isPublished() const {
            return state() == STATE_PUBLISHED;
        }

        bool isManaged() const {
            switch(scope()) {
            case ServiceScope::EXTERNAL:
                return false;
            default:
                return true;
            }
        }

        //Does this service provide configuration for other services?
        //This is true for Services of type QSettings.
        virtual bool provideConfig() const {
            return false;
        }

        virtual const service_descriptor& descriptor() const final override {
            return m_descriptor;
        }

        virtual const service_config& config() const final override {
            return m_config;
        }


        const QStringList& getBeanRefs() const  {
            return m_beanRefsCache;
        }

        virtual void notifyPublished() = 0;

        virtual bool registerAlias(const QString& alias) override {
            return m_context->registerAlias(this, alias);
        }

        virtual const Condition& registeredCondition() const override {
            return m_condition;
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


        DescriptorRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent);

        DescriptorRegistration(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
        DescriptorRegistration{base, index, name, desc, config, parent, parent} {

        }

        // Prepares the service.
        // If scope == SINGLETON, will create the service.
        // If scope == SERVICE_GROUP, will create multiple services.
        // If scope == PROTOTYPE, will store the dependencies for later use.
        // Otherwise, does nothing.
        virtual bool prepareService(const QVariantList& dependencies, descriptor_list& created) = 0;

        virtual int unpublish() = 0;

        QVariantMap& resolvedPlaceholders() {
            return m_resolvedPlaceholders;
        }

        DescriptorRegistration* base() const {
            return m_base;
        }

        bool isActiveInProfile() const;

    protected:
        service_descriptor m_descriptor;
        QString m_name;
        std::vector<QPropertyNotifier> bindings;
        unsigned const m_index;
        StandardApplicationContext* const m_context;
        DescriptorRegistration* const m_base;
        Condition m_condition;
        QVariantMap m_resolvedPlaceholders;
        service_config m_config;
        QStringList m_beanRefsCache;
    };



    class ServiceRegistrationImpl : public DescriptorRegistration {

        friend class StandardApplicationContext;

        ServiceRegistrationImpl(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* context, QObject* parent);

        ServiceRegistrationImpl(DescriptorRegistration* base, unsigned index, const QString& name, const service_descriptor& desc, const service_config& config, StandardApplicationContext* parent) :
        ServiceRegistrationImpl{base, index, name, desc, config, parent, parent} {

        }


        virtual ServiceScope scope() const override {
            return ServiceScope::SINGLETON;
        }

        virtual bool provideConfig() const override {
            return m_descriptor.impl_type == typeid(QSettings);
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

        virtual QObjectList obtainServices(descriptor_list&) override {
            return theService ? QObjectList{theService} : QObjectList{};
        }





        virtual void print(QDebug out) const override;




        virtual bool prepareService(const QVariantList& dependencies, descriptor_list& created) override;


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
        QMetaObject::Connection onDestroyed;
        int m_state;
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

        virtual subscription_handle_t createBindingTo(const detail::source_property_descriptor& sourceProperty, registration_handle_t target, const detail::property_descriptor& targetProperty) override;

        void notifyPublished() override {
        }

        virtual int state() const override {
            return STATE_IGNORE;
        }

        virtual QObject* getObject() const override {
            return nullptr;
        }


        virtual QObjectList obtainServices(descriptor_list&) override {
            return {};
        }



        virtual void print(QDebug out) const override;



        virtual bool prepareService(const QVariantList& dependencies, descriptor_list& created) override;


        virtual void onSubscription(subscription_handle_t) override;





        virtual int unpublish() override {
            return 0;
        }



    private:
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
            return STATE_INIT;
        }

        virtual QObject* getObject() const override {
            return nullptr;
        }

        virtual void print(QDebug out) const override;



        virtual subscription_handle_t createBindingTo(const detail::source_property_descriptor& sourceProperty, registration_handle_t target, const detail::property_descriptor& targetProperty) override;




        virtual bool prepareService(const QVariantList& dependencies, descriptor_list& created) override;

        virtual void onSubscription(subscription_handle_t subscription) override;





        virtual int unpublish() override;

        virtual QObjectList obtainServices(descriptor_list&) override;

        QVariantList m_dependencies;


    private:
        subscription_handle_t proxySubscription;
    };

    class ObjectRegistration : public DescriptorRegistration {

        friend class StandardApplicationContext;

        ObjectRegistration(unsigned index, const QString& name, const service_descriptor& desc, QObject* obj, StandardApplicationContext* parent) :
            DescriptorRegistration{nullptr, index, name, desc, service_config{}, parent},
            theObj(obj){
            //Do not connect the signal QObject::destroyed if obj is the ApplicationContext itself:
            if(obj != parent->m_injectedContext) {
                connect(obj, &QObject::destroyed, parent, [this] { m_context->contextObjectDestroyed(this);});
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

        virtual QObjectList obtainServices(descriptor_list&) override {
            return QObjectList{theObj};
        }




        virtual bool prepareService(const QVariantList&, descriptor_list&) override {
            return theObj;
        }


        virtual void print(QDebug out) const override;


        virtual void onSubscription(subscription_handle_t subscription) override {
            emit subscription->objectPublished(theObj);
        }




    private:
        QObject* const theObj;
    };


    class ProxySubscription;

    class ProxyRegistrationImpl : public detail::ProxyRegistration {


        friend class StandardApplicationContext;

        ProxyRegistrationImpl(const std::type_info& type, const QMetaObject* metaObject, StandardApplicationContext* parent);


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

        bool add(DescriptorRegistration* reg);

        bool canAdd(DescriptorRegistration* reg) const;

        virtual void onSubscription(subscription_handle_t subscription) override;



        virtual void print(QDebug out) const final override {
            out.nospace().noquote() << "Services [" << registeredServices().size() << "] with service-type '" << detail::type_name(m_type) << "'";
        }

        const QLoggingCategory& loggingCategory() const {
            return applicationContext()->loggingCategory();
        }

        QObjectList obtainServices(descriptor_list&);


        const std::type_info& m_type;
        const QMetaObject* m_meta;
        ProxySubscription* proxySubscription;
        StandardApplicationContext* const m_context;
    };




    static QMetaProperty getProperty(registration_handle_t reg, const detail::source_property_descriptor& sourceProperty);




    enum class Status {
        ok,
        fixable,
        fatal
    };


    Status validate(bool allowPartial, const descriptor_list& published, descriptor_list& unpublished);

    bool checkTransitiveDependentsOn(const service_descriptor& descriptor, const QString& name, const std::unordered_set<dependency_info>& dependencies) const;

    bool findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<dependency_info>& dependents) const;

    void unpublish();

    void contextObjectDestroyed(DescriptorRegistration*);

    DescriptorRegistration* getActiveRegistrationByName(const QString& name) const;


    std::pair<QVariant,Status> resolveDependency(const descriptor_list& published, DescriptorRegistration* reg, const dependency_info& d, bool allowPartial);

    static QVariantList resolveDependencies(const QVariantList& dependencies, descriptor_list& created);

    static QVariant resolveDependency(const QVariant& arg, descriptor_list& created);

    Status configure(DescriptorRegistration*, QVariantMap& resolvedPlaceholders, QObject*, descriptor_list& toBePublished, bool allowPartial);

    bool init(DescriptorRegistration*, ServiceInitializationPolicy);

    void runPostProcessors(DescriptorRegistration*, const QList<QApplicationContextPostProcessor*>& postProcessors);

    std::pair<Status,bool> resolveBeanRef(QVariant& value, descriptor_list& toBePublished, bool allowPartial);

    DescriptorRegistration* findAutowiringCandidate(service_registration_handle_t, const QMetaProperty&);

    bool registerBoundProperty(registration_handle_t target, const char* propName);

    bool validateResolvers(const service_descriptor& descriptor, const service_config& config);

    detail::PlaceholderResolver* getResolver(const QString&);

    void onSettingsAdded(QSettings*);

    QObject* obtainHandleFromApplicationThread(std::function<QObject*()>);

    void insertByName(const QString& name, DescriptorRegistration* reg);

    bool canChangeActiveProfiles();

    QSettings* settingsForProfile(QSettings* settings, const QString& profile);

    void initSettingsForActiveProfiles();

    // QObject interface
public:
    bool event(QEvent *event) override;


private:

    struct ProfileAndName {
        QString profile;
        QString name;

        friend bool operator==(const ProfileAndName& left, const ProfileAndName& right) {
            return left.profile == right.profile && left.name == right.name;
        }
    };

    struct ProfileNameHash {
        std::size_t operator()(const ProfileAndName& profileAndName) const {
            return qHash(profileAndName.profile) ^ qHash(profileAndName.name);
        }
    };

    descriptor_list registrations;

    std::unordered_map<QString,std::unordered_set<DescriptorRegistration*>> registrationsByName;

    mutable std::unordered_map<std::type_index,ProxyRegistrationImpl*> proxyRegistrationCache;
    mutable QMutex mutex;
    mutable QWaitCondition m_condition;
    std::unordered_map<registration_handle_t,std::unordered_set<QString>> m_boundProperties;
    std::atomic<unsigned> nextIndex;
    const QLoggingCategory& m_loggingCategory;
    QApplicationContext* const m_injectedContext;

    detail::QSettingsWatcher* m_SettingsWatcher = nullptr;
    std::unordered_map<QString,QPointer<detail::PlaceholderResolver>> resolverCache;
    Profiles* m_activeProfiles;
    std::unordered_map<ProfileAndName,QSettings*,ProfileNameHash> m_profileSettings;
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

