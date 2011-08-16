#!/bin/bash
i=1
pass=0
FILES=$(find . -name "??_patch.xml" | sort)
NR_TESTS=$(echo "$FILES" | wc -l)
echo 1..$NR_TESTS
for FILE in $FILES; do
	../src/xmlpatch -si $FILE -o - $(dirname $FILE)/orig.xml | diff - ${FILE%patch.xml}result.xml > /dev/null
	RTN=$?
	LINE="- $i $(sed '2,$d' ${FILE%patch.xml}desc.txt) ($FILE)"
	if [ $RTN -eq 0 ]; then
		echo ok $LINE
		let pass++
	else
		echo not ok $LINE
	fi
	let i++
done
[ $pass -eq $NR_TESTS ]
