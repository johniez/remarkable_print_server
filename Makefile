# Needs to source the rm2 toolchain environment, which will set the CXX variable appropriately.

all:
	${CXX} -std=c++11 -O2 -Wall main.cc -o server -luuid
