#!/bin/sh
# programs.sh BUILD_DIR -- run every example program end to end.
#
# For each programs/*.s: assemble with t13as, run on t13sim (must HALT),
# and check the result word with t13view --peek against the value below.
# For each programs/*.c: compile it natively with $CC, run it, and check
# that what it prints (NAME=value) equals what t13sim left in memory.
# That is the check that the hand translation C -> T13 assembly is right.
# POSIX sh only. Run from the repo root.

B=${1:-build}
CC=${CC:-cc}
OUT=$B/programs
fail=0
mkdir -p "$OUT"

# expect NAME EXPR=VALUE STEPS: expected result of programs/NAME.s
expect() {
    name=$1 want=$2 steps=$3
    src=programs/$name.s
    if ! "$B/t13as" "$src" -o "$OUT/$name.img" -l "$OUT/$name.lst"; then
        echo "FAIL $name: t13as failed"; fail=1; return
    fi
    if ! "$B/t13sim" "$OUT/$name.img" -o "$OUT/$name.trc" >"$OUT/$name.simout" 2>&1; then
        echo "FAIL $name: t13sim did not halt"; cat "$OUT/$name.simout"; fail=1; return
    fi
    tail -n 1 "$OUT/$name.trc" | grep -q "^END HALT .* STEPS=$steps\$" ||
        { echo "FAIL $name: want $steps steps, got: $(tail -n 1 "$OUT/$name.trc")"; fail=1; }
    got=$("$B/t13view" "$OUT/$name.trc" --peek "${want%%=*}")
    [ "$got" = "$want" ] || { echo "FAIL $name: t13sim left $got, want $want"; fail=1; }
    echo "ok   $name.s: $got after $steps steps"

    # The C original, if there is one, must print the same thing natively.
    if [ -f "programs/$name.c" ]; then
        if ! $CC -std=c99 -Wall -Wextra -Wpedantic -Werror -O2 "programs/$name.c" -o "$OUT/$name.native"; then
            echo "FAIL $name.c: native build failed"; fail=1; return
        fi
        native=$("$OUT/$name.native")
        [ "$native" = "$got" ] || { echo "FAIL $name.c: native prints $native, t13sim left $got"; fail=1; }
        echo "ok   $name.c: native $CC prints $native"
    fi
}

expect power   result=64       17
expect fact    result=40320    57
expect recfact '*result=120'   112

# Every program in programs/ must be covered above.
for s in programs/*.s; do
    n=$(basename "$s" .s)
    grep -q "^expect $n " "$0" || { echo "FAIL $s: no expect line in tests/programs.sh"; fail=1; }
done

if [ "$fail" -eq 0 ]; then
    echo "programs: all passed"
fi
exit "$fail"
