# make 수행 명령어 : make <-f makefile 파일명> <option> <targets> 
## => 그냥 'make'수행 -> 현대 디렉터리 내에서 'makefile' or 'Makefile'빌드
## => target 지정 X -> makefile 내의 모든 target 빌드

# makefile 구성 : 여러 targets으로(grouping) 구성 
## target : <dependencies>   #dependencies가 충족될때(file들이 존재할때) commands 수행 
## [tab] shell commands

#1. Macros
CC = gcc
CFLAGS = -g -W -Wall -o
SRC = system_server.c \
	system_server.h         

#2. targets
all : system_server.o
	

system_server.o : ${SRC}
	${CC} ${CFLAGS} system_server.o ${SRC}

#3. clean
clean :
	rm -rf system_server.o


 





