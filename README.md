# QApplicationContext
A DI-Container for Qt-based applications, inspired by Spring

## Motivation

As an experienced developer, you know how crucial it is to follow the SOC*  (**S**eparation **O**f **C**oncern) - principle:  
Each component shall be responsible for "one task" only, (or, at least, for one set of related tasks).  
Things that are outside of a component's realm shall be delegated to other components.  
This, of course, means that some components will need to get references to those other components, commonly referred to as "Dependencies".  
Following this rule greatly increases testability of your components, thus making your software more robust.

But, sooner or later, you will ask yourself: How do I wire all those inter-dependent components together?  
How do I create application-wide "singletons" (without resorting to C++ singletions, which are notoriously brittle), and how can I create multiple implmentations of the same interface?

## Features

- Provides an easy-to use Container for Dependency-Injection of Qt-based components.
- Offers a typesafe syntax for declaring dependencies between components in C++.
- Automatically determines the precise order in which the inter-dependent components must be instantiated.
- Helps make the components *container-agnostic*.
- Dependency-injection via constructor.
- Dependency-injection via Qt-properties.
- Supports both one-to-one and one-to-many relations between components.
- Further configuration of components after creation, including externalized configuration (using `QSettings`).
- Automatic invocation of an *init-method* after creation, using Qt-slots.
- Offers a Qt-signal for "published" components, together with a type-safe `subscribe()` mechanism.
- Fail-fast, i.e. terminate compilation with meaningful diagnostics if possible.
- Helps to find runtime-problems by generating verbose logging (using a `QLoggingCategory`).

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
    context -> registerService<RestPropFetcher,QNetworkAccessManager>("hamburgWeather"); // 3
    
    context -> publish(); // 4

1. Creates a StandardQApplicationContext on the heap. Note that we assign it to a pointer of the interface QApplicationContext. This avoids accidental use of non-public API.
2. Registers a QNetworkAccessManager with the context. We are supplying no parameters here, so the component will get an auto-generated name!
3. Registers a RestPropFetcher with the context. Since the constructor requires an argument of type `QNetworkAccessManager*`, we simply supply the type as the second argument to the function.
   Everything else is taken care of automatically. We assign the name "hamburgWeather" to this service.
4. The context is published. It will instantiate a QNetworkAccessManager first, then a RestPropFetcher, injecting the QNetworkAccessManager.


However, something is actually missing from the above code: the RestPropFetcher must be configured with a Url before it can be used!  
Let's assume we want to use the Url `"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"`.  
How do we do this?
The answer is simple: we supply the required property-name and value at registration:

    context -> registerService<RestPropFetcher,QNetworkAccessManager>("hamburgWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}}); 

Likewise, we could register a second service that will fetch the weather-information for Berlin:

    context -> registerService<RestPropFetcher,QNetworkAccessManager>("berlinWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10382"}}); 

## One-to-many relations

Now, let's extend our example a bit: we want to create a component that shall receive the fetched values from all RestPropFetchers and
somehow sum them up. Such a component could look like this:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropFetcherAggregator(const QList<RestPropFetcher>& fetchers, QObject* parent = nullptr);
    };

Note that the above component comprises a one-to-N relationship with its dependent components.
We must notify the QApplicationContext about this, so that it can correctly inject all the matching dependencies into the component's constructor.  
The following statement will do the trick:

   context -> registerService<PropFetcherAggregator,Dependency<RestPropFetcher,Cardinality::N>>("propFetcherAggregation");
   
## Service-interfaces

In the preceeding example, we have used our QObject-derived class `RestPropFetcher` directly. We have also specified 
the class as the dependency-type for `PropFetcherAggregator`.  
However, in most complex applications you will likely use an abstract base-class (or 'interface').
Additionally, this interface need not be derived from QObject!  
This is well supported by QApplicationContext. First, let's declare our interface:

    class PropFetcher  {

      public:
      
      virtual QString value() const = 0;
      
      virtual ~PropFetcher() = default;
    };

Then, we modify our class `RestPropFetcher` so that it derives from both QObject and this interface:

    class RestPropFetcher : public QObject, public PropFetcher {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)
      
      Q_PROPERTY(QString url READ url WRITE setURl NOTIFY urlChanged)
      
      public:
      
      explicit RestPropFetcher(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

      virtual QString value() const override;

      QString url() const;

      void setUrl(const QString&);

      signals:

      void valueChanged();
      
      void urlChanged();
    };

And lastly, we modify our class `PropFetcherAggregator` so that it accepts dependencies of the interface-type:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropFetcherAggregator(const QList<PropFetcher>& fetchers, QObject* parent = nullptr);
    };

Putting it all together, we use the helper-template `Service` for specifying both an interface-type and an implementation-type:


    using namespace com::neppert::context;
    
    QApplicationContext* context = new StandardQApplicationContext; 
    
    context -> registerService<PropFetcherAggregator,Dependency<PropFetcher,Cardinality::N>>("propFetcherAggration");
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}}); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("berlinWeather", {{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10382"}}); 
    
    context -> publish(); 

Two noteworthy things:

1. You may have noticed that the registration of the `QNetworkAccessManager` is no longer there.  
The reason for this is that the class has an accessible default-constructor. `QApplicationContext` makes sure that whenever a dependency
for a specific type is resolved and a matching service has not been explicitly registered, a default-instance will be created if possible.
2. The order of registrations has been switched: now, the dependent service `PropFetcherAggregator` is registered before the services it depends on.
This was done to demonstrate that **the order of registrations is actually completely irrelevant**!  
`QApplicationContext` figures out automatically what the correct order must be.

## Externalized Configuration

In the above example, we were configuring the Url with a String-literal in the code. This is less than optimal, as we usually want to be able
to change such configuration-values without re-compiling the program.  
This is made possible with so-called *placeholders* in the configured values:  
A placeholder embedded in `${   }` will be resolved by the QApplicationContext using Qt's `QSettings` class.  
You simply register one or more instances of `QSettings` with the context, using `QApplicationContext::registerObject()`.
This is what it looks like if you out-source the "url" configuration-value into an external configuration-file:

    context -> registerObject(new QSettings{"application.ini", QSettings::IniFormat, context});
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "${hamburgWeatherUrl}"}}); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("bearlinWeather", {{"url", "${hamburgBerlinUrl}"}}); 


You could even improve on this by re-factoring the common part of the Url into its own configuration-value:

    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "${weatherUrl}${hamburgStationId}"}}); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("bearlinWeather", {{"url", "${weatherUrl}${berlinStationId}"}}); 
    
**Note:** Every property supplied to `QApplicationContext::registerService()` will be considered a potential Q_PROPERTY of the target-service. `QApplicationContext::publish()` will fail if no such property can be
found.  
However, if you prefix the property-key with a dot, it will be considered a *private property*. It will still be resolved via QSettings, but no attempt will be made to access a matching Q_PROPERTY.
Such *private properties* may be passed to a `QApplicationContextPostProcessor` (see below).

## Referencing other members of the ApplicationContext

Sometimes, it may be necessary to inject one member of the ApplicationContext into another member not via constructor, but via a Q_PROPERTY.  
Suppose that each `PropFetcher` shall have (for whatever reason) a reference to the `PropFetcherAggregator`.  
This cannot be done via constructor-arguments, as it would constitute a dependency-circle!  
However, we could introduce a Q_PROPERTY like this:

    class PropFetcher : public QObject {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)
      
      Q_PROPERTY(PropFetcherAggregator* summary READ summary WRITE setSummary NOTIFY summaryChanged)

      public:
      
      explicit PropFetcher(QObject* parent = nullptr);

      virtual QString value() const = 0;
      
      virtual PropFetcherAggregator* summary() const = 0;
      
      virtual void setSummary(PropFetcherAggregator*) = 0;

      signals:
      void valueChanged();
      
      void summaryChanged();
    };

And here's how this property will be automatically set to the ApplicationContext's `PropFetcherAggregator`. Note the ampersand in the property-value that means *reference to another member*:


    context -> registerService<PropFetcherAggregator,Dependency<PropFetcher,Cardinality::N>>("propFetcherAggregator");
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {
      {"url", "${weatherUrl}${hamburgStationId}"},
      {"summary", "&propFetcherAggregator"}
    }); 

## Accessing a service after registration

So far, we have published the ApplicationContext and let it take care of wiring all the components together.  
In some cases, you need to obtain a reference to a member of the Context after it has been published.  
This is where the return-value of `QApplicationContext::registerService()` comes into play. It offers a Q_PROPERTY `publishedObjects` with a corresponding signal which is emitted
when the corresponding services are published.  
As a Q_OBJECT cannot be a class-template, the property and signal use the generic `QObject*`, which is unfortunate.  
Therefore, it it strongly recommended to use the method `ServiceRegistration::subscribe()` instead.  
In addition to being type-safe, this method has the advantage that is will automatically notify you if you subscribe after the service has already been published.  
This code shows how to do this:

    auto registration = context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "${weatherUrl}${hamburgStationId}"}}); 
    
    registration -> subscribe(this, [](PropFetcher* fetcher) { qInfo() << "I got the PropFether!"; });

## Accessing published members of the ApplicationContext

In the previous paragraph, we used the `ServiceRegistration` obtained by `QApplicationContext::registerService()`, which refers to a single member of the ApplicationContext.
However, we might be interested in all members of a certain service-type.  
This can be achieved using `QApplicationContext::getRegistration()`, which yields a `ServiceRegistration` that represents *all services of the requested service-type*.


    auto registration = context -> getRegistration<PropFetcher>();
    
    registration -> subscribe(this, [](PropFetcher* fetcher) { qInfo() << "I got another PropFetcher!"; });
    
## Tweaking services (QApplicationContextPostProcessor)

Whenever a service has been instantiated and all properties have been set, QApplicationContext will apply all registered `QApplicationContextPostProcessor`s 
to it. These are user-supplied QObjects that implement the aforementioned interface which comprises a single method:

    QApplicationContextPostProcessor::process(QApplicationContext*, QObject*,const QVariantMap&)

You might apply further configuration to your service there, or perform logging or monitoring tasks.
Any information that you might want to pass to a QApplicationContextPostProcessor can be supplied as
so-called *private properties*: Just prefix the property-key with a dot.


## 'Starting' services

The last step done in `ApplicationContext::publish()` for each service is the invocation of an *init-method*, should one have been 
registered.

*Init-methods* are supplied as part of the `service_config`, for example like this:

    context -> registerService<PropFetcherAggregator,Dependency<PropFetcher,Cardinality::N>>("propFetcherAggregator", service_config{{}, false, "init"});

or, more conveniently:

    context -> registerService<PropFetcherAggregator,Dependency<PropFetcher,Cardinality::N>>("propFetcherAggregator", {}, false, "init");

Suitable *init-methods* are `Q_INVOKABLE`-methods with either no arguments, or with one argument of type `QApplicationContext*`.

## Resolving ambiguities

Sometimes, multiple instances of a service with the same service-type have been registered.  
(In our previous examples, this was the case with two instances of the `PropFetcher` service-type.)  
If you want to inject only one of those into a dependent service, how can you do that?  
Well, using the name of the registered service seems like a good idea.  
The name of a dependency can be supplied to the helper-class `Dependency`. Previously, we did not ever need to instantiate a `Dependency`, but
rather used it as a type-argument only.  
Now, we will create an instance of `Dependency` and supply a name to it:

    context -> registerService<PropFetcherAggregator>("propFetcherAggregator", service_config{{}, false, "init"}, Dependency<PropFetcher,Cardinality::N>{"hamburgWeather"});


## The Service-lifefycle

Every service that is registered with a QApplicationContext will go through the following states, in the order shown.
The names of these states are shown for illustration-purposes only. They are not visible outside the QApplicationContext.  
However, some transitions trigger may have observable side-effects.

|External Trigger|Internal Step|State|Observable side-effect|
|---|---|---|---|
|ApplicationContext::registerService()||REGISTERED| |
|ApplicationContext::publish()|Instantiation via constructor or service_factory|NEW|Invocation of Services's constructor|
| |Set properties|AFTER_PROPERTIES_SET|Invocation of property-setters|
| |Apply QApplicationContextPostProcessor|PROCESSED|Invocation of user-supplied QApplicationContextPostProcessor::process()| 
| |if exists, invoke init-method|PUBLISHED|emit signal Registration::publishedObjectsChanged|
|~ApplicationContext()|delete service|DESTROYED|Invoke Services's destructor|




