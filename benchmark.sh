#!/bin/bash
#cmd = 'time ./mdu -j $i /home > grep ".*user"'
TIMEFORMAT=%R
echo "numThreads time"
for i in $(seq 1 100);
do
	var=`time ( ./mdu -j $i /pkg > /dev/null 2>&1 ) 2>&1`
	echo "$i $var"
done

