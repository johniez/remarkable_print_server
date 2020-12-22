# Needs to source the rm2 toolchain environment, which will set the CXX variable appropriately.

all:
	${CXX} -D_FORTIFY_SOURCE=2 -std=c++11 -O2 -Wall -Wformat -Werror=format-security -fstack-protector-strong main.cc -o pdf-print-server -luuid
