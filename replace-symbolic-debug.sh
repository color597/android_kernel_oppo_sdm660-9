#!/bin/bash

set -e

dir=`realpath $1`

process() { 
  echo "Processing $1"
  symbolics=$(get_symbolics $1)
  local l
  for l in $symbolics; do
    # -s: Don't expand if the target dir is also a symbolic link
    local t
    t=$(realpath -s $(dirname $l)/$(readlink $l))
    # Is the target a subdir of the dir being processed?
	echo "t is       $t     l is $l"
	echo "param-1 is $1"
	
    if [[ $t != $1/* ]]; then
      if [ -d $t ]; then
         process $t
         echo "Removing dir $l"
         rm -f $l
         echo "Copying dir $t to $l"
         cp -r $t $l
      elif [ -f $t ]; then
         echo "Removing file $l"
         rm -f $l
         echo "Copying file $t to $l"
         cp $t $l
      else
         echo "Invalid target $t"
         exit 1
      fi
    fi
  done
}

get_symbolics() {
  echo `find $1 -type l | grep -v '.git'`
}

process $dir

