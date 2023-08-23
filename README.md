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







