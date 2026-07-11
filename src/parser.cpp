#include "anvil.hpp"

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
