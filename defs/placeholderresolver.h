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

    QVariant resolve(QConfigurationResolver* appContext, const service_config& config) const;

    bool hasPlaceholders() const;

    static PlaceholderResolver* parse(const QString& placeholderString, QObject* parent, const QLoggingCategory& loggingCategory = defaultLoggingCategory());

    const QString& expression() const {
        return m_placeholderText;
    }


private:

    PlaceholderResolver(const QString& placeholderText, const QLoggingCategory& loggingCategory, QObject* parent);

    void addStep(const QString& literal);

    void addStep(const QString& placeholder, const QString& defaultValue, bool hasWildcard);


    struct resolvable_step {
        virtual ~resolvable_step() = default;
        virtual QVariant resolve(QConfigurationResolver* appContext, const service_config& config) = 0;
        virtual QString placeholder() const = 0;
    };

    struct literal_step;

    struct placeholder_step;



    QString m_placeholderText;
    std::deque<std::unique_ptr<resolvable_step>> m_steps;
    const QLoggingCategory& m_loggingCategory;
};

}
