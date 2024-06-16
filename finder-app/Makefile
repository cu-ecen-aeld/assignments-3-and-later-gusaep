
APP="writer"

all: clean writer

writer: clean
	${CC} -g -o ${APP} -Wall -Wextra ${APP}.c

.PHONY: clean
clean:
	rm -f ${APP}