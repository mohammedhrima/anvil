#!/usr/bin/env bash
set -uo pipefail

cd "$(dirname "$0")/.."

ulimit -s 8192 2>/dev/null || true

case "$(uname -s)" in
    Linux)  OS=linux ;;
    Darwin) OS=mac ;;
    *)      OS=unknown ;;
esac
ANVIL=${ANVIL:-build/$OS/anvil}

if [ ! -x "$ANVIL" ]; then
    echo "no binary at $ANVIL -- run 'make' first" >&2
    exit 2
fi

CASES=tests/cases
GOLDEN=tests/golden
mkdir -p "$GOLDEN"

DEEP=$CASES/06_deep_expression.an
PARENS=$CASES/13_deep_parens.an
{ printf 'x = 1'; for _ in $(seq 1 19999); do printf '+1'; done; printf '\n'; } > "$DEEP"
{ printf 'x = '; for _ in $(seq 1 20000); do printf '('; done; printf '1';
                 for _ in $(seq 1 20000); do printf ')'; done; printf '\n'; } > "$PARENS"
trap 'rm -f "$DEEP" "$PARENS"' EXIT

normalize() {
    LC_ALL=C sed -E -e 's/\x1b\[[0-9;]*m//g' \
                    -e 's/\(anvil\) [^>]*> /(anvil) PROMPT> /g' \
                    -e 's/PROMPT> +/PROMPT> /g' \
                    -e 's/[[:space:]]+$//' \
                    -e 's#/tmp/anvil_err\.[A-Za-z0-9]+#/tmp/anvil_err.XXXXXX#g' \
                    -e 's/^.*(Segmentation fault|Bus error|Abort trap|Killed).*$/<killed by signal: \1>/'
}

pass=0 fail=0 updated=0

for case_file in "$CASES"/*.an; do
    name=$(basename "$case_file" .an)
    stdin_file="$CASES/$name.stdin"
    [ -f "$stdin_file" ] || stdin_file=/dev/null

    actual=$( { "$ANVIL" "$case_file" < "$stdin_file"; echo "__EXIT__=$?"; } 2>&1 | normalize )

    golden_file="$GOLDEN/$name.txt"

    if [ -n "${UPDATE:-}" ]; then
        printf '%s\n' "$actual" > "$golden_file"
        echo "updated  $name"
        updated=$((updated + 1))
        continue
    fi

    if [ ! -f "$golden_file" ]; then
        echo "MISSING GOLDEN  $name  (run UPDATE=1 tests/run.sh)" >&2
        fail=$((fail + 1))
        continue
    fi

    if diff -u "$golden_file" <(printf '%s\n' "$actual") > /tmp/anvil_diff.$$ 2>&1; then
        echo "ok       $name"
        pass=$((pass + 1))
    else
        echo "FAIL     $name"
        sed 's/^/         /' /tmp/anvil_diff.$$
        fail=$((fail + 1))
    fi
    rm -f /tmp/anvil_diff.$$
done

echo
if [ -n "${UPDATE:-}" ]; then
    echo "$updated golden file(s) written"
    exit 0
fi
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
