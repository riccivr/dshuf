#!/bin/sh
#
# Mathematical Invariant and Property-Based Tests for dshuf
#

DSHUF="./dshuf"
PASSED=0
FAILED=0

tmp_in="prop_in.$$.tmp"
tmp_out="prop_out.$$.tmp"
trap 'rm -f "$tmp_in" "$tmp_out"' EXIT INT TERM

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
printf "Running Invariant & Property Tests\n"
printf "========================================\n"

# Invariant 1: Permutation Invariant (sort(out) == sort(in))
awk 'BEGIN { for (i=1; i<=200; i++) print "Artist_" (i%10) "\tTrack_" i }' > "$tmp_in"
run_test "Permutation invariant: no lines dropped or duplicated" \
	's_in=$(sort "$tmp_in"); s_out=$('"$DSHUF"' -k 1 -s 42 "$tmp_in" | sort); [ "$s_in" = "$s_out" ] && echo ok' \
	0 \
	"ok"

# Invariant 2: Determinism Invariant (same seed -> identical output)
run_test "Determinism invariant with fixed seed" \
	'o1=$('"$DSHUF"' -k 1 -s 987654 "$tmp_in"); o2=$('"$DSHUF"' -k 1 -s 987654 "$tmp_in"); [ "$o1" = "$o2" ] && echo ok' \
	0 \
	"ok"

# Invariant 3: Cluster Separation Property
# 10 artists x 10 tracks = 100 tracks. Ideal interval = 10.
# Min gap under dshuf -j 0.05 must be >= 4.
awk 'BEGIN { for (i=0; i<100; i++) print int(i/10) "\tTrack_" i }' > "$tmp_in"
run_test "Cluster separation: min distance between same artist >= 4" \
	''"$DSHUF"' -k 1 -j 0.05 -s 1234 "$tmp_in" | awk -F"\t" '\''
	{
		artist = $1;
		if (last[artist] != "") {
			dist = NR - last[artist];
			if (min_dist == 0 || dist < min_dist) min_dist = dist;
		}
		last[artist] = NR;
	}
	END {
		if (min_dist >= 4) print "ok";
		else print "failed: " min_dist;
	}'\''' \
	0 \
	"ok"

# Invariant 4: Multi-Key Separation Property
# 4 artists x 3 albums x 10 tracks.
awk 'BEGIN {
	for (i=0; i<120; i++) {
		artist = int(i / 30);
		album = int(i / 10);
		print artist "\t" album "\tTrack_" i;
	}
}' > "$tmp_in"
run_test "Multi-key hierarchy: album repeats separated by gap >= 4" \
	''"$DSHUF"' -k 1:1.0 -k 2:0.5 -j 0.05 -s 77 "$tmp_in" | awk -F"\t" '\''
	{
		album = $2;
		if (last[album] != "") {
			dist = NR - last[album];
			if (min_dist == 0 || dist < min_dist) min_dist = dist;
		}
		last[album] = NR;
	}
	END {
		if (min_dist >= 4) print "ok";
		else print "failed: " min_dist;
	}'\''' \
	0 \
	"ok"

# Invariant 5: Starvation Bound Property
# 50 tracks of Artist 1, 50 tracks of 50 unique artists.
# First 25 and last 25 tracks should each have roughly 10-15 tracks of Artist 1.
awk 'BEGIN {
	for (i=1; i<=50; i++) print "Artist_1\tTrack_" i;
	for (i=51; i<=100; i++) print "Artist_" i "\tTrack_" i;
}' > "$tmp_in"
run_test "Starvation bound: dominant cluster evenly distributed" \
	''"$DSHUF"' -k 1 -s 333 "$tmp_in" | awk -F"\t" '\''
	{
		if (NR <= 25 && $1 == "Artist_1") first_q++;
		if (NR > 75 && $1 == "Artist_1") last_q++;
	}
	END {
		if (first_q >= 8 && first_q <= 17 && last_q >= 8 && last_q <= 17) print "ok";
		else printf "skewed: first=%d, last=%d\n", first_q, last_q;
	}'\''' \
	0 \
	"ok"

# Invariant 6: Pure random convergence at jitter 1.0 (all same key)
awk 'BEGIN { for (i=1; i<=50; i++) print "SameArtist\tTrack_" i }' > "$tmp_in"
run_test "Pure random convergence at jitter 1.0 without deadlock" \
	''"$DSHUF"' -k 1 -j 1.0 -s 555 "$tmp_in" | wc -l' \
	0 \
	"50"

# Invariant 7: Output Limit Bounds (-n K)
run_test "Output limit -n 10 emits exactly 10 lines" \
	''"$DSHUF"' -n 10 "$tmp_in" | wc -l' \
	0 \
	"10"

run_test "Output limit -n larger than input emits full count" \
	''"$DSHUF"' -n 1000 "$tmp_in" | wc -l' \
	0 \
	"50"

# Invariant 8: Single and zero line input
run_test "Empty input produces zero output" \
	'printf "" | '"$DSHUF"'' \
	0 \
	""

run_test "Single line input preserves content" \
	'printf "solo track\n" | '"$DSHUF"'' \
	0 \
	"solo track"

printf "========================================\n"
printf "Metric Invariants: %d passed, %d failed\n" "$PASSED" "$FAILED"
printf "========================================\n"

if [ "$FAILED" -ne 0 ]; then
	exit 1
fi
exit 0