#include "anvil.hpp"

void put(std::FILE *stream, const std::string &color, const std::string &msg)
{
    if(color.empty())
        std::println(stream, "{}", msg);
    else
        std::println(stream, "\033[{}m{}\033[0m", color, msg);
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

std::vector<std::string> split_string(const std::string &s)
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
            if(node->left->token.type != Type::Ide)
                throw std::runtime_error("left side of '=' must be a name");
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
                std::vector<std::string> words = split_string(cmd);
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
