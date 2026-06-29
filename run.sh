
cmd="clang++ "
san="-fsanitize=null -g3 "
flags="-std=c++23 -Wimplicit-fallthrough "
readline="-I/opt/homebrew/opt/readline/include -L/opt/homebrew/opt/readline/lib -lreadline "
export MACOSX_DEPLOYMENT_TARGET=$(sw_vers -productVersion)

rm -rf a.out && $cmd main.cpp $san $flags $readline && ./a.out anvil.an

