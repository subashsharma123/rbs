#####################################################
# Makefile 
#####################################################

TARGETS = server client coordinator  

all: $(TARGETS)

.cpp.o:
	g++ -g -o $@ -c $< 

server: svr.o 
	g++ -g -o $@ $^ 
client: cli.o 
	g++ -g -o $@ $^ 
coordinator: cor.o 
	g++ -g -o $@ $^ -lpthread 

clean:
	rm -f $(TARGETS) *.o
