#include "placeholderresolver.h"
namespace mcnepp::qtdi::detail {

    QVariant PlaceholderResolver::resolve(const QString& group, QVariantMap& resolvedPlaceholders) const {
        QString resolvedString;
        for(auto& resolvable : m_steps) {
            QVariant resolved = resolvable->resolve(m_context, group, resolvedPlaceholders);
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
        virtual QVariant resolve(QApplicationContext*, const QString&, QVariantMap&) override {
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
        virtual QVariant resolve(QApplicationContext* appContext, const QString& group, QVariantMap& resolvedPlaceholders) override {
            QVariant resolved;
            if(group.isEmpty()) {
                resolved = appContext->getConfigurationValue(key, hasWildcard);
            } else {
                resolved = appContext->getConfigurationValue(makeConfigPath(appContext->resolveConfigValue(group, {}, resolvedPlaceholders).toString(), key), hasWildcard);
            }
            if(!resolved.isValid()) {
                //If not found in ApplicationContext's configuration, look in the map of already resolved placeholders:
                resolved = resolvedPlaceholders[key];
                if(resolved.typeId() == QMetaType::QString) {
                    resolved = appContext->resolveConfigValue(resolved.toString());
                }
                if(!resolved.isValid() && !defaultValue.isEmpty()) {
                    resolved = defaultValue;
                }
            }
            if(resolved.isValid()) {
                resolvedPlaceholders[key] = resolved;
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

    std::unique_ptr<PlaceholderResolver::resolvable_step> PlaceholderResolver::addStep(const QString& literal) {
        return std::make_unique<literal_step>(literal);
    }

    std::unique_ptr<PlaceholderResolver::resolvable_step>  PlaceholderResolver::addStep(const QString& placeholder, const QString& defaultValue, bool hasWildcard) {
        return std::make_unique<placeholder_step>(placeholder, defaultValue, hasWildcard);
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

    void PlaceholderResolver::clearPlaceholders(QVariantMap& resolvedPlaceholders) const
    {
        for(auto& resolvable : m_steps) {
            if(!resolvable->placeholder().isEmpty()) {
                resolvedPlaceholders.remove(resolvable->placeholder());
            }
        }
    }

    PlaceholderResolver* PlaceholderResolver::parse(const QString &placeholderString, QApplicationContext* parent)
    {
        constexpr int STATE_START = 0;
        constexpr int STATE_FOUND_DOLLAR = 1;
        constexpr int STATE_FOUND_PLACEHOLDER = 2;
        constexpr int STATE_FOUND_DEFAULT_VALUE = 3;
        constexpr int STATE_ESCAPED = 4;
        QString token;
        QString defaultValueToken;
        std::deque<std::unique_ptr<resolvable_step>> steps;

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
                    qCCritical(parent->loggingCategory()).nospace().noquote() << "Invalid placeholder '" << placeholderString << "'";
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
                        steps.push_back(addStep(token));
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
                        steps.push_back(addStep(token, defaultValueToken, hasWildcard));
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
                        qCCritical(parent->loggingCategory()).nospace().noquote() << "Invalid placeholder '" << placeholderString << "'";
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
                steps.push_back(addStep(token));
            }
            break;
        case STATE_ESCAPED:
            token += '\\';
            steps.push_back(addStep(token));
            break;
        default:
            qCCritical(parent->loggingCategory()).nospace().noquote() << "Unbalanced placeholder '" << placeholderString << "'";
            return nullptr;

        }
        return new PlaceholderResolver{placeholderString, parent, std::move(steps)};
    }

    bool PlaceholderResolver::isLiteral(const QString &expression)
    {
        return !expression.contains("${");
    }

    PlaceholderResolver::PlaceholderResolver(const QString& placeholderText, QApplicationContext* parent, std::deque<std::unique_ptr<resolvable_step>>&& steps) :
        QObject{parent},
        m_context{parent},
        m_placeholderText{placeholderText},
        m_steps{std::move(steps)},
        m_loggingCategory{parent->loggingCategory()}
    {

    }


}
