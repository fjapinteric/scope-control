OBJECTS = scope-control.o
HEADERS = 
LDFLAGS = -g -lm
CFLAGS = -g

all: scope-control

scope-control: $(OBJECTS) $(HEADERS)


clean:
	rm -vf scope-control $(OBJECTS)
