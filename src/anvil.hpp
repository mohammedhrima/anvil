#pragma once

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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <queue>
#include <sys/wait.h>

enum class Type
{
    Ide, Num, Cmd, Str, File, Print, Dot, Coma, Lpar,
    Rpar, Lcbra, Rcbra, Fdec, Fcall, If, Else, Elif, While,
    Assign, Add, Sub, Mul, Div, Loop, Exec, Bloc,
    Arr, Lbra, Rbra, For, In, Record, Eof,
};

template <>
struct std::formatter<Type> : std::formatter<std::string_view>
{
    auto format(Type type, format_context &ctx) const
    {
        constexpr std::array<std::string_view, static_cast<int>(Type::Eof) + 1> type_names{
            "Ide", "Num", "Cmd", "Str", "File", "Print", "Dot", "Coma", "Lpar",
            "Rpar", "Lcbra", "Rcbra", "Fdec", "Fcall", "If", "Else", "Elif", "While",
            "Assign", "Add", "Sub", "Mul", "Div", "Loop", "Exec", "Bloc",
            "Arr", "Lbra", "Rbra", "For", "In", "Record", "Eof"
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
    std::unique_ptr<Node> clone() const;
};

template<>
struct std::formatter<Node> : std::formatter<std::string_view>
{
    auto format(const Node &node, format_context &ctx) const
    {
        return std::format_to(ctx.out(), "node {}", node.token);
    }
};

class ThreadPool
{
private:
    void worker()
    {
        while(true)
        {
            std::function<void()> job;
            {
                std::unique_lock lock(mtx);
                cv.wait(lock, [this]{ return stop || !jobs.empty(); });
                if(stop && jobs.empty()) return;
                job = std::move(jobs.front());
                jobs.pop();
            }
            job();
        }
    }

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> jobs;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop = false;

public:
    ThreadPool(std::size_t n)
    {
        for(std::size_t i = 0; i < n; i++)
            workers.emplace_back([this]{ worker(); });
    }
    ~ThreadPool()
    {
        {
            std::unique_lock lock(mtx);
            stop = true;
        }
        cv.notify_all();
        for(auto &w : workers) w.join();
    }

    template<class F>
    std::future<std::invoke_result_t<F>> enqueue(F f)
    {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(f));
        std::future<R> fut = task->get_future();
        {
            std::unique_lock lock(mtx);
            jobs.push([task]{ (*task)(); });
        }
        cv.notify_one();
        return fut;
    }
};