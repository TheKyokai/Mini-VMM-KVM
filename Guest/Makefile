guest.img: guest.o
	ld -T guest.ld guest.o -o guest1.img

guest.o: guest.c
	$(CC) -m64 -ffreestanding -fno-pic -c -o $@ $^

clean:
	rm -f guest.o guest.img
