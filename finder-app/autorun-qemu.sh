#!/bin/sh
set -x
cd $(dirname $0)
echo "INIT" > /dev/kmsg
echo "Running test script"
./finder-test.sh
rc=$?
if [ ${rc} -eq 0 ]; then
    echo "Completed with success!!"
else
    echo "Completed with failure, failed with rc=${rc}"
fi
echo "finder-app execution complete, dropping to terminal"
/bin/sh
