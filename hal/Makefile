# make 수행 명령어 : make <-f makefile 파일명> <option> <targets> 
## => 그냥 'make'수행 -> 현대 디렉터리 내에서 'makefile' or 'Makefile'빌드
## => target 지정 X -> makefile 내의 모든 target 빌드

# makefile 구성 : 여러 targets으로(grouping) 구성 
## target : <dependencies>   #dependencies가 충족될때(file들이 존재할때) commands 수행 
## [tab] shell commands

#1. Macros
CC = gcc
CFLAG_c = -g -W -Wall -c

CXXLIBS = -lpthread -lm -lrt
CXXFLAG_c = -g -W -Wall -O0 -std=c++14 -c
CXX = g++

SYSTEM = ./../system
UI = ./../ui
WEB_SERVER = ./../web_server
HAL = ./
PROJECT_LIBS = ./../project_libs
INCLUDES = -I$(SYSTEM) -I$(UI) -I$(WEB_SERVER) -I$(HAL) -I${PROJECT_LIBS}

SRC1 = camera_HAL.cpp \

SRC2 = ControlThread.cpp \

INC1 = camera_HAL.h 
INC2 = ControlThread.h 

CXX_OBJs = camera_HAL.o ControlThread.o

#2 targets
all : ${CXX_OBJs}

camera_HAL.o: ${SRC1} ${INC1}
	$(CXX) ${INCLUDES} $(CXXFLAG_c) camera_HAL.o ${SRC1}

ControlThread.o: ${SRC2} ${INC2}
	$(CXX) ${INCLUDES} $(CXXFLAG_c) ControlThread.o ${SRC2}

#3. clean
clean :
	rm -rf ${CXX_OBJs}


 





