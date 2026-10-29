#!/bin/sh
# golden.sh BUILD_DIR -- end-to-end tests of the t13as, t13sim and t13view executables.
#
# Assembles each golden program and diffs the image and listing against
# tests/golden/, then checks the error path and the usage exit codes.
# Then runs each image on t13sim and diffs the trace against tests/golden/,
# and checks the t13view command line on those traces.
# POSIX sh only (runs under busybox on Alpine too). Run from the repo root.

B=${1:-build}
T=$B/t13as
S=$B/t13sim
V=$B/t13view
OUT=$B/golden
fail=0
mkdir -p "$OUT"

# check NAME SOURCE: outputs must equal tests/golden/NAME.img and NAME.lst
check() {
    if ! "$T" "$2" -o "$OUT/$1.img" -l "$OUT/$1.lst" 2>"$OUT/$1.err"; then
        echo "FAIL $1: t13as exited non-zero"
        cat "$OUT/$1.err"
        fail=1
        return
    fi
    for ext in img lst; do
        if ! diff -u "tests/golden/$1.$ext" "$OUT/$1.$ext"; then
            echo "FAIL $1: $ext differs from tests/golden/$1.$ext"
            fail=1
        fi
    done
}

check halt tests/golden/halt.s
check power programs/power.s
check callret tests/golden/callret.s

# Error path: exit 1, message on stderr, no image written.
rm -f "$OUT/bad_undef.img"
"$T" tests/golden/bad_undef.s -o "$OUT/bad_undef.img" 2>"$OUT/bad_undef.err"
st=$?
[ "$st" -eq 1 ] || { echo "FAIL bad_undef: exit $st, want 1"; fail=1; }
grep -q "tests/golden/bad_undef.s:4: error: undefined symbol 'lop'" "$OUT/bad_undef.err" ||
    { echo "FAIL bad_undef: expected message not on stderr"; cat "$OUT/bad_undef.err"; fail=1; }
[ ! -e "$OUT/bad_undef.img" ] || { echo "FAIL bad_undef: image written despite errors"; fail=1; }

# Usage and unreadable input: exit 2.
"$T" >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL usage: exit $st, want 2"; fail=1; }
"$T" tests/golden/no_such_file.s -o "$OUT/none.img" >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL missing source: exit $st, want 2"; fail=1; }
"$T" a.s b.s >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL two sources: exit $st, want 2"; fail=1; }

# ---- t13sim ----------------------------------------------------------------

# simcheck NAME: run the image just assembled; trace must equal tests/golden/NAME.trc
simcheck() {
    "$S" "$OUT/$1.img" -o "$OUT/$1.trc" >"$OUT/$1.simout" 2>&1
    st=$?
    [ "$st" -eq 0 ] || { echo "FAIL sim $1: exit $st, want 0"; cat "$OUT/$1.simout"; fail=1; }
    if ! diff -u "tests/golden/$1.trc" "$OUT/$1.trc"; then
        echo "FAIL sim $1: trace differs from tests/golden/$1.trc"
        fail=1
    fi
}

simcheck halt
simcheck power
simcheck callret

# Default output name: x.img -> x.trc; the summary line goes to stdout.
cp "$OUT/halt.img" "$OUT/dflt.img"
rm -f "$OUT/dflt.trc"
"$S" "$OUT/dflt.img" >"$OUT/dflt.out" 2>&1
cmp -s "$OUT/dflt.trc" tests/golden/halt.trc || { echo "FAIL sim default output name"; fail=1; }
grep -q "HALT after 1 steps, PC=0002" "$OUT/dflt.out" ||
    { echo "FAIL sim summary line"; cat "$OUT/dflt.out"; fail=1; }

# A runtime error: exit 1, the trace is still written and ends with END ERROR.
printf 'T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000: 5100 0000\n' >"$OUT/div0.img"
"$S" "$OUT/div0.img" -o "$OUT/div0.trc" >"$OUT/div0.out" 2>&1
st=$?
[ "$st" -eq 1 ] || { echo "FAIL sim div0: exit $st, want 1"; fail=1; }
tail -n 1 "$OUT/div0.trc" | grep -qx "END ERROR PC=0000 STEPS=0 divide by zero" ||
    { echo "FAIL sim div0: last line"; tail -n 1 "$OUT/div0.trc"; fail=1; }

# Step limit: exit 1 and END LIMIT.
printf 'T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0000: B000 0000\n' >"$OUT/spin.img"
"$S" "$OUT/spin.img" -o "$OUT/spin.trc" -n 5 >/dev/null 2>&1
st=$?
[ "$st" -eq 1 ] || { echo "FAIL sim limit: exit $st, want 1"; fail=1; }
tail -n 1 "$OUT/spin.trc" | grep -qx "END LIMIT PC=0000 STEPS=5" ||
    { echo "FAIL sim limit: last line"; tail -n 1 "$OUT/spin.trc"; fail=1; }

# A bad image: exit 2, path:line message, no trace.
printf 'T13IMG 1\nENTRY 0000\nSEG TEXT 0000\n0001: 0000\n' >"$OUT/badimg.img"
rm -f "$OUT/badimg.trc"
"$S" "$OUT/badimg.img" -o "$OUT/badimg.trc" 2>"$OUT/badimg.err"
st=$?
[ "$st" -eq 2 ] || { echo "FAIL sim bad image: exit $st, want 2"; fail=1; }
grep -q "badimg.img:4: error: odd address 0x0001" "$OUT/badimg.err" ||
    { echo "FAIL sim bad image: message"; cat "$OUT/badimg.err"; fail=1; }
[ ! -e "$OUT/badimg.trc" ] || { echo "FAIL sim bad image: trace written"; fail=1; }

# Usage errors: exit 2.
"$S" >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL sim usage: exit $st, want 2"; fail=1; }
"$S" "$OUT/halt.img" -n abc >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL sim bad -n: exit $st, want 2"; fail=1; }

# ---- t13view ---------------------------------------------------------------

# Every view runs on every golden trace, in text and HTML, and exits 0.
for n in halt power callret; do
    for v in --summary --waterfall --grid --blocks --loaded --segments; do
        for h in "" --html; do
            "$V" "tests/golden/$n.trc" $v $h --name "$n.s" >"$OUT/view.out" 2>&1 ||
                { echo "FAIL view $n $v $h: exit $?"; cat "$OUT/view.out"; fail=1; }
        done
    done
done
[ "$("$V" tests/golden/power.trc --peek result)" = "result=64" ] || { echo "FAIL view --peek"; fail=1; }
"$V" tests/golden/power.trc --peek nope >/dev/null 2>&1
st=$?
[ "$st" -eq 1 ] || { echo "FAIL view --peek unknown symbol: exit $st, want 1"; fail=1; }
"$V" tests/golden/power.trc -o "$OUT/power.summary" && grep -q "END HALT after 17 steps" "$OUT/power.summary" ||
    { echo "FAIL view -o"; fail=1; }

# A malformed trace: exit 2 with path:line.
sed 's/^STEP 2 /STEP 3 /' tests/golden/power.trc >"$OUT/skip.trc"
"$V" "$OUT/skip.trc" 2>"$OUT/skip.err"
st=$?
[ "$st" -eq 2 ] || { echo "FAIL view bad trace: exit $st, want 2"; fail=1; }
grep -q "skip.trc:15: error: STEP 3 after STEP 1" "$OUT/skip.err" ||
    { echo "FAIL view bad trace: message"; cat "$OUT/skip.err"; fail=1; }
"$V" >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL view usage: exit $st, want 2"; fail=1; }
"$V" tests/golden/power.trc --grid --loaded >/dev/null 2>&1
st=$?
[ "$st" -eq 2 ] || { echo "FAIL view two views: exit $st, want 2"; fail=1; }

if [ "$fail" -eq 0 ]; then
    echo "golden: all passed"
fi
exit "$fail"
