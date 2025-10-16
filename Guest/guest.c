#include <stdint.h>


static void outb(uint8_t value) 
{
	uint16_t port = 0XE9;
	asm("outb %0,%1" 
		: /* No output */ 
		: "a" (value), "Nd" (port) 
		: "memory");
}

static uint8_t inb()
{
	uint16_t port = 0XE9;
	uint8_t value = 0;
	asm("inb %1,%0" 
		: "=a" (value) 
		: "Nd" (port) 
		: "memory");
	return value;
}

static void print(char* data)
{
	for (char* current = data; *current; ++current)
		outb(*current);
}


void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {

	/*
		INSERT CODE BELOW THIS LINE
	*/
	uint8_t val;
	print("Hello World!\n");
	// while ((val = inb()) != '\n')
	// {
	// 	print(&val);
	// }
	// print("\n");

	/*
		INSERT CODE ABOVE THIS LINE
	*/

	for (;;)
		asm("hlt");
}
