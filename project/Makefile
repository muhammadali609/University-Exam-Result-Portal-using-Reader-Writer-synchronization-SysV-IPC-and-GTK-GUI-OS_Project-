all: result_portal

result_portal: main.o reader_writer.o
	$(CC) -o result_portal main.o reader_writer.o

gui_portal: gtk_gui.o reader_writer.o
	$(CC) -o gui_portal gtk_gui.o reader_writer.o $(shell pkg-config --cflags --libs gtk+-3.0)

main.o: main.c reader_writer.h
	$(CC) -Wall -Wextra -std=c11 -c main.c

reader_writer.o: reader_writer.c reader_writer.h
	$(CC) -Wall -Wextra -std=c11 -c reader_writer.c

gtk_gui.o: gtk_gui.c reader_writer.h
	$(CC) -Wall -Wextra -std=c11 $(shell pkg-config --cflags gtk+-3.0) -c gtk_gui.c

clean:
	rm -f *.o result_portal gui_portal

