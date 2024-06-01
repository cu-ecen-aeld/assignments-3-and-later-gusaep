#!/usr/bin/env bash

# Accepts the following arguments:
# - the first argument is a full path to a file (including filename) on the filesystem
#   referred to below as writefile
# - the second argument is a text string which will be written within this file, referred
#   to below as writestr

# Exits with value 1 error and print statements if any of the arguments
#  above were not specified

# Creates a new file with name and path writefile with content writestr,
#  overwriting any existing file and creating the path if it doesn’t exist.
#  Exits with value 1 and error print statement if the file could not be created.
set -e
writer() {
    dir="$(dirname $1)"
    if [ ! -d dir ]
    then
        mkdir -p $dir
    fi
    echo "$2" > $1
}


usage() {
    echo "$0 <fullpath> <string to write>"
}

if [ $# -lt 2 ]
then
    echo "Not enough arguments: $#"
    usage "$0"
    exit 1
fi

writer "$1" "$2"
