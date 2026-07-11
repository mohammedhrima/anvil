#include "anvil.hpp"

std::string get_prompt()
{
    const char *user = getenv("USER");
    std::string username = user ? user : "unknown";
    char host[256];
    std::string hostname = "unkown";
    if (gethostname(host, sizeof(host)) == 0) {
        hostname = host;
    }
    std::string dir = std::filesystem::current_path().filename().string();
    std::string prompt = std::format("\001\033[32m\002(anvil)\001\033[0m\002 {}@{} {} > ", username, hostname, dir);
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

class Captured
{
public:
    int code;
    std::string out;
    std::string err;
};

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
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return {code, out, err};
}

std::unique_ptr<Node> run_command(Node *cmd)
{
    std::vector<Captured> results(cmd->children.size());
    std::vector<std::thread> threads;
    for(std::size_t i = 0; i < cmd->children.size(); i++)
        threads.emplace_back([&results, cmd, i]{
            results[i] = capture_run(cmd->children[i]->token.value);
        });
    for(auto &t : threads)
        t.join();

    bool all_ok = true;
    int code = 0;
    std::string out, err;
    for(auto &r : results)
    {
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
