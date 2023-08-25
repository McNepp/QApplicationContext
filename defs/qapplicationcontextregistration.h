#pragma once

#include <functional>
#include <typeinfo>
#include <unordered_set>
#include <QObject>

namespace com::neppert::context {

///
/// An  type that serves as a "handle" for registrations in a QApplicationContext.
/// This class has a read-only Q_PROPERTY `publishedObjects' with a corresponding signal `publishedObjectsChanged`.
/// However, it would be quite unwieldly to use that property directly in order to get hold of the
/// Objects managed by this Registration.
/// Rather, use the class-template `ServiceRegistration` and its type-safe method `ServiceRegistration::onPublished()`.
///
class Registration : public QObject {
    Q_OBJECT

    Q_PROPERTY(QObjectList publishedObjects READ getPublishedObjects NOTIFY publishedObjectsChanged)

public:
    [[nodiscard]] virtual const std::type_info& service_type() const = 0;

    [[nodiscard]] virtual QObjectList getPublishedObjects() const = 0;

signals:

    void publishedObjectsChanged();

protected:

    explicit Registration(QObject* parent = nullptr) : QObject(parent) {

    }

    virtual ~Registration() = default;
};

///
/// \brief A type-safe wrapper for a Registration.
/// Instances of this class are being returned by the public function-templates QApplicationContext::registerService(),
/// QApplicationContext::registerObject(QObject*) and IApplicationService::getRegistration().
///
/// This class offers the type-safe function `onPublished()` which should be preferred over directly connecting to the signal `publishedObjectsChanged()`.
///
template<typename S> class ServiceRegistration : public Registration {
    friend class QApplicationContext;

public:
    [[nodiscard]] virtual const std::type_info& service_type() const override {
        return unwrap()->service_type();
    }



    [[nodiscard]] virtual QObjectList getPublishedObjects() const override {
        return unwrap()->getPublishedObjects();
    }



    ///
    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the callable immediately with the current getPublishedObjects().
    /// \param context
    /// \param callable
    ///
    template<typename F> void onPublished(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        connect(this, &Registration::publishedObjectsChanged, context, Notifier<F>{callable, this}, connectionType);
        emit publishedObjectsChanged();
    }

    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the setter immediately with the current getPublishedObjects().
    /// \param target
    /// \param setter
    ///
    template<typename T,typename A,typename R> void onPublished(T* target, R (T::*setter)(A*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        onPublished(target, std::bind(std::mem_fn(setter), target, std::placeholders::_1), connectionType);
    }



    Registration* unwrap() const {
        return static_cast<Registration*>(parent());
    }



private:
    explicit ServiceRegistration(Registration* reg) : Registration(reg)
    {
        connect(reg, &Registration::publishedObjectsChanged, this, &Registration::publishedObjectsChanged);
    }

    static ServiceRegistration* wrap(Registration* reg) {
        return reg ? new ServiceRegistration(reg) : nullptr;
    }



    template<typename F> struct Notifier {
        F callable;

        Registration* source;

        std::unordered_set<S*> publishedObjects;

        void operator()() {
            for(auto obj : source->getPublishedObjects()) {
                S* ptr = dynamic_cast<S*>(obj);
                if(ptr && publishedObjects.insert(ptr).second) {
                    callable(ptr);
                }
            }
        }
    };
};





}
