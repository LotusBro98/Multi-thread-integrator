#!/bin/bash

/usr/bin/time -f \
"%e\treal time\n\
%U\tuser time\n\
%P\tCPU\n\
%R\tminor page faults\n\
%w\tvoluntary context switces\n\
%c\tinvoluntary context switches" \
./integrate 0 3.141592653 $2 $1 0.001
