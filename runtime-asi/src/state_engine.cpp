// state_engine.cpp — tiny recursive-descent evaluator (SF-4).
//
// Grammar (all operators left-associative):
//   expr   := and_expr ( '||' and_expr )*
//   and    := cmp ( '&&' cmp )*
//   cmp    := unary ( ('=='|'!='|'<='|'>='|'<'|'>') unary )?
//   unary  := '!' unary | primary
//   primary:= number | identifier | '(' expr ')'
//
// Unknown identifiers resolve to NaN; every comparison with NaN is false,
// and '!' of a NaN operand is false too (do not trust what we could not
// read) — that is the fail-closed contract.

#include "state_engine.hpp"

#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <limits>

namespace senseforge {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

class Evaluator {
public:
    Evaluator(const std::string& text, const BindingValues& values)
        : text_(text), values_(values) {}

    bool Run() {
        SkipSpaces();
        if (pos_ >= text_.size()) {
            return false;  // empty condition — fail-closed
        }
        const double result = ParseOr();
        SkipSpaces();
        return pos_ >= text_.size() && !std::isnan(result) && result != 0.0;
    }

private:
    void SkipSpaces() {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool Consume(const char* token) {
        SkipSpaces();
        const std::size_t len = std::strlen(token);
        if (text_.compare(pos_, len, token) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    double ParseOr() {
        double left = ParseAnd();
        while (Consume("||")) {
            const double right = ParseAnd();
            // NaN-poisoning: unknown on either side is not "true".
            if (std::isnan(left) || std::isnan(right)) {
                left = kNaN;
            } else {
                left = ((left != 0.0) || (right != 0.0)) ? 1.0 : 0.0;
            }
        }
        return left;
    }

    double ParseAnd() {
        double left = ParseCmp();
        while (Consume("&&")) {
            const double right = ParseCmp();
            if (std::isnan(left) || std::isnan(right)) {
                left = kNaN;
            } else {
                left = ((left != 0.0) && (right != 0.0)) ? 1.0 : 0.0;
            }
        }
        return left;
    }

    double ParseCmp() {
        double left = ParseUnary();
        SkipSpaces();
        // Order matters: two-char operators before single-char.
        if (Consume("==")) {
            const double right = ParseUnary();
            return (std::isnan(left) || std::isnan(right))
                       ? kNaN
                       : ((left == right) ? 1.0 : 0.0);
        }
        if (Consume("!=")) {
            const double right = ParseUnary();
            return (std::isnan(left) || std::isnan(right))
                       ? kNaN
                       : ((left != right) ? 1.0 : 0.0);
        }
        if (Consume("<=")) {
            const double right = ParseUnary();
            return (std::isnan(left) || std::isnan(right))
                       ? kNaN
                       : ((left <= right) ? 1.0 : 0.0);
        }
        if (Consume(">=")) {
            const double right = ParseUnary();
            return (std::isnan(left) || std::isnan(right))
                       ? kNaN
                       : ((left >= right) ? 1.0 : 0.0);
        }
        if (Consume("<")) {
            const double right = ParseUnary();
            return (std::isnan(left) || std::isnan(right))
                       ? kNaN
                       : ((left < right) ? 1.0 : 0.0);
        }
        if (Consume(">")) {
            const double right = ParseUnary();
            return (std::isnan(left) || std::isnan(right))
                       ? kNaN
                       : ((left > right) ? 1.0 : 0.0);
        }
        return left;
    }

    double ParseUnary() {
        if (Consume("!")) {
            const double operand = ParseUnary();
            if (std::isnan(operand)) {
                return kNaN;  // !unknown stays unknown — fail-closed
            }
            return (operand == 0.0) ? 1.0 : 0.0;
        }
        return ParsePrimary();
    }

    double ParsePrimary() {
        SkipSpaces();
        if (pos_ >= text_.size()) {
            return kNaN;
        }
        if (text_[pos_] == '(') {
            ++pos_;
            const double inner = ParseOr();
            SkipSpaces();
            if (pos_ < text_.size() && text_[pos_] == ')') {
                ++pos_;
            } else {
                return kNaN;  // unbalanced — fail-closed
            }
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
            text_[pos_] == '.' || text_[pos_] == '-') {
            char* end = nullptr;
            const double value = std::strtod(text_.c_str() + pos_, &end);
            if (end == text_.c_str() + pos_) {
                return kNaN;
            }
            pos_ = static_cast<std::size_t>(end - text_.c_str());
            return value;
        }
        if (std::isalpha(static_cast<unsigned char>(text_[pos_])) ||
            text_[pos_] == '_') {
            std::size_t begin = pos_;
            while (pos_ < text_.size() &&
                   (std::isalnum(static_cast<unsigned char>(text_[pos_])) ||
                    text_[pos_] == '_')) {
                ++pos_;
            }
            const auto it =
                values_.find(text_.substr(begin, pos_ - begin));
            return it == values_.end() ? kNaN : it->second;
        }
        return kNaN;
    }

    std::string text_;
    std::size_t pos_ = 0;
    const BindingValues& values_;
};

}  // namespace

bool EvaluateCondition(const std::string& condition,
                       const BindingValues& values) {
    Evaluator evaluator(condition, values);
    return evaluator.Run();
}

}  // namespace senseforge
