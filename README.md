# QApplicationContext
A DI-Container for Qt-based applications, inspired by Spring

## Motivation

As an experienced developer, you know that is is vital to design your software-components with the *SOC*  (**S**eparation **O**f **C**oncern) - principle in mind:  
each component shall be responsible for "one task", or at least, one set of related tasks.  
Things that are outside of a component's realm shall be delegated to other components.  
This of course means that some components will need to get references to those other components, commonly referred to as "Dependencies".  
Following this rule greatly increases testability of your components, thus making your software more robust.

But, sooner or later, you will ask yourself: How do I wire all those inter-dependent components together?  
How do I create application-wide "singletons" (without resorting to C++ singletions, which are notoriously brittle), and how can I create multiple implmentations of the same interface?

## Features

- Provide an easy-to use Container for Dependency-Injection of Qt-based components.
- Offer a typesafe syntax for declaring dependencies between components in C++.
- Relieve the developer of the need to know the precise order in which the inter-dependent components must be instantiated.
- Dependency-injection via constructor.
- Dependency-injection via Qt-properties.
- Support both 1 -> 1 and 1 -> N relations between components.
- Further configuration of components after creation, including externalized configuration (using `QSettings`).
- Automatic invocation of an *init-method* after creation, using Qt-slots.
- Offer a Qt-signal for "published" components, together with a type-safe `onPublished(...)` slot.
- Fail-fast, i.e. terminate compilation with meaningful diagnostics if possible.
- Help to find runtime-problems by generating verbose logging (using a `QLoggingCategory`).

## An example

Suppose you have created a component named 'RestPropFetcher'. This component fetches a String-value from a Website, using http-GET and exposes this value as a Q_PROPERTY.
Naturally, this component will make use of a `QNetworkAccessManager`. Also, it will have a writable String-property for setting the URL.
The declaration of such a component may look like this:

    class RestPropFetcher : public QObject {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)

      Q_PROPERTY(QString url READ url WRITE setURl NOTIFY urlChanged)
      
      public:
      
      explicit RestPropFetcher(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

      QString value() const;

      QString url() const;

      void setUrl(const QString&);

      signals:
      void valueChanged();
      void urlChanged();
    };

Given the above component, here is the minimal code for creating a QApplicationContext which exposes such a RestPropFetcher:

    using namespace com::neppert::context;
    
    QApplicationContext* context = new StandardQApplicationContext; // 1
    
    context -> registerService<QNetworkAccessManager>(); // 2
    context -> registerService<RestPropFetcher,QNetworkAccessManager("hamburgWeather"); // 3
    
    context -> publish(); // 4

1. Creates a StandardQApplicationContext on the heap. Note that we assign it to a pointer of the interface QApplicationContext. This avoids accidental use of non-public API.
2. Registers a QNetworkAccessManager with the context. We are supplying no parameters here, so the component will get an auto-generated name!
3. Registers a RestPropFetcher with the context. Since the constructor requires an argument of type `QNetworkAccessManager*`, we simply supply the type as the second argument to the function.
   Everything else is taken care of automatically. We assign the name "propFetcher" to this service.
4. The context is published. It will instantiate a QNetworkAccessManager first, then a RestPropFetcher, injecting the QNetworkAccessManager.


However, something is actually missing from the above code: the RestPropFetcher must be configured with a Url before it can be used!  
Let's assume we want to use the Url `"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"`.  
How do we do this?
The answer is simple: we supply the required property-name and value at registration:

    context -> registerService<RestPropFetcher,QNetworkAccessManager("hamburgWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}}); 

Likewise, we could register a second service that will fetch the weather-information for Berlin:

    context -> registerService<RestPropFetcher,QNetworkAccessManager("berlinWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10382"}}); 

## 1 -> N relations

Now, let's extend our example a bit: we want to create a component that shall receive the fetched values from all RestPropFetchers and
somehow sum them up. Such a component could look like this:

    class PropSummary : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropSummary(const QList<RestPropFetcher>& fetchers, QObject* parent = nullptr);
    };

Note that the above component comprises a 1 -> N relationship with its dependent components.
We must notify the QApplicationContext about this, so that it can correctly inkect all the matching dependencies into the component's constructor.  
The following statement will do the trick:

   context -> registerService<PropSummary,Dependency<RestPropFetcher,Cardinality::N>>();
   
## Service-interfaces

In the preceeding example, we have used our QObject-derived class `RestPropFetcher` directly. We have also specified 
the class as the dependency-type for `RestPropSummary`.  
However, in most complex applications you will likely use an abstract base-class (or 'interface').
Additionally, this interface need not be derived from QObject!  
This is well supported by QApplicationContext. First, let's declare our interface:

    class PropFetcher : public QObject {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)

      public:
      
      explicit PropFetcher(QObject* parent = nullptr);

      virtual QString value() const = 0;

      signals:
      void valueChanged();
    };

Then, we modify our class `RestPropFetcher` so that it derives from this interface:

    class RestPropFetcher : public PropFetcher {
      Q_OBJECT

      Q_PROPERTY(QString url READ url WRITE setURl NOTIFY urlChanged)
      
      public:
      
      explicit RestPropFetcher(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

      virtual QString value() const override;

      QString url() const;

      void setUrl(const QString&);

      signals:
      void urlChanged();
    };

And lastly, we modify our class `PropSummary` so that it accepts dependencies of the interface-type:

    class PropSummary : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropSummary(const QList<PropFetcher>& fetchers, QObject* parent = nullptr);
    };

Putting it all together, we use the helper-template `Service` for specifying both an interface-type and an implementation-type:


    using namespace com::neppert::context;
    
    QApplicationContext* context = new StandardQApplicationContext; 
    
    context -> registerService<PropSummary,Dependency<PropFetcher,Cardinality::N>>();
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager("hamburgWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}}); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager("berlinWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10382"}}); 
    
    context -> publish(); 

Two noteworthy things:

1. You may have noticed, that the registration of the `QNetworkAccessManager` is no longer there!  
The reason for this is that the class has an accessible default-constructor. `QApplicationContext` makes sure that whenever a dependency
for a specific type is resolved and a matching service has not been explicitly registered, a default-instance will be created if possible.
2. The order of registrations has been switched: now, the dependent service `PropSummary` is registered before the services it depends on.
This was done to demonstrate that **the order of registrations is actually completely irrelevant**!  
`QApplicationContext` figures out automatically what the correct order must be.













