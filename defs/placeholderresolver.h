#pragma once
#include "qapplicationcontext.h"
#include <deque>

namespace mcnepp::qtdi::detail {

///
/// \brief Resolves placeholders via the QApplicationContext's configuraton.
/// <br>Instances are created using the static function parse(const QString&, QObject*, const QLoggingCategory& loggingCategory)
///
class PlaceholderResolver : public QObject {

public:

    QVariant resolve(const QString& group, QVariantMap& resolvedPlaceholders) const;

    QVariant resolve(const QString& group = {}) const {
        QVariantMap resolvedPlaceholders;
        return resolve(group, resolvedPlaceholders);
    }

    bool hasPlaceholders() const;

    void clearPlaceholders(QVariantMap& resolvedPlaceholders) const;

    static PlaceholderResolver* parse(const QString& placeholderString, QApplicationContext* parent);

    ///
    /// \brief Is the supplied expression a literal?
    /// <br>This function implements a fast heuristic. If it returns `true`, it is guaranteed that there is no placeholder within
    /// the supplied expression. For example, the function will return `true` for the empty String.<br>
    /// If the function returns `false`, however, there *could be placeholders* contained, thus invoking parse(const QString&,QApplicationContext*)
    /// should be the next step.
    /// \return `true` if the expression contains no placeholders.
    ///
    static bool isLiteral(const QString& expression);

    const QString& expression() const {
        return m_placeholderText;
    }


private:

    struct resolvable_step {
        virtual ~resolvable_step() = default;
        virtual QVariant resolve(QApplicationContext* appContext, const QString& group, QVariantMap& resolvedPlaceholders) = 0;
        virtual QString placeholder() const = 0;
    };

    PlaceholderResolver(const QString& placeholderText, QApplicationContext* parent, std::deque<std::unique_ptr<resolvable_step>>&&);

    struct literal_step;

    struct placeholder_step;

    static std::unique_ptr<resolvable_step> addStep(const QString& literal);

    static std::unique_ptr<resolvable_step> addStep(const QString& placeholder, const QString& defaultValue, bool hasWildcard);

    QApplicationContext* const m_context;
    QString m_placeholderText;
    std::deque<std::unique_ptr<resolvable_step>> m_steps;
    const QLoggingCategory& m_loggingCategory;
};

}
