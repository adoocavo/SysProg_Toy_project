# make 수행 명령어 : make <-f makefile 파일명> <option> <targets> 
## => 그냥 'make'수행 -> 현대 디렉터리 내에서 'makefile' or 'Makefile'빌드
## => target 지정 X -> makefile 내의 모든 target 빌드

# makefile 구성 : 여러 targets으로(grouping) 구성 
## target : <dependencies>   #dependencies가 충족될때(file들이 존재할때) commands 수행 
## [tab] shell commands

#1. Macros
CC = gcc
CFLAGS_c = -g -W -Wall -c

SRCs = system_server.c \

INCs = system_server.h

SYSTEM = ./
UI = ./../ui
WEB_SERVER = ./../web_server
HAL = ./../hal
PROJECT_LIBS = ./../project_libs     #./time까지 작성 안해도 ㄱㅊ???
INCLUDES = -I$(SYSTEM) -I$(UI) -I$(WEB_SERVER) -I$(HAL) -I${PROJECT_LIBS}

#2. targets
all : system_server.o
	

system_server.o : ${SRCs} ${INCs}
	${CC} ${INCLUDES} ${CFLAGS_c} system_server.o ${SRCs}

#3. clean
clean :
	rm -rf system_server.o


 





