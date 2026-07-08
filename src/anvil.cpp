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
        return Token{it->second, std::move(v)};
    return Token{Type::Ide, std::move(v)};
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
        return Token{it->second, std::move(v)};
    throw std::runtime_error("unknown symbol <" + v + ">");
}

Token Token::eof()
{
    return Token(Type::Eof);
}

std::vector<Token> tokens;
int pos = 0;
std::filesystem::path source_file;
bool DEBUG = false;

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

std::unique_ptr<Node> Node::clone() const
{
    auto n = std::make_unique<Node>(token);
    if(left)  n->left  = left->clone();
    if(right) n->right = right->clone();
    for(auto &c : children) n->children.push_back(c->clone());
    for(auto &[k, v] : on)  n->on[k] = v->clone();
    return n;
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
        if (tokens[pos].type == Type::Lpar)
        {
            next(); // skip Lpar
            do {
                if (tokens[pos].type == Type::Rpar) break;
                node->add_child(expr_node(0));
                while(tokens[pos].type == Type::Coma) next();
            } while(true);
            expect(Type::Rpar);
            node->token.type = Type::Fcall;
        }
        return node;
    }
    case Type::Cmd: {
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        expect(Type::Lpar);
        do {
            if (tokens[pos].type == Type::Rpar) break;
            node->add_child(expr_node(0));
            while(tokens[pos].type == Type::Coma) next();
        } while(true);
        expect(Type::Rpar);
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
        std::unique_ptr<Node> node = expr_node(0);
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
    case Type::Lbra:
    {
        token.type = Type::Arr;
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        while(tokens[pos].type == Type::Coma) next();
        do {
            if (tokens[pos].type == Type::Rbra) break;
            node->add_child(expr_node(0));
            while(tokens[pos].type == Type::Coma) next();
        } while(true);
        expect(Type::Rbra);
        return node;
    }
    case Type::For:
    {
        std::unique_ptr<Node> node = std::make_unique<Node>(std::move(token));
        node->left = std::make_unique<Node>(expect(Type::Ide));
        expect(Type::In);
        node->right = expr_node(0);
        node->add_child(prime_node());
        return node;
    }
    case Type::Fdec:
    {
        std::unique_ptr<Node> node = std::make_unique<Node>(expect(Type::Ide));
        node->token.type = Type::Fdec;
        expect(Type::Lpar);
        do {
            if (tokens[pos].type == Type::Rpar) break;
            node->add_child(std::make_unique<Node>(expect(Type::Ide)));
            while(tokens[pos].type == Type::Coma) next();
        } while(true);
        expect(Type::Rpar);
        node->left = prime_node();
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
            case Type::Assign: op = 10; break;
            case Type::Add:    op = 20; break;
            case Type::Sub:    op = 20; break;
            case Type::Mul:    op = 30; break;
            case Type::Div:    op = 30; break;
            case Type::Dot:    op = 40; break;
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
std::unordered_map<std::string, std::unique_ptr<Node>> funcs;

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

unsigned pool_size = std::thread::hardware_concurrency();
std::unique_ptr<ThreadPool> pool;

ThreadPool &thread_pool()
{
    if(!pool)
        pool = std::make_unique<ThreadPool>(pool_size ? pool_size : 4);
    return *pool;
}

std::vector<std::unique_ptr<Node>> parse_source()
{
    tokens.clear();
    pos = 0;
    tokenize(source_file);
    std::vector<std::unique_ptr<Node>> nodes;
    while(tokens[pos].type != Type::Eof)
        nodes.emplace_back(expr_node(0));
    return nodes;
}

std::unique_ptr<Node> evaluate(const std::unique_ptr<Node> &node);
void run_statement(const std::unique_ptr<Node> &node);
bool is_reloaded = false;

void put(std::FILE *stream, const std::string &color, const std::string &msg)
{
    if(color.empty())
        std::println(stream, "{}", msg);
    else
        std::println(stream, "\033[{}m{}\033[0m", color, msg);
}

struct Captured { int code; std::string out; std::string err; };

Captured capture_run(const std::string &command)
{
    char errpath[] = "/tmp/anvil_err.XXXXXX";
    int fd = mkstemp(errpath);
    std::string full = command + " 2>" + errpath;
    auto pipe = popen(full.c_str(), "r");
    if(!pipe)
    {
        if(fd >= 0) { close(fd); unlink(errpath); }
        return {127, "", "failed to start command"};
    }
    std::string out;
    char buffer[128];
    while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
        out += buffer;
    int status = pclose(pipe);
    std::string err;
    if(fd >= 0)
    {
        std::ifstream ef(errpath);
        err.assign((std::istreambuf_iterator<char>(ef)), std::istreambuf_iterator<char>());
        close(fd);
        unlink(errpath);
    }
    return {WEXITSTATUS(status), out, err};
}

std::unique_ptr<Node> run_command(Node *cmd)
{
    std::vector<std::future<Captured>> futures;
    for(auto &c : cmd->children)
        futures.push_back(thread_pool().enqueue([s = c->token.value]{
            return capture_run(s);
        }));

    bool all_ok = true;
    int code = 0;
    std::string out, err;
    for(auto &f : futures)
    {
        Captured r = f.get();
        if(r.code != 0) { all_ok = false; if(code == 0) code = r.code; }
        out += r.out;
        err += r.err;
    }

    std::unique_ptr<Node> rec = std::make_unique<Node>(Token(Type::Record));
    rec->on["flag"] = std::make_unique<Node>(Token::string(all_ok ? "success" : "error"));
    rec->on["code"] = std::make_unique<Node>(Token::number(std::to_string(code)));
    rec->on["out"] = std::make_unique<Node>(Token::string(out));
    rec->on["err"] = std::make_unique<Node>(Token::string(err));
    return rec;
}

void dispatch_handlers(Node *record, Node *on)
{
    std::string flag = "error";
    if(auto it = record->on.find("flag"); it != record->on.end())
        flag = it->second->token.value;
    std::string key = (flag == "success") ? "success" : "failure";
    for(auto &child : on->children)
    {
        if(child->token.type != Type::Assign) continue;
        if(child->left->token.name == key)
            for(auto &stmt : child->right->children)
                run_statement(stmt);
    }
}

std::unique_ptr<Node> make_record(const std::string &flag, const std::string &out)
{
    std::unique_ptr<Node> rec = std::make_unique<Node>(Token(Type::Record));
    rec->on["flag"] = std::make_unique<Node>(Token::string(flag));
    rec->on["out"] = std::make_unique<Node>(Token::string(out));
    return rec;
}

std::string read_file_str(const std::string &path, bool &ok)
{
    std::ifstream f(path, std::ios::binary);
    if(!f) { ok = false; return ""; }
    ok = true;
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string filter_lines(const std::string &s, const std::vector<std::string> &ignores)
{
    std::string out;
    std::size_t i = 0;
    while(i < s.size())
    {
        std::size_t nl = s.find('\n', i);
        std::size_t end = (nl == std::string::npos) ? s.size() : nl + 1;
        std::string line = s.substr(i, end - i);
        bool skip = false;
        for(auto &pat : ignores)
            if(line.find(pat) != std::string::npos) { skip = true; break; }
        if(!skip) out += line;
        i = end;
    }
    while(!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

std::vector<std::string> walk_dir(const std::string &dir, const std::string &ext,
                                  const std::vector<std::string> &skip)
{
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::none, ec), end;
    for(; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec))
        {
            std::string name = it->path().filename().string();
            for(auto &s : skip)
                if(name == s) { it.disable_recursion_pending(); break; }
            continue;
        }
        if(!it->is_regular_file(ec)) continue;
        std::string path = it->path().string();
        if(!ext.empty() && (path.size() < ext.size() ||
            path.compare(path.size() - ext.size(), ext.size(), ext) != 0))
            continue;
        out.push_back(path);
    }
    return out;
}

std::vector<std::string> split_ws(const std::string &s)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while(i < s.size())
    {
        while(i < s.size() && std::isspace((unsigned char)s[i])) i++;
        std::size_t start = i;
        while(i < s.size() && !std::isspace((unsigned char)s[i])) i++;
        if(i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

std::unique_ptr<Node> evaluate(const std::unique_ptr<Node> &node)
{
    if(DEBUG) std::println("evaluate {}", *node);

    switch(node->token.type)
    {
        case Type::Cmd:
        {
            if(node->children.empty())
                throw std::runtime_error("Cmd expects an array argument");
            std::unique_ptr<Node> result = std::make_unique<Node>(node->token);
            for(auto &child : node->children)
            {
                std::unique_ptr<Node> arr = evaluate(child);
                if(arr->token.type != Type::Arr)
                    throw std::runtime_error("Cmd expects an array");
                if(arr->children.empty())
                    continue;

                bool has_cmd = false, has_str = false;
                for(auto &e : arr->children)
                    (e->token.type == Type::Cmd ? has_cmd : has_str) = true;
                if(has_cmd && has_str)
                    throw std::runtime_error("a Cmd array must be all strings or all commands");

                if(has_cmd)
                {
                    for(auto &e : arr->children)
                        for(auto &command : e->children)
                            result->add_child(command->clone());
                }
                else
                {
                    std::string joined;
                    for(auto &e : arr->children)
                    {
                        if(!joined.empty()) joined += ' ';
                        joined += e->token.value;
                    }
                    result->add_child(std::make_unique<Node>(Token::string(joined)));
                }
            }
            return result;
        }
        case Type::Arr:
        {
            std::unique_ptr<Node> result = std::make_unique<Node>(node->token);
            for(auto &e : node->children)
                result->add_child(evaluate(e));
            return result;
        }
        case Type::Num: return std::make_unique<Node>(node->token);
        case Type::Str: return std::make_unique<Node>(node->token);
        case Type::Add: case Type::Sub: case Type::Mul: case Type::Div:
        {
            std::unique_ptr<Node> l = evaluate(node->left);
            std::unique_ptr<Node> r = evaluate(node->right);
            bool nums = l->token.type == Type::Num && r->token.type == Type::Num;
            if(node->token.type == Type::Add && !nums)
                return std::make_unique<Node>(Token::string(l->token.value + r->token.value));
            if(!nums)
                throw std::runtime_error("arithmetic requires numbers");
            long a = std::stol(l->token.value);
            long b = std::stol(r->token.value);
            long res = node->token.type == Type::Add ? a + b
                     : node->token.type == Type::Sub ? a - b
                     : node->token.type == Type::Mul ? a * b
                     : (b != 0 ? a / b : 0);
            return std::make_unique<Node>(Token::number(std::to_string(res)));
        }
        case Type::Ide:
        {
            auto it = cmds.find(node->token.name);
            if(it == cmds.end())
                throw std::runtime_error("command not found " + node->token.name);
            return it->second->clone();
        }
        case Type::Assign:
        {
            std::string name = node->left->token.name;
            std::unique_ptr<Node> value = evaluate(node->right);
            cmds[name] = std::move(value);
            return std::make_unique<Node>(cmds[name]->token);
        }
        case Type::Fdec:
        {
            funcs[node->token.name] = node->clone();
            return std::make_unique<Node>(node->token);
        }
        case Type::Fcall:
        {
            static const std::unordered_map<std::string, std::pair<std::FILE *, std::string>> logs{
                {"output",  {stdout, ""}},
                {"okput",   {stdout, "32"}},
                {"errput",  {stderr, "31"}},
                {"warnput", {stderr, "33"}},
                {"infoput", {stdout, "36"}},
            };
            if(auto it = logs.find(node->token.name); it != logs.end())
            {
                if(node->children.empty())
                    throw std::runtime_error(node->token.name + " expects an argument");
                std::unique_ptr<Node> arg = evaluate(node->children[0]);
                put(it->second.first, it->second.second, arg->token.value);
                return std::make_unique<Node>(node->token);
            }
            if(node->token.name == "setThreadPool")
            {
                if(node->children.empty())
                    throw std::runtime_error("setThreadPool expects a number");
                std::unique_ptr<Node> arg = evaluate(node->children[0]);
                pool_size = std::stoul(arg->token.value);
                pool.reset();
                return std::make_unique<Node>(node->token);
            };
            if(node->token.name == "setenv")
            {
                std::string name = evaluate(node->children.at(0))->token.value;
                std::string value = evaluate(node->children.at(1))->token.value;
                ::setenv(name.c_str(), value.c_str(), 1);
                return std::make_unique<Node>(node->token);
            };
            if(node->token.name == "enableDebug")
            {
                DEBUG = true;
                return std::make_unique<Node>(node->token);
            };
            if(node->token.name == "walk")
            {
                std::string dir = evaluate(node->children.at(0))->token.value;
                std::string ext = node->children.size() > 1 ? evaluate(node->children[1])->token.value : "";
                std::vector<std::string> skip;
                if(node->children.size() > 2)
                    for(auto &e : evaluate(node->children[2])->children)
                        skip.push_back(e->token.value);
                std::unique_ptr<Node> arr = std::make_unique<Node>(Token(Type::Arr));
                for(auto &p : walk_dir(dir, ext, skip))
                    arr->add_child(std::make_unique<Node>(Token::string(p)));
                return arr;
            }
            if(node->token.name == "read")
            {
                std::string path = evaluate(node->children.at(0))->token.value;
                bool ok = false;
                std::string content = read_file_str(path, ok);
                return make_record(ok ? "success" : "error", content);
            }
            if(node->token.name == "write")
            {
                std::string path = evaluate(node->children.at(0))->token.value;
                std::string content = evaluate(node->children.at(1))->token.value;
                std::ofstream f(path, std::ios::binary);
                f << content;
                return make_record(f ? "success" : "error", "");
            }
            if(node->token.name == "diff")
            {
                std::string a = evaluate(node->children.at(0))->token.value;
                std::string b = evaluate(node->children.at(1))->token.value;
                std::vector<std::string> ignores;
                if(node->children.size() > 2)
                    for(auto &e : evaluate(node->children[2])->children)
                        ignores.push_back(e->token.value);
                bool same = filter_lines(a, ignores) == filter_lines(b, ignores);
                return make_record(same ? "success" : "error", same ? "" : "mismatch");
            }
            if(node->token.name == "replace")
            {
                std::string s = evaluate(node->children.at(0))->token.value;
                std::string from = evaluate(node->children.at(1))->token.value;
                std::string to = evaluate(node->children.at(2))->token.value;
                if(!from.empty())
                    for(std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
                        s.replace(pos, from.size(), to);
                return std::make_unique<Node>(Token::string(s));
            }
            if(auto it = funcs.find(node->token.name); it != funcs.end())
            {
                Node *fn = it->second.get();
                if(node->children.size() != fn->children.size())
                    throw std::runtime_error("wrong number of arguments to " + node->token.name);
                std::vector<std::unique_ptr<Node>> args;
                for(auto &a : node->children)
                    args.push_back(evaluate(a));
                std::vector<std::pair<std::string, std::unique_ptr<Node>>> saved;
                for(std::size_t i = 0; i < fn->children.size(); i++)
                {
                    std::string p = fn->children[i]->token.name;
                    saved.emplace_back(p, cmds.count(p) ? std::move(cmds[p]) : nullptr);
                    cmds[p] = std::move(args[i]);
                }
                for(auto &stmt : fn->left->children)
                    run_statement(stmt);
                for(auto &[p, val] : saved)
                {
                    if(val) cmds[p] = std::move(val);
                    else cmds.erase(p);
                }
                return std::make_unique<Node>(node->token);
            }
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

                if(cmd == "reload")
                {
                    is_reloaded = true;
                    break;
                }
                std::vector<std::string> words = split_ws(cmd);
                if(!words.empty() && funcs.count(words[0]))
                {
                    std::unique_ptr<Node> call = std::make_unique<Node>(Token(Type::Ide, words[0]));
                    call->token.type = Type::Fcall;
                    for(std::size_t i = 1; i < words.size(); i++)
                        call->add_child(std::make_unique<Node>(Token::string(words[i])));
                    evaluate(call);
                }
                else if(auto it = cmds.find(cmd); it != cmds.end() && it->second->token.type == Type::Cmd)
                    run_command(it->second.get());
                else
                {
                    auto output = execute_command(cmd);
                    if (output)
                        std::print("{}", *output);
                }
                std::fflush(nullptr);
            }
            return std::make_unique<Node>(node->token);
        }
        case Type::Dot:
        {
            std::unique_ptr<Node> left = evaluate(node->left);
            Node *right = node->right.get();
            if(right->token.type == Type::Fcall && right->token.name == "on")
            {
                std::unique_ptr<Node> record =
                    left->token.type == Type::Cmd ? run_command(left.get()) : std::move(left);
                dispatch_handlers(record.get(), right);
                return record;
            }
            if(right->token.type == Type::Ide)
            {
                auto it = left->on.find(right->token.name);
                if(it == left->on.end())
                    throw std::runtime_error("no field " + right->token.name);
                return it->second->clone();
            }
            return left;
        }
        case Type::For:
        {
            std::string var = node->left->token.name;
            std::unique_ptr<Node> iter = evaluate(node->right);
            if(iter->token.type != Type::Arr)
                throw std::runtime_error("for expects an array");
            for(auto &element : iter->children)
            {
                cmds[var] = element->clone();
                for(auto &stmt : node->children[0]->children)
                    run_statement(stmt);
            }
            return std::make_unique<Node>(node->token);
        }
        default:
        {
            std::println("{}", node->token);
            throw std::runtime_error("unexpected node <" + node->token.value + ">");
        }
    }
}

void run_statement(const std::unique_ptr<Node> &node)
{
    if(node->token.type == Type::Assign)
    {
        evaluate(node);
        return;
    }
    std::unique_ptr<Node> value = evaluate(node);
    if(value && value->token.type == Type::Cmd)
        run_command(value.get());
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
        throw std::runtime_error("expected 1 argument");
        source_file = std::filesystem::canonical(argv[1]);

        do
        {
            is_reloaded = false;
            cmds.clear();

            std::vector<std::unique_ptr<Node>> nodes = parse_source();

            if(DEBUG)
                for(auto &e: nodes)
                    e->print(0);

            for(auto &e: nodes)
            {
                run_statement(e);
                if(is_reloaded) break;
            }
        } while(is_reloaded);
    }
    catch (std::exception &error)
    {
        std::cerr << error.what() << std::endl;
    }
}