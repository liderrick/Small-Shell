#Compiler and compiler flags
CXX = gcc
CXXFLAGS = -std=c99
#CXX = g++
#CXXFLAGS = -std=c++0x
#CXXFLAGS += -Wall
#CXXFLAGS += -pedantic-errors
#CXXFLAGS += -g
#CXXFLAGS += -O3
#LDFLAGS = -lboost_date_time

#Project executable output file
PROJ = smallsh

#Object files
OBJS = smallsh.o

#Source files
SRCS = smallsh.c

#Header files
# HEADERS =

#Compile project executable from object files
${PROJ}: ${OBJS}
	${CXX} ${CXXFLAGS} ${OBJS} -o ${PROJ}

#Compile object files from source files
${OBJS}: ${SRCS}
	${CXX} ${CXXFLAGS} -c $(@:.o=.c)

#Remove project executable and object files
clean:
	rm ${PROJ} ${OBJS}

#Citation:
#Format of this makefile based off of: http://web.engr.oregonstate.edu/~rookert/cs162/03.mp4