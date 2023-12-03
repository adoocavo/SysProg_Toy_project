#1. Macros
TARGET = toy_system

DIRs = system \
	ui \
	web_server \
	project_libs \
	hal \


#source에 존재하는 header file 검색 경로 지정
SYSTEM = ./system
UI = ./ui
WEB_SERVER = ./web_server
HAL = ./hal
PROJECT_LIBS = ./project_libs
INCLUDES = -I$(SYSTEM) -I$(UI) -I$(WEB_SERVER) -I$(HAL) -I${PROJECT_LIBS}

BUILD_DIRs = ${DIRs}

CC = gcc
CFLAG = -g -W -Wall -o
CFLAG_c = -g -W -Wall -c      #obj file 생성까지만 => linking 수행X -> 헤더파일 신경X

CXXLIBS = -lpthread -lm -lrt
CXXFLAG = -g -W -Wall -O0 -std=c++17 -o
CXXFLAG_c = -g -W -Wall -O0 -std=c++17 -c
CXX = g++


OBJs = ./system/system_server.o\
	 ./ui/gui.o \
	 ./ui/input.o \
	 ./web_server/web_server.o \
	 ./project_libs/time/currTime.o \
	 main.o

CXX_OBJs = ./hal/camera_HAL.o \
		 ./hal/ControlThread.o \

SRCs = main.c

#2. targets
${TARGET} : main.o  
	#sub directory별로 build
	@ echo ${BUILD_DIRs}
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE}); \
		if test $$? -ne @; then break; fi; done
			
	#최종 실행파일 build 	
	$(CXX) -o $(TARGET) $(OBJs) $(CXX_OBJs) $(CXXLIBS)

##main obj file 생성 
main.o : ${SRCs} 
	${CC} ${INCLUDES} ${CFLAG_c} main.o ${SRCs}
		
#3. clean
clean : 
	#sub directory에서 생성된 실행파일 삭제 
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE} clean); done
	
	#최종 실행파일 삭제
	rm -rf main.o ${TARGET}		    


 





