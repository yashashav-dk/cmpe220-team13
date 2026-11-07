#!/bin/sh
# golden.sh BUILD_DIR -- end-to-end tests of the t13as executable.
#
# Assembles each golden program and diffs the image and listing against
# tests/golden/, then checks the error path and the usage exit codes.
# POSIX sh only (runs under busybox on Alpine too). Run from the repo root.

B=${1:-build}
T=$B/t13as
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

if [ "$fail" -eq 0 ]; then
    echo "golden: all passed"
fi
exit "$fail"
