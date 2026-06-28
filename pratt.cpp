// Pratt parser (top-down operator-precedence) — self-contained demo.
// Build: clang++ pratt.cpp -std=c++23 -o pratt && ./pratt "1 + 2 * 3"
//
// The whole precedence engine is Parser::parse_expr below. Everything else
// (lexer, AST, printer) is scaffolding so this file runs on its own.

#include <cctype>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// ---------- Tokens ----------
enum class Tok {
    Number, Ident,
    Plus, Minus, Star, Slash, Caret,
    LParen, RParen, Assign,
    End,
};

struct Token {
    Tok kind;
    std::string text;
};

// ---------- Lexer ----------
std::vector<Token> lex(std::string_view src)
{
    std::vector<Token> out;
    std::size_t i = 0;
    auto one = [&](Tok k) { out.push_back({k, std::string(1, src[i])}); i++; };

    while (i < src.size()) {
        char c = src[i];
        if (std::isspace((unsigned char)c)) { i++; continue; }

        if (std::isdigit((unsigned char)c)) {
            std::size_t s = i;
            while (i < src.size() && (std::isdigit((unsigned char)src[i]) || src[i] == '.'))
                i++;
            out.push_back({Tok::Number, std::string(src.substr(s, i - s))});
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            std::size_t s = i;
            while (i < src.size() && (std::isalnum((unsigned char)src[i]) || src[i] == '_'))
                i++;
            out.push_back({Tok::Ident, std::string(src.substr(s, i - s))});
            continue;
        }
        switch (c) {
        case '+': one(Tok::Plus);   break;
        case '-': one(Tok::Minus);  break;
        case '*': one(Tok::Star);   break;
        case '/': one(Tok::Slash);  break;
        case '^': one(Tok::Caret);  break;
        case '(': one(Tok::LParen); break;
        case ')': one(Tok::RParen); break;
        case '=': one(Tok::Assign); break;
        default:
            throw std::runtime_error("lex: unknown char '" + std::string(1, c) + "'");
        }
    }
    out.push_back({Tok::End, ""});
    return out;
}

// ---------- AST ----------
// One node type: a token plus up to two children.
//   leaf   -> left == right == nullptr
//   unary  -> left set, right == nullptr
//   binary -> both set
// unique_ptr owns the subtree (RAII, move-only) and breaks the recursion.
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    Token token;
    ExprPtr left, right;
    Expr(Token t, ExprPtr l, ExprPtr r)
        : token(std::move(t)), left(std::move(l)), right(std::move(r)) {}
};

static ExprPtr leaf(Token t)                       { return std::make_unique<Expr>(std::move(t), nullptr, nullptr); }
static ExprPtr unary(Token op, ExprPtr x)          { return std::make_unique<Expr>(std::move(op), std::move(x), nullptr); }
static ExprPtr binary(Token op, ExprPtr l, ExprPtr r) { return std::make_unique<Expr>(std::move(op), std::move(l), std::move(r)); }

// ---------- Binding powers (precedence) ----------
// Left binding power of an INFIX operator; 0 means "not infix" (stops the loop).
static int infix_bp(Tok k)
{
    switch (k) {
    case Tok::Assign:           return 10;   // lowest, right-associative
    case Tok::Plus:
    case Tok::Minus:            return 20;
    case Tok::Star:
    case Tok::Slash:            return 30;
    case Tok::Caret:            return 40;   // right-associative
    default:                    return 0;
    }
}
static bool right_assoc(Tok k) { return k == Tok::Caret || k == Tok::Assign; }

// Prefix (unary +/-) binds tighter than * but looser than ^,
// so  -a*b == (-a)*b   while   -a^b == -(a^b).
static constexpr int PREFIX_BP = 35;

// ---------- Parser ----------
struct Parser {
    std::vector<Token> toks;
    std::size_t pos = 0;

    const Token &peek() const { return toks[pos]; }
    Token next()              { return toks[pos++]; }

    // THE engine. min_bp = "only consume operators that bind tighter than this".
    ExprPtr parse_expr(int min_bp)
    {
        ExprPtr left = nud(next());            // prefix / leaf part

        while (true) {
            int lbp = infix_bp(peek().kind);
            if (lbp <= min_bp)                 // operator too weak (or End) -> stop
                break;
            Token op = next();
            int next_min = right_assoc(op.kind) ? lbp - 1 : lbp;
            ExprPtr right = parse_expr(next_min);
            left = binary(std::move(op), std::move(left), std::move(right));
        }
        return left;
    }

    // "null denotation": what a token means with nothing to its left.
    ExprPtr nud(Token t)
    {
        switch (t.kind) {
        case Tok::Number:
        case Tok::Ident:
            return leaf(std::move(t));
        case Tok::Plus:
        case Tok::Minus:                       // unary prefix
            return unary(std::move(t), parse_expr(PREFIX_BP));
        case Tok::LParen: {
            ExprPtr e = parse_expr(0);         // reset precedence inside ( )
            if (peek().kind != Tok::RParen)
                throw std::runtime_error("expected ')'");
            next();
            return e;
        }
        default:
            throw std::runtime_error("expected expression, got '" + t.text + "'");
        }
    }
};

// ---------- Pretty-print as Lisp-style (shows the tree shape) ----------
static std::string show(const ExprPtr &e)
{
    if (!e->left && !e->right)                  // leaf
        return e->token.text;
    if (e->left && !e->right)                   // unary
        return "(" + e->token.text + " " + show(e->left) + ")";
    return "(" + e->token.text + " " + show(e->left) + " " + show(e->right) + ")";
}

int main(int argc, char **argv)
{
    std::vector<std::string> inputs;
    if (argc > 1)
        inputs.emplace_back(argv[1]);
    else
        inputs = {
            "1 + 2 * 3",
            "a = b = c + d",
            "2 ^ 3 ^ 2",
            "-a * b + c",
            "(1 + 2) * 3",
        };

    for (const auto &in : inputs) {
        try {
            Parser p{lex(in)};
            ExprPtr ast = p.parse_expr(0);
            if (p.peek().kind != Tok::End)
                throw std::runtime_error("trailing tokens");
            std::println("{:<16} =>  {}", in, show(ast));
        } catch (const std::exception &ex) {
            std::println("{:<16} =>  ERROR: {}", in, ex.what());
        }
    }
}
