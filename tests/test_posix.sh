#!/bin/sh
#
# POSIX Utility Syntax Conformance Tests for dshuf (IEEE Std 1003.1 Guidelines 1-14)
#

DSHUF="./dshuf"
PASSED=0
FAILED=0

tmp1="posix_test_1.$$.tmp"
tmp2="posix_test_2.$$.tmp"
trap 'rm -f "$tmp1" "$tmp2"' EXIT INT TERM

run_test() {
	desc="$1"
	cmd="$2"
	expected_status="$3"
	expected_output="$4"

	actual_output=$(eval "$cmd" 2>/dev/null)
	status=$?
	actual_output=$(printf '%s' "$actual_output" | tr -d '\r')
	expected_output=$(printf '%s' "$expected_output" | tr -d '\r')

	if [ "$status" -ne "$expected_status" ]; then
		printf "[FAIL] %s (expected exit status %d, got %d)\n" "$desc" "$expected_status" "$status"
		FAILED=$((FAILED + 1))
		return
	fi

	if [ -n "$expected_output" ] && [ "$actual_output" != "$expected_output" ]; then
		printf "[FAIL] %s\n  Expected:\n%s\n  Got:\n%s\n" "$desc" "$expected_output" "$actual_output"
		FAILED=$((FAILED + 1))
		return
	fi

	printf "[PASS] %s\n" "$desc"
	PASSED=$((PASSED + 1))
}

printf "========================================\n"
printf "Running POSIX Conformance Tests\n"
printf "========================================\n"

# Guideline 3, 4, 5: Combined single-letter flags
run_test "Combined flags (-Sz)" \
	'printf "a\0b\0c\0" | '"$DSHUF"' -Sz -w4 -s42 | tr "\0" " "' \
	0 \
	"$(printf 'a\0b\0c\0' | tr '\0' ' ')"

# Guideline 5, 6, 7: Attached vs detached option-arguments
run_test "Attached option-argument for -n (-n2)" \
	'printf "line1\nline2\nline3\n" | '"$DSHUF"' -n2 | wc -l' \
	0 \
	"2"

run_test "Detached option-argument for -n (-n 2)" \
	'printf "line1\nline2\nline3\n" | '"$DSHUF"' -n 2 | wc -l' \
	0 \
	"2"

run_test "Attached option-argument for -w (-w16)" \
	'printf "A\t1\nB\t2\n" | '"$DSHUF"' -w16 -k1 | wc -l' \
	0 \
	"2"

run_test "Attached option-argument for -s (-s1337)" \
	'printf "first\nsecond\n" | '"$DSHUF"' -s1337' \
	0 \
	"$(printf 'first\nsecond\n' | $DSHUF -s 1337)"

run_test "Attached option-argument for -j (-j0.05)" \
	'printf "first\nsecond\n" | '"$DSHUF"' -j0.05 -s42' \
	0 \
	"$(printf 'first\nsecond\n' | $DSHUF -j 0.05 -s42)"

run_test "Attached option-argument for -k (-k2)" \
	'printf "1\tA\n2\tB\n" | '"$DSHUF"' -k2 -s42' \
	0 \
	"$(printf '1\tA\n2\tB\n' | $DSHUF -k 2 -s42)"

run_test "Combined flag with attached argument (-Sn2)" \
	'printf "line1\nline2\nline3\n" | '"$DSHUF"' -Sn2 | wc -l' \
	0 \
	"2"

# Guideline 10: End-of-options delimiter (--)
printf "alpha entry\n" > "$tmp1"
run_test "End of options (--) for file operand" \
	''"$DSHUF"' -- '"$tmp1"'' \
	0 \
	"alpha entry"

# Guideline 13: '-' designates standard input
printf "file1 alpha\n" > "$tmp1"
printf "file2 beta\n" > "$tmp2"

run_test "Interleaved stdin '-' between files" \
	'printf "stdin gamma\n" | '"$DSHUF"' -s42 '"$tmp1"' - '"$tmp2"' | wc -l' \
	0 \
	"3"

run_test "Stdin only via '-'" \
	'printf "single line\n" | '"$DSHUF"' -' \
	0 \
	"single line"

# Option argument validation & exit status
run_test "Missing option argument for -k exits 2" \
	''"$DSHUF"' -k' \
	2 \
	""

run_test "Missing option argument for -n exits 2" \
	''"$DSHUF"' -n' \
	2 \
	""

run_test "Missing option argument for -w exits 2" \
	''"$DSHUF"' -w' \
	2 \
	""

run_test "Missing option argument for -s exits 2" \
	''"$DSHUF"' -s' \
	2 \
	""

run_test "Invalid unknown option exits 2" \
	''"$DSHUF"' -X' \
	2 \
	""

run_test "Non-existent input file exits 2" \
	''"$DSHUF"' non_existent_file_xyz.tsv' \
	2 \
	""

run_test "Option order independence (-n2 -w64 vs -w64 -n2)" \
	'printf "a\nb\nc\n" | '"$DSHUF"' -n2 -w64 -s42' \
	0 \
	"$(printf 'a\nb\nc\n' | $DSHUF -w64 -n2 -s42)"

printf "========================================\n"
printf "POSIX Conformance: %d passed, %d failed\n" "$PASSED" "$FAILED"
printf "========================================\n"

if [ "$FAILED" -ne 0 ]; then
	exit 1
fi
exit 0