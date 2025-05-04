#include "qapplicationcontext.h"

namespace mcnepp::qtdi {




class Matchers final {
    friend class Condition;

    class NeverMatcher : public Condition::Matcher {
    public:
        virtual bool matches(QApplicationContext*) const override {
            return false;
        }

        virtual void print(QDebug out) const override {
            out << "Never";
        }



        virtual bool overlaps(const Matcher*) const override {
            return false;
        }

        virtual bool equals(const Matcher* other) const override {
            return dynamic_cast<const NeverMatcher*>(other) != nullptr;
        }


        explicit NeverMatcher(int initialRefCount = 0) {
            ref.storeRelaxed(initialRefCount);
        }

        virtual Matcher* otherwise() const override {
            return Condition::always().m_data.get();
        }
    };

    class AlwaysMatcher : public Condition::Matcher {
    public:
        virtual bool matches(QApplicationContext*) const override {
            return true;
        }

        virtual void print(QDebug) const override {

        }



        virtual bool overlaps(const Matcher* other) const override {
            //this condition overlaps all other conditions, except Never.
            return !dynamic_cast<const NeverMatcher*>(other);
        }

        virtual bool equals(const Matcher* other) const override {
            return other -> isAlways();
        }

        virtual bool isAlways() const  override {
            return true;
        }

        virtual Matcher* otherwise() const override {
            static NeverMatcher neverMatcher{1}; //Prevent deletion of static object
            return &neverMatcher;
        }


        explicit AlwaysMatcher(int initialRefCount = 0) {
            ref.storeRelaxed(initialRefCount);
        }
    };



    class ProfileMatcher : public Condition::Matcher {
    public:
        ProfileMatcher(Profiles&& profiles, bool positiveMatch) :
            m_profiles{std::move(profiles)},
            m_positiveMatch{positiveMatch} {

        }
        ProfileMatcher(const Profiles& profiles, bool positiveMatch) :
            m_profiles{profiles},
            m_positiveMatch{positiveMatch} {

        }

        bool matches(QApplicationContext* context) const override {
            return m_profiles.intersects(context->activeProfiles()) == m_positiveMatch;
        }

        bool hasProfiles() const override {
            return true;
        }

        bool overlaps(const Matcher* other) const override {
            if(auto otherMatcher = dynamic_cast<const ProfileMatcher*>(other)) {
                //If both are positive, they overlap if the profiles intersect.
                //Otherwise, they overlap if the profiles do not intersect:
                return (m_positiveMatch == otherMatcher -> m_positiveMatch) == m_profiles.intersects(otherMatcher -> m_profiles);
            }
            return false;
        }

        bool equals(const Matcher* other) const override {
            if(auto otherMatcher = dynamic_cast<const ProfileMatcher*>(other)) {
                return *this == *otherMatcher;
            }
            return false;
        }

        friend bool operator==(const ProfileMatcher& left, const ProfileMatcher& right) {
            return left.m_profiles == right.m_profiles && left.m_positiveMatch == right.m_positiveMatch;
        }

        void print(QDebug out) const override {
            if(m_positiveMatch) {
                out.nospace() << "[if profile in {";
            } else {
                out.nospace() << "[if profile not in {";
            }
            const char* del = "";
            for(auto& profile : m_profiles) {
                out << del << profile;
                del = ", ";
            }
            out << "}]";
        }

        virtual Matcher* otherwise() const override {
            return new ProfileMatcher{m_profiles, !m_positiveMatch};
        }
    private:

        Profiles m_profiles;
        bool m_positiveMatch;
    };

    class PropertyExistsMatcher : public Condition::Matcher {
    public:
        PropertyExistsMatcher(QAnyStringView expression, bool valid) :
            m_expression{expression.toString()},
            m_valid{valid}    {

        }

        bool matches(QApplicationContext* context) const override {
            return context->resolveConfigValue(m_expression).isValid() == m_valid;
        }




        bool equals(const Matcher* other) const override {
            if(auto otherMatcher = dynamic_cast<const PropertyExistsMatcher*>(other)) {
                return *this == *otherMatcher;
            }
            return false;
        }

        friend bool operator==(const PropertyExistsMatcher& left, const PropertyExistsMatcher& right) {
            return left.m_expression == right.m_expression && left.m_valid == right.m_valid;
        }

        void print(QDebug out) const override {
            out.nospace() << (m_valid ? "[if config exists: '" : "[if config absent: '") << m_expression << "']";
        }

        virtual Matcher* otherwise() const override {
            return new PropertyExistsMatcher{m_expression, !m_valid};
        }
    private:
        QString m_expression;
        bool m_valid;
    };

    class PropertyMatcher : public Condition::Matcher {
        using predicate_t = Condition::ConfigHelper::predicate_t;
    public:
        PropertyMatcher(QAnyStringView expression, const QVariant& value, int matchType, predicate_t lessPredicate) :
            m_expression{expression.toString()},
            m_refValue{value},
            m_matchType{matchType},
            m_lessPredicate{lessPredicate} {

        }

        bool matches(QApplicationContext* context) const override {
            QVariant value = context->resolveConfigValue(m_expression);

            switch(m_matchType) {
            case Condition::ConfigHelper::MATCH_TYPE_EQUALS:
                return value == m_refValue;
            case Condition::ConfigHelper::MATCH_TYPE_NOT_EQUALS:
                return value != m_refValue;
            case Condition::ConfigHelper::MATCH_TYPE_LESS:
                return value.isValid() && m_lessPredicate(value, m_refValue);
            case Condition::ConfigHelper::MATCH_TYPE_GREATER:
                return value.isValid() && m_lessPredicate(m_refValue, value);
            case Condition::ConfigHelper::MATCH_TYPE_LESS_OR_EQUAL:
                return value.isValid() && !m_lessPredicate(m_refValue, value);
            case Condition::ConfigHelper::MATCH_TYPE_GREATER_OR_EQUAL:
                return value.isValid() && !m_lessPredicate(value, m_refValue);
            default:
                return false;
            }
        }


        bool equals(const Matcher* other) const override {
            if(auto otherMatcher = dynamic_cast<const PropertyMatcher*>(other)) {
                return *this == *otherMatcher;
            }
            return false;
        }

        friend bool operator==(const PropertyMatcher& left, const PropertyMatcher& right) {
            return left.m_expression == right.m_expression && left.m_refValue == right.m_refValue && left.m_matchType == right.m_matchType;
        }

        void print(QDebug out) const override {
            out.nospace() << "[if config '" << m_expression << "'";
            switch(m_matchType) {
            case Condition::ConfigHelper::MATCH_TYPE_EQUALS:
                out << " == ";
                break;
            case Condition::ConfigHelper::MATCH_TYPE_NOT_EQUALS:
                out << " != ";
                break;
            case Condition::ConfigHelper::MATCH_TYPE_LESS:
                out << " < ";
                break;
            case Condition::ConfigHelper::MATCH_TYPE_GREATER:
                out << " > ";
                break;
            case Condition::ConfigHelper::MATCH_TYPE_LESS_OR_EQUAL:
                out << " <= ";
                break;
            case Condition::ConfigHelper::MATCH_TYPE_GREATER_OR_EQUAL:
                out << " >= ";
                break;
            }
            out << m_refValue.toString() << "]";
        }

        static int inverseMatchType(int matchType) {
            switch(matchType) {
            case Condition::ConfigHelper::MATCH_TYPE_EQUALS:
                return Condition::ConfigHelper::MATCH_TYPE_NOT_EQUALS;
            case Condition::ConfigHelper::MATCH_TYPE_NOT_EQUALS:
                return Condition::ConfigHelper::MATCH_TYPE_EQUALS;
            case Condition::ConfigHelper::MATCH_TYPE_LESS:
                return Condition::ConfigHelper::MATCH_TYPE_GREATER_OR_EQUAL;
            case Condition::ConfigHelper::MATCH_TYPE_GREATER:
                return Condition::ConfigHelper::MATCH_TYPE_LESS_OR_EQUAL;
            case Condition::ConfigHelper::MATCH_TYPE_LESS_OR_EQUAL:
                return Condition::ConfigHelper::MATCH_TYPE_GREATER;
            case Condition::ConfigHelper::MATCH_TYPE_GREATER_OR_EQUAL:
                return Condition::ConfigHelper::MATCH_TYPE_LESS;
            default:
                qCCritical(defaultLoggingCategory()).nospace() << "Invalid matchType " << matchType;
                return matchType;
            }
        }

        Matcher* otherwise() const override {
            return new PropertyMatcher{m_expression, m_refValue, inverseMatchType(m_matchType), m_lessPredicate};
        }
    private:

        QString m_expression;
        QVariant m_refValue;
        int m_matchType;
        predicate_t m_lessPredicate;
    };

    class PropertyMatchesMatcher : public Condition::Matcher {
    public:
        PropertyMatchesMatcher(QAnyStringView expression, const QRegularExpression& regEx, bool match = true) :
            m_expression{expression.toString()},
            m_regEx{regEx},
            m_match{match} {

        }

        bool matches(QApplicationContext* context) const override {
            return m_regEx.match(context->resolveConfigValue(m_expression).toString()).hasMatch() == m_match;
        }



        bool equals(const Matcher* other) const override {
            if(auto otherMatcher = dynamic_cast<const PropertyMatchesMatcher*>(other)) {
                return *this == *otherMatcher;
            }
            return false;
        }

        friend bool operator==(const PropertyMatchesMatcher& left, const PropertyMatchesMatcher& right) {
            return left.m_expression == right.m_expression && left.m_regEx == right.m_regEx && left.m_match == right.m_match;
        }


        void print(QDebug out) const override {
            out.nospace() << "[if config '" << m_expression << (m_match ? "' matches '" : "' does not match '") << m_regEx.pattern() << "']";
        }

        Matcher* otherwise() const override {
            return new PropertyMatchesMatcher{m_expression, m_regEx, !m_match};
        }
    private:

        QString m_expression;
        QRegularExpression m_regEx;
        bool m_match;
    };

    static Condition::Matcher* matcherForProfiles(Profiles && profiles, bool positiveMatch)
    {
        return new ProfileMatcher{std::move(profiles), positiveMatch};
    }

    static Condition::Matcher* matcherForProfiles(const Profiles & profiles, bool positiveMatch)
    {
        return new ProfileMatcher{profiles, positiveMatch};
    }

    static Condition::Matcher* matcherForConfigEntryExists(QAnyStringView expression, bool valid)
    {
        return new PropertyExistsMatcher{expression, valid};
    }

    static Condition::Matcher *matcherForConfigEntryMatches(QAnyStringView expression, const QRegularExpression &regEx)
    {
        return new PropertyMatchesMatcher{expression, QRegularExpression{regEx}};
    }
};

Condition Condition::ProfileHelper::operator ==(QAnyStringView profile) const {
    return Condition{Matchers::matcherForProfiles(Profiles{profile.toString()}, true)};
}

Condition Condition::ProfileHelper::operator !=(QAnyStringView profile) const {
    return Condition{Matchers::matcherForProfiles(Profiles{profile.toString()}, false)};
}

Condition Condition::ProfileHelper::operator&(const Profiles &profiles) const {
    return Condition{Matchers::matcherForProfiles(profiles, true)};
}

Condition Condition::ProfileHelper::operator&(Profiles &&profiles) const {
    return Condition{Matchers::matcherForProfiles(std::move(profiles), true)};
}

Condition Condition::ProfileHelper::operator^(const Profiles &profiles) const {
    return Condition{Matchers::matcherForProfiles(profiles, false)};
}

Condition Condition::ProfileHelper::operator^(Profiles &&profiles) const {
    return Condition{Matchers::matcherForProfiles(std::move(profiles), false)};
}

Condition Condition::ConfigHelper::Entry::exists() const {
    return Condition{Matchers::matcherForConfigEntryExists(m_expression, true)};
}

Condition Condition::ConfigHelper::Entry::operator!() const {
    return Condition{Matchers::matcherForConfigEntryExists(m_expression, false)};
}

Condition Condition::ConfigHelper::Entry::operator ==(const QVariant &refValue) const {
    return Condition{matcherForConfigEntry(m_expression, refValue, MATCH_TYPE_EQUALS, nullptr)};
}

Condition Condition::ConfigHelper::Entry::operator !=(const QVariant &refValue) const {
    return Condition{matcherForConfigEntry(m_expression, refValue, MATCH_TYPE_NOT_EQUALS, nullptr)};
}

Condition Condition::ConfigHelper::Entry::matches(const QRegularExpression &regEx) const {
    return Condition{Matchers::matcherForConfigEntryMatches(m_expression, regEx)};
}

Condition Condition::ConfigHelper::Entry::matches(const QString &regEx, QRegularExpression::PatternOptions options) const {
    return matches(QRegularExpression{regEx, options});
}


Condition Condition::always() {
    static Matchers::AlwaysMatcher alwaysMatcher{1}; //Always set reference-count to one to prevent deletion
    return Condition{&alwaysMatcher};
}


QDebug operator<<(QDebug out, const Condition& cond) {
    QDebugStateSaver saver{out};
    cond.m_data->print(out);
    return out;
}

Condition::Matcher *Condition::matcherForConfigEntry(QAnyStringView expression, const QVariant &refValue, int matchType, Condition::ConfigHelper::predicate_t lessPredicate)
{
    return new Matchers::PropertyMatcher{expression, refValue, matchType, lessPredicate};
}

bool Condition::matches(QApplicationContext *context) const {
    return m_data->matches(context);
}

bool Condition::hasProfiles() const {
    return m_data->hasProfiles();
}

bool operator==(const Condition &left, const Condition &right) {
    return left.m_data == right.m_data || left.m_data->equals(right.m_data.get());
}

bool operator!=(const Condition &left, const Condition &right) {
    return left.m_data != right.m_data && !left.m_data->equals(right.m_data.get());
}

bool Condition::overlaps(const Condition &other) const {
    return m_data->overlaps(other.m_data.get()) || other.m_data->overlaps(m_data.get());
}

bool Condition::isAlways() const {
    return m_data->isAlways();
}

Condition Condition::operator!() const {
    return Condition{m_data->otherwise()};
}




}
