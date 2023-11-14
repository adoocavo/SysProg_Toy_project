#1. Macros
DIRs = system \
	ui \
	web_server \

BUILD_DIRs = ${DIRs}

CC = gcc
CFLAGS = -g -W -Wall -o
EXEs = system/system_server.o\
	 ui/gui.o \
	 ui/input.o \
	 web_server/web_server.o

#2. targets
all : 
	#sub directory별로 build
	@ echo ${BUILD_DIRs}
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE}); \
		if test $$? -ne @; then break; fi; done
	
	#최종 실행파일 build 
	${CC} ${CFLAGS} toy_system ${EXEs}   
 		
#3. clean
clean : 
	#sub directory에서 생성된 실행파일 삭제 
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE} clean); done
	
	#최종 실행파일 삭제
	rm -rf toy_system		    


 





