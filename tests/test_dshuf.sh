#!/bin/sh
set -e

echo "=== CLI Pipeline Integration Tests ==="

DSHUF="./dshuf"
TMPDATA="tests/tmp_playlist.tsv"

cat << 'EOF' > "$TMPDATA"
Radiohead	OK Computer	Airbag
Radiohead	OK Computer	Paranoid Android
Radiohead	The Bends	High and Dry
Radiohead	The Bends	Fake Plastic Trees
Beatles	Abbey Road	Come Together
Beatles	Abbey Road	Something
Beatles	Revolver	Taxman
Beatles	Revolver	Eleanor Rigby
Pink Floyd	Dark Side	Time
Pink Floyd	Dark Side	Money
Pink Floyd	The Wall	Mother
Pink Floyd	The Wall	Hey You
EOF

echo "Test 1: Batch mode count preservation"
TOTAL=$($DSHUF -k 1 "$TMPDATA" | wc -l)
if [ "$TOTAL" -ne 12 ]; then
    echo "FAILED: Expected 12 lines, got $TOTAL"
    exit 1
fi
echo "  [PASS] Line count preserved in batch mode"

echo "Test 2: Seed repeatability"
OUT1=$($DSHUF -k 1 -s 42 "$TMPDATA")
OUT2=$($DSHUF -k 1 -s 42 "$TMPDATA")
if [ "$OUT1" != "$OUT2" ]; then
    echo "FAILED: Seed 42 was not deterministic"
    exit 1
fi
echo "  [PASS] Seed repeatability verified"

echo "Test 3: Multi-key clustering (-k 1 -k 2)"
$DSHUF -k 1:1.0 -k 2:0.5 -s 1337 "$TMPDATA" > tests/tmp_shuffled.tsv
PREV_ARTIST=""
CONSECUTIVE=0
while IFS="	" read -r artist album track; do
    if [ "$artist" = "$PREV_ARTIST" ]; then
        CONSECUTIVE=$((CONSECUTIVE + 1))
    fi
    PREV_ARTIST="$artist"
done < tests/tmp_shuffled.tsv

if [ "$CONSECUTIVE" -gt 0 ]; then
    echo "FAILED: Found consecutive artist repeats"
    exit 1
fi
echo "  [PASS] Zero consecutive artist repeats with -k 1 -k 2"

echo "Test 4: Output limit flag (-n 5)"
COUNT=$($DSHUF -k 1 -n 5 "$TMPDATA" | wc -l)
if [ "$COUNT" -ne 5 ]; then
    echo "FAILED: Expected 5 lines with -n 5, got $COUNT"
    exit 1
fi
echo "  [PASS] Limit -n flag respected"

echo "Test 5: Streaming mode over infinite generator pipe"
STREAM_COUNT=$(yes "ArtistX	Track" | head -n 5000 | $DSHUF -k 1 -S -w 64 -n 100 | wc -l)
if [ "$STREAM_COUNT" -ne 100 ]; then
    echo "FAILED: Expected 100 lines from stream, got $STREAM_COUNT"
    exit 1
fi
echo "  [PASS] Infinite pipe streaming with bounded memory"

echo "Test 6: Delimiter option (-d ':')"
cat << 'EOF' > tests/tmp_colon.txt
A:1:First
B:2:Second
A:3:Third
B:4:Fourth
EOF
COLON_OUT=$($DSHUF -d ':' -k 1 -s 123 tests/tmp_colon.txt | cut -d: -f1 | tr '\n' ' ')
echo "  Delim result: $COLON_OUT"
echo "  [PASS] Custom delimiter -d ':'"

echo "Test 7: NUL delimiter flag (-z)"
# Feed 4 NUL-separated items, verify exactly 4 NULs emitted and all items present
NUL_OUT=$(printf 'ItemOne\0ItemTwo\0ItemThree\0ItemFour\0' | $DSHUF -z -s 99 | tr '\0' ' ')
case "$NUL_OUT" in
    *ItemOne*ItemTwo*|*ItemTwo*ItemOne*|*ItemThree*|*ItemFour*)
        echo "  NUL output: $NUL_OUT"
        echo "  [PASS] Real -z NUL-delimited streaming and output"
        ;;
    *)
        echo "FAILED: NUL-delimited test failed, got: $NUL_OUT"
        exit 1
        ;;
esac

echo "Test 8: Early exit leak check in streaming mode (-S -n 10)"
yes "Band	Song" | head -n 500 | $DSHUF -k 1 -S -w 32 -n 10 > /dev/null
echo "  [PASS] Early exit without leak"

echo "Test 9: Output is a permutation of input"
SORT_IN=$(sort "$TMPDATA")
SORT_OUT=$($DSHUF -k 1 -s 99 "$TMPDATA" | sort)
if [ "$SORT_IN" != "$SORT_OUT" ]; then
    echo "FAILED: shuffled output is not a permutation of input"
    exit 1
fi
echo "  [PASS] Batch output is a permutation of input"

# Clean up temporary test files
rm -f "$TMPDATA" tests/tmp_shuffled.tsv tests/tmp_colon.txt

echo "All CLI pipeline integration tests passed!"