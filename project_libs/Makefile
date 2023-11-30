#1. Macros
DIRs = time

BUILD_DIRs = ${DIRs}

#CC = gcc
#CFLAGS_c = -g -W -Wall -c      #obj file 생성까지만 => linking 수행X -> 헤더파일 신경X

#2. targets
all : 
	#sub directory별로 build
	@ echo ${BUILD_DIRs}
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE}); \
		if test $$? -ne @; then break; fi; done
			
#3. clean
clean : 
	#sub directory에서 생성된 실행파일 삭제 
	@ for dir in ${BUILD_DIRs}; \
	do \
		(cd $${dir}; ${MAKE} clean); done
   


 





