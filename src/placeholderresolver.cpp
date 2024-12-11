#include "placeholderresolver.h"
namespace mcnepp::qtdi::detail {



    QVariant PlaceholderResolver::resolve(QConfigurationResolver* appContext, const service_config& config) const {
        QString resolvedString;
        for(auto& resolvable : m_steps) {
            QVariant resolved = resolvable->resolve(appContext, config);
            if(!resolved.isValid()) {
                qCCritical(m_loggingCategory).nospace() << "Could not resolve placeholder " << resolvable->placeholder();

                return resolved;
            }
            if(m_steps.size() == 1) {
                return resolved;
            }
            resolvedString += resolved.toString();
        }
        return resolvedString;
    }


    struct PlaceholderResolver::literal_step : resolvable_step {
        virtual QVariant resolve(QConfigurationResolver*, const service_config&) override {
            return literal;
        }

        virtual QString placeholder() const override {
            return QString{};
        }



        literal_step(const QString& literal) : literal{literal} {

        }

        QString literal;
    };

    struct PlaceholderResolver::placeholder_step : resolvable_step {
        virtual QVariant resolve(QConfigurationResolver* appContext, const service_config& config) override {
            QVariant resolved = appContext->getConfigurationValue(QConfigurationResolver::makePath(config.group, key), hasWildcard);
            if(!resolved.isValid()) {
                if(!key.startsWith('.') && config.properties.contains("."+key)) {
                    //If not found in ApplicationContext's configuration, look in the "private properties":
                    auto cv = config.properties["." + key];
                    switch(cv.configType) {
                    case ConfigValueType::SERVICE:
                        break;
                    default:
                        resolved = cv.expression;
                        if(resolved.typeId() == QMetaType::QString) {
                            return appContext->resolveConfigValue(resolved.toString());
                        }
                    }
                }
                if(!resolved.isValid() && !defaultValue.isEmpty()) {
                    return defaultValue;
                }
            }
            return resolved;
        }

        virtual QString placeholder() const override {
            return key;
        }


        placeholder_step(const QString& key, const QString& defaultValue, bool hasWildcard) :
            key{key},
            defaultValue{defaultValue},
            hasWildcard{hasWildcard} {

        }

        QString key;
        QString defaultValue;
        bool hasWildcard;

    };

    void PlaceholderResolver::addStep(const QString& literal) {
        m_steps.push_back(std::make_unique<literal_step>(literal));
    }

    void PlaceholderResolver::addStep(const QString& placeholder, const QString& defaultValue, bool hasWildcard) {
        m_steps.push_back(std::make_unique<placeholder_step>(placeholder, defaultValue, hasWildcard));
    }

    bool PlaceholderResolver::hasPlaceholders() const
    {
        for(auto& resolvable : m_steps) {
            if(!resolvable->placeholder().isEmpty()) {
                return true;
            }
        }
        return false;
    }

    PlaceholderResolver* PlaceholderResolver::parse(const QString &placeholderString, QObject* parent, const QLoggingCategory& loggingCategory)
    {
        constexpr int STATE_START = 0;
        constexpr int STATE_FOUND_DOLLAR = 1;
        constexpr int STATE_FOUND_PLACEHOLDER = 2;
        constexpr int STATE_FOUND_DEFAULT_VALUE = 3;
        constexpr int STATE_ESCAPED = 4;
        QString token;
        QString defaultValueToken;
        std::unique_ptr<PlaceholderResolver> resolver{new PlaceholderResolver{placeholderString, loggingCategory, parent}};

        int lastStateBeforeEscape = STATE_START;
        int state = STATE_START;
        bool hasWildcard = false;
        for(int pos = 0; pos < placeholderString.length(); ++pos) {
            auto ch = placeholderString[pos];
            switch(ch.toLatin1()) {

            case '\\':
                switch(state) {
                case STATE_ESCAPED:
                    token += '\\';
                    state = lastStateBeforeEscape;
                    continue;
                case STATE_FOUND_DOLLAR:
                    token += '$';
                    lastStateBeforeEscape = STATE_START;
                    state = STATE_ESCAPED;
                    continue;
                default:
                    lastStateBeforeEscape = state;
                    state = STATE_ESCAPED;
                    continue;
                }

            case '$':
                switch(state) {
                case STATE_ESCAPED:
                    token += '$';
                    state = lastStateBeforeEscape;
                    continue;
                case STATE_FOUND_DOLLAR:
                    token += '$';
                    [[fallthrough]];
                case STATE_START:
                    state = STATE_FOUND_DOLLAR;
                    continue;
                default:
                    qCCritical(loggingCategory).nospace().noquote() << "Invalid placeholder '" << placeholderString << "'";
                    return nullptr;
                }


            case '{':
                switch(state) {
                case STATE_ESCAPED:
                    token += '{';
                    state = lastStateBeforeEscape;
                    continue;
                case STATE_FOUND_DOLLAR:
                    if(!token.isEmpty()) {
                        resolver->addStep(token);
                        token.clear();
                    }
                    state = STATE_FOUND_PLACEHOLDER;
                    continue;
                default:
                    state = STATE_START;
                    token += ch;
                    continue;
                }

            case '}':
                switch(state) {
                case STATE_ESCAPED:
                    token += '}';
                    state = lastStateBeforeEscape;
                    continue;
                case STATE_FOUND_DEFAULT_VALUE:
                case STATE_FOUND_PLACEHOLDER:
                    if(!token.isEmpty()) {
                        resolver->addStep(token, defaultValueToken, hasWildcard);
                        defaultValueToken.clear();
                        token.clear();
                        hasWildcard = false;
                    }
                    state = STATE_START;
                    continue;
                default:
                    token += ch;
                    continue;
                }
            case ':':
                switch(state) {
                case STATE_ESCAPED:
                    token += ':';
                    state = lastStateBeforeEscape;
                    continue;
                case STATE_FOUND_PLACEHOLDER:
                    state = STATE_FOUND_DEFAULT_VALUE;
                    continue;
                }

            case '*':
                switch(state) {
                case STATE_FOUND_PLACEHOLDER:
                    //Look-ahead: The only valid wildcard notation starts with '*/'
                    if(pos + 1 >= placeholderString.length() || placeholderString[pos+1] != '/') {
                        qCCritical(loggingCategory).nospace().noquote() << "Invalid placeholder '" << placeholderString << "'";
                        return nullptr;
                    }
                    hasWildcard = true;
                    ++pos;
                    continue;
                default:
                    token += ch;
                    continue;
                }

            default:
                switch(state) {
                case STATE_FOUND_DOLLAR:
                    token += '$';
                    state = STATE_START;
                    [[fallthrough]];
                case STATE_START:
                case STATE_FOUND_PLACEHOLDER:
                    token += ch;
                    continue;
                case STATE_FOUND_DEFAULT_VALUE:
                    defaultValueToken += ch;
                    continue;
                case STATE_ESCAPED:
                    token += ch;
                    state = lastStateBeforeEscape;
                    continue;
                default:
                    token += ch;
                    continue;
                }
            }
        }
        switch(state) {
        case STATE_FOUND_DOLLAR:
            token += '$';
            [[fallthrough]];
        case STATE_START:
            if(!token.isEmpty()) {
                resolver->addStep(token);
            }
            return resolver.release();
        case STATE_ESCAPED:
            token += '\\';
            resolver->addStep(token);
            return resolver.release();
        default:
            qCCritical(loggingCategory).nospace().noquote() << "Unbalanced placeholder '" << placeholderString << "'";
            return nullptr;
        }
    }

    PlaceholderResolver::PlaceholderResolver(const QString& placeholderText, const QLoggingCategory& loggingCategory, QObject* parent) :
        QObject{parent},
        m_placeholderText{placeholderText},
        m_loggingCategory{loggingCategory}
    {

    }


}
