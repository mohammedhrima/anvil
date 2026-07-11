#include "anvil.hpp"

std::vector<Token> tokens;
int pos = 0;
std::filesystem::path source_file;
bool DEBUG = false;

std::unordered_map<std::string, std::unique_ptr<Node>> cmds;
std::unordered_map<std::string, std::unique_ptr<Node>> funcs;

bool is_reloaded = false;

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
        return 1;
    }
}