# QApplicationContext
A DI-Container for Qt-based applications, inspired by Spring

## Motivation

As an experienced developer, you know how crucial it is to follow the SOC (Separation Of Concern) - principle:  
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
Naturally, this component will make use of a `QNetworkAccessManager`. Also, a URL will be passed into the constructor.
The declaration of such a component may look like this:

    class RestPropFetcher : public QObject {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)

      
      public:
      
      RestPropFetcher(const QString& url, QNetworkAccessManager* networkManager, QObject* parent = nullptr);

      QString value() const;

      QString url() const;


      signals:
      void valueChanged();
    };


Given the above component, the invocation of the constructor would look like this:

    RestPropFetcher* fetcher = new RestPropFetcher{ QString{"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}, new QNetworkAccessManager}; 

When using a QApplicationContext, you can translate this constructor-call directly into an invocation of `mcnepp::qtdi::QApplicationContext::registerService()`. 

We will soon see why this direct translation is usually not a good idea, but just for the record, this is what it looks like:

    using namespace mcnepp::qtdi;
    
    QApplicationContext* context = new StandardApplicationContext; // 1
    
    context -> registerService<RestPropFetcher>("hamburgWeather", QString{"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}, new QNetworkAccessManager); // 2
    
    context -> publish(); // 3

1. Creates a `mcnepp::qtdi::StandardApplicationContext` on the heap. Note that we assign it to a pointer of the interface `mcnepp::qtdi::QApplicationContext`. This avoids accidental use of non-public API.
2. Registers a RestPropFetcher with the context. The first argument is the name that this service shall have in the QApplicationContext.
   The next two arguments (the URL and the pointer to the `QNetworkAccessManager`) will be stored with the registration. They will be passed on to the constructor when the service is published.
3. The context is published. It will instantiate a RestPropFetcher and pass the two arguments to the constructor.

The above code has an obvious flaw: The `QNetworkAccessManager` is created outside the QApplicationContext. It will not be managed by the Context. Should another service need a `QNetworkAccessManager`,
you would have to create another instance.

We fix this by not providing the pointer to the `QNetworkAccessManager`, but instead using a kind of "placeholder" for it. This placeholder is the class-template `mcnepp::qtdi::Dependency`.
We create Dependencies by using one of the functions mcnepp::qtdi::inject(), mcnepp::qtdi::injectIfPresent() or mcnepp::qtdi::inject().

You can think of `inject` as a request to the QApplicationContext: *If a service of this type has been registered, please provide it here!*.

By leveraging `inject()`, our code becomes this:


    using namespace mcnepp::qtdi;
    
    QApplicationContext* context = new StandardApplicationContext; // 1
    
    context -> registerService<QNetworkAccessManager>(); // 2
    context -> registerService<RestPropFetcher>("hamburgWeather", QString{"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}, inject<QNetworkAccessManager>()); // 3
    
    context -> publish(); // 4

1. Creates a StandardApplicationContext on the heap. 
2. Registers a QNetworkAccessManager with the context. We are supplying no parameters here, so the component will get an auto-generated name!
3. Registers a RestPropFetcher with the context. Again, we assgn it the name "hamburgWeather". 
   And again, we pass the first constructor-argument (the URL) directly. However, for the second argument, we use `inject<QNetworkAccessManager>()`.
4. The context is published. It will instantiate a QNetworkAccessManager first, then a RestPropFetcher, injecting the QNetworkAccessManager into its constructor.


## Externalized Configuration

In the above example, we were configuring the Url with a String-literal in the code. This is less than optimal, as we usually want to be able
to change such configuration-values without re-compiling the program.  
This is made possible with so-called *placeholders* in the configured values:  
A placeholder embedded in `${   }` will be resolved by the QApplicationContext using Qt's `QSettings` class.  
You simply register one or more instances of `QSettings` with the context, using mcnepp::qtdi::QApplicationContext::registerObject().
This is what it looks like if you out-source the "url" configuration-value into an external configuration-file:

    context -> registerObject(new QSettings{"application.ini", QSettings::IniFormat, context});
    
    context -> registerService<RestPropFetcher>("hamburgWeather", QString{"${hamburgWeatherUrl}"}, inject<QNetworkAccessManager>()); 
    context -> registerService<RestPropFetcher>("berlinWeather", QString{"${berlinWeatherUrl}"}, inject<QNetworkAccessManager>()); 


You could even improve on this by re-factoring the common part of the Url into its own configuration-value:

    context -> registerService<RestPropFetcher>("hamburgWeather", QString{"${baseUrl}?stationIds=${hamburgStationId}"}, inject<QNetworkAccessManager>()); 
    context -> registerService<RestPropFetcher>("berlinWeather", QString{"${baseUrl}?stationIds=${berlinStationId}"}, inject<QNetworkAccessManager>()); 
    


## Configuring services with Q_PROPERTY

We have seen how we can inject configuration-values into Service-constructors. Another way of configuring Services is to use Q_PROPERTY declarations.
Suppose we modify the declaration of `RestPropFetcher` like this:

    class RestPropFetcher : public QObject {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)
      
      Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)

      
      public:
      
      explicit RestPropFetcher(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

      QString value() const;

      QString url() const;
      
      void setUrl(const QString&);

      signals:
      void valueChanged();
      void urlChanged();
    };


Now, the "url" cannot be injected into the constructor. Rather, it must be set explicitly via the corresponding Q_PROPERTY.
For this, the yet unused `service_config` argument comes into play: It contains a `QVariantMap` with the names and values of properties to set:

    context -> registerService<RestPropFetcher>("hamburgWeather", make_config({{"url", "${baseUrl}?stationIds=${hamburgStationId}"}}), inject<QNetworkAccessManager>());
    context -> registerService<RestPropFetcher>("berlinWeather", make_config({{"url", "${baseUrl}?stationIds=${berlinStationId}"}}), inject<QNetworkAccessManager>()); 

As you can see, the code has changed quite significantly: instead of supplying the Url as a constructor-argument, you use mcnepp::qtdi::make_config() and pass in the key/value-pair for configuring
the service's url as a Q_PROPERTY.

**Note:** Every property supplied to mcnepp::qtdi::QApplicationContext::registerService() will be considered a potential Q_PROPERTY of the target-service. mcnepp::qtdi::QApplicationContext::publish() will fail if no such property can be
found.  
However, if you prefix the property-key with a dot, it will be considered a *private property*. It will still be resolved via QSettings, but no attempt will be made to access a matching Q_PROPERTY.
Such *private properties* may be passed to a mcnepp::qtdi::QApplicationContextPostProcessor (see below).



## Managed Services vs. Un-managed Objects

The function mcnepp::qtdi::QApplicationContext::registerService() that was shown in the preceeding example results in the creation of a `managed service`.  
The entire lifecylce of the registered Service will be managed by the QApplicationContext.  
Sometimes, however, it will be necessary to register an existing QObject with the ApplicationContext and make it available to other components as a dependency.  
A reason for this may be that the constructor of the class does not merely accept other `QObject`-pointers (as "dependencies"), but also `QString`s or other non-QObject-typed value.  
A good example would be registering objects of type `QSettings`, which play an important role in *Externalized Configuration* (see below).  


There are some crucial differences between mcnepp::qtdi::QApplicationContext::registerService() and mcnepp::qtdi::QApplicationContext::registerObject(), as the following table shows:

| |registerService|registerObject|
|---|---|---|
|Instantiation of the object|upon mcnepp::qtdi::QApplicationContext::publish(bool)|prior to the registration with the QApplicationContext|
|When does the Q_PROPERTY `Registration::publishedObjects` become non-empty?|upon mcnepp::qtdi::QApplicationContext::publish(bool)|immediately after the registration|
|Naming of the QObject|`QObject::objectName` is set to the name of the registration|`QObject::objectName` is not touched by QApplicationContext|
|Handling of Properties|The key/value-pairs supplied at registration will be set as Q_PROPERTYs by QApplicationContext|All properties must be set before registration|
|Processing by mcnepp::qtdi::QApplicationContextPostProcessor|Every service will be processed by the registered QApplicationContextPostProcessors|Object is not processed|
|Invocation of *init-method*|If present, will be invoked by QApplicationContext|If present, must have been invoked prior to registration|
|Destruction of the object|upon destruction of the QApplicationContext|at the discrection of the code that created it|




## Types of dependency-relations

In our previous example, we have seen the dependency of our `RestPropFetcher` to a `QNetworkAccessManager`.  
This constitutes a *mandatory dependency*: instantion of the `RestPropFetcher`will fail if no `QNetworkAccessManager`can be found.
However, there are more ways to express a dependency-relation.  
This is reflected by the enum-type `mcnepp::qtdi::Kind` and its enum-constants as listed below:


### MANDATORY

As stated before, mandatory dependencies enforce that there is exactly one service of the dependency-type present in the ApplicationContext.
Otherwise, publication will fail.
Mandatory dependencies can be specified by simply listing the type of the dependency as a template-argument:

    registerService<RestPropFetcher>("", service-config{}, inject<QNetworkAccessManager>());


### OPTIONAL
A service that has an *optional dependency* to another service may be instantiated even when no matching other service can be found in the ApplicationContext.
In that case, `nullptr` will be passed to the service's constructor.  
Optional dependencies are specified the `Dependency` helper-template. Suppose it were possible to create our `RestPropFetcher` without a `QNetworkAccessManage`:

    registerService<RestPropFetcher>("", service-config{}, injectIfPresent<QNetworkAccessManager>());

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

    context -> registerService<PropFetcherAggregator>("propFetcherAggregation", injectAll<RestPropFetcher>());

**Note:**, while constructing the `QList` with dependencies, the ordering of registrations of **non-interdependent services** will be honoured as much as possible.
In the above example, the services "hamburgWeather" and "berlinWeather" will appear in that order in the `QList` that is passed to the `PropFetcherAggreator`.
    

### PRIVATE_COPY

Sometime, you may want to ensure that every instance of your service will get its own instance of a dependency. This might be necessary if the dependency shall be configured (i.e. modified) by the
dependent service, thus potentially affecting other dependent services.  
QApplicationContext defines the dependency-type PRIVATE_COPY for this. Applied to our example, you would enfore a private `QNetworkAccessManager` for both `RestPropFetcher`s like this:

    context -> registerService<RestPropFetcher>("berlinWeather", injectPrivateCopy<QNetworkAccessManager>()); 
    context -> registerService<RestPropFetcher>("hamburgWeather", injectPrivateCopy<QNetworkAccessManager<()); 
    
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
    
    context -> registerService<PropFetcherAggregator,Dependency<PropFetcher,Kind::N>>("propFetcherAggration");
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>>("hamburgWeather", make_config({{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}}), inject<QNetworkAccessManager>()); 
    context -> registerService<Service<PropFetcher,RestPropFetcher>>("berlinWeather", make_config({{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10382"}}), inject<QNetworkAccessManager>()); 
    
    context -> publish(); 

Two noteworthy things:

1. You may have noticed that the registration of the `QNetworkAccessManager` is no longer there.  
The reason for this is that the class has an accessible default-constructor. `QApplicationContext` makes sure that whenever a dependency
for a specific type is resolved and a matching service has not been explicitly registered, a default-instance will be created if possible.
2. The order of registrations has been switched: now, the dependent service `PropFetcherAggregator` is registered before the services it depends on.
This was done to demonstrate that, **regardless of the ordering of registrations**, the dependencies will be resolved correctly!  
`QApplicationContext` figures out automatically what the correct order must be.


## The Service-lifefycle

Every service that is registered with a QApplicationContext will go through the following states, in the order shown.
The names of these states are shown for illustration-purposes only. They are not visible outside the QApplicationContext.  
However, some transitions may have observable side-effects.

|External Trigger|Internal Step|State|Observable side-effect|
|---|---|---|---|
|ApplicationContext::registerService()||REGISTERED| |
|ApplicationContext::publish(bool)|Instantiation via constructor or service_factory|NEW|Invocation of Services's constructor|
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


    context -> registerService<PropFetcherAggregator>("propFetcherAggregator", injectAll<PropFetcher>());
    
    context -> registerService<Service<PropFetcher,RestPropFetcher>>("hamburgWeather", {
      {"url", "${weatherUrl}${hamburgStationId}"},
      {"summary", "&propFetcherAggregator"}
    },
    inject<QNetworkAccessManager>()); 

## Accessing a service after registration

So far, we have published the ApplicationContext and let it take care of wiring all the components together.  
In some cases, you need to obtain a reference to a member of the Context after it has been published.  
This is where the return-value of mcnepp::qtdi::QApplicationContext::registerService() comes into play. It offers a Q_PROPERTY mcnepp::qtdi::Registration::publishedObjects() with a corresponding signal which is emitted
when the corresponding services are published.  
As a Q_OBJECT cannot be a class-template, the property and signal use the generic `QObject*`, which is unfortunate.  
Therefore, it it strongly recommended to use the method mcnepp::qtdi::ServiceRegistration::subscribe() instead.  
In addition to being type-safe, this method has the advantage that is will automatically notify you if you subscribe after the service has already been published.  
This code shows how to do this:

    auto registration = context -> registerService<Service<PropFetcher,RestPropFetcher>>("hamburgWeather", make_config({{"url", "${weatherUrl}${hamburgStationId}"}}), inject<QNetworkAccessManager>()); 
    
    registration -> subscribe(this, [](PropFetcher* fetcher) { qInfo() << "I got the PropFether!"; });

## Accessing published members of the ApplicationContext

In the previous paragraph, we used the mcnepp::qtdi::ServiceRegistration obtained by mcnepp::qtdi::QApplicationContext::registerService(), which refers to a single member of the ApplicationContext.
However, we might be interested in all members of a certain service-type.  
This can be achieved using mcnepp::qtdi::QApplicationContext::getRegistration(), which yields a mcnepp::qtdi::ServiceRegistration that represents *all services of the requested service-type*.


    auto registration = context -> getRegistration<PropFetcher>();
    
    registration -> subscribe(this, [](PropFetcher* fetcher) { qInfo() << "I got another PropFetcher!"; });
    
## Tweaking services (QApplicationContextPostProcessor)

Whenever a service has been instantiated and all properties have been set, QApplicationContext will apply all registered mcnepp::qtdi::QApplicationContextPostProcessor`s 
to it. These are user-supplied QObjects that implement the aforementioned interface which comprises a single method:

    QApplicationContextPostProcessor::process(QApplicationContext*, QObject*,const QVariantMap&)

You might apply further configuration to your service there, or perform logging or monitoring tasks.
Any information that you might want to pass to a QApplicationContextPostProcessor can be supplied as
so-called *private properties*: Just prefix the property-key with a dot.


## 'Starting' services

The last step done in mcnepp::qtdi::QApplicationContext::publish() for each service is the invocation of an *init-method*, should one have been 
registered.

*Init-methods* are supplied as part of the `service_config`, for example like this:

    context -> registerService<PropFetcherAggregator>("propFetcherAggregator", make_config({}, "", false, "init"), injectAll<PropFetcher>());

Suitable *init-methods* are `Q_INVOKABLE`-methods with either no arguments, or with one argument of type `QApplicationContext*`.

## Resolving ambiguities

Sometimes, multiple instances of a service with the same service-type have been registered.  
(In our previous examples, this was the case with two instances of the `PropFetcher` service-type.)  
If you want to inject only one of those into a dependent service, how can you do that?  
Well, using the name of the registered service seems like a good idea.
The following code will still provide a `QList<PropFetcher*>` to the `PropFetcherAggregator`. However, the List will
contain solely the service that was registered under the name "hamburgWeather":  

    context -> registerService<PropFetcherAggregator>("propFetcherAggregator", injectAll<PropFetcher>("hamburgWeather"));

## Customizing service-instantiation

So far, we have seen that each call to mcnepp::qtdi::QApplicationContext::registerService() mirrors a corresponding constructor-invocation of the service-type.

In order for this to work, several pre-conditions must be true:

1. the constructor of the service must be accessible, i.e. declared `public`.
2. The number of mandatory arguments must match the arguments provided via `QApplicationContext::registerService()`, in excess of the service-name and, optionally, the `service_config`.
3. For each `Dependency<T>` with `Kind::MANDATORY`, `Kind::OPTIONAL` or `Kind::PRIVATE_COPY`, the argument-type must be `T*`.
4. For each `Dependency<T>` with `Kind::N`, the argument-type must be `QList<T*>`.

If any of these conditions fails, then the invocation of `QApplicationContext::registerService()` will fail compilation.

However, there is a remedy: by specializing the template mcnepp::qtdi::service_factory for your service-type, you can provide your own "factory-function" that can overcome any of the above obstacles.

As with all such specializations, it must reside in the namespace of the primary template, i.e. `mcnepp::qtdi`.

The specialization must provide a call-operator that takes the arguments provided by QApplicationContext, adapts those arguments to the arguments expected by the
service-type, and returns a pointer to the newly created service.

Suppose that the declaration of the class `PropFetcherAggregator` has been changed in the following way:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      static PropFetcherAggregator* create(const std::vector<PropFetcher*>& fetchers, QObject* parent = nullptr);
      
      private:
      
      explicit PropFetcherAggregator(const std::vector<PropFetcher*>& fetchers, QObject* parent = nullptr);
    };

Two things have been changed: 

1. Instead of the constructor, which is now private, there is a public static factory-function "create".
2. The list of PropFetchers is supplied as a `std::vector`, not as a `QList`.

Here is all that we have to do:

    namespace mcnepp::qtdi {
      template<> struct service_factory<PropFetcherAggregator> {
        PropFetcherAggregator* operator()(const QList<PropFetcher*> fetchers) const {
          return PropFetcherAggregator::create(std::vector<PropFetcher*>{fetchers.begin(), fetchers.end()});
        }
      };
    }

And, voila: We can register our service exactly as we did before!



## Publishing an ApplicationContext more than once

Sometimes, it may be desirable to inovoke QApplicationContext::publish(bool) more than once.
Proceeding with the previous example, there may be several independent modules that each want to supply a service of type `PropFetcher`.
Each of these modules will rightly assume that the dependency of type `QNetworkAccessManager` will be automatically supplied by the QApplicationContext.

But which module shall then invoke QApplicationContext::publish(bool)? Do we need to coordinate this with additional code?
That could be a bit unwieldly. Luckily, this is not necessary.

Given that each module has access to the (global) QApplicationContext, you can simply do this in some initialization-code in module A:

    context -> registerService<Service<PropFetcher,RestPropFetcher>>("hamburgWeather", make_config({{"url", "${weatherUrl}${hamburgStationId}"}}), inject<QNetworkAccessManager>()); 
    context -> publish();

...and this in module B:

    context -> registerService<Service<PropFetcher,RestPropFetcher>>("berlinWeather", make_config({{"url", "${weatherUrl}${berlinStationId}"}}), inject<QNetworkAccessManager>()); 
    context -> publish();

At the first `publish()`, an instance of `QNetworkAccessManager` will be instantiated. It will be injected into both `RestPropFetchers`.

This will work <b>regardless of the order in which the modules are initialized</b>!

Now let's get back to our class `PropFetcherAggregator` from above. We'll assume that a third module C contains this initialization-code:

    context -> registerService<PropFetcherAggregator>("propFetcherAggregator", injectAll<PropFetcher>());
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

## Publish-mode ('allowPartial')

The function QApplicationContext::publish(bool) has a boolean argument `allowPartial` with a default-value of `false`.
The following table shows how this argument affects the outcome of the function:

### allowPartial = false:
- If publication of one service fails for whatever reason, the function will immediately return without attempts to publish other services.
- All errors that occur while publishing a service will be logged with the level QtMsgType::QtCriticalMessage.

### allowPartial = true:
- If publication of one service fails for reasons that may be fixed, the function will continue to publish other services. 
  Such reasons include:
  - unresolved dependencies (as those may be registered later).
  - unresolved config-values (as those may be configured later).
- Such "fixable errors" that occur while publishing a service will be logged with the level QtMsgType::QtWarningMessage.
- If publication of one service fails for reasons that will prevail, the function will will immediately return without attempts to publish other services.
  Such reasons include:
  - ambiguous dependencies (as those cannot not be removed from the ApplicationContext).
  - non-existing names of Q_PROPERTYs.
  - syntactically erronous config-keys (such as `"$interval}"`).
- Such "fatal errors" that occur while publishing a service will be logged with the level QtMsgType::QtCriticalMessage.













