#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>
#include <linux/kvm.h>
#include <pthread.h>

// Guest Code Start
#define GUEST_START_ADDR 0x0000 

// PDE bits
#define PDE64_PRESENT (1u << 0)
#define PDE64_RW (1u << 1)
#define PDE64_USER (1u << 2)
#define PDE64_PS (1u << 7)

// CR0 and CR4
#define CR0_PE (1u << 0)
#define CR0_PG (1u << 31)
#define CR4_PAE (1u << 5)

#define EFER_LME (1u << 8)
#define EFER_LMA (1u << 10)

// Commonly used sizes
#define SIZE_4KB (1u << 12)
#define SIZE_2MB (1u << 21)
#define SIZE_4MB (1u << 22)
#define SIZE_8MB (1u << 23)

#define MAP_TABLE_SIZE (1u << 9)
#define MAP_TABLE_OFFSET (1u << 12)

// Guest Img List
struct file_node {
	char* file_name;
	struct file_node *next;
};

char* get_file(struct file_node **head);
void push_file(struct file_node **head, char *name);


// Data Loading
int parse_arguments(int argc, char *argv[], size_t *mem_size, size_t *page_size, struct file_node **guest_list);

// KVM 
struct vm {
	int kvm_fd;
	int vm_fd;
	int vcpu_fd;
	char *mem; // Mapped guest mem
	size_t mem_size; // Mapped guest mem size
	struct kvm_run *run; 
	int run_mmap_size; // KVM_RUN mmap size
};

int kvm_init();
void kvm_close(int kvm_fd);

int vm_create(int kvm_fd, struct vm *v, size_t mem_size, size_t page_size, char *guest_image);
int vm_run(struct vm *v, pthread_t thread_id);
void vm_destroy(struct vm *v);

void* vm_start(void* vm);

// VM Thread List
struct vm_node {
    struct vm v;
    pthread_t thread_id;
    struct vm_node *next;
};

struct vm_node* get_node_vm(struct vm_node *head, int index);
void push_node_vm(struct vm_node **head, struct vm_node *node);
struct vm_node* remove_node_vm(struct vm_node **head);


// IN/OUT

void inb(struct vm *v);
void outb(struct vm *v);

int 
main(int argc, char *argv[])
{
    int arg_status;
    size_t mem_size, page_size;
	struct file_node *guest_list = NULL;
    if ((arg_status = parse_arguments(argc, argv, &mem_size, &page_size, &guest_list)) < 0)
        return arg_status;
	
	printf("MP_Size: %lu\n",mem_size);
	printf("PG_Size: %lu\n",page_size);
	
    struct vm_node *head = NULL;

	// Initializing KVM
	int kvm_fd = kvm_init();
	if (kvm_fd < 0){
		return 1;
	}

    while (guest_list)
    {
        struct vm_node *node = (struct vm_node*) malloc(sizeof(struct vm_node));
        node->next = NULL;
		char *guest = get_file(&guest_list);
		printf("Guest: %s\n", guest);
        int stat = vm_create(kvm_fd, &node->v, mem_size, page_size, guest);
        if (stat != 0)
		{
			free(node);
			continue;
		}
        pthread_t thread_id;
        stat = pthread_create(&thread_id, NULL, vm_start, (void*)(&node->v));
        if (stat != 0)
        {
            vm_destroy(&(node->v));
			free(node);
            continue;
        }
        node->thread_id = thread_id;
        push_node_vm(&head, node);
    }

    struct vm_node *current_wait = NULL;
    while ((current_wait = remove_node_vm(&head)) != NULL)
    {
        pthread_join(current_wait->thread_id, NULL);
    }

	kvm_close(kvm_fd);
    return 0;
}

// Data Loading
int 
parse_arguments(int argc, char *argv[], size_t *mem_size, size_t *page_size, struct file_node **guest_list)
{
    if (argc < 7)
    {
        printf("Error: incorrect command format\nCorrect format: ./mini_hypervisor <--memory/-m> <2/4/8(MB)> <--page/-p> <4(KB)/2(MB)> <--guest/-g> <guest image file>\nAll arguments are mandatory\n");
        return -1;
    }
    int mem_done = 0;
    int page_done = 0;
    int guest_done = 0;
    for (int i=1; i<argc; i+=2)
    {
        if ((!strcmp("--memory", argv[i]) || !strcmp("-m", argv[i])) && !mem_done)
        {
            int req_mem_size = atoi(argv[i+1]);
            if (req_mem_size == 2 || req_mem_size == 4 || req_mem_size == 8)
            {
                switch(req_mem_size)
                {
                    case 2:
                        *mem_size = SIZE_2MB;
                        break;
                    case 4:
                        *mem_size = SIZE_4MB;
                        break;
                    case 8:
                        *mem_size = SIZE_8MB;
                        break;
                }
                mem_done = 1;
                continue;
            }
            else 
            {
                printf("Error: guest physical memory size must be 2, 4 or 8(MB)\nRequested: %s\n", argv[i+1]);
                return -2;
            }
        }
        if ((!strcmp("--page", argv[i]) || !strcmp("-p", argv[i])) && !page_done)
        {
            int req_page_size =  atoi(argv[i+1]);
            if (req_page_size == 2 || req_page_size == 4)
            {	
				switch(req_page_size)
                {
                    case 2:
                        *page_size = SIZE_2MB;
                        break;
                    case 4:
                        *page_size = SIZE_4KB;
                        break;
                }
                page_done = 1;
                continue;
            }
            else 
            {
                printf("Error: page size must be 4(KB) or 2(MB)\nRequested: %s\n", argv[i+1]);
                return -3;
            }
        }
        if ((!strcmp("--guest", argv[i]) || !strcmp("-g", argv[i])) && !guest_done)
        {
            char * image = argv[i+1];
			push_file(guest_list, image);
			if (argc <= i+2)
			{
				guest_done = 1;
				continue;
			}
			while (argc > i+2 && argv[i+2][0] != '-')
			{
				i++;
				image = argv[i+1];
				push_file(guest_list, image);
			}
            guest_done = 1;
        }
    }
    if (guest_done && mem_done && page_done)
        return 0;

    printf("Error: Not all provided arguments are valid\n");
    if (!mem_done)
        printf("Guest physical memory not specified\nUse: --memory/-m 2/4/8(MB)\n");
    if (!page_done)
        printf("Page size not specified\nUse: --page/-p 4(KB)/2(MB)\n");
    if (!guest_done)
        printf("Guest image file not specified\nUse: --guest/-g <guest image file>\n");
    
    return -4;
}


// VM Creation functions
int vm_init(int kvm_fd, struct vm *v, size_t mem_size);
int load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr);
void setup_segments_64(struct kvm_sregs *sregs);
void setup_long_mode(struct vm *v, struct kvm_sregs *sregs, size_t mem_size, size_t page_size);
int setup_registers(struct vm *v, size_t mem_size);


int 
vm_init(int kvm_fd, struct vm *v, size_t mem_size)
{
	struct kvm_userspace_memory_region region;	

	memset(v, 0, sizeof(*v));
	v->kvm_fd = kvm_fd;
	v->vm_fd = v->vcpu_fd = -1;
	v->mem = MAP_FAILED;
	v->run = MAP_FAILED;
	v->run_mmap_size = 0;
	v->mem_size = mem_size;

    // Creating a VM via KVM
	v->vm_fd = ioctl(v->kvm_fd, KVM_CREATE_VM, 0);
	if (v->vm_fd < 0) 
    {
		perror("KVM_CREATE_VM");
		return -1;
	}

    // Mapping/Allocating memory for the guest
	v->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (v->mem == MAP_FAILED) 
    {
		perror("mmap user_mem");
		return -1;
	}

    // Setting guest physical memory region
	region.slot = 0;
	region.flags = 0;
	region.guest_phys_addr = 0;
	region.memory_size = v->mem_size;
	region.userspace_addr = (uintptr_t)v->mem;
    if (ioctl(v->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) 
    {
		perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
	}

    // Creating a VCPU for the VM
	v->vcpu_fd = ioctl(v->vm_fd, KVM_CREATE_VCPU, 0);
    if (v->vcpu_fd < 0) 
    {
		perror("KVM_CREATE_VCPU");
        return -1;
	}

    // Getting the size required for mapping the VCPU via its fd into a KVM_RUN struct
	v->run_mmap_size = ioctl(v->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (v->run_mmap_size <= 0) 
    {
		perror("KVM_GET_VCPU_MMAP_SIZE");
		return -1;
	}

    // Mapping the KVM_RUN
	v->run = mmap(NULL, v->run_mmap_size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, v->vcpu_fd, 0);
	if (v->run == MAP_FAILED) 
    {
		perror("mmap KVM_RUN");
		return -1;
	}

	return 0;
}

int 
load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr) 
{
	FILE *guest_img_file = fopen(image_path, "rb");
    // Opening the guest image file
	if (!guest_img_file) 
    {
		perror("Failed to open guest image");
		return -1;
	}

    // Checking the size of the image    
    if (fseek(guest_img_file, 0, SEEK_END) < 0)
    {
        perror("Failed to seek to the end of guest image");
		fclose(guest_img_file);
		return -1;
    }

	long guest_img_size = ftell(guest_img_file);
	if (guest_img_size < 0) 
    {
		perror("Failed to get size of guest image");
		fclose(guest_img_file);
		return -1;
	}
	rewind(guest_img_file);

	if((uint64_t)guest_img_size > v->mem_size - load_addr) 
    {
		printf("Guest image is too large for the VM memory\n");
		fclose(guest_img_file);
		return -1;
	}

    // Loading the code within the image into guest memory
	if (fread((uint8_t*)v->mem + load_addr, 1, (size_t)guest_img_size, guest_img_file) != (size_t)guest_img_size) 
    {
		perror("Failed to read guest image and load the code");
		fclose(guest_img_file);
		return -1;
	}
	fclose(guest_img_file);

	return 0;
}

void 
setup_segments_64(struct kvm_sregs *sregs)
{
    // Selectors not needed 
	// code.selector = 0x8, // Code segment selector
	struct kvm_segment code = {
		.base = 0,
		.limit = 0xffffffff,
		.present = 1, // Present or loaded into memory
		.type = 11, // Code: execute, read, accessed
		.dpl = 0, // Descriptor Privilage Level: 0 (0, 1, 2, 3)
		.db = 0, // Default size - ignored in long mode
		.s = 1, // System bit: 0 - System segment, 1 - Code/data segment
		.l = 1, // Long mode flag - only used for code segments
		.g = 1, // 4KB granularity
	};
	struct kvm_segment data = code;
	data.type = 3; // Data: read, write, accessed
	data.l = 0; // Ignored 
	// data.selector = 0x10; // Data segment selector

	sregs->cs = code;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = data;
}

void
setup_long_mode(struct vm *v, struct kvm_sregs *sregs, size_t mem_size, size_t page_size)
{
	// 9 bits are used for adressing the table => 512 entries per table => tables aligned on 4KB (12 zeros)
	uint64_t page_count = mem_size/page_size; // Pages required for covering physical memory
	uint64_t pt_count = page_count/MAP_TABLE_SIZE; // Last level tables required for above mentioned pages
	uint64_t current_address;
	
	
	// Page Map Level 4 Table
	uint64_t pml4_addr = 0x1000; // Address chosen for level 4 page table
	uint64_t *pml4 = (void *)(v->mem + pml4_addr); // Offset to real address
	// Page Directory Pointer Table
	uint64_t pdpt_addr = 0x2000;
	uint64_t *pdpt = (void *)(v->mem + pdpt_addr);
	// Page Directory Table
	uint64_t pd_addr = 0x3000;
	uint64_t *pd = (void *)(v->mem + pd_addr);

	// Setting first entries for PML4T and PDPT
	pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr; 
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

	
	switch (page_size)
	{
	case SIZE_4KB:
		// Setting up Page table 0

		// Page Table
		uint64_t pt_addr = 0x4000;
		uint64_t *pt = (void *)(v->mem + pt_addr);
 
		pd[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pt_addr;
		// RIP will be mapped to PT[0]
		pt[0] = GUEST_START_ADDR | PDE64_PRESENT | PDE64_RW | PDE64_USER;
		// RSP will be mapped to PT[511]
		pt[511] = (mem_size - SIZE_4KB) | PDE64_PRESENT | PDE64_RW | PDE64_USER;
		
		current_address = 0x5000;
		for (int i=1; i<MAP_TABLE_SIZE && current_address < mem_size - SIZE_4KB - 1; i++, current_address += MAP_TABLE_OFFSET)
			pt[i] = current_address | PDE64_PRESENT | PDE64_RW | PDE64_USER; 
			
		
		pt_count--;
		uint64_t current_table = 1;
		
		// Mapping leftover pages
		while (pt_count > 0)
		{
			pd[current_table] = PDE64_PRESENT | PDE64_RW | PDE64_USER | current_address;
			pt = (void *)(v->mem + current_address);
			current_address += MAP_TABLE_OFFSET;
			for (int i=0; i<MAP_TABLE_SIZE && current_address < mem_size - SIZE_4KB  - 1; i++, current_address += MAP_TABLE_OFFSET)
				pt[i] = current_address | PDE64_PRESENT | PDE64_RW | PDE64_USER;
			
			pt_count--;
			current_table++;
		}
		
		
		break;
	case SIZE_2MB:
		// Setting up PD Table

		pd[0] = 0 | PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;

		page_count--;
		int current_page = 1;
		current_address = 0x200000;

		while (page_count > 0)
		{
			pd[current_page] = current_address | PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
			current_address += SIZE_2MB;
			current_page++;
			page_count--;
		}

		break;
	default:
		break;
	}


    // Setting up control registers 
	sregs->cr3  = pml4_addr; // Registering level 4 table
	sregs->cr4  = CR4_PAE; // "Physical Address Extension" enabled for long mode
	sregs->cr0  = CR0_PE | CR0_PG; // "Protected Mode Enable" and "Paging" enabled
	sregs->efer = EFER_LME | EFER_LMA; // "Long Mode Active" and "Long Mode Enable"

	// 64-bit segment initialization
	// printf("Setting up segments\n");
	setup_segments_64(sregs);
	// printf("Done setting up long mode\n");
}

int 
setup_registers(struct vm *v, size_t mem_size)
{
	struct kvm_regs regs;
	memset(&regs, 0, sizeof(regs));
	regs.rflags = 0x2;
	
	regs.rip = 0; 
	regs.rsp = SIZE_2MB; // Placing the stack at the top of the first 2MB

	if (ioctl(v->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
		perror("KVM_SET_REGS");
		return -1;
	}
	return 0;
}


// Guest service
void 
inb(struct vm *v)
{
	uint8_t value = getchar();
	value = value != EOF ? value : 0;
	*((char*)v->run + v->run->io.data_offset) = value;
}

void 
outb(struct vm *v)
{
	char *p = (char *)v->run;
	printf("%c", *(p + v->run->io.data_offset));
}


// KVM ctl and VM creation and ctl
int 
kvm_init()
{
	// Opening KVM
	int kvm_fd = open("/dev/kvm", O_RDWR);
	if (kvm_fd < 0) 
    {
		perror("open /dev/kvm");
		return -1;
	}

    // Checking KVM version
    int api = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api != KVM_API_VERSION) 
    {
        printf("KVM API mismatch: kernel=%d headers=%d\n", api, KVM_API_VERSION);
		close(kvm_fd);
        return -1;
    }

	return kvm_fd;
}

void 
kvm_close(int kvm_fd)
{
	if (kvm_fd >= 0) 
    {
		close(kvm_fd);
	}
}

int 
vm_create(int kvm_fd, struct vm *v, size_t mem_size, size_t page_size, char *guest_image)
{
	struct kvm_sregs sregs;
	

	if (vm_init(kvm_fd, v, mem_size)) {
		printf("Failed to init the VM\n");
		return 1;
	}

	if (ioctl(v->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
		perror("KVM_GET_SREGS");
		vm_destroy(v);
		return 1;
	}

	setup_long_mode(v, &sregs, mem_size, page_size);

    if (ioctl(v->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
		perror("KVM_SET_SREGS");
		vm_destroy(v);
		return 1;
	}

	if (load_guest_image(v, guest_image, GUEST_START_ADDR) < 0) {
		printf("Failed to load guest image\n");
		vm_destroy(v);
		return 1;
	}

	if (setup_registers(v,mem_size) < 0){
		vm_destroy(v);
		return 1;
	}

    return 0;
}

int 
vm_run(struct vm *v, pthread_t thread_id)
{
	int stop = 0;
	int stat = 0;
	printf("Starting the VM\n");
	while(stop == 0) {
		stat = ioctl(v->vcpu_fd, KVM_RUN, 0);
		if (stat == -1) {
			printf("KVM_RUN failed\n");
			vm_destroy(v);
			return -1;
		}

		switch (v->run->exit_reason) {
			case KVM_EXIT_IO:
				if (v->run->io.port == 0xE9) 
				{
					switch (v->run->io.direction)
					{
					case KVM_EXIT_IO_OUT:
						outb(v);
						break;
					case KVM_EXIT_IO_IN:
						inb(v);
						break;
					}
				}
				continue;
			case KVM_EXIT_HLT:
				printf("Thread %lu KVM_EXIT_HLT\n", thread_id);
				stop = 1;
				break;
			case KVM_EXIT_SHUTDOWN:
				stop = 1;
				break;
			default:
				break;
    	}
  	}
	return 0;
}

void 
vm_destroy(struct vm *v) 
{
    // Unmapping RUN and VMEM
	if (v->run && v->run != MAP_FAILED) 
    {
		munmap(v->run, (size_t)v->run_mmap_size);
		v->run = MAP_FAILED;
	}

	if(v->mem && v->mem != MAP_FAILED) 
    {
		munmap(v->mem, v->mem_size);
		v->mem = MAP_FAILED;
	}

    // Closing VCPU and VM
	if (v->vcpu_fd >= 0) 
    {
		close(v->vcpu_fd);
		v->vcpu_fd = -1;
	}

	if (v->vm_fd >= 0) 
    {
		close(v->vm_fd);
		v->vm_fd = -1;
	}
}

void* 
vm_start(void* vm)
{
    struct vm *v = (struct vm*)vm;
    pthread_t tid = pthread_self();

    vm_run(v, tid);

    vm_destroy(v);
}

// VM Thread List
struct vm_node* 
get_node_vm(struct vm_node *head, int index)
{
    struct vm_node *current = head;
    int id = 0;
    while (current){
        if (id == index)
            break;
        id++;
        current = current->next;
    }
    return current;
}

void 
push_node_vm(struct vm_node **head, struct vm_node *node)
{
    if (!*head)
    {
        *head = node;
        return;
    }
    struct vm_node *current;
    for (current = *head; current->next; current = current->next);
    current->next = node;
}

struct vm_node* 
remove_node_vm(struct vm_node **head)
{
    if (!*head)
        return NULL;
    struct vm_node *node = *head;
    *head = node->next;
    node->next = NULL;
    return node;
}

// Guest Img List
char* 
get_file(struct file_node **head)
{
	if (!head || !*head) return NULL;
	struct file_node *node = *head;
	*head = (*head)->next;
	char* ret = node->file_name;
	free(node);
	return ret;
}

void 
push_file(struct file_node **head, char *name)
{
	if (!head) return;
	if (!*head)
	{
		*head = (struct file_node*) malloc(sizeof(struct file_node));
		(*head)->file_name = name;
		(*head)->next = NULL;
		return;
	}
	struct file_node *current = *head;
	for (; current->next; current = current->next);
	struct file_node *node = (struct file_node*) malloc(sizeof(struct file_node));
	node->file_name = name;
	node->next = NULL;
	current->next = node;
	return;
}
