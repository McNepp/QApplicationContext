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

We fix this by not providing the pointer to the `QNetworkAccessManager`, but instead using a kind of "placeholder" for it. This placeholder is the class-template `mcnepp::qtdi::Dependency`.
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


## Externalized Configuration

In the above example, we were configuring the Url with a String-literal in the code. This is less than optimal, as we usually want to be able
to change such configuration-values without re-compiling the program.  
This is made possible with so-called *placeholders* in the configured values:  
When passed to the function mcnepp::qtdi::resolve(), a placeholder embedded in `${   }` will be resolved by the QApplicationContext using Qt's `QSettings` class.  
You simply register one or more instances of `QSettings` with the context, using mcnepp::qtdi::QApplicationContext::registerObject().
This is what it looks like if you out-source the "url" configuration-value into an external configuration-file:

    context -> registerObject(new QSettings{"application.ini", QSettings::IniFormat, context});
    
    context -> registerService(service<RestPropFetcher>(resolve("${hamburgWeatherUrl}"), inject<QNetworkAccessManager>()), "hamburgWeather"); 
    context -> registerService(service<RestPropFetcher>(resolve("${berlinWeatherUrl}"), inject<QNetworkAccessManager>()), "berlinWeather"); 


You could even improve on this by re-factoring the common part of the Url into its own configuration-value:

    context -> registerService(service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${hamburgStationId}"), inject<QNetworkAccessManager>()), "hamburgWeather"); 
    context -> registerService(service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${berlinStationId}"), inject<QNetworkAccessManager>()), "berlinWeather"); 

### Configuring values of non-String types

With mcnepp::qtdi::resolve(), you can also process arguments of types other than `QString`.
You just need to specify the template-argument explicitly, or pass a default-value to be used when the expression cannot be resolved.

Let's say there was an argument of type `int` that specified the connection-timeout in milliseconds. Then, the service-declaration would be:

    auto decl = service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${hamburgStationId}"), resolve<int>("${connectionTimeout}"), inject<QNetworkAccessManager>());

You will notice the explicit type-argument used on mcnepp::qtdi::resolve(). Needless to say, the configured value for the key "connectionTimeout" must resolve to a valid integer-literal!


### Specifying default values

Sometimes, you may want to provide a constructor-argument that can be externally configured, but you are unsure whether the configuration will always be present at runtime.

There are two ways of doing this:

1. You can supply a default-value to the function-template mcnepp::qtdi::resolve() as its second argument: `resolve("${connectionTimeout}", 5000)`. This works only for constructor-arguments (see previous section).
2. You can put a default-value into the placeholder-expression, separated from the placeholder by a colon: `"${connectionTimeout:5000}"`. Such an embedded default-value
takes precedence over one supplied to mqnepp::qtdi::resolve(). This works for both constructor-arguments and Q_PROPERTYs (see next section).

### Specifying an explicit Group

If you structure your configuration in a hierarchical manner, you may find it useful to put your configuration-values into a `QSettings::group()`.
Such a group can be specified for your resolvable values by means of the mcnepp::qtdi::make_config() function. In the following example, the configuration-keys "baseUrl", "hamburgStationId" and "connectionTimeout"
are assumed to reside in the group named "mcnepp":

    auto decl = service<RestPropFetcher>(resolve("${baseUrl}?stationIds=${hamburgStationId}"), resolve<int>("${connectionTimeout}"), inject<QNetworkAccessManager>());
    appContext -> registerService(decl, "hamburgWeather", make_config({}, "mcnepp"));


## Configuring services with Q_PROPERTY

We have seen how we can inject configuration-values into Service-constructors. Another way of configuring Services is to use Q_PROPERTY declarations.
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
For this, the yet unused `service_config` argument comes into play: It contains a `QVariantMap` with the names and values of properties to set:

    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()), "hamburgWeather", make_config({{"url", "${baseUrl}?stationIds=${hamburgStationId}"}, {"connectionTimeout", "${connectionTimeout:5000}}));
    context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()), "berlinWeather", make_config({{"url", "${baseUrl}?stationIds=${berlinStationId}"}, {"connectionTimeout", "${connectionTimeout:5000}})); 

As you can see, the code has changed quite significantly: instead of supplying the Url as a constructor-argument, you use mcnepp::qtdi::make_config() and pass in the key/value-pairs for configuring
the service's url and connectionTimeouts as Q_PROPERTYs.

**Note:** Every property supplied to mcnepp::qtdi::QApplicationContext::registerService() will be considered a potential Q_PROPERTY of the target-service. mcnepp::qtdi::QApplicationContext::publish() will fail if no such property can be
found.  
However, if you prefix the property-key with a dot, it will be considered a *private property*. It will still be resolved via QSettings, but no attempt will be made to access a matching Q_PROPERTY.
Such *private properties* may be passed to a mcnepp::qtdi::QApplicationContextPostProcessor (see section "Tweaking services" below).



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
|When is the signal `objectPublished(QObject*)` emitted?|upon mcnepp::qtdi::QApplicationContext::publish(bool)|immediately after the registration|
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
Mandatory dependencies can be specified by using inject():

    context->registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()));

In case you have the ServiceRegistration for the dependency at hand, you may skip the invocation of mcnepp::qtdi::inject() and use the ServiceRegistration directly:


....auto networkRegistration = context->registerService<QNetworkAccessManager>();
    context->registerService(service<RestPropFetcher>(networkRegistration));

### OPTIONAL
A service that has an *optional dependency* to another service may be instantiated even when no matching other service can be found in the ApplicationContext.
In that case, `nullptr` will be passed to the service's constructor.  
Optional dependencies are specified the `Dependency` helper-template. Suppose it were possible to create our `RestPropFetcher` without a `QNetworkAccessManage`:

    context->registerService(service<RestPropFetcher>(injectIfPresent<QNetworkAccessManager>()));

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

## Converting Dependencies

As we have seen in the previous section, a mandatory Dependency of type T will be injected into the dependent Service's constructor as a `T*`.
<br>Likewise, a one-to-many Dependency of type T will be injected into the dependent Service's constructor as a `QList<T*>`.
<br>However, sometimes a Service may have a constructor that accepts its dependencies in a different format.
Suppose that the class `PropFetcherAggregator` were declared like this:

    class PropFetcherAggregator : public QObject {
      Q_OBJECT
      
      public:
      
      static constexpr MAX_FETCHERS = 10;
      
      explicit PropFetcherAggregator(const std::array<RestPropFetcher*,MAX_FETCHERS>& fetchers, QObject* parent = nullptr);
    };

There is no implicit conversion from a QList to `a std::array`.
Thus, we'd have to write a converter that accepts a `QObjectList` and produces a `std::array`. Actually, the converter's argument-type must be `QVariant`, as that 
will be passed by the ApplicationContext.


    struct propfetcher_set_converter {
      using array_t = std::array<RestPropFetcher*,PropFetcherAggregator::MAX_FETCHERS>;

      array_t operator()(const QVariant& arg) const {
        array_t target;
        auto qlist = arg.value<QObjectList>();
        std::copy_n(qlist.begin(), std::min(target.size(), qlist.size()), target.begin());
        return target;        
      }
    };

Now when we register the `PropFetcherAggregator` with an ApplicationContext, we simply specify this converter as a type-argument:

    context -> registerService(service<PropFetcherAggregator>(injectAll<RestPropFetcher,propfetcher_set_converter>()));

## Service-interfaces

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
    
    /*2*/ context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher>(), "hamburgWeather", make_config({{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}})); 
    
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

    auto reg = context -> registerService(service<RestPropFetcher>(inject<QNetworkAccessManager>()).advertiseAs<PropFetcher,QNetworkManagerAware>(), "hamburgWeather", make_config({{"url", "https://dwd.api.proxy.bund.dev/v30/stationOverviewExtended?stationIds=10147"}})); 

**Note:** The return-value `reg` will be of type `ServiceRegistration<RestPropFetcher,ServiceScope::SINGLETON>`.

You may convert this value to `ServiceRegistraton<PropFetcher,ServiceScope::SINGLETON>` as well as `ServiceRegistration<QNetworkManagerAware,ServiceScope::SINGLETON>`,
using the member-function ServiceRegistration::as(). Conversions to other types will not succeed.

## The Service-lifefycle

Every service that is registered with a QApplicationContext will go through the following states, in the order shown.
The names of these states are shown for illustration-purposes only. They are not visible outside the QApplicationContext.  
However, some transitions may have observable side-effects.

|External Trigger|Internal Step|State|Observable side-effect|
|---|---|---|---|
|ApplicationContext::registerService()||INIT| |
|ApplicationContext::publish(bool)|Instantiation via constructor or service_factory|CREATED|Invocation of Services's constructor|
| |Set properties|AFTER_PROPERTIES_SET|Invocation of property-setters|
| |Apply QApplicationContextPostProcessor|PROCESSED|Invocation of user-supplied mcnepp::qtdi::QApplicationContextPostProcessor::process()| 
| |if exists, invoke init-method|READY|Anything that the init-method might do|
| |emit signal `objectPublished(QObject*)`|PUBLISHED|Invocation of slots connected via mcnepp::qtdi::Registration::subscribe()|
|~ApplicationContext()|delete service|DESTROYED|Invoke Services's destructor|

### Service-prototypes

As shown above, a service that was registered using mcnepp::qtdi::QApplicationContext::registerService() will be instantiated once mcnepp::qtdi::QApplicationContext::publish(bool) is invoked.
A single instance of the service will be injected into every other service that depends on it.
<br>However, there may be some services that cannot be shared between dependent services. In this case, use mcnepp::qtdi::QApplicationContext::registerPrototype() instead.
<br>Such a registration will not necessarily instantiate the service on mcnepp::qtdi::QApplicationContext::publish(bool).
Only if there are other services depending on it will a new instance be created and injected into the dependent service.



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
which makes this a *reference to another member*:


    context -> registerService(service<PropFetcherAggregator>(injectAll<PropFetcher>()), "propFetcherAggregator");
    
    context -> registerService(service<PropFetcher,RestPropFetcher(inject<QNetworkAccessManager>()), "hamburgWeather", make_config({
      {"url", "${weatherUrl}${hamburgStationId}"},
      {"summary", "&propFetcherAggregator"}
    }));


In the preceeding example, we used a reference to another member to initialize a.

We can take this one step further and use *a reference to a property of another member*.
This can be achieved by using a property-value with the format `"&ref.prop"`. The following example shows this:

    QTimer timer1;
    
    auto reg1 = context -> registerObject(&timer1, "timer1"); // 1
    
    auto reg2 = context -> registerService<QTimer>("timer2", make_config({{"interval", "&timer1.interval"}})); // 2
    
    context -> publish(); 

1. We register a `QTimer` as "timer1".
2. We register a second `QTimer` as "timer2". We use mcnepp::qtdi::make_config() to initialize the property `interval` of the second timer with the first timer's propery.

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

## Accessing a service after registration

So far, we have published the ApplicationContext and let it take care of wiring all the components together.  
In some cases, you need to obtain a reference to a member of the Context after it has been published.  
This is where the return-value of mcnepp::qtdi::QApplicationContext::registerService() comes into play: mcnepp::qtdi::ServiceRegistration.

It offers the method mcnepp::qtdi::ServiceRegistration::subscribe(), which is a type-safe version of a Qt-Signal.
(It is actually implemented in terms of the Signal mcnepp::qtdi::Registration::objectPublished(QObject*)).

In addition to being type-safe, the method mcnepp::qtdi::ServiceRegistration::subscribe() has the advantage that it will automatically inject the service if you subscribe after the service has already been published.  
This code shows how to utilize it:

    auto registration = context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()), "hamburgWeather", make_config({{"url", "${weatherUrl}${hamburgStationId}"}})); 
    
    registration.subscribe(this, [](PropFetcher* fetcher) { qInfo() << "I got the PropFetcher!"; });
    
The function mcnepp::qtdi::ServiceRegistration::subscribe() does return a value of type mcnepp::qtdi::Subscription.
Usually, you may ignore this return-value. However, it can be used for error-checking and for cancelling a subscription, should that be necessary.

## Accessing published services of the ApplicationContext

In the previous paragraph, we used the mcnepp::qtdi::ServiceRegistration obtained by mcnepp::qtdi::QApplicationContext::registerService(), which refers to a single member of the ApplicationContext.
However, we might be interested in all services of a certain service-type.  
This can be achieved using mcnepp::qtdi::QApplicationContext::getRegistration(), which yields a mcnepp::qtdi::ServiceRegistration that represents *all services of the requested service-type*.


    auto registration = context -> getRegistration<PropFetcher>();
    qInfo() << "There have been" << registration.maxPublications() << "RestPropFetchers so far!";    

You may also access a specific service by name:

    auto registration = context -> getRegistration<PropFetcher>("hamburgWeather");
    if(!registration) {
     qWarning() << "Could not obtain service 'hamburgWeather'";
    }
    



## Tweaking services (QApplicationContextPostProcessor)

Whenever a service has been instantiated and all properties have been set, QApplicationContext will apply all registered mcnepp::qtdi::QApplicationContextPostProcessor`s 
to it. These are user-supplied QObjects that implement the aforementioned interface which comprises a single method:

    QApplicationContextPostProcessor::process(QApplicationContext*, QObject*,const QVariantMap&)

In this method, you might apply further configuration to your service there, or perform logging or monitoring tasks.<br>
Any information that you might want to pass to a QApplicationContextPostProcessor can be supplied as
so-called *private properties* via mcnepp::qtdi::make_config(). Just prefix the property-key with a dot.


## 'Starting' services

The last step done in mcnepp::qtdi::QApplicationContext::publish() for each service is the invocation of an *init-method*, should one have been 
registered.

*Init-methods* are supplied as part of the `service_config`, for example like this:

    context -> registerService(service<PropFetcherAggregator>(injectAll<PropFetcher>()), "propFetcherAggregator", make_config({}, "", false, "init"));

Suitable *init-methods* are must be `Q_INVOKABLE` methods with either no arguments, or with one argument of type `QApplicationContext*`.

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
2. The number of mandatory arguments must match the arguments provided via `QApplicationContext::registerService()`, in excess of the service-name and, optionally, the `service_config`.
3. For each `Dependency<T>` with `Kind::MANDATORY` or `Kind::OPTIONAL`, the argument-type must be `T*`.
4. For each `Dependency<T>` with `Kind::N`, the argument-type must be `QList<T*>`.

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

### Provide a custom factory

You custom factory must provide a suitable operator().
Additionally, it should provide a type-declaration `service_type`:


      struct propfetcher_factory {
        using service_type = PropFetcherAggregator;
        
        PropFetcherAggregator* operator()(const QList<PropFetcher*> fetchers) const {
          return PropFetcherAggregator::create(std::vector<PropFetcher*>{fetchers.begin(), fetchers.end()});
        }
      };
    }

Now, when registering our service, we must supply an instance of the `propfetcher_factory`. We do this by using the function mcnepp::qtdi::serviceWithFactory() instead of mcnepp::qtdi::service():

    context -> registerService(serviceWithFactory(propfetcher_factory{}, injectAll<PropFetcher>()));



## Publishing an ApplicationContext more than once

Sometimes, it may be desirable to inovoke QApplicationContext::publish(bool) more than once.
Proceeding with the previous example, there may be several independent modules that each want to supply a service of type `PropFetcher`.
Each of these modules will rightly assume that the dependency of type `QNetworkAccessManager` will be automatically supplied by the QApplicationContext.

But which module shall then invoke QApplicationContext::publish(bool)? Do we need to coordinate this with additional code?
That could be a bit unwieldly. Luckily, this is not necessary.

Given that each module has access to the (global) QApplicationContext, you can simply do this in some initialization-code in module A:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()), "hamburgWeather", make_config({{"url", "${weatherUrl}${hamburgStationId}"}})); 
    context -> publish();

...and this in module B:

    context -> registerService(service<PropFetcher,RestPropFetcher>(inject<QNetworkAccessManager>()), "berlinWeather", make_config({{"url", "${weatherUrl}${berlinStationId}"}})); 
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
    

1. No need to specify Dependency anymore. Therefore, we can use the simplified overload of QApplicationContext::registerService().
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
|mcnepp::qtdi::Registration::subscribe()|any|the signal will be delivered to the target-thread using its event-queue.|
|mcnepp::qtdi::QApplicationContext::registerService()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid ServiceRegistration.|
|mcnepp::qtdi::QApplicationContext::registerObject()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid ServiceRegistration.|
|mcnepp::qtdi::ServiceRegistration::registerAlias(const QString&)|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return `false`.|
|mcnepp::qtdi::Registration::autowire()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid Subscription.|
|mcnepp::qtdi::bind()|only the ApplicationContext's|Invocation from another thread will log a diagnostic and return an invalid Subscription.|
|mcnepp::qtdi::QApplicationContext::publish(bool)|only the ApplicationContext's|All published services will live in the ApplicationContext's thread.|













