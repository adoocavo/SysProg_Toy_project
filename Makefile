#1. Macros
DIRs = system \
	ui \
	web_server \
	project_libs \

BUILD_DIRs = ${DIRs}

CC = gcc
CFLAGS = -g -W -Wall -o
CFLAGS_c = -g -W -Wall -c      #obj file 생성까지만 => linking 수행X -> 헤더파일 신경X

OBJs = ./system/system_server.o\
	 ./ui/gui.o \
	 ./ui/input.o \
	 ./web_server/web_server.o \
	 ./project_libs/time/currTime.o \
	 main.o

INCs = ./system/system_server.h \
	./ui/gui.h \
	./ui/input.h \
	./web_server/web_server.h \
	./project_libs/time/currTime.h
	
SRCs = main.c

#2. targets
all : main.o
	#sub directory별로 build
	@ echo ${BUILD_DIRs}
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE}); \
		if test $$? -ne @; then break; fi; done
			
	#최종 실행파일 build 
	${CC} ${CFLAGS} toy_system ${OBJs}   


##main obj file 생성 
main.o : ${SRCs} ${INCs} 
	${CC} ${CFLAGS_c} main.o ${SRCs}
		
#3. clean
clean : 
	#sub directory에서 생성된 실행파일 삭제 
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE} clean); done
	
	#최종 실행파일 삭제
	rm -rf main.o toy_system		    


 





