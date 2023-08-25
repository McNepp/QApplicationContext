#pragma once

#include <functional>
#include <typeinfo>
#include <unordered_set>
#include <QObject>

namespace com::neppert::context {

///
/// \brief An  type that serves as a "handle" for registrations in a QApplicationContext.
/// This class has a read-only Q_PROPERTY `publishedObjects' with a corresponding signal `publishedObjectsChanged`.
/// However, it would be quite unwieldly to use that property directly in order to get hold of the
/// Objects managed by this Registration.
/// Rather, use the class-template `ServiceRegistration` and its type-safe method `ServiceRegistration::subscribe()`.
///
class Registration : public QObject {
    Q_OBJECT

    /// \brief The List of published Objects managed by this Registration.
    /// If this is a Registration obtained by QApplicationContext::registerService(), the List
    /// can comprise of at most one Object.
    /// However, if the Registration was obtained by QApplicationContext::getRegistration(),
    /// it may comprise multiple objects of the supplied service-type.
    ///
    Q_PROPERTY(QObjectList publishedObjects READ getPublishedObjects NOTIFY publishedObjectsChanged)

public:
    ///
    /// \brief The service-type that this Registration manages.
    /// \return The service-type that this Registration manages.
    ///
    [[nodiscard]] virtual const std::type_info& service_type() const = 0;

    /// \brief The List of published Objects managed by this Registration.
    /// If this is a Registration obtained by QApplicationContext::registerService(), the List
    /// can comprise of at most one Object.
    /// However, if the Registration was obtained by QApplicationContext::getRegistration(),
    /// it may comprise multiple objects of the supplied service-type.
    ///
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
/// QApplicationContext::registerObject() and QApplicationContext::getRegistration().
///
/// This class offers the type-safe function `subscribe()` which should be preferred over directly connecting to the signal `publishedObjectsChanged()`.
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
    /// \param context the target-context.
    /// \param callable the piece of code to execute.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    ///
    template<typename F> void subscribe(QObject* context, F callable, Qt::ConnectionType connectionType = Qt::AutoConnection) {
        connect(this, &Registration::publishedObjectsChanged, context, Notifier<F>{callable, this}, connectionType);
        emit publishedObjectsChanged();
    }

    /// \brief Receive all published QObjects in a type-safe way.
    /// Connects to the `publishedObjectsChanged` signal and propagates new QObjects to the callable.
    /// Type `F` is assumed to be a Callable that accepts an argument of type `S*`.
    /// If the ApplicationContext has already been published, this method
    /// will invoke the setter immediately with the current getPublishedObjects().
    /// \param target the object on which the setter will be invoked.
    /// \param setter the method that will be invoked.
    /// \param connectionType determines whether the signal is processed synchronously or asynchronously
    ///
    template<typename T,typename A,typename R> void subscribe(T* target, R (T::*setter)(A*), Qt::ConnectionType connectionType = Qt::AutoConnection) {
        subscribe(target, std::bind(std::mem_fn(setter), target, std::placeholders::_1), connectionType);
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
