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
#include <functional>

enum class Type
{
    Ide, Num, Cmd, Str, File, Dot, Dots, Lpar,
    Rpar, Lbra, Rbra, Fdec, Fcall, If, Else, Elif,
    While, Assign, Add, Sub, Mul, Div,
};

template <>
struct std::formatter<Type> : std::formatter<std::string_view>
{
    auto format(Type type, format_context &ctx) const
    {
        constexpr std::array<std::string_view, 22> type_names{
            "Ide", "Num", "Cmd", "Str", "File", "Dot", "Dots", "Lpar",
            "Rpar", "Lbra", "Rbra", "Fdec", "Fcall", "If", "Else",
            "Elif", "While", "Assign", "Add", "Sub", "Mul", "Div",
        };
        return std::format_to(ctx.out(), "{}", type_names.at(std::to_underlying(type)));
    };
};

class Token
{
public:
    Type type;
    std::string value;
    // int nvalue;

    Token() = default;
    Token(Type);
    Token(Type, std::string);

    static Token string(std::string);
    static Token identifier(std::string);
    static Token symbol(std::string);
    static Token number(std::string);
};

template <>
struct std::formatter<Token> : std::formatter<std::string_view>
{
    auto format(const Token &token, format_context &ctx) const
    {
        return std::format_to(ctx.out(), "token {} value: <{}>", token.type, token.value);
    };
};

Token::Token(Type type) : type(type) {};
Token::Token(Type type, std::string value) : type(type), value(std::move(value)) {};

Token Token::string(std::string v)
{
    return Token(Type::Str, std::move(v));
}

Token Token::number(std::string v)
{
    return Token(Type::Num, std::move(v));
}

Token Token::identifier(std::string v)
{
    static const std::unordered_map<std::string_view, Type> keywords{
        {"Cmd", Type::Cmd}, {"fn", Type::Fdec}, {"File", Type::File},
        {"if", Type::If}, {"else", Type::Else}, {"elif", Type::Elif},
        {"while", Type::While},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token{it->second, std::move(v)};
    return Token{Type::Ide, std::move(v)};
}

Token Token::symbol(std::string v)
{
    static const std::unordered_map<std::string_view, Type> keywords{
        {"=", Type::Assign}, {"(", Type::Lpar}, {")", Type::Rpar},
        {"{", Type::Lbra}, {"}", Type::Rbra}, {"+", Type::Add},
        {"-", Type::Sub}, {"*", Type::Mul}, {"/", Type::Div},
        {":", Type::Dots},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token{it->second, std::move(v)};
    throw std::runtime_error("unknown symbol <" + v + ">");
}

std::vector<Token> tokens;
int pos = 0;
void tokenize(const std::filesystem::path &path)
{
    std::filesystem::path filepath{path};
    std::ifstream file(filepath);

    if (!file.is_open())
        throw std::runtime_error("Failed to open " + filepath.string());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i < content.length();)
    {
        if (std::isspace((unsigned char)content[i]))
        {
            i++;
            continue;
        }
        if (isalpha(content[i]) || content[i] == '_')
        {
            auto s = i;
            while (isalnum(content[i]) || content[i] == '_')
                i++;
            tokens.emplace_back(Token::identifier(content.substr(s, i - s)));
            continue;
        }
        if(isdigit(content[i]))
        {
             auto s = i;
            while (isdigit(content[i]))
                i++;
            tokens.emplace_back(Token::number(content.substr(s, i - s)));
            continue;
        }
        if (content[i] == '\"')
        {
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
            while (i < content.length() && content[i] != '\n')
                i++;
            continue;
        }
        throw std::runtime_error("unkonwn character <" + std::string(1, content[i]) + ">");
    }
    for (auto t : tokens)
        std::println("{}", t);
}

//  Parser
class Node
{
public:
    Token token;
    std::unique_ptr<Node> left, right;

    Node() = default;
    Node(Token);
    void print(int);
};

template<>
struct std::formatter<Node> : std::formatter<std::string_view>
{
    auto format(const Node &node, format_context &ctx) const
    {
        return std::format_to(ctx.out(), "node {}", node.token);
    }
};

Node::Node(Token token) : token(std::move(token)) {}

void Node::print(int space)
{
    std::println("{:{}}{}", "", space, *this);
    if(left != nullptr) left->print(space + 6);
    if(right != nullptr) right->print(space + 6);
}

std::unique_ptr<Node> expr_node(int min_op);
Token next() { return tokens[pos++]; }
static constexpr int MAX_OP = 50;

std::unique_ptr<Node> prime_node()
{
    Token token = next();
    switch (token.type)
    {
    case Type::Num:  return std::make_unique<Node>(std::move(token));
    case Type::Ide: {
        if(tokens[pos].type == Type::Dots)
        {
            next(); // skip Dots
            
        }
        return std::make_unique<Node>(std::move(token));
    }
    case Type::Add: case Type::Sub: case Type::Mul: case Type::Div:
    {
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        node->left = expr_node(MAX_OP);
        return node;
    }
    default:
        throw std::runtime_error("expected expression go " + token.value);
    }
}

std::unique_ptr<Node> expr_node(int min_op)
{
    std::unique_ptr<Node> left = prime_node(); // { token : 1 }

    while (true)
    {
        int op = 0;
        switch(tokens[pos].type)
        {
            case Type::Assign:  op = 10; break;
            case Type::Add:     op = 20; break;
            case Type::Sub:     op = 20; break;
            case Type::Mul:     op = 30; break;
            case Type::Div:     op = 30; break;
            default: break;
        };
        if(op <= min_op) break;
        Token token = next();
        std::unique_ptr node = std::make_unique<Node>(std::move(token));
        node->left = std::move(left);
        node->right = expr_node(op);
        left = std::move(node);
    }
    return left;
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
        throw std::runtime_error("expected 1 argument");
        tokenize(std::filesystem::canonical(argv[1]));
        std::unique_ptr<Node> ast = expr_node(0);
        ast->print(0);
        // 1 + 2 * 3 + 3 + 4
        // std::function<int(const std::unique_ptr<Node>&)> evaluate = [&](const std::unique_ptr<Node> &node){
        //     switch(node->token.type){
        //     case Type::Num: return std::stoi(node->token.value);
        //     case Type::Add: return evaluate(node->left) + evaluate(node->right);
        //     case Type::Sub: return evaluate(node->left) - evaluate(node->right);
        //     case Type::Mul: return evaluate(node->left) * evaluate(node->right);
        //     case Type::Div: return evaluate(node->left) / evaluate(node->right);
        //     default: throw std::runtime_error("invalid op");    
        //     };
        // };
        // int e = evaluate(ast);
        // std::println("result: {}", e);

    }
    catch (std::exception &error)
    {
        std::cerr << error.what() << std::endl;
    }
}