#!/usr/bin/env sh

# takes two args:
#  1: directory on the file system: filesdir
#  2: text string to be searched: searchstr

# Return values
#  1 and print statements if the parameters are not specified
#  1 and print statements if files dir does not exists
#  0 With the message: "The number of files are X and the number of matching lines are Y"
#    X: the number of files in the directory and subdirectories
#    Y: number of matching lines found

finder() {
    X=$(find $1 -mindepth 1 | wc -l)
    Y=$(grep -rin $1 -e $2 | wc -l)
    echo "The number of files are $X and the number of matching lines are $Y"
}


usage() {
    echo "$0 <directory> <string to search>"
}

if [ $# -lt 2 ]
then
    echo "Not enough arguments"
    usage "$0"
    exit 1
fi

if [ ! -e $1 ]
then
    echo "$1: does not exists"
    exit 1
fi

if [ ! -d $1 ]
then
    echo "$1: is not a directory"
    exit 1
fi

finder "$1" "$2"
