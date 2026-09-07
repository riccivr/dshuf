#!/bin/sh
#
# Throughput Benchmark for dshuf
#

DSHUF="./dshuf"
LINES=100000

printf "========================================\n"
printf "Generating %d synthetic playlist lines...\n" "$LINES"
printf "========================================\n"

bench_file="/tmp/dshuf_bench_$$.tsv"
trap 'rm -f "$bench_file"' EXIT INT TERM

awk -v n="$LINES" 'BEGIN {
	for (i = 1; i <= n; i++) {
		artist = "Artist_" (i % 500);
		album = "Album_" (i % 2500);
		printf "%s\t%s\tTrack_%d\n", artist, album, i;
	}
}' > "$bench_file"

file_bytes=$(wc -c < "$bench_file")
file_mb=$(awk -v b="$file_bytes" 'BEGIN { printf "%.2f", b / (1024 * 1024) }')

printf "Benchmark dataset: %s MB (%d lines)\n\n" "$file_mb" "$LINES"

run_bench() {
	title="$1"
	cmd="$2"

	printf "Running: %s...\n" "$title"
	start_time=$(date +%s%N 2>/dev/null || date +%s)
	eval "$cmd" > /dev/null
	end_time=$(date +%s%N 2>/dev/null || date +%s)

	if [ ${#start_time} -gt 10 ]; then
		elapsed_sec=$(awk -v s="$start_time" -v e="$end_time" 'BEGIN { printf "%.3f", (e - s) / 1000000000 }')
	else
		elapsed_sec=$((end_time - start_time))
		[ "$elapsed_sec" -eq 0 ] && elapsed_sec=1
	fi

	lines_per_sec=$(awk -v n="$LINES" -v t="$elapsed_sec" 'BEGIN { if (t > 0) printf "%d", n / t; else print "N/A" }')
	mb_per_sec=$(awk -v m="$file_mb" -v t="$elapsed_sec" 'BEGIN { if (t > 0) printf "%.2f", m / t; else print "N/A" }')

	printf "  Elapsed time : %s s\n" "$elapsed_sec"
	printf "  Throughput   : %s lines/sec\n" "$lines_per_sec"
	printf "  Data rate    : %s MB/s\n\n" "$mb_per_sec"
}

run_bench "Single-key batch shuffle (-k 1)" \
	''"$DSHUF"' -k 1 -s 42 "'"$bench_file"'"'

run_bench "Multi-key batch shuffle (-k 1:1.0 -k 2:0.5)" \
	''"$DSHUF"' -k 1:1.0 -k 2:0.5 -s 42 "'"$bench_file"'"'

run_bench "Streaming pipeline shuffle (-k 1 -S -w 128)" \
	'cat "'"$bench_file"'" | '"$DSHUF"' -k 1 -S -w 128 -s 42'

printf "========================================\n"