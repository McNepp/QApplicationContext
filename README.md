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

    using namespace mcnepp::qtdi;
    
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


## Externalized Configuration

In the above example, we were configuring the Url with a String-literal in the code. This is less than optimal, as we usually want to be able
to change such configuration-values without re-compiling the program.  
This is made possible with so-called *placeholders* in the configured values:  
A placeholder embedded in `${   }` will be resolved by the QApplicationContext using Qt's `QSettings` class.  
You simply register one or more instances of `QSettings` with the context, using `QApplicationContext::registerObject()`.
This is what it looks like if you out-source the "url" configuration-value into an external configuration-file:

    context -> registerObject(new QSettings{"application.ini", QSettings::IniFormat, context});
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "${hamburgWeatherUrl}"}}); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("berlinWeather", {{"url", "${hamburgBerlinUrl}"}}); 


You could even improve on this by re-factoring the common part of the Url into its own configuration-value:

    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "${weatherUrl}${hamburgStationId}"}}); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("berlinWeather", {{"url", "${weatherUrl}${berlinStationId}"}}); 
    
**Note:** Every property supplied to `QApplicationContext::registerService()` will be considered a potential Q_PROPERTY of the target-service. `QApplicationContext::publish()` will fail if no such property can be
found.  
However, if you prefix the property-key with a dot, it will be considered a *private property*. It will still be resolved via QSettings, but no attempt will be made to access a matching Q_PROPERTY.
Such *private properties* may be passed to a `QApplicationContextPostProcessor` (see below).


## Managed Services vs. Un-managed Objects

The function `QApplicationContext::registerService()` that was shown in the preceeding example results in the creation of a `managed service`.  
The entire lifecylce of the registered Service will be managed by the QApplicationContext.  
Sometimes, however, it will be necessary to register an existing QObject with the ApplicationContext and make it available to other components as a dependency.  
A reason for this may be that the constructor of the class does not merely accept other `QObject`-pointers (as "dependencies"), but also `QString`s or other non-QObject-typed value.  
A good example would be registering objects of type `QSettings`, which play an important role in *Externalized Configuration* (see below).  


There are some crucial differences between `QApplicationContext::registerService()` and `QApplicationContext::registerObject()`, as the following table shows:

| |registerService|registerObject|
|---|---|---|
|Instantiation of the object|upon `QApplicationContext::publish()`|prior to the registration with the QApplicationContext|
|When does the Q_PROPERTY `Registration::publishedObjects` become non-empty?|upon `QApplicationContext::publish()`|immediately after the registration|
|Naming of the QObject|`QObject::objectName` is set to the name of the registration|`QObject::objectName` is not touched by QApplicationContext|
|Handling of Properties|The key/value-pairs supplied at registration will be set as Q_PROPERTYs by QApplicationContext|All properties must be set before registration|
|Processing by `QApplicationContextPostProcessor`|Every service will be processed by the registered QApplicationContextPostProcessors|Object is not processed|
|Invocation of *init-method*|If present, will be invoked by QApplicationContext|If present, must have been invoked prior to registration|
|Destruction of the object|upon destruction of the QApplicationContext|at the discrection of the code that created it|




## Types of dependency-relations

In our previous example, we have seen the dependency of our `RestPropFetcher` to a `QNetworkAccessManager`.  
This constitutes a *mandatory dependency*: instantion of the `RestPropFetcher`will fail if no `QNetworkAccessManager`can be found.
However, there are more ways to express a dependency-relation.  
This is reflected by the enum-type `mcnepp::qtdi::Cardinality` and its enum-constants as listed below:


### MANDATORY

As stated before, mandatory dependencies enforce that there is exactly one service of the dependency-type present in the ApplicationContext.
Otherwise, publication will fail.
Mandatory dependencies can be specified by simply listing the type of the dependency as a template-argument:

    registerService<RestPropFetcher,QNetworkAccessManager>();

Just for the sake of consistency, you could also use the `Dependency` helper-template, even though this is not recommended:

    registerService<RestPropFetcher,Dependency<QNetworkAccessManager,Cardinality::MANDATORY>>();

### OPTIONAL
A service that has an *optional dependency* to another service may be instantiated even when no matching other service can be found in the ApplicationContext.
In that case, `nullptr` will be passed to the service's constructor.  
Optional dependencies are specified the `Dependency` helper-template. Suppose it were possible to create our `RestPropFetcher` without a `QNetworkAccessManage`:

    registerService<RestPropFetcher,Dependency<QNetworkAccessManager,Cardinality::OPTIONAL>>();

### N (one-to-many)

Now, let's extend our example a bit: we want to create a component that shall receive the fetched values from all RestPropFetchers and
somehow sum them up. Such a component could look like this:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropFetcherAggregator(const QList<RestPropFetcher*>& fetchers, QObject* parent = nullptr);
    };

Note that the above component comprises a one-to-,any relationship with its dependent components.
We must notify the QApplicationContext about this, so that it can correctly inject all the matching dependencies into the component's constructor.  
The following statement will do the trick:

    context -> registerService<PropFetcherAggregator,Dependency<RestPropFetcher,Cardinality::N>>("propFetcherAggregation");

### PRIVATE_COPY

Sometime, you may want to ensure that every instance of your service will get its own instance of a dependency. This might be necessary if the dependency shall be configured (i.e. modified) by the
dependent service, thus potentially affecting other dependent services.  
QApplicationContext defines the dependency-type PRIVATE_COPY for this. Applied to our example, you would enfore a private `QNetworkAccessManager` for both `RestPropFetcher`s like this:

    context -> registerService<RestPropFetcher,Dependency<QNetworkAccessManager,Cardinality::PRIVATE_COPY>("berlinWeather"); 
    context -> registerService<RestPropFetcher,Dependency<QNetworkAccessManager,Cardinality::PRIVATE_COPY>("hamburgWeather"); 
    
**Note:** The life-cycle of instances created with PRIVATE_COPY will not be managed by the ApplicationContext! Rather, the ApplicationContext will set the dependent object's `QObject::parent()` to the dependent service, thus it will be destructed when its parent is destructed.
    
The following table sums up the characteristics of the different types of dependencies:

<table><tr><th>&nbsp;</th><th>Normal behaviour</th><th>What if no dependency can be found?</th><th>What if more than one dependency can be found?</th></tr>
<tr><td>MANDATORY</td><td>Injects one dependency into the dependent service.</td><td>If the dependency-type has an accessible default-constructor, this will be used to register and create an instance of that type.
<br>If no default-constructor exists, publication of the ApplicationContext will fail.</td>
<td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
<tr><td>OPTIONAL</td><td>Injects one dependency into the dependent service</td><td>Injects `nullptr` into the dependent service.</td>
<td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
<tr><td>N</td><td>Injects all dependencies of the dependency-type that have been registered into the dependent service, using a `QList`</td>
<td>Injects an empty `QList` into the dependent service.</td>
<td>See 'Normal behaviour'</td></tr>
<tr><td>PRIVATE_COPY</td><td>Injects a newly created instance of the dependency-type and sets its `QObject::parent()` to the dependent service.</td>
<td>If the dependency-type has an accessible default-constructor, this will be used to create an instance of that type.<br>
If no default-constructor exists, publication of the ApplicationContext will fail.</td>
<td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
</table>


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
      
      explicit PropFetcherAggregator(const QList<PropFetcher*>& fetchers, QObject* parent = nullptr);
    };

Putting it all together, we use the helper-template `Service` for specifying both an interface-type and an implementation-type:


    using namespace mcnepp::qtdi;
    
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

## The Service-lifefycle

Every service that is registered with a QApplicationContext will go through the following states, in the order shown.
The names of these states are shown for illustration-purposes only. They are not visible outside the QApplicationContext.  
However, some transitions may have observable side-effects.

|External Trigger|Internal Step|State|Observable side-effect|
|---|---|---|---|
|ApplicationContext::registerService()||REGISTERED| |
|ApplicationContext::publish()|Instantiation via constructor or service_factory|NEW|Invocation of Services's constructor|
| |Set properties|AFTER_PROPERTIES_SET|Invocation of property-setters|
| |Apply QApplicationContextPostProcessor|PROCESSED|Invocation of user-supplied QApplicationContextPostProcessor::process()| 
| |if exists, invoke init-method|PUBLISHED|emit signal Registration::publishedObjectsChanged|
|~ApplicationContext()|delete service|DESTROYED|Invoke Services's destructor|



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

## Publishing an ApplicationContext more than once

Sometimes, it may be desirable to inovoke QApplicationContext::publish() more than once.
Proceeding with the previous example, there may be several independent modules that each want to supply a service of type `PropFetcher`.
Each of these modules will rightly assume that the dependency of type `QNetworkAccessManager` will be automatically supplied by the QApplicationContext.

But which module shall then invoke QApplicationContext::publish()? Do we need to coordinate this with additional code?
That could be a bit unwieldly. Luckily, this is not necessary.

Given that each module has access to the (global) QApplicationContext, you can simply do this in some initialization-code in module A:

    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("hamburgWeather", {{"url", "${weatherUrl}${hamburgStationId}"}}); 
    context -> publish();

...and this in module B:

    context -> registerService<Service<PropFetcher,RestPropFetcher>,QNetworkAccessManager>("berlinWeather", {{"url", "${weatherUrl}${berlinStationId}"}}); 
    context -> publish();

At the first `publish()`, an instance of `QNetworkAccessManager` will be instantiated. It will be injected into both `RestPropFetchers`.

This will work <b>regardless of the order in which the modules are initialized</b>!

Now let's get back to our class `PropFetcherAggregator` from above. We'll assume that a third module C contains this initialization-code:

    context -> registerService<PropFetcherAggregator,Dependency<PropFetcher,Cardinality::N>>("propFetcherAggregator");
    context -> publish();

Unfortunately, this code will only have the desired effect of injecting all `PropFetchers` into the `PropFetcherAggregator` if it is executed after the code in modules A and B.
Thus, we have once again introduced a mandatory order of initialization!

But we can do better: First, we need to tweak our class `PropFetcherAggregator` a little:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropFetcherAggregator(const QList<PropFetcher*>& fetchers = {}, QObject* parent = nullptr);
      
      void addPropFetcher(PropFetcher*);
    };

As you can see, we've added a default-value for the constructor-argument, thus made the class default-constructible. 
We've also added a member-function `addPropFetcher(PropFetcher*)`, which we'll put to use in the revised initialization-code in module C:

    auto aggregatorRegistration = context -> registerService<PropFetcherAggregator>("propFetcherAggregator"); // No need to specify Dependency anymore!
    
    aggregatorRegistration->autowire(&PropFetcherAggregator::addPropFetcher); // Will cause all PropFetchers to be injected into PropFetcherAggregator.
    
    context -> publish();

And that's all that is needed to get rid of any mandatory order of initialization of the modules A, B and C.












