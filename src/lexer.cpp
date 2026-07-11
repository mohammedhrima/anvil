#include "anvil.hpp"

Token::Token(Type type) : type(type) {};
Token::Token(Type type, std::string text) : type(type)
{
    if (type == Type::Ide)
        name = std::move(text);
    else
        value = std::move(text);
};

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
        {"while", Type::While}, {"loop", Type::Loop},
        {"for", Type::For}, {"in", Type::In},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token(it->second, std::move(v));
    return Token(Type::Ide, std::move(v));
}

Token Token::symbol(std::string v)
{
    static const std::unordered_map<std::string_view, Type> keywords{
        {"=", Type::Assign}, {"(", Type::Lpar}, {")", Type::Rpar},
        {"{", Type::Lcbra}, {"}", Type::Rcbra}, {"+", Type::Add},
        {"-", Type::Sub}, {"*", Type::Mul}, {"/", Type::Div},
        {".", Type::Dot}, {",", Type::Coma},
        {"[", Type::Lbra}, {"]", Type::Rbra},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token(it->second, std::move(v));
    throw std::runtime_error("unknown symbol <" + v + ">");
}

Token Token::eof()
{
    return Token(Type::Eof);
}

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
        if (isdigit(content[i]))
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
        if (std::string_view("=(){}[]+-/*.,").contains(content[i]))
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
    tokens.emplace_back(Token::eof());
    if(!DEBUG) return;
    for (auto t : tokens)
        std::println("{}", t);
}
