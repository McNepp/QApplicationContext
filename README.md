# QApplicationContext
A DI-Container for Qt-based applications, inspired by Spring

## Motivation

As an experienced developer, you know how crucial it is to follow the SOC (Separation Of Concern) - principle:  
Each component shall be responsible for "one task" only, (or, at least, for one set of related tasks).  
Things that are outside of a component's realm shall be delegated to other components.  
This, of course, means that some components will need to get references to those other components, commonly referred to as "Dependencies".  
Following this rule greatly increases testability of your components, thus making your software more robust.

But, sooner or later, you will ask yourself: How do I wire all those inter-dependent components together?  
How do I create application-wide "singletons" (without resorting to C++ singletions, which are notoriously brittle), and how can I create multiple implementations of the same interface?

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

When using a QApplicationContext, you can translate this constructor-call directly into an instantiation of mcnepp::qtdi::Service.

We will soon see why this direct translation is usually not a good idea, but just for the record, this is what it looks like:

    using namespace mcnepp::qtdi;
    
    QApplicationContext* context = new StandardApplicationContext; // 1
    
    auto decl = service<RestPropFetcher>(QString{"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}, new QNetworkAccessManager}; // 2
    
    context -> registerService(decl, "hamburgWeather"); // 3
    
    context -> publish(); // 4

1. Creates a `mcnepp::qtdi::StandardApplicationContext` on the heap. Note that we assign it to a pointer of the interface `mcnepp::qtdi::QApplicationContext`. This avoids accidental use of non-public API.
2. Creates a Declaration for the RestPropFetcher. The two arguments (the URL and the pointer to the `QNetworkAccessManager`) will be stored with the registration. They will be passed on to the constructor when the service is published.
3. Registers a RestPropFetcher with the context. The first argument is the name that this service shall have in the QApplicationContext.
4. The context is published. It will instantiate a RestPropFetcher and pass the two arguments to the constructor.

The above code has an obvious flaw: The `QNetworkAccessManager` is created outside the QApplicationContext. It will not be managed by the Context. Should another service need a `QNetworkAccessManager`,
you would have to create another instance.

We fix this by not providing the pointer to the `QNetworkAccessManager`, but instead using a kind of "proxy" for it. This proxy is of the opaque type `mcnepp::qtdi::Dependency`.
We can create Dependencies by using one of the functions mcnepp::qtdi::inject(), mcnepp::qtdi::injectIfPresent() or mcnepp::qtdi::inject().

You can think of `inject` as a request to the QApplicationContext: *If a service of this type has been registered, please provide it here!*.

By leveraging `inject()`, our code becomes this:


    using namespace mcnepp::qtdi;
    
    QApplicationContext* context = new StandardApplicationContext; // 1
    
    context -> registerService<QNetworkAccessManager>(); // 2
    auto decl = service<RestPropFetcher>(QString{"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}, inject<QNetworkAccessManager>()); // 3
    context -> registerService(decl, "hamburgWeather"); // 4
    
    context -> publish(); // 5

1. Creates a StandardApplicationContext on the heap. 
2. Registers a QNetworkAccessManager with the context. We are supplying no explicit name here, so the component will get an auto-generated name!
   **Note:** This line uses the simplified overload of QApplicationContext::registerService() for Services with no dependencies.
3. Creates a Declaration for a RestPropFetcher. Again, we pass the first constructor-argument (the URL) directly. However, for the second argument, we use `inject<QNetworkAccessManager>()`.
4. Registers the descriptor with the context (as before).
5. The context is published. It will instantiate a QNetworkAccessManager first, then a RestPropFetcher, injecting the QNetworkAccessManager into its constructor.

<b>Note:</b> In the above example, we obtain the mcnepp::qtdi::ServiceRegistration for the `QNetworkAccessManager` in line 2.<br>
Whenever we want to express a dependency for a Service and we have the corresponding ServiceRegistration at hand, we can leave out the `inject()` and pass the ServiceRegistration directly as a dependency.
<br>Thus, the lines 2 to 4 from our example could be simplified like this:

    auto networkRegistration = context -> registerService<QNetworkAccessManager>(); // 2
    context -> registerService(service<RestPropFetcher>(QString{"https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"), networkRegistration}, "hamburgWeather"); // 3


## Parent-Child relation

In the preceding example, the class `RestPropFetcher` was introduced which had the following constructor:

    RestPropFetcher(const QString& url, QNetworkAccessManager* networkManager, QObject* parent = nullptr);

This is typical for QObject-based services: the first arguments (aka the dependencies) are mandatory, while the last argument is an optional `parent`.
<br>In the preceding example, the service was registered by supplying the first two arguments explicitly. Thus, no `parent` was supplied.
<br>QApplicationContext will check for every service after creation whether the service already has a QObject::parent().
If not, **it will set itself as the service's parent** using QObject::setParent(QObject*).
<br>However, what would you do if you had a constructor with a *mandatory parent*?
<br>In that case, mcnepp::qtdi::injectParent() comes to the rescue:

    context->registerService(service<RestPropFetcher>(QString{"https://whatever"}, inject<QNetworkAccessManager>(), injectParent()));

This will cause the ApplicationContext to inject itself into the constructor as the parent.


## Externalized Configuration {#externalized-configuration}

In the above example, we were configuring the Url with a String-literal in the code. This is less than optimal, as we usually want to be able
to change such configuration-values without re-compiling the program.  
This is made possible with so-called *placeholders* in the configured values:  
When passed to the function mcnepp::qtdi::resolve(), a placeholder embedded in `${   }` will be resolved by the QApplicationContext using Qt's `QSettings` class.  
You simply register one or more instances of `QSettings` with the context, using mcnepp::qtdi::QApplicationContext::registerObject().

### Using Constructor-arguments {#constructor-arguments}

This is what it looks like if you out-source the "url" configuration-value into an external configuration-file:

    context -> registerObject(new QSettings{"application.ini", QSettings::IniFormat, context});
    
    context -> registerService(service<RestPropFetcher>(resolve("${hamburgWeatherUrl}"), inject<QNetworkAccessManager>()), "hamburgWeather"); 
    context -> registerService(service<RestPropFetcher>(resolve("${berlinWeatherUrl}"), inject<QNetworkAccessManager>()), "berlinWeather"); 


You could even improve on this by re-factoring the common part of the Url into its own configuration-value:

    context -> registerService(service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${hamburgStationId}"), inject<QNetworkAccessManager>()), "hamburgWeather"); 
    context -> registerService(service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${berlinStationId}"), inject<QNetworkAccessManager>()), "berlinWeather"); 

### Order of lookup

Whenever a *placeholder* shall be looked up, the ApplicationContext will search the following sources, until it can resolve the *placeholder*:

-# The environment, for a variable corresponding to the *placeholder*.
-# The instances of `QSettings` that have been registered in the ApplicationContext.

### Configuring values of non-String types

With mcnepp::qtdi::resolve(), you can also process arguments of types other than `QString`.
You just need to specify the template-argument explicitly, or pass a default-value to be used when the expression cannot be resolved.

Let's say there was an argument of type `int` that specified the connection-timeout in milliseconds. Then, the service-declaration would be:

    auto decl = service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${hamburgStationId}"), resolve<int>("${connectionTimeout}"), inject<QNetworkAccessManager>());

You will notice the explicit type-argument used on mcnepp::qtdi::resolve(). Needless to say, the configured value for the key "connectionTimeout" must resolve to a valid integer-literal!


### Specifying default values

Sometimes, you may want to provide a constructor-argument that can be externally configured, but you are unsure whether the configuration will always be present at runtime.

There are two ways of doing this:

1. You can supply a default-value to the function-template mcnepp::qtdi::resolve() as its second argument: `resolve("${connectionTimeout}", 5000)`. This works only for constructor-arguments (see [Using Constructor-arguments](#constructor-arguments)).
2. You can put a default-value into the placeholder-expression, separated from the placeholder by a colon: `"${connectionTimeout:5000}"`. Such an embedded default-value
takes precedence over one supplied to mqnepp::qtdi::resolve(). This works for both constructor-arguments and Q_PROPERTYs (see [Configuring services with Q_PROPERTY](#configuring-services)).

### Specifying an explicit Group

If you structure your configuration in a hierarchical manner, you may find it useful to put your configuration-values into a `QSettings::group()`.
Such a group can be specified for your resolvable values by means of the mcnepp::qtdi::withGroup(const QString&) function. In the following example, the configuration-keys "baseUrl", "hamburgStationId" and "connectionTimeout"
are assumed to reside in the group named "mcnepp":

    auto decl = ;
    appContext -> registerService(service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${hamburgStationId}"), resolve<int>("${connectionTimeout}"), inject<QNetworkAccessManager>())
    << withGroup("mcnepp"), 
    "hamburgWeather");

### Lookup in sub-sections

Every key will be looked up in the section that has been provided via as an argument to mcnepp::qtdi::withGroup(const QString&), argument, unless the key itself starts with a forward slash,
which denotes the root-section.

A special syntax is available for forcing a key to be looked up in parent-sections if it cannot be resolved in the provided section:
Insert `*/` right after the opening sequence of the placeholder.

    context -> registerService(service<QIODevice,QFile>(resolve("${*/filename}")) << withGroup("files"), "file");

The key "filename" will first be searched in the section "files". If it cannot be found, it will be searched in the root-section.



## Configuring properties of services {#configuring-services}

We have seen how we can inject configuration-values into Service-constructors.
<br>However, a service may have additional dependencies or configuration-values that need to be supplied after construction.
<br>For this purpose, there are various overloads of mcnepp::qtdi::Service::operator<<().

### Using Q_PROPERTY

One way of configuring services is to use its `Q_PROPERTY` declarations.
Suppose we modify the declaration of `RestPropFetcher` like this:

    class RestPropFetcher : public QObject {
      Q_OBJECT

      Q_PROPERTY(QString value READ value NOTIFY valueChanged)
      
      Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
      
      Q_PROPERTY(int connectionTimeout READ connectionTimeout WRITE setConnectionTimeout NOTIFY connectionTimeoutChanged)

      
      public:
      
      explicit RestPropFetcher(QNetworkAccessManager* networkManager, QObject* parent = nullptr);

      QString value() const;

      QString url() const;
      
      void setUrl(const QString&);
      
      void setConnectionTimeout(int);
      
      int connectionTimeout() const;

      signals:
      
      void valueChanged();
      void urlChanged();
      void connectionTimeoutChanged();
    };


Now, the "url" cannot be injected into the constructor. Rather, it must be set explicitly via the corresponding Q_PROPERTY.
In order to achieve this, we must add configuration-entries to the service. 
We use the left-shift operator `<<` for this, as shown below:

    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "${baseUrl}?stationIds=${hamburgStationId}") << propValue("connectionTimeout", "${connectionTimeout:5000}"), "hamburgWeather");
    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "${baseUrl}?stationIds=${berlinStationId}") << propValue("connectionTimeout", "${connectionTimeout:5000}"), "berlinWeather"); 

As you can see, the code has changed quite significantly: instead of supplying the Url as a constructor-argument, you pass in the key/value-pairs for configuring
the service's url and connectionTimeouts as Q_PROPERTYs.

**Note:** Every mcnepp::qtdi::propValue() supplied to mcnepp::qtdi::QApplicationContext::registerService() will be considered a potential `Q_PROPERTY` of the target-service. mcnepp::qtdi::QApplicationContext::publish() will fail if no such property can be
found.  
However, if you use mcnepp::qtdi::placeholderValue() instead, it will still be resolved via QSettings, but no attempt will be made to access a matching Q_PROPERTY.
Such *placeholder-value* may be passed to a mcnepp::qtdi::QApplicationContextPostProcessor (see section [Tweaking services](#tweaking-services) below).

Also, *placeholder-value* can be very useful in conjunction with [Service-templates](#service-templates).

### Configuring services with type-safe 'setters'

In the previous paragraph, you could see how Q_PROPERTYs of the services were initialized using the property-names.
<br>Now, we will show arbitrary service-properties can be configured, even if no Q_PROPERTY has been declared.
<br>Instead of using the property-name, we'll reference the member-function that sets the value.
<br>We use one of the various overloads of mcnepp::qtdi::propValue() in order to create a type-safe configuration-entry.
<br>Here is an example setting the `transferTimeout` of a QNetworkAccessManager. This property is not declared with the Q_PROEPRTY macro,
thus, it cannot be set using the QMetaType-system:

    context -> registerService(service<QNetworkAccessManager>() << propValue(&QNetworkAccessManager::setTransferTimeout, 5000), "networkManager"); 

<br>Using *setters* can, of course, be combined with resolving configuration-values:

    context -> registerService(service<QNetworkAccessManager>() << propValue(&QNetworkAccessManager::setTransferTimeout, "${transferTimeout}"), "networkManager"); 


### Auto-refreshable configuration-values

If you configure a Q_PROPERTY using mcnepp::qtdi::propValue(), the property of the Service-Object will be set 
exactly once, immediately after creation, right before any [service-initializers](#service-initializers) may be invoked. After that, the service will be published.
<br>There may be times, however, when you want a property to be refreshed automatically every time the value in the corresponding QSettings-object changeds.
<br>This can be achieved by using mcnepp::qtdi::autoRefresh(const QString&,const QString&).
<br>The following line will configure a QTimer's `interval` using an auto-refreshable configuration-value:

    context->registerService(service<QTimer>() << autoRefresh("interval", "${timerInterval}"), "timer");

In case the configured QSettings-object uses a file as its persistent storage, any change to that file will be immediately detected. It will lead to a re-evaluation
of the property. In case the configured QSettings-object uses a different persistent storage (such as the Windows Registry), the changes will be polled periodically.
<br>Auto-refresh will also work with more complex expressions for the property. In the following example, the property `objectName` will be automatically refreshed when either one of the configuration-values
`prefix` or `suffix` is modified:

    context->registerService(service<QTimer>() << autoRefresh("objectName", "timer-${prefix}${suffix}"), "timer");

In case all properties for one service shall be auto-refreshed, there is a more concise way of specifying it:

     context->registerService(service<QTimer>() << withAutoRefresh << propValue("objectName", "theTimer") << propValue("interval", "${timerInterval}"), "timer");


**Note:** Auto-refresh will be disabled by default in mcnepp::qtdi::StandardApplicationContext.
<br>It must be explicitly enabled by putting the following configuration-entry into one of the QSettings-objects registered with the context:

    [qtdi]
    enableAutoRefresh=true
    ; Optionally, specify the refresh-period:
    autoRefreshMillis=2000

Auto-refreshable properties can also be specified using *setters*:

    context->registerService(service<QTimer>() << autoRefresh(&QTimer::setInterval, "${timerInterval}"), "timer");
    

## Service-prototypes

As shown above, a service that was registered using mcnepp::qtdi::QApplicationContext::registerService() will be instantiated once mcnepp::qtdi::QApplicationContext::publish(bool) is invoked.
<br>A single instance of the service will be injected into every other service that depends on it.

However, there may be some services that cannot be shared between dependent services. In this case, use mcnepp::qtdi::prototype() instead of mcnepp::qtdi::service()
as an argument to mcnepp::qtdi::QApplicationContext::registerService().
<br>Such a registration will not necessarily instantiate the service on mcnepp::qtdi::QApplicationContext::publish(bool).
Only if there are other services depending on it will a new instance be created and injected into the dependent service.

Every instance of a service-protoype that gets injected into a dependent service will be made a QObject-child of the dependent service.
In other words, the dependent service becomes the *owner* of the prototype-instance.

The same is true for *references to other members*: if a protoype is referenced via the ampersand-syntax, the instance of that prototype will be made a child of the service that references it.

## Service-templates {#service-templates}

A service-template is a recipe for configuring a service without actually registering a concrete service.
Such a template can then be re-used when further concrete services are registered.
<br>(For those familiar with Spring-DI: this would be an *"abstract"* bean-definition).
<br>A Service-template can be registered like this:

    auto restFetcherTemplateRegistration = context -> registerService(serviceTemplate<RestPropFetcher>()
        << propValue("connectionTimeout", "${connectionTimeout:5000}")
        << propValue("url", "${baseUrl}?stationIds=${stationId}"),
    "fetcherBase");

<br>The return-value has the type `ServiceRegistration<RestPropFetcher,ServiceScope::TEMPLATE>`.
It can be supplied as an additional argument to subsequent registrations:

    context -> registerService(service<RestPropFetcher>() << placeholderValue("stationId", "10147"), restFetcherTemplateRegistration, "hamburgWeather");

If a service-registration utilizes a service-template, the type of the registered service must be implicitly convertible to the service-template's type.
In particular, it can be the same type (as can be seen in the example above).



Service-templates have the following capabilities:

-# Uniform configuration. You may configure Q_PROPERTYs in a uniform way for all services that use this template. See the property `connectionTimeout` in the above example,
which will be set to the same value for every service derived from this template. Even more interesting is the use of the placeholder `${stationId}` in the template's configuration.
It will be resolved by use of a *placeholder-value at the registration of the concrete service.
-# Init-Methods. You may specify an *init-method* via the `mcnepp::qtdi::service_traits` of the service-template.
-# Uniform advertising of service-interfaces. You may once specify the set of interfaces that a service-template advertises. That way, you don't
need to repeat this for every service that uses the template. (See section [Service-interfaces](#service-interfaces) below).

### Generic (un-validated) properties of Service-templates

Registration of a service-template differs from the registration of a "normal" service in one important aspect:

The properties that you provide via mcnepp::qtdi::service() will not be validated against the Q_PROPERTYs of the service's implementation-type!

The rationale is that the service-template may be used by services of yet unknown type. The validation of a Q_PROPERTY will therefore be postponed until registration of
the concrete service that derives from this service-template.

This makes it possible to register configured service-templates without assuming any particular service-type at all!
<br>In order to facilitate this, the type-argument of the function mcnepp::qtdi::serviceTemplate() has a default-type of `QObject`.

Using this knowledge, let's register a service-template for arbitrary services that support a Q_PROPERTY `url`:


    auto urlAware = context->registerService(serviceTemplate() << propValue("url", "http://github.com"), "urlAware");


## Managed Services vs. Un-managed Objects

The function mcnepp::qtdi::QApplicationContext::registerService() that was shown in the preceeding example results in the creation of a `managed service`.  
The entire lifecylce of the registered Service will be managed by the QApplicationContext.  
Sometimes, however, it will be necessary to register an existing QObject with the ApplicationContext and make it available to other components as a dependency.  
A reason for this may be that the constructor of the class does not merely accept other `QObject`-pointers (as "dependencies"), but also `QString`s or other non-QObject-typed value.  
A good example would be registering objects of type `QSettings`, which play an important role in [Externalized Configuration](#externalized-configuration).  


There are some crucial differences between mcnepp::qtdi::QApplicationContext::registerService(), when invoked with either mcnepp::qtdi::service() or mcnepp::qtdi::prototype().
Also, there are differences to mcnepp::qtdi::QApplicationContext::registerObject(), as the following table shows:

| |registerService(service())|registerService(prototype())|registerObject|
|---|---|---|---|
|Instantiation of the object|Upon mcnepp::qtdi::QApplicationContext::publish()|In mcnepp::qtdi::QApplicationContext::publish(),<br>but only if another service requests it|Prior to the registration with the QApplicationContext|
|When is the signal `objectPublished(QObject*)` emitted?|Upon mcnepp::qtdi::QApplicationContext::publish()|In mcnepp::qtdi::QApplicationContext::publish(),<br>but only if another service requests it|Immediately after the registration|
|Naming of the QObject|`QObject::objectName` is set to the name of the registration||`QObject::objectName` is not touched by QApplicationContext|
|Handling of Properties|The key/value-pairs supplied at registration will be set as Q_PROPERTYs by QApplicationContext||All properties must be set before registration|
|Processing by mcnepp::qtdi::QApplicationContextPostProcessor|Every service will be processed by the registered QApplicationContextPostProcessors||Object is not processed|
|Invocation of *init-method*|If present, will be invoked by QApplicationContext||If present, must have been invoked prior to registration|
|Parent/Child-relation|If a Service has no parent after its *init-method* has run, the ApplicationContext will become the service's parent.|The instance of the prototype will be made a child of the service that required it.|The parent of the Object will not be touched.|
|Destruction of the object|Upon destruction of the QApplicationContext|Upon destruction of the Service that owns the prototype-instance.|At the discrection of the code that created it|




## Types of dependency-relations

In our previous example, we have seen the dependency of our `RestPropFetcher` to a `QNetworkAccessManager`.  
This constitutes a *mandatory dependency*: instantion of the `RestPropFetcher`will fail if no `QNetworkAccessManager`can be found.
However, there are more ways to express a dependency-relation.  
This is reflected by the enum-type `mcnepp::qtdi::DependencyKind` and its enum-constants as listed below:


### MANDATORY

As stated before, mandatory dependencies enforce that there is exactly one service of the dependency-type present in the ApplicationContext.
Otherwise, publication will fail.
Mandatory dependencies can be specified by using inject():

    context->registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()));

In case you have the ServiceRegistration for the dependency at hand, you may skip the invocation of mcnepp::qtdi::inject() and use the ServiceRegistration directly:


....auto networkRegistration = context->registerService<QNetworkAccessManager>();
    context->registerService(service<RestPropFetcher>(networkRegistration));

### OPTIONAL
A service that has an *optional dependency* to another service may be instantiated even when no matching other service can be found in the ApplicationContext.
In that case, `nullptr` will be passed to the service's constructor.  
Optional dependencies are specified using the function mcnepp::qtdi::injectIfPresent(). Suppose it were possible to create our `RestPropFetcher` without a `QNetworkAccessManage`:

    context->registerService(service<RestPropFetcher>(injectIfPresent<QNetworkAccessManager>()));

When the Service is later created, the ApplicationContext will look for another Service of type `QNetworkAccessManager`. 
If exactly one matching Service is found, it will be injected into the `RestPropFetcher`. Otherwise, `nullptr` will be injected.


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

    context -> registerService(service<PropFetcherAggregator>(injectAll<RestPropFetcher>()), "propFetcherAggregation");

**Note:**, while constructing the `QList` with dependencies, the ordering of registrations of **non-interdependent services** will be honoured as much as possible.
In the above example, the services "hamburgWeather" and "berlinWeather" will appear in that order in the `QList` that is passed to the `PropFetcherAggreator`.

In case you have the ProxyRegistration for the dependency at hand, you may skip the invocation of mcnepp::qtdi::inject() and use the ProxyRegistration directly:

....auto networkRegistration = context->getRegistration<QNetworkAccessManager>();
    context->registerService(service<PropFetcherAggregator>(networkRegistration));


    
The following table sums up the characteristics of the different types of dependencies:

<table><tr><th>&nbsp;</th><th>Normal behaviour</th><th>What if no dependency can be found?</th><th>What if more than one dependency can be found?</th></tr>
<tr><td>MANDATORY</td><td>Injects one dependency into the dependent service.</td><td>Publication of the ApplicationContext will fail.</td>
<td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
<tr><td>OPTIONAL</td><td>Injects one dependency into the dependent service</td><td>Injects `nullptr` into the dependent service.</td>
<td>Publication will fail with a diagnostic, unless a `requiredName` has been specified for that dependency.</td></tr>
<tr><td>N</td><td>Injects all dependencies of the dependency-type that have been registered into the dependent service, using a `QList`</td>
<td>Injects an empty `QList` into the dependent service.</td>
<td>See 'Normal behaviour'</td></tr>
</table>


## Service-interfaces {#service-interfaces}

In the preceeding example, we have registered our QObject-derived class `RestPropFetcher` directly, using the function template mcnepp::qtdi::service()
with one type-argument (aka `"service<RestPropFetcher>()"`).<br>
Such a registration will register the implementation-type `RestPropFetcher` with the same service-interface.
<br>We have also specified 
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

Having done this, we will now register the `RestPropFetcher` with the *service-interface* `PropFetcher`:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()));

As you can see, there are now two type-arguments: the first one being the service-interface, the second being the implementation-type.

What would you do if you wanted to advertise the service with more than one service-interface?
Well, you use the form with one type-argument again, but then have it followed by a call to `advertiseAs()`, which accepts an arbitrary number of interface-types!

Here's what that would look like:

    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher>());

In order to make use of our interface, we modify our class `PropFetcherAggregator` so that it accepts dependencies of the interface-type:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      explicit PropFetcherAggregator(const QList<PropFetcher*>& fetchers, QObject* parent = nullptr);
    };

Putting it all together, we use the helper-template `Service` for specifying both an interface-type and an implementation-type:


    using namespace mcnepp::qtdi;
    
    QApplicationContext* context = new StandardQApplicationContext; 
    
    context -> registerService<QNetworkAccessManager>();
    
    context -> registerService(service<PropFetcherAggregator>(injectAllOf<PropFetcher>()), "propFetcherAggration");
    
    /*2*/ context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher>() << propValue("url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"), "hamburgWeather"); 
    
    context -> publish(); 

**Note:** The order of registrations has been switched: now, the dependent service `PropFetcherAggregator` is registered before the services it depends on.
This was done to demonstrate that, **regardless of the ordering of registrations**, the dependencies will be resolved correctly! 
Also, there are two type-arguments for the Service now: the first specifies the type of service-interface, the second the implementation-type.

### Multiple service-interfaces

A service may be advertised under more than one service-interface. This makes sense if the service-type implements several non-QObject interfaces that 
other services depend on.
Suppose the class `RestPropFetcher` implements an additional interface `QNetworkManagerAware`:

    class QNetworkManagerAware {
       virtual ~QNetworkManagerAware() = default;
       
       virtual setNetworkManager(QNetworkAccessManager*) = 0;
    };
    
    class PropFetcher : public QObject, public QNetworkManagerAware {
    
    explicit PropFetcher(const QString& url, QObject* parent = nulptr);
    
    virtual setNetworkManager(QNetworkAccessManager*) override;
    ...//same as before
    };
    
    class RestPropFetcher : public PropFetcher, public QNetworkManagerAware {
    ...
    };

In order to advertise a `RestPropFetcher` as both a `PropFetcher` and a `QNetworkManagerAware`, we use:

    auto reg = context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher,QNetworkManagerAware>() << propValue("url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"), "hamburgWeather"); 

**Note:** The return-value `reg` will be of type `ServiceRegistration<RestPropFetcher,ServiceScope::SINGLETON>`.

You may convert this value to `ServiceRegistraton<PropFetcher,ServiceScope::SINGLETON>` as well as `ServiceRegistration<QNetworkManagerAware,ServiceScope::SINGLETON>`,
using the member-function ServiceRegistration::as(). Conversions to other types will not succeed.


## Profiles

Every ApplicationContext has one or more mcnepp::qtdi::QApplicationContext::activeProfiles().

If not otherwise specified, there is one active profile name `"default"`.
When using a mcnepp::qtdi::StandardApplicationContext, the *active profiles* can be set by putting an entry with the key `"qtdi/activeProfiles"` into the ApplicationContext's configuration.

When registering a %Service, you may specify one or more profiles for which the %Service shall be active. If you supply an empty set (which is the default),
the %Service will be active always.

### Registering optional services

With the help of Profiles, it becomes possible to register some some services that will be instantiated only if a certain profile is active.
<br>For example, in addition to the existing service "hamburgWeather", you may register an additional `RestPropFetcher` named "munichWeather" that will be instantiated only if the profile `"bavaria"` is activated:

    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher,QNetworkManagerAware>() << propValue("url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"), "hamburgWeather");
    
    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher,QNetworkManagerAware>() << propValue("url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10870"), 
    "munichWeather",
    {"bavaria"}); 

### Registering alternative services

With the help of profiles, you may circumvent the usual restriction regarding the uniqueness of service-names:
<br>You may indeed register two different Services under the same name - provided their list of profiles is disjunct.

One handy application is the registration of *Mock-Services*. Given the above interface `PropFetcher`, you may want to use a Mock-implementation if your REST-Endpoint is not available.

Let's recap the "normal" registration of a `RestPropFetcher`, advertised under the interface `PropFetcher`:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"), 
    "hamburgWeather"); 

If you want to register an alternative *Mock-Service* , this will be the necessary code:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"),
    "hamburgWeather",
    {"default"}); // 1
    
    context -> registerService(service<PropFetcher,MockPropFetcher>(), "hamburgWeather", {"mock"}); // 2

1. Added an explicit profile as the last argument to mcnepp::qtdi::QApplicationContext::registerService(). In this case, we use the default-profile.
2. Registered a second service under the same name and advertised using the same interface. This is only possible because we are using a different profile `"mock"`.

With everything else untouched, the application will behave exactly as before. The profile `"default"`will be active, and a %Service with implementation-type `RestPropFetcher` will be created.
<br>However, if we change the *active profile* to "mock", an instance of implementation-type `MockPropFetcher` will be instantiated instead!

### Configuring the active profiles

If the environment variable `QTDI_ACTIVE_PROFILES` is defined, its value will determine the *active profiles* of each newly constructed ApplicationContext.
<br>**Note:** Changing the value of the environment variable will have no impact on already constructed ApplicationContexts.
<br>The *active profiles* may be overwritten through configuration-entries: From every QSettings registered with an ApplicationContext, the following entry will be read: 

    [qtdi]
    activeProfiles=mock

The value for the entry will be read, converted to a QStringList and appended to the ApplicationContext's *active profiles*.
<br>If neither the environment variable `QTDI_ACTIVE_PROFILES` is defined nor an entry `"qtdi/activeProfiles"` found, the *active profile* will be "default".
<br>You may also set the active profiles programmatically using mcnepp::qtdi::StandardApplicationContext::setActiveProfiles().
<br>**Note:** The active profiles can be changed only as long as no profile-dependent services have been published!
Any attempt at doing otherwise will fail and result in an error logged.


### Detection of ambiguous registrations

As stated before, profiles offer a way to register more than one service with the same name - something that is strictly forbidden otherwise.

However, the set of profiles for which those services are registered must be *disjunct*. 
Some examples of that will fail at registration-time:

    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"default", "test"});
    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"test"});

The second registration will fail because if "test" were set as the *active profile* in the ApplicationContext, both registration would be active, which is forbidden.
<br>Note that the registration will fail even though for some other profile ("default", for example) there would be no amgiguity!

    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"default", "test"});
    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"test", "default"});

The second registration will fail because the set of profiles is the same as for the first registration. The order of the profiles is irrelevant.


    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"default", "test"});
    context -> registerService(service<QNetworkAccessManager>(), "networkManager");

The second registration will fail because it would be active for *any profile*. Thus, if "test" or "default" were set as the *active profile* in the ApplicationContext, 
both registrations would be active, which is forbidden.
<br>Note that the registration will fail even though for any profile other than "test" or "default" there would be no ambiguity.

### Detection of ambiguity at publication

Some cases of ambiguity cannot be detected at registration-time. However, they will be caught when mcnepp::qtdi::QApplicationContext::publish() is invoked:

    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"default"});
    context -> registerService(service<QNetworkAccessManager>(), "networkManager", {"test"});
    context -> publish();



The above registrations will succeed. However, if you set the *active profiles* of the ApplicationContext to `{"default", "test"}`, 
the invocation of mcnepp::qtdi::QApplicationContext::publish() will fail.




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

And here's how this property will be automatically set to the ApplicationContext's `PropFetcherAggregator`. Note the ampersand as the first character of the property-value,
which makes this a *named reference to another member*:


    context -> registerService(service<PropFetcherAggregator>(injectAll<PropFetcher>()), "propFetcherAggregator");
    
    context -> registerService(service<PropFetcher,RestPropFetcher(inject<QNetworkAccessManager>())
      << propValue("url", "${weatherUrl}${hamburgStationId}")
      << propValue("summary", "&propFetcherAggregator"), 
    "hamburgWeather");

By the way, if you prefer a more type-safe way of expressing a relation with another service, here is a slightly different form of configuration,
using member-function-pointers and service-registrations. The effect will be exactly the same, though:


    auto propFetcherReg = context -> registerService(service<PropFetcherAggregator>(injectAll<PropFetcher>()), "propFetcherAggregator");
    
    context -> registerService(service<PropFetcher,RestPropFetcher(inject<QNetworkAccessManager>())
      << propValue("url", "${weatherUrl}${hamburgStationId}")
      << propValue(&PropFetcher::setSummary, propFetcherReg),
      "hamburgWeather");


## Connecting Signals of Services to Slots

Qt's core concept for creating workflow from one QObject to another is via *signals and slots*.
<br>For example, QTimer declares the *signal* QTimer::timeout().
<br>Suppose we have written a class like this:

    class Cleaner : public QObject {
        public void clean();
    };

Given pointer `QTimer* timer` and a pointer `Cleaner* cleaner`, we would connect them like this:

    QTimer* timer = new QTimer;
    QCleaner* cleaner = new QCleaner;
    
    QObject::connect(timer, &QTimer::timeout, cleaner, &Cleaner::clean);

Suppose we'd like to do the same thing with services that have been registered in an ApplicationContext: We want to connect a registered `Cleaner` with a registered `timer`.
<br>However, since the ApplicationContext manages instantiation for us, we do not have immediate access to services. 
<br>But we do have access to the ServiceRegistrations!
<br>So, given two `ServiceRegistrations`, the equivalent code would look like this:

    ServiceRegistration<QTimer> timerReg = context->registerService<QTimer>();
    ServiceRegistration<QCleaner> cleanerReg = context->registerService<Cleaner>();
    
    timerReg.subscribe(cleaner, [cleaner](QTimer* timer) {
      cleaner.subscribe(cleaner, [timer](QCleaner* cleaner) {
        QObject::connect(timer, &QTimer::timeout, cleaner, &Cleaner::clean);
      };
    });

That looks pretty ugly, right?
<br>Luckily, the function mcnepp::qtdi::connectServices() hides all that ugly boilerplate code from us:

    connectServices(timerReg, &QTimer::timeout, cleanerReg, &Cleaner::clean);

As you will have notices, this looks almost exactly like the original example with the pointers-to-QObjects.

## Binding source-properties to target-properties of other members of the ApplicationContext

In the preceeding example, we used a reference to another member for the purpose of **initializing** a Q_PROPERTY.

However, we might also want to **bind** a target-service's property to the corresponding source-property of another service.

This can be achieved using the function mcnepp::qtdi::bind() like this:

    QTimer timer1;
    
    auto reg1 = context -> registerObject(&timer1, "timer1"); // 1
    
    auto reg2 = context -> registerService<QTimer>("timer2"); // 2
    
    bind(reg1, "interval", reg2, "interval"); // 3
    
    context -> publish(); 
    
    timer1.setInterval(4711); // 4

1. We register a `QTimer` as "timer1".
2. We register a second `QTimer` as "timer2". 
3. We bind the property `interval` of the second timer to the first timer's propery.
4. We change the first timer's interval. This will also change the second timer's interval!

This way of binding properties has the advantage that it can also be applied to ServiceRegistrations obtained via QApplicationContext::getRegistration(), 
aka those that represent more than one service. The source-property will be bound to every target-service automatically!



### Type-safe bindings

The above binding used property-names to denote the source- and target-properties.
<br>In case the source-service offers a signal that corresponds with the property, you can use pointers to member-functions instead, which is more type-safe.
<br>Here is an example:

    QTimer timer1;
    
    auto reg1 = context -> registerObject(&timer1, "timer1"); // 1
    
    auto reg2 = context -> registerService<QTimer>("timer2"); // 2
    
    bind(reg1, &QTimer::objectNameChanged, reg2, &QTimer::setObjectName); // 3
    
    context -> publish(); 
    
    timer1.setObjectName("new Name"); // 4

1. We register a `QTimer` as "timer1".
2. We register a second `QTimer` as "timer2". 
3. We bind the property `objectName` of the second timer to the first timer's propery.
4. We change the first timer's objectName. This will also change the second timer's objectName!


## Subscribing to a service after registration

So far, we have published the ApplicationContext and let it take care of wiring all the components together.  
In some cases, you need to obtain a reference to a member of the Context after it has been published.  
This is where the return-value of mcnepp::qtdi::QApplicationContext::registerService() comes into play: mcnepp::qtdi::ServiceRegistration.

It offers the method mcnepp::qtdi::ServiceRegistration::subscribe(), which is a type-safe version of a Qt-Signal.
(It is actually implemented in terms of the Signal mcnepp::qtdi::Registration::objectPublished(QObject*)).

In addition to being type-safe, the method mcnepp::qtdi::ServiceRegistration::subscribe() has the advantage that it will automatically inject the service if you subscribe after the service has already been published.  
This code shows how to utilize it:

    auto registration = context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "${weatherUrl}${hamburgStationId}"), "hamburgWeather"); 
    
    registration.subscribe(this, [](PropFetcher* fetcher) { qInfo() << "I got the PropFetcher!"; });
    
The function mcnepp::qtdi::ServiceRegistration::subscribe() does return a value of type mcnepp::qtdi::Subscription.
Usually, you may ignore this return-value. However, it can be used for error-checking and for cancelling a subscription, should that be necessary.


### Subscribing to multiple Services

As shown above, the method Registration::subscribe() is the way to go if you want to get a hold on a published service.

But what if you would like to do something with more than one Service?

Use mcnepp::qtdi::combine(), followed by mcnepp::qtdi::ServiceCombination::subscribe().

The following example combines one service of type `PropFetcher` and one `QTimer` and invokes a member-function `fetch`
with both arguments:

    auto propFetcherRegistration = context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "${weatherUrl}${hamburgStationId}"), "hamburgWeather"); 
    auto timerRegistration = context->getRegistration<QTime>();
    combine(propFetcherRegistration, timerRegistration).subscribe(this, [this](PropFetcher* fetcher, QTimer* timer) 
      { 
         this->fetch(fetcher, timer); 
      });



## Accessing published services of the ApplicationContext

In the previous paragraph, we used the mcnepp::qtdi::ServiceRegistration obtained by mcnepp::qtdi::QApplicationContext::registerService(), which refers to a single member of the ApplicationContext.
However, we might be interested in all services of a certain service-type.  
This can be achieved using mcnepp::qtdi::QApplicationContext::getRegistration(), which yields a mcnepp::qtdi::ProxyRegistration that represents *all services of the requested service-type*.


    auto registration = context -> getRegistration<PropFetcher>();
    qInfo() << "There have been" << registration.registeredServices().size() << "RestPropFetchers so far!";    

You may also access a specific service by name:

    auto registration = context -> getRegistration("hamburgWeather");
    if(!registration) {
      qWarning() << "Could not obtain service 'hamburgWeather'";
    } else {
      registration.as<PropFetcher>().subscribe(this, [](PropFetcher* fetcher) { qInfo() << "Got a PropFetcher!"; });
    }
    

## Tweaking services (QApplicationContextPostProcessor) {#tweaking-services}

Whenever a service has been instantiated and all properties have been set, QApplicationContext will apply all registered mcnepp::qtdi::QApplicationContextPostProcessor`s 
to it. These are user-supplied QObjects that implement the aforementioned interface which comprises a single method:

    QApplicationContextPostProcessor::process(service_registration_handle_t, QObject*,const QVariantMap&)

In this method, you might apply further configuration to your service there, or perform logging or monitoring tasks.<br>
Any information that you might want to pass to a QApplicationContextPostProcessor can be supplied as
a so-called *placeholder-value* via mcnepp::qtdi::placeholderValue(const QString&,const QVariant&).


## Service-Initializers {#service-initializers}

The last step done in mcnepp::qtdi::QApplicationContext::publish() for each service is the invocation of an *init-method*, should one have been 
specified.

The same *init-method* should be used for every service of a certain type. In order to achieve this, you need to specialize
mcnepp::qtdi::service_traits for your service-type and declare a type-alias named `initializer_type`.
<br>It is recommended to make use of the helper-template mcnepp::qtdi::default_service_traits for this purpose, which will declare the necessary
type-aliases for you:

A suitable type would be a callable `struct` with either one argument of the service-type, or with two arguments, the second being of type `QApplicationContext*`.

    struct RestPropFetcher_initializer{
      void operator()(PropFetcherAggregator* service) const {
         service -> init();
      }
    };
    
    namespace mcnepp::qtdi {
      template<> struct service_traits<RestPropFetcher> : default_service_traits<RestPropFetcher,RestPropFetcher_initializer> {
      };
    }



### Initialization by function-reference

In the above example, the callable `struct RestPropFetcher_initializer` simply invokes the method `PropFetcherAggregator::init()`.
Shouldn't we be able to get rid of the `struct RestPropFetcher_initializer` somehow?
<br>This is indeed possible. The helper-type mcnepp::qtdi::service_initializer takes a pointer to a member-function and converts it into a type.
That way, we can reference the member-function (almost) directly in our service_traits:

    namespace mcnepp::qtdi {
      template<> struct service_traits<RestPropFetcher> : default_service_traits<RestPropFetcher,service_initializer<&RestPropFetcher::init>> {
      };
    }


### Specifying initializers via interfaces

Suppose that the init-method was part of the service-interface `PropFetcher` that was introduced above.

    class PropFetcher  {

      public:
      
      virtual QString value() const = 0;
      
      virtual void init() = 0;
      
      virtual ~PropFetcher() = default;
    };

We would like to specify the use of the member-function `PropFetcher::init()` for all services that implement this interface.
<br>Well, this is how it's done:

    namespace mcnepp::qtdi {
      template<> struct service_traits<PropFetcher> : default_service_traits<PropFetcher,service_initializer<&PropFetcher::init>> {
      };
    }

Of course, in order to take advantage of this, we must advertise our `RestPropFetcher` under the interface `PropFetcher`.
<br>Now, if you advertise a service under more than one interface, an ambiguity could arise, in case more than one interface declares its own service_initializer.
In that case, compilation will fail with a corresponding diganostic.
<br>In order to fix this error, you should specify an initializer_type in the service_traits of the service's *implementation-type*. 


### Specifying initializers per Registration

There is also method that lets you specify an *init-method* without the need for a specialization of mcnepp::qtdi::service_traits.
<br>This will register only one specific service with the supplied *init-method*:

    context -> registerService(service<PropFetcherAggregator>(injectAll<RestPropFetcher>()).withInit(&PropFetcherAggregator::init));

See mcnepp::qtdi::Service::withInit()


### Changing the order of initialization

Per default, publication of a Service is announced **after** the *init-method* has run.
<br>However, there may be cases where you would like to subscribe to ServiceRegistrations and have the Subscription be invoked
**before** the *init-method* has run.
<br>In order to achieve this, there is the enumeration mcnepp::qtdi::ServiceInitializationPolicy.
<br>You may specify a different ServiceInitializationPolicy via the service_traits.
The following example will determine that Services of type PropFetcher will be announced **before** their method `PropFetcher::init`
has run:

    namespace mcnepp::qtdi {
      template<> struct service_traits<PropFetcher> : default_service_traits<PropFetcher,service_initializer<&PropFetcher::init>,ServiceInitializationPolicy::AFTER_PUBLICATION> {
      };
    }

Of course, a ServiceInitializationPolicy can also be supplied per registration, as shown here:

    context -> registerService(service<PropFetcherAggregator>(injectAll<RestPropFetcher>()).withInit<ServiceInitializationPolicy::AFTER_PUBLICATION>(&PropFetcherAggregator::init));



## Resolving ambiguities

Sometimes, multiple instances of a service with the same service-type have been registered.  
(In our previous examples, this was the case with two instances of the `PropFetcher` service-type.)  
If you want to inject only one of those into a dependent service, how can you do that?  
Well, using the name of the registered service seems like a good idea.
The following code will still provide a `QList<PropFetcher*>` to the `PropFetcherAggregator`. However, the List will
contain solely the service that was registered under the name "hamburgWeather":  

    context -> registerService(service<PropFetcherAggregator>(injectAll<PropFetcher>("hamburgWeather")), "propFetcherAggregator");

## Customizing service-instantiation

So far, we have seen that each call to mcnepp::qtdi::QApplicationContext::registerService() mirrors a corresponding constructor-invocation of the service-type.

In order for this to work, several pre-conditions must be true:

1. the constructor of the service must be accessible, i.e. declared `public`.
2. The number of mandatory arguments must match the arguments provided via `QApplicationContext::registerService()`.
3. For each mandatory or optional dependency, the argument-type must be `T*`.
4. For each dependency with cardinality N, the argument-type must be `QList<T*>`.

If any of these conditions fails, then the invocation of `QApplicationContext::registerService()` will fail compilation.

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

Now, service-registration will fail, as `PropFetcherAggregator`'s constructor cannot be invoked.

Luckily, there are two ways of solving this: you can either specialize the template mcnepp::qtdi::service_factory for your service-type, or you can provide your own factory.

(This twofold-approach follows precedent from the standard-library, where you can either specizalize std::hash, or provide your own 'hasher' to classes such as std::unordered_set.)

### Specializing service_factory

As with all such specializations, it must reside in the namespace of the primary template, i.e. `mcnepp::qtdi`.

The specialization must provide a call-operator that takes the arguments provided by QApplicationContext, adapts those arguments to the arguments expected by the
service-type, and returns a pointer to the newly created service.<br>
Additionally, it should provide a type-declaration `service_type`:

Here is all that we have to do:

    namespace mcnepp::qtdi {
      template<> struct service_factory<PropFetcherAggregator> {
        using service_type = PropFetcherAggregator;
      
        PropFetcherAggregator* operator()(const QList<PropFetcher*> fetchers) const {
          return PropFetcherAggregator::create(std::vector<PropFetcher*>{fetchers.begin(), fetchers.end()});
        }
      };
    }

And, voila: We can register our service exactly as we did before!

### Provide a custom factory for the service_traits

Your custom factory must provide a suitable operator().
Additionally, it should provide a type-declaration `service_type`:


      struct propfetcher_factory {
        using service_type = PropFetcherAggregator;
        
        PropFetcherAggregator* operator()(const QList<PropFetcher*> fetchers) const {
          return PropFetcherAggregator::create(std::vector<PropFetcher*>{fetchers.begin(), fetchers.end()});
        }
      };
    }

This factory should now be used with every service of type `PropFetcherAggregator`. To achieve this, you declare a type-alias named `factory_type` in the mcnepp::qtdi::service_traits:

    namespace mcnepp::qtdi {
      template<> struct service_traits<PropFetcherAggregator> : default_service_traits<PropFetcherAggregator> {
        using factory_type = propfetcher_factory;
      };
    }

### Use a custom factory selectively

In the previous paragraph, it was shown how you declare a custom factory and register it via the mcnepp::qtdi::service_traits.
However, there might be occasions where you want to rely on the default mechanism for constructing your service most of the times, but
want to supply the custom factory occasionally.
<br>There is an overload of mcnepp::qtdi::service() that does this. You simply pass an instance of the custom factory as the first argument:

    context->registerService(service(propfetcher_factory{}, injectAll<PropFetcher>()));




## Publishing an ApplicationContext more than once

Sometimes, it may be desirable to inovoke QApplicationContext::publish(bool) more than once.
Proceeding with the previous example, there may be several independent modules that each want to supply a service of type `PropFetcher`.
Each of these modules will rightly assume that the dependency of type `QNetworkAccessManager` will be automatically supplied by the QApplicationContext.

But which module shall then invoke QApplicationContext::publish(bool)? Do we need to coordinate this with additional code?
That could be a bit unwieldly. Luckily, this is not necessary.

Given that each module has access to the (global) QApplicationContext, you can simply do this in some initialization-code in module A:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "${weatherUrl}${hamburgStationId}"), "hamburgWeather"); 
    context -> publish();

...and this in module B:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()) << propValue("url", "${weatherUrl}${berlinStationId}"), "berlinWeather"); 
    context -> publish();

At the first `publish()`, an instance of `QNetworkAccessManager` will be instantiated. It will be injected into both `RestPropFetchers`.

This will work <b>regardless of the order in which the modules are initialized</b>!

Now let's get back to our class `PropFetcherAggregator` from above. We'll assume that a third module C contains this initialization-code:

    context -> registerService(service<PropFetcherAggregator>(injectAll<PropFetcher>()), "propFetcherAggregator");
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

    auto aggregatorRegistration = context -> registerService<PropFetcherAggregator>("propFetcherAggregator"); // 1
    
    aggregatorRegistration->autowire(&PropFetcherAggregator::addPropFetcher); // 2
    
    context -> publish();
    

1. No need to specify the dependency anymore. Therefore, we can use the simplified overload of QApplicationContext::registerService().
2. Will cause all PropFetchers to be injected into PropFetcherAggregator.

And that's all that is needed to get rid of any mandatory order of initialization of the modules A, B and C.

## Publish-mode ('allowPartial')

The function QApplicationContext::publish(bool) has a boolean argument `allowPartial` with a default-value of `false`.
The following table shows how this argument affects the outcome of the function:

### allowPartial = false:
- Transactional behaviour: Only if it can be validated that all service-dependencies can be resolved will publication begin.
- Will either publish all services or no service at all.
- All errors that occur while publishing a service will be logged with the level QtMsgType::QtCriticalMessage.

### allowPartial = true:
- Iterative behaviour: Will publish those services whose dependencies can be resolved.
- If publication of one service is not possible reasons that may be fixed, the function will continue to publish other services. 
  Such reasons include:
  - unresolved dependencies (as those may be registered later).
  - unresolved config-values (as those may be configured later).
- Such "fixable errors" that occur while publishing a service will be logged with the level QtMsgType::QtWarningMessage.
- If publication of one service fails for a reason that is not fixable, the function will will immediately return without attempts to publish other services.
  Such reasons include:
  - ambiguous dependencies (as those cannot not be removed from the ApplicationContext).
  - non-existing names of Q_PROPERTYs.
  - syntactically erronous config-keys (such as `"$interval}"`).
- Such "fatal errors" that occur while publishing a service will be logged with the level QtMsgType::QtCriticalMessage.

## The global ApplicationContext

In many applications, you will instantiate exactly one QApplicationContext.
In that case, the static function mcnepp::qtdi::QApplicationContext::instance() is a convenient means of providing access to that single instance.
<br>When you create a QApplicationContext, the constructor will set a global variable to the new QApplicationContext, unless
it has already been set. Consequently, the first QApplicationContext that you create in your application will become the global instance.
<br>(Please not that QCoreApplication::instance() exhibits the same behaviour.)

## The implicitly registered Services

There are two Services that will be implicitly available in all instances of mcnepp::qtdi::StandardApplicationContext:

- The ApplicationContext itself, registered under the name "context".
- The QCoreApplication::instance(), registered under the name "application".

Both Services will have `ServiceScope::EXTERNAL`.

Of course, the QCoreApplication::instance() can only be registered if it has been created before the StandardApplicationContext.

If that is not the case, a hook will be installed that will register 
the QCoreApplication::instance() with the mcnepp::qtdi::QApplicationContext::instance() later.




## Multi-threading

Since the class mcnepp::qtdi::QApplicationContext is derived from QObject, each ApplicationContext has a *thread-affinity* to the thread that created it.

As a consequence, there are some restrictions regarding the threads from which some of the ApplicationContext's functions may be invoked.

The main thing to keep in mind is this: **An ApplicationContext may only be modified and published in the ApplicationContext's thread.**

However, any thread may safely obtain a mcnepp::qtdi::ServiceRegistration or a mcnepp::qtdi::ProxyRegistration and subscribe to its publication-signal. The signal will be delivered
using the target-thread's event-queue.

The following table sums this up:

|Function|Allowed threads|Remarks|
|---|---|---|
|mcnepp::qtdi::QApplicationContext::getRegistration(const QString&) const|any| |
|mcnepp::qtdi::QApplicationContext::getRegistration() const|any| |
|mcnepp::qtdi::QApplicationContext::getRegistrations() const|any| |
|mcnepp::qtdi::QApplicationContext::published() const|any| |
|mcnepp::qtdi::QApplicationContext::pendingPublication() const|any| |
|mcnepp::qtdi::QApplicationContext::instance()|any| |
|mcnepp::qtdi::QApplicationContext::registerService()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid ServiceRegistration.|
|mcnepp::qtdi::QApplicationContext::registerObject()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid ServiceRegistration.|
|mcnepp::qtdi::ServiceRegistration::registerAlias(const QString&)|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return `false`.|
|mcnepp::qtdi::Registration::autowire()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid Subscription.|
|mcnepp::qtdi::bind()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid Subscription.|
|mcnepp::qtdi::QApplicationContext::publish(bool)|only the ApplicationContext's|All published services will live in the ApplicationContext's thread.|

## Extending QApplicationContext

It is not possible to extend the class mcnepp::qtdi::StandardApplicationContext, as it is `final`.

However, it is possible to extend the interface mcnepp::qtdi::QApplicationContext in your own code.
You could add additional virtual functions to the extended interface, to be used by client-code:

    class IExtendedApplicationContext : public mcnepp::qtdi::QApplicationContext { 
    public:
    
       using QApplicationContext::QApplicationContext;    
       
       virtual void doSomeFancyRegistration() = 0;
    };

Now, you could not implement this interface `IExtendedApplicationContext` in a class that already implements `QApplicationContext`, as that would constitute multiple inheritance of the same base.
<br>The class-template mcnepp::qtdi::ApplicationContextImplBase solves that problem for you.
<br>You need to supply the type of the extended interface as a type-argument:


    class ExtendedApplicationContext : public mcnepp::qtdi::ApplicationContextImplBase<QApplicationContext> { 
      public:
        ExtendedApplicationContext(QObject* parent = nullptr) : ApplicationContextImplBase<QApplicationContext>{parent}
        {
            finishConstruction();
        }
        
        virtual void doSomeFancyRegistration() override;
    };


Should you really insist on implementing mcnepp::qtdi::QApplicationContext from scratch,
here are some rules that you should follow:

- You can implement the **public** pure virtual functions simply by invoking them on the *delegate*.
- The **protected** pure virtual functions can be implemented by using the corresponding static `delegate...()` functions from QApplicationContext. Pass in your *delegate* as the first argument, as shown below.
- Be aware that the first StandardApplicationContext that you create as a *delegate* will automatically become the global instance. This is probably not what you want.
Therefore, consider to unset the delegate explicitly, as shown in the code below!
- Be aware that the *delegate* will be injected into all services as a parent automatically (unless they have an explicit parent). Also, the *init-methods* will receive
the *delegate* as an argument, as will the QApplicationContextPostProcessor::process() method.<br>
In order to inject your own implementation instead, use the function mcnepp::qtdi::newDelegate() This is also shown below.
- Do not forget to connect your instance to the signals emitted by the delegate. You may use the static function mcnepp::qtdi::QApplicationContext::delegateConnectSignals(), as shown below.

Here is an (incomplete) example of a custom implementation of mcnepp::qtdi::QApplicationContext:

    class ExtendedApplicationContext : public mcnepp::qtdi::QApplicationContext {
      Q_OBJECT
      
      public:

        explicit ExtendedApplicationContext(QObject *parent) :
                QApplicationContext{parent},
                m_delegate{new newDelegate(this)}
        {
            unsetInstance(m_delegate); // Remove the delegate as global instance, should it have been set.
            if(setInstance(this)) { // Attempt to set this as global instance.
                qCInfo(loggingCategory()).noquote().nospace() << "Installed " << this << " as global instance";
            } 
    // Propagate signals from delegate to this:
            delegateConnectSignals(m_delegate, this);
        }


        ~ExtendedApplicationContext() {
            unsetInstance(this);
        }

        bool publish(bool allowPartial) override {
        // Implement a public pure virtual method by invoking on delegate:
            return m_delegate->publish(allowPartial);
        }

     // More public methods...
     
      protected:

        mcnepp::qtdi::service_registration_handle_t registerService(const QString &name, const service_descriptor &descriptor, const service_config& config, ServiceScope scope, const QStringList& profiles, QObject* baseObject) override {
        // Implement a protected pure virtual method by leveraging the corresponding static helper:
            return delegateRegisterService(m_delegate, name, descriptor, config, scope, profiles, baseObject);
        }
        
     // More protected methods...

      private:
        mcnepp::qtdi::QApplicationContext* const m_m_delegate;
    };













