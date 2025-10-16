#include <stdint.h>

// Ports
#define BYTE_PORT 0xE9
#define FILE_PORT 0x0278

// File op flags
#define F_OPEN 0x1
#define F_CLOSE 0x2
#define F_READ 0x3
#define F_WRITE 0x4
#define F_SEEK 0x5

// Fopen flags
#define FO_RD 0x1
#define FO_WR 0x2
#define FO_RDWR 0x3


static void outb(uint16_t port, uint8_t value) 
{
	asm("outb %0,%1" 
		: /* No output */ 
		: "a" (value), "Nd" (port) 
		: "memory");
}

static uint8_t inb(uint16_t port)
{
	uint8_t value = 0;
	asm("inb %1,%0" 
		: "=a" (value) 
		: "Nd" (port) 
		: "memory");
	return value;
}

static void outw(uint16_t port, uint16_t value) 
{
	asm("outw %0,%1" 
		: /* No output */ 
		: "a" (value), "Nd" (port) 
		: "memory");
}

static uint16_t inw(uint16_t port)
{
	uint16_t value = 0;
	asm("inw %1,%0" 
		: "=a" (value) 
		: "Nd" (port) 
		: "memory");
	return value;
}

static void outl(uint16_t port, uint32_t value) 
{
	asm("outl %0,%1" 
		: /* No output */ 
		: "a" (value), "Nd" (port) 
		: "memory");
}

static uint32_t inl(uint16_t port)
{
	uint32_t value = 0;
	asm("inl %1,%0" 
		: "=a" (value) 
		: "Nd" (port) 
		: "memory");
	return value;
}


static void out(uint8_t value)
{
	outb(BYTE_PORT, value);
}

static uint8_t in()
{
	return inb(BYTE_PORT);
}

static void print(char* data)
{
	for (char* current = data; *current; ++current)
		out(*current);
}

static char getchar()
{
	return in();
}


typedef struct file 
{
	uint32_t fd;
} FILE;

static FILE* fopen(const char* file_path, int flags)
{
	outb(FILE_PORT, F_OPEN);
	outl(FILE_PORT, (uint32_t)(uintptr_t)file_path);
	outb(FILE_PORT, flags);
	return 0;
}

static int fread(FILE* file, char* buffer, uint32_t size)
{
	return 0;
}

static int fwrite(FILE* file, char* buffer, uint32_t size)
{
	return 0;
}

static int fseek(FILE* file, uint32_t offset)
{
	return 0;
}

static int fclose(FILE* file)
{
	return 0;
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {

	/*
		INSERT CODE BELOW THIS LINE
	*/

	fopen("Ajmo da vidimo", FO_RDWR);

	/*
		INSERT CODE ABOVE THIS LINE
	*/

	for (;;)
		asm("hlt");
}
