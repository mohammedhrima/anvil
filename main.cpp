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
#include <filesystem>
#include <cstdlib>
#include <readline/readline.h>
#include <unistd.h>
#include <readline/history.h>
#include <expected>

enum class Type
{
    Ide, Num, Cmd, Str, File, Print, Dot, Dots, Coma, Lpar,
    Rpar, Lcbra, Rcbra, Fdec, Fcall, If, Else, Elif, While, 
    Assign, Add, Sub, Mul, Div, Loop, Exec, Bloc, Eof,
};

template <>
struct std::formatter<Type> : std::formatter<std::string_view>
{
    auto format(Type type, format_context &ctx) const
    {
        constexpr std::array<std::string_view, static_cast<int>(Type::Eof) + 1> type_names{
            "Ide", "Num", "Cmd", "Str", "File", "Print", "Dot", "Dots", "Coma", "Lpar",
            "Rpar", "Lcbra", "Rcbra", "Fdec", "Fcall", "If", "Else", "Elif", "While", 
            "Assign", "Add", "Sub", "Mul", "Div", "Loop", "Exec", "Bloc", "Eof"
        };
        return std::format_to(ctx.out(), "{}", type_names.at(std::to_underlying(type)));
    };
};

class Token
{
public:
    Type type;
    std::string name;
    std::string value;
    // int nvalue;

    Token() = default;
    Token(Type);
    Token(Type, std::string);

    static Token string(std::string);
    static Token identifier(std::string);
    static Token symbol(std::string);
    static Token number(std::string);
    static Token eof();
};

template <>
struct std::formatter<Token> : std::formatter<std::string_view>
{
    auto format(const Token &token, format_context &ctx) const
    {
        return std::format_to(ctx.out(), "token {} <{}>", token.type,
                              token.name.length() ? token.name : token.value);
    };
};

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
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token{it->second, std::move(v)};
    return Token{Type::Ide, std::move(v)};
}

Token Token::symbol(std::string v)
{
    static const std::unordered_map<std::string_view, Type> keywords{
        {"=", Type::Assign}, {"(", Type::Lpar}, {")", Type::Rpar},
        {"{", Type::Lcbra}, {"}", Type::Rcbra}, {"+", Type::Add},
        {"-", Type::Sub}, {"*", Type::Mul}, {"/", Type::Div},
        {":", Type::Dots}, {".", Type::Dot}, {",", Type::Coma},
    };
    if (auto it = keywords.find(v); it != keywords.end())
        return Token{it->second, std::move(v)};
    throw std::runtime_error("unknown symbol <" + v + ">");
}

Token Token::eof()
{
    return Token(Type::Eof);
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
        if (std::string_view("=(){}+-/*:.,").contains(content[i]))
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
    for (auto t : tokens)
        std::println("{}", t);
}

//  Parser
class Node
{
public:
    Token token;
    std::unique_ptr<Node> left, right;
    std::vector<std::unique_ptr<Node>> children;
    std::unordered_map<std::string, std::unique_ptr<Node>> on;

    Node() = default;
    Node(Token);
    void print(int);
    void add_child(std::unique_ptr<Node>);
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
    if(!children.empty()) std::println("{:{}}children:", "", space + 3);
    for(auto &e : children) e->print(space + 6);
}

void Node::add_child(std::unique_ptr<Node> child)
{
    children.push_back(std::move(child));
}

std::unique_ptr<Node> expr_node(int min_op);
Token next() { return tokens[pos++]; }
Token expect(Type type)
{
    if(tokens[pos].type != type)
        throw std::runtime_error("unexpected type");
    return next();
}
static constexpr int MAX_OP = 100;

std::unique_ptr<Node> prime_node()
{
    Token token = next();
    switch (token.type)
    {
    case Type::Num: case Type::Str: case Type::Loop:
        return std::make_unique<Node>(std::move(token));
    case Type::Ide: {
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        if (tokens[pos].type == Type::Dots)
        {
            next(); // skip Dots
            switch(tokens[pos].type)
            {
                case Type::Cmd: break;
                default:  std::runtime_error("invalid data type ");
            }
            node->token.type = next().type; // skip data type
        }
        else if (tokens[pos].type == Type::Lpar)
        {
            next(); // skip Dots
            do {
                if (tokens[pos].type == Type::Rpar) break;
                node->add_child(expr_node(MAX_OP));
                while(tokens[pos].type == Type::Coma) next();
            } while(true);
            expect(Type::Rpar);
            node->token.type = Type::Fcall;
        }
        return node;
    }
    case Type::Add: case Type::Sub: case Type::Mul: case Type::Div: case Type::Assign:
    {
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        node->left = expr_node(MAX_OP);
        return node;
    }
    case Type::Lpar:
    {
        // next();
        std::unique_ptr<Node> node =expr_node(0);
        expect(Type::Rpar);
        return node;
    }
    case Type::Lcbra:
    {
        // next();
        token.type = Type::Bloc;
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        do {
            if (tokens[pos].type == Type::Rcbra) break;
            node->add_child(expr_node(0));
            while(tokens[pos].type == Type::Coma) next();
        } while(true);
        expect(Type::Rcbra);
        return node;
    } 
    default:
        throw std::runtime_error("expected expression <" + token.value + ">");
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
            case Type::Dot:     op = 40; break;
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

std::unordered_map<std::string, std::unique_ptr<Node>> cmds;

static std::string get_prompt()
{
    const char *user = getenv("USER");
    std::string username = user ? user : "unknown";
    char host[256];
    std::string hostname = "unkown";
    if (gethostname(host, sizeof(host)) == 0) {
        hostname = host;
    }
    std::string dir = std::filesystem::current_path().filename().string();
    std::string prompt = std::format("(anvil) {}@{} {} > ", username, hostname, dir);
    return prompt;
}

std::expected<std::string, std::string> execute_command(const std::string& command) {
    char buffer[128];
    std::string result;

    auto pipe = popen(command.c_str(), "r");

    if (!pipe) 
        return std::unexpected("Failed to start command process.");

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    int return_code = pclose(pipe);
    if (return_code != 0) {
        return std::unexpected("Command failed during execution.");
    }
    return result;
}

std::unique_ptr<Node> evaluate(const std::unique_ptr<Node> &node)
{
    std::println("evaluate {}", *node);

    switch(node->token.type)
    {
        case Type::Cmd:
        {
            // TODO: check if it already exists
            cmds[node->token.name] = std::make_unique<Node>(node->token);
            std::println("new cmd {}", *cmds[node->token.name]);
            return std::make_unique<Node>(node->token);
        }
        case Type::Str: return std::make_unique<Node>(node->token);
        case Type::Ide:
        {
            auto it = cmds.find(node->token.name);
            if(it == cmds.end())
                throw std::runtime_error("command not found " + node->token.name);
            return std::make_unique<Node>(it->second->token);
        }
        case Type::Assign:
        {
            std::string name = node->left->token.name;
            std::unique_ptr<Node> right = evaluate(node->right);
            std::unique_ptr<Node> &slot = cmds[name];         
            slot = std::make_unique<Node>(node->left->token);  
            slot->token.value = right->token.value;            
            return std::make_unique<Node>(slot->token);
        }
        case Type::Fcall:
        {
            if(node->token.name == "print")
            {
                std::unique_ptr<Node> left = evaluate(node->left);
                std::println("command {}", left->token.value);
                return std::make_unique<Node>(node->token);
            };
            throw std::runtime_error("unknown fcall");
        }
        case Type::Loop:
        {
            std::string prompt = get_prompt();      
            while(1)
            {
                char *line = readline(prompt.data());
                if(!line) break;
                if(*line) add_history(line);
            
                std::string cmd(line);
                free(line);

                cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), [](unsigned char ch){
                    return !std::isspace(ch);
                }));

                cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), [](unsigned char ch){
                    return !std::isspace(ch);
                }).base(), cmd.end());

                if(cmds.contains(cmd))
                {
                    cmd = cmds[cmd]->token.value;
                }
                // else
                {
                    auto output = execute_command(cmd);
                    if (output) {
                        std::print("{}", *output);
                    }
                    std::fflush(nullptr); 
                }
            }
            return std::make_unique<Node>(node->token);
        }
        default:
            throw std::runtime_error("expected expression <" + node->token.value + ">");
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
        throw std::runtime_error("expected 1 argument");
        tokenize(std::filesystem::canonical(argv[1]));

        std::vector<std::unique_ptr<Node>> nodes;
        while(tokens[pos].type != Type::Eof)
            nodes.emplace_back(expr_node(0));
        
        for(auto &e: nodes)
            e->print(0);
        
        // for(auto &e: nodes)
        //     evaluate(e);
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