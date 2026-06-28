#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

enum class Type {
    Id, Cmd, String, File, DOT, DOTS,
    LPAR, RPAR, LBAR, RBRA, FDEC, FCALL,
    If, Else, Elif, While, Assign, Add, Sub, Mul, Div,
};

template <>
struct std::formatter<Type> : std::formatter<string_view> {
    auto format(Type type, format_context &ctx) const
    {
        constexpr std::array<std::string_view, 21> type_names {
             "Id", "Cmd", "String", "File", "DOT", "DOTS", "LPAR", "RPAR", "LBAR",
            "RBRA", "FDEC", "FCALL", "If", "Else", "Elif", "While", "Assign",
            "Add", "Sub", "Mul", "Div",
        };
        return std::format_to(ctx.out(), "{}", type_names.at(std::to_underlying(type)));
    };
};

class Token
{
  public:
    Type type;
    std::string value;

    Token() = default;
    Token(Type);
    Token(Type, std::string);

    static Token string(std::string);
    static Token identifier(std::string);
    static Token symbol(std::string);
};

template <>
struct std::formatter<Token> : std::formatter<string_view> {
    auto format(const Token &token, format_context &ctx) const
    {
        return std::format_to(ctx.out(), "token {} value: <{}>", token.type, token.value);
    };
};

Token::Token(Type type) : type(type) {};
Token::Token(Type type, std::string value) : type(type), value(std::move(value)) {};

Token Token::string(std::string v)
{
    return Token(Type::String, std::move(v));
}

Token Token::identifier(std::string v)
{
    static const std::unordered_map<std::string_view, Type> keywords{
        {"Cmd", Type::Cmd}, {"fn", Type::FDEC},
        {"File", Type::File}, {"if", Type::If},
        {"else", Type::Else}, {"elif", Type::Elif},
        {"while", Type::While},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token{it->second, std::move(v)};
    return Token{Type::Id, std::move(v)};
}

Token Token::symbol(std::string v)
{
    static const std::unordered_map<std::string_view, Type> keywords{
        {"=", Type::Assign}, {"(", Type::LPAR}, {")", Type::RPAR},
        {"{", Type::LBAR}, {"}", Type::RBRA}, {"+", Type::Add}, 
        {"-", Type::Sub}, {"*", Type::Mul}, {"/", Type::Div},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token{it->second, std::move(v)};
    throw std::runtime_error("unknown symbol <" + v + ">");
}

std::vector<Token> tokenize(const std::filesystem::path &path)
{
    std::filesystem::path filepath{path};
    std::ifstream file(filepath);
    std::vector<Token> tokens;

    if (!file.is_open())
        throw std::runtime_error("Failed to open " + filepath.string());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i < content.length();) {
        if (std::isspace((unsigned char)content[i]))
        {
            i++;
            continue;
        }
        if (isalpha(content[i]) || content[i] == '_') {
            auto s = i;
            while (isalnum(content[i]) || content[i] == '_')
                i++;
            tokens.emplace_back(Token::identifier(content.substr(s, i - s)));
            continue;
        }
        if (content[i] == '\"') {
            auto s = i++;
            while (i < content.length() && content[i] != content[s])
                i++;
            if (i == content.length() || content[i] != content[s])
                throw std::runtime_error("unterminated string literal");
            tokens.emplace_back(Token::string(content.substr(s + 1, i - s - 1)));
            i++;
            continue;
        }
        if (std::string_view("=(){}+-/*").contains(content[i]))
        {
            tokens.emplace_back(Token::symbol(content.substr(i, 1)));
            i++;
            continue;
        }
        if (content[i] == '#')
        {
            while (content[i] != '\n') i++;
            continue;
        }
        throw std::runtime_error("unkonwn character <" + std::to_string(content[i]) + ">");
    }
    for (auto t : tokens)
        std::println("{}", t);
    return tokens;
}

//  Parser
class Node {
public:
    Token token;
    std::unique_ptr<Node> left, right;

    Node(Token token);
};

Node expr()
{
    // auto left = 
}

Node::Node(Token token) : token(std::move(token)) {}

int main(int argc, char **argv)
{
    try {
        if (argc != 2)
            throw std::runtime_error("expected 1 argument");
        tokenize(std::filesystem::canonical(argv[1]));

    } catch (std::exception &error) {
        std::cerr << error.what() << std::endl;
    }
}