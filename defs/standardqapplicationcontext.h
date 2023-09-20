#pragma once

#include <mutex>
#include <memory>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <typeindex>
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

    virtual bool publish() final override;

    virtual bool published() const final override;




protected:

    virtual Registration* registerService(const QString& name, service_descriptor* descriptor) override;

    virtual Registration* registerObject(const QString& name, QObject* obj, service_descriptor* descriptor) override;

    virtual Registration* getRegistration(const std::type_info& service_type) const override;



private:

    struct StandardRegistration : public Registration {
        StandardRegistration(StandardApplicationContext* parent) : Registration(parent) {

        }

        virtual StandardApplicationContext* applicationContext() const final override {
            return static_cast<StandardApplicationContext*>(parent());
        }

        virtual bool registerAutoWiring(const std::type_info& type, binder_t binder) final override;
    private:
        struct Injector : public PublicationNotifier {
            injector_t m_injector;

            Injector(Registration* dependency, injector_t injector) : PublicationNotifier(dependency),
                m_injector(injector) {
            }

            void notify(QObject* obj) const override {
                m_injector(obj);
            }
        };

        struct TargetBinder : public PublicationNotifier {
            binder_t m_binder;
            Registration* dependency;

            TargetBinder(Registration* target, binder_t binder, Registration* dependency) : PublicationNotifier(target),
                m_binder(binder) {
                this->dependency = dependency;
            }

            void notify(QObject* obj) const override {
                    injector_t injector = m_binder(obj);
                    if(!injector) {
                        qCritical(loggingCategory()).noquote().nospace() << "Cannot inject " << dependency->service_type().name() << " into " << obj;
                        return;
                    }
                    connect(dependency, &Registration::publishedObjectsChanged, obj, Injector{dependency, injector});
            }
        };


        std::unordered_map<std::type_index,binder_t> autowirings;

    };


    struct DescriptorRegistration : public StandardRegistration {
        const std::type_info& service_type() const override {
            return descriptor->service_type;
        }


        const QString& name() const  {
            return m_name;
        }

        QObject* getObject() const {
            return theService;
        }

        bool isPublished() const {
            return theService != nullptr;
        }




        virtual QObjectList getPublishedObjects() const override {
            QObjectList result;
            if(theService) {
                result.push_back(theService);
            }
            return result;
        }

        virtual void notifyPublished() = 0;


        bool matches(const std::type_index& type) const {
            return descriptor->matches(type);
        }


        DescriptorRegistration(const QString& name, service_descriptor* desc, StandardApplicationContext* parent);


        virtual bool operator==(const DescriptorRegistration& other) const = 0;

        virtual QObject* publish(const QObjectList& dependencies) = 0;

        virtual bool unpublish() = 0;

        virtual QObjectList privateObjects() const = 0;

        virtual QObject* createPrivateObject(const QObjectList& dependencies) = 0;


        friend QDebug operator << (QDebug out, const DescriptorRegistration& reg) {
            return out.nospace().noquote() << "Object '" << reg.name() << "' with service-type '" << reg.service_type().name() << "' and impl-type '" << reg.descriptor->impl_type.name() << "'";
        }


        std::unique_ptr<service_descriptor> descriptor;

        QObject* theService;
        QString m_name;
        bool isAnonymous;
        QVariantMap resolvedProperties;
    };

    using descriptor_set = std::unordered_set<DescriptorRegistration*>;


    struct ServiceRegistration : public DescriptorRegistration {

        ServiceRegistration(const QString& name, service_descriptor* desc, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent} {

        }

        void notifyPublished() override {
            if(theService) {
                emit publishedObjectsChanged();
            }
        }

        virtual QObject* createPrivateObject(const QObjectList& dependencies) override {
            QObject* obj = descriptor->create(dependencies);
            if(obj) {
                m_privateObjects.push_back(obj);
            }
            return obj;
        }

        virtual QObjectList privateObjects() const override {
            return m_privateObjects;
        }


        virtual QObject* publish(const QObjectList& dependencies) override {
            if(!theService) {
                theService = descriptor->create(dependencies);
            }
            return theService;
        }



        virtual bool unpublish() override {
            if(theService) {
                delete theService;
                theService = nullptr;
                return true;
            }
            return false;
        }

        bool operator==(const DescriptorRegistration& other) const override {
            if(&other == this) {
                return true;
            }
            if(!dynamic_cast<const ServiceRegistration*>(&other)) {
                return false;
            }

            return *descriptor == *other.descriptor;
        }


        QObjectList m_privateObjects;
    };

    struct ObjectRegistration : public DescriptorRegistration {



        ObjectRegistration(const QString& name, QObject* obj, service_descriptor* desc, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent} {
            theService = obj;
        }

        void notifyPublished() override {
        }

        virtual bool unpublish() override {
            return false;
        }

        virtual QObject* publish(const QObjectList& dependencies) override {
            return theService;
        }

        virtual QObject* createPrivateObject(const QObjectList& dependencies) override {
            return nullptr;
        }

        virtual QObjectList privateObjects() const override {
            return {};
        }


        bool operator==(const DescriptorRegistration& other) const override {
            if(&other == this) {
                return true;
            }

            if(!dynamic_cast<const ObjectRegistration*>(&other)) {
                return false;
            }


            return *descriptor == *other.descriptor && theService == other.theService;
        }
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

        virtual QObjectList getPublishedObjects() const override {
            QObjectList result;
            for(auto reg : registrations) {
                if(reg->getObject()) {
                    result.push_back(reg->getObject());
                }
            }
            return result;
        }

        void add(DescriptorRegistration* reg) {
            if(registrations.insert(reg).second) {
                connect(reg, &Registration::publishedObjectsChanged, this, &ProxyRegistration::publishedObjectsChanged);
            }
        }



    private:

        descriptor_set registrations;
    };




    template<typename C> static DescriptorRegistration* find_by_type(const C& regs, const std::type_index& type);

    bool checkTransitiveDependentsOn(service_descriptor* descriptor, const std::unordered_set<std::type_index>& dependencies) const;

    void findTransitiveDependenciesOf(service_descriptor* descriptor, std::unordered_set<std::type_index>& dependents) const;

    bool isResolvable() const;

    void unpublish();

    QVariant getConfigurationValue(const QString& key) const;

    static QStringList getBeanRefs(const service_config&);

    void contextObjectDestroyed(QObject*);

    DescriptorRegistration* getRegistrationByName(const QString& name) const;


    QObject* resolveDependency(const descriptor_set& published, std::vector<DescriptorRegistration*>& publishedNow, DescriptorRegistration* reg, const dependency_info& d, QObject* temporaryParent);

    std::pair<Registration*,bool> registerDescriptor(DescriptorRegistration* registration);

    bool configure(DescriptorRegistration*,QObject*, const QList<QApplicationContextPostProcessor*>& postProcessors);

    QVariant resolveValue(const QVariant& value);

    descriptor_set registrations;

    std::unordered_map<QString,DescriptorRegistration*> registrationsByName;



    mutable std::unordered_map<std::type_index,ProxyRegistration*> proxyRegistrationCache;

    bool successfullyPublished;
};




}

