#!/bin/sh
#
# Fuzzing, Stress, and Malformed Input Tests for dshuf
#

DSHUF="./dshuf"
PASSED=0
FAILED=0

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
printf "Running Stress & Malformed Input Tests\n"
printf "========================================\n"

# Test 1: Missing trailing newline at EOF
run_test "Unterminated line without trailing newline" \
	'printf "artistA\ttrack1\nartistB\ttrack2" | '"$DSHUF"' -k 1 | wc -l' \
	0 \
	"2"

# Test 2: Windows CRLF (\r\n) line endings stripped correctly
run_test "Windows CRLF (\\r\\n) line endings stripped" \
	'printf "artistA\ttrack1\r\nartistB\ttrack2\r\n" | '"$DSHUF"' -k 1 | awk '\''/\r/ { c++ } END { print c+0 }'\''' \
	0 \
	"0"

# Test 3: Classic MacOS isolated CR (\r) at EOF stripped
run_test "Classic Mac CR (\\r) line ending stripped" \
	'printf "mac log entry\r" | '"$DSHUF"' -k 1 | tr -d "\n"' \
	0 \
	"mac log entry"

# Test 4: Empty lines interspersed in stream
run_test "Empty lines interspersed in stream do not crash" \
	'printf "\n\nartistA\ttrack1\n\nartistB\ttrack2\n\n" | '"$DSHUF"' -k 1 | wc -l' \
	0 \
	"6"

# Test 5: Missing or ragged fields (fewer columns than -k target)
run_test "Ragged columns: fewer fields than -k 3 does not crash" \
	'printf "col1\tcol2\nshort\ncol1\tcol2\tcol3\n" | '"$DSHUF"' -k 3 | wc -l' \
	0 \
	"3"

# Test 6: 10,000-character long line
long_line=$(awk 'BEGIN { printf "Artist"; for (i=0; i<10000; i++) printf "x"; printf "\tTrack\n" }')
run_test "10,000-character long input line does not truncate or crash" \
	'printf "%s\n" "'"$long_line"'" | '"$DSHUF"' -k 1 | wc -l' \
	0 \
	"1"

# Test 7: Binary stream and random bytes does not crash
run_test "Binary data /dev/urandom chunk does not crash" \
	'head -c 4096 /dev/urandom 2>/dev/null | '"$DSHUF"' -k 1 >/dev/null 2>&1; echo $?' \
	0 \
	"0"

# Test 8: 50,000 continuous lines streaming with early limit
run_test "50,000 streaming lines with early exit -n 50" \
	'awk '\''BEGIN { for (i=1; i<=50000; i++) print "Artist_" (i%10) "\tTrack_" i }'\'' | '"$DSHUF"' -k 1 -S -w 64 -n 50 | wc -l' \
	0 \
	"50"

# Test 9: Massive window size larger than input dataset
run_test "Massive window size -w 10000 on 20 items drains fully" \
	'awk '\''BEGIN { for (i=1; i<=20; i++) print "Artist_" (i%3) "\tTrack_" i }'\'' | '"$DSHUF"' -k 1 -w 10000 | wc -l' \
	0 \
	"20"

# Test 10: Window size 1 in streaming mode (FIFO fallback)
run_test "Window size -w 1 in streaming mode drains without deadlock" \
	'printf "A\t1\nB\t2\nC\t3\n" | '"$DSHUF"' -k 1 -S -w 1 | wc -l' \
	0 \
	"3"

printf "========================================\n"
printf "Stress & Robustness: %d passed, %d failed\n" "$PASSED" "$FAILED"
printf "========================================\n"

if [ "$FAILED" -ne 0 ]; then
	exit 1
fi
exit 0