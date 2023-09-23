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

    virtual bool publish(bool allowPartial = false) final override;

    virtual unsigned published() const final override;

    virtual unsigned pendingPublication() const override;


protected:

    virtual Registration* registerService(const QString& name, const service_descriptor& descriptor) override;

    virtual Registration* registerObject(const QString& name, QObject* obj, const service_descriptor& descriptor) override;

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
            return descriptor.service_type;
        }


        const QString& name() const  {
            return m_name;
        }

        virtual QObject* getObject() const = 0;

        virtual bool isPublished() const = 0;

        virtual bool isEqual(const service_descriptor& descriptor, QObject* obj) const = 0;




        virtual QObjectList getPublishedObjects() const override {
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


        DescriptorRegistration(const QString& name, const service_descriptor& desc, StandardApplicationContext* parent);


        virtual QObject* publish(const QObjectList& dependencies) = 0;

        virtual bool unpublish() = 0;

        virtual QObjectList privateObjects() const = 0;

        virtual QObject* createPrivateObject(const QObjectList& dependencies) = 0;


        friend QDebug operator << (QDebug out, const DescriptorRegistration& reg) {
            return out.nospace().noquote() << "Object '" << reg.name() << "' with service-type '" << reg.service_type().name() << "' and impl-type '" << reg.descriptor.impl_type.name() << "'";
        }


        service_descriptor descriptor;
        QString m_name;
        QVariantMap resolvedProperties;
    };

    using descriptor_set = std::unordered_set<DescriptorRegistration*>;


    struct ServiceRegistration : public DescriptorRegistration {

        ServiceRegistration(const QString& name, const service_descriptor& desc, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            published(false),
            theService(nullptr) {

        }

        void notifyPublished() override {
            if(theService) {
                published = true;
                emit publishedObjectsChanged();
            }
        }

        virtual bool isPublished() const override {
            return published;
        }

        virtual QObject* getObject() const override {
            return theService;
        }


        virtual QObject* createPrivateObject(const QObjectList& dependencies) override {
            QObject* obj = descriptor.create(dependencies);
            if(obj) {
                m_privateObjects.push_back(obj);
            }
            return obj;
        }

        virtual bool isEqual(const service_descriptor& descriptor, QObject* obj) const override {
            return descriptor == this->descriptor;
        }

        virtual QObjectList privateObjects() const override {
            return m_privateObjects;
        }


        virtual QObject* publish(const QObjectList& dependencies) override {
            if(!theService) {
                theService = descriptor.create(dependencies);
            }
            return theService;
        }



        virtual bool unpublish() override {
            if(published) {
                delete theService;
                theService = nullptr;
                published = false;
                return true;
            }
            return false;
        }

    private:
        QObject* theService;
        QObjectList m_privateObjects;
        bool published;
    };

    struct ObjectRegistration : public DescriptorRegistration {



        ObjectRegistration(const QString& name, const service_descriptor& desc, QObject* obj, StandardApplicationContext* parent) :
            DescriptorRegistration{name, desc, parent},
            theObj(obj){
        }

        void notifyPublished() override {
        }

        virtual bool isEqual(const service_descriptor& descriptor, QObject* obj) const override {
            return descriptor == this->descriptor && theObj == obj;
        }

        virtual bool isPublished() const override {
            return true;
        }

        virtual bool unpublish() override {
            return false;
        }

        virtual QObject* getObject() const override {
            return theObj;
        }

        virtual QObject* publish(const QObjectList& dependencies) override {
            return theObj;
        }

        virtual QObject* createPrivateObject(const QObjectList& dependencies) override {
            return nullptr;
        }

        virtual QObjectList privateObjects() const override {
            return {};
        }

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



    enum class Status {
        ok,
        fixable,
        fatal
    };

    template<typename C> static DescriptorRegistration* find_by_type(const C& regs, const std::type_index& type);

    bool checkTransitiveDependentsOn(const service_descriptor& descriptor, const std::unordered_set<std::type_index>& dependencies) const;

    void findTransitiveDependenciesOf(const service_descriptor& descriptor, std::unordered_set<std::type_index>& dependents) const;

    void unpublish();

    QVariant getConfigurationValue(const QString& key) const;

    static QStringList getBeanRefs(const service_config&);

    void contextObjectDestroyed(QObject*);

    DescriptorRegistration* getRegistrationByName(const QString& name) const;


    std::pair<QObject*,Status> resolveDependency(const descriptor_set& published, std::vector<DescriptorRegistration*>& publishedNow, DescriptorRegistration* reg, const dependency_info& d, QObject* temporaryParent, bool allowPartial);

    std::pair<DescriptorRegistration*,bool> registerDescriptor(QString name, const service_descriptor& descriptor, QObject* obj);

    Status configure(DescriptorRegistration*,QObject*, const QList<QApplicationContextPostProcessor*>& postProcessors, bool allowPartial);

    std::pair<QVariant,Status> resolveValue(const QVariant& value, bool allowPartial);

    descriptor_set registrations;

    std::unordered_map<QString,DescriptorRegistration*> registrationsByName;



    mutable std::unordered_map<std::type_index,ProxyRegistration*> proxyRegistrationCache;
};




}

