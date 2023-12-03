# make 수행 명령어 : make <-f makefile 파일명> <option> <targets> 
## => 그냥 'make'수행 -> 현대 디렉터리 내에서 'makefile' or 'Makefile'빌드
## => target 지정 X -> makefile 내의 모든 target 빌드

# makefile 구성 : 여러 targets으로(grouping) 구성 
## target : <dependencies>   #dependencies가 충족될때(file들이 존재할때) commands 수행 
## [tab] shell commands

#1. Macros
CC = gcc
CFLAGS_c = -g -W -Wall -c

SRCs = web_server.c \

INCs = web_server.h 

      
SYSTEM = ./../system
UI = ./../ui
WEB_SERVER = ./
HAL = ./../hal
PROJECT_LIBS = ./../project_libs
INCLUDES = -I$(SYSTEM) -I$(UI) -I$(WEB_SERVER) -I$(HAL) -I${PROJECT_LIBS}

#2. targets
all : web_server.o
	

web_server.o : ${SRCs} ${INCs}
	${CC} ${INCLUDES} ${CFLAGS_c} web_server.o ${SRCs}

#3. clean
clean :
	rm -rf web_server.o


 





