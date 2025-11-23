
#include "my_vm.h"
#include <string.h>   // optional for memcpy if you later implement put/get
#include <pthread.h>

// -----------------------------------------------------------------------------
// Global Declarations (optional)
// -----------------------------------------------------------------------------

char* phys_mem = NULL; 

pde_t *page_dir = NULL; 

int num_phys_pages = 0;
int num_virt_pages = 0;

char *phys_bitmap = NULL;
char *virt_bitmap = NULL;

pthread_mutex_t vm_lock = PTHREAD_MUTEX_INITIALIZER;


struct tlb tlb_store; // Placeholder for your TLB structure

// Optional counters for TLB statistics
static unsigned long long tlb_lookups = 0;
static unsigned long long tlb_misses  = 0;

// -----------------------------------------------------------------------------
// Bit Functions
// -----------------------------------------------------------------------------
int get_bit(char * bitmap, int index) {
    int byte_idx = index / 8;
    int bit_idx = index % 8;
    int mask = 1 << bit_idx;
    int result = (bitmap[byte_idx] & mask) >> bit_idx;
    return result;
}

void set_bit(char * bitmap, int index) {
    int byte_idx = index/8;
    int bit_idx = index%8;
    int mask = 1 << bit_idx;
    bitmap[byte_idx] = bitmap[byte_idx] | mask;
    return;
}

void clear_bit(char * bitmap, int index) {
    int byte_idx = index/8;
    int bit_idx = index%8;
    int mask = ~(1 << bit_idx);
    bitmap[byte_idx] = bitmap[byte_idx] & mask;
    return;
}


// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
/*
 * set_physical_mem()
 * ------------------
 * Allocates and initializes simulated physical memory and any required
 * data structures (e.g., bitmaps for tracking page use).
 *
 * Return value: None.
 * Errors should be handled internally (e.g., failed allocation).
 */
void set_physical_mem(void) {
    // TODO: Implement memory allocation for simulated physical memory.
    // Use 32-bit values for sizes, page counts, and offsets.

    if (phys_mem != NULL) {
        return ; // already initialized
    }

    pthread_mutex_lock(&vm_lock);

    phys_mem = malloc(MEMSIZE);
    if (!phys_mem) {
        fprintf(stderr, "Error allocating physical memory\n");
        exit(1);
    }

    // 1 bit per page
    num_phys_pages = MEMSIZE / PGSIZE;
    num_virt_pages = MAX_MEMSIZE / PGSIZE;
    
    phys_bitmap = calloc(num_phys_pages / 8, 1);
    virt_bitmap = calloc(num_virt_pages / 8, 1);

    int num_entries = 1 << 10; // 1024
    page_dir = calloc(num_entries, sizeof(pde_t));  
    if (!page_dir) {
        fprintf(stderr, "Error allocating page directory\n");
        exit(1);
    }

    for (int i = 0; i < TLB_ENTRIES; i++) {
        tlb_store.entries[i].valid = false;
    }

    pyhread_mutex_unlock(&vm_lock);

}

// -----------------------------------------------------------------------------
// TLB
// -----------------------------------------------------------------------------

/*
 * TLB_add()
 * ---------
 * Adds a new virtual-to-physical translation to the TLB.
 * Ensure thread safety when updating shared TLB data.
 *
 * Return:
 *   0  -> Success (translation successfully added)
 *  -1  -> Failure (e.g., TLB full or invalid input)
 */
int TLB_add(void *va, void *pa)
{
    // TODO: Implement TLB insertion logic.
    return -1; // Currently returns failure placeholder.
}

/*
 * TLB_check()
 * -----------
 * Looks up a virtual address in the TLB.
 *
 * Return:
 *   Pointer to the corresponding page table entry (PTE) if found.
 *   NULL if the translation is not found (TLB miss).
 */
pte_t *TLB_check(void *va)
{
    // TODO: Implement TLB lookup.
    return NULL; // Currently returns TLB miss.
}

/*
 * print_TLB_missrate()
 * --------------------
 * Calculates and prints the TLB miss rate.
 *
 * Return value: None.
 */
void print_TLB_missrate(void)
{
    double miss_rate = 0.0;
    // TODO: Calculate miss rate as (tlb_misses / tlb_lookups).
    if (tlb_lookups > 0) {
        miss_rate = (double)tlb_misses / (double)tlb_lookups;
    }
    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}

// -----------------------------------------------------------------------------
// Page Table
// -----------------------------------------------------------------------------

/*
 * translate()
 * -----------
 * Translates a virtual address to a physical address.
 * Perform a TLB lookup first; if not found, walk the page directory
 * and page tables using a two-level lookup.
 *
 * Return:
 *   Pointer to the PTE structure if translation succeeds.
 *   NULL if translation fails (e.g., page not mapped).
 */
pte_t *translate(pde_t *pgdir, void *va)
{
    // TODO: Extract the 32-bit virtual address and compute indices
    // for the page directory, page table, and offset.
    // Return the corresponding PTE if found.

    vaddr32_t va_u = VA2U(va);
    tlb_lookups++;

    pte_t *tlb_pte = TLB_check(va);
    if (tlb_pte != NULL) {
        return tlb_pte;
    }

    tlb_misses++;

    uint32_t pd_index = PDX(va_u);
    uint32_t pt_index = PTX(va_u);
    uint32_t offset = OFF(va_u);

    pde_t pde = pgdir[pd_index];
    if (pde == 0) {
        return NULL;
    }

    pte_t * page_table = (pte_t *)(uintptr_t)pde;
    pte_t pte = page_table[pt_index];
    
    if (pte != 0) {
        TLB_add(va, (void *)(uintptr_t)pte);
        return &page_table[pt_index];
    }

    return NULL; // Translation unsuccessful placeholder.
}


void * allocate_phys_page() {
    for (uint32_t i = 0; i < num_phys_pages; i++) {
        if (get_bit(phys_bitmap, i) == 0) {
            set_bit(phys_bitmap, i);
            return phys_mem + (i * PGSIZE);
        }
    }
    return NULL; // No free page found
}
/*
 * map_page()
 * -----------
 * Establishes a mapping between a virtual and a physical page.
 * Creates intermediate page tables if necessary.
 *
 * Return:
 *   0  -> Success (mapping created)
 *  -1  -> Failure (e.g., no space or invalid address)
 */
int map_page(pde_t *pgdir, void *va, void *pa)
{
    // TODO: Map virtual address to physical address in the page tables.
    vaddr32_t va_u = VA2U(va);
    paddr32_t pa_u = (paddr32_t)(uintptr_t)pa;

    uint32_t pd_index = PDX(va_u);
    uint32_t pt_index = PTX(va_u);
    uint32_t offset = OFF(va_u);

    pde_t pde = pgdir[pd_index];

    if (pde == 0) { // page table doesnt exist
        void* new_table = allocate_phys_page();

        if (new_table == NULL) { 
            // no free pages left
            return -1;
        }

        memset(new_table, 0, PGSIZE);
        
        pgdir[pd_index] = (pde_t)(uintptr_t)new_table;

        pde = pgdir[pd_index];
    }

    
    pte_t * page_table = (pte_t *)(uintptr_t)pde; // physical address -> ptr
    uint32_t pfn = pa_u >> PFN_SHIFT;
    page_table[pt_index] = (pfn << PFN_SHIFT);
    uint32_t vpn = va_u >> PFN_SHIFT;

    // update bitmaps
    set_bit(phys_bitmap, pfn);
    set_bit(virt_bitmap, vpn); 

    // add to TLB
    TLB_add(va, pa);

    return 0; 
}

// -----------------------------------------------------------------------------
// Allocation
// -----------------------------------------------------------------------------

/*
 * get_next_avail()
 * ----------------
 * Finds and returns the base virtual address of the next available
 * block of contiguous free pages.
 *
 * Return:
 *   Pointer to the base virtual address if available.
 *   NULL if there are no sufficient free pages.
 */
void *get_next_avail(int num_pages)
{
    // TODO: Implement virtual bitmap search for free pages.
    
    pthread_mutex_lock(&vm_lock);
    uint32_t free_pages = 0;
    for (uint32_t vpn = 0; vpn < num_virt_pages; vpn++) {
        if (get_bit(virt_bitmap, vpn) == 1) {
            free_pages = 0;
        } else {
            free_pages++;
        }


        if (free_pages >= num_pages) {
            // Found consecutive free pages
            uint32_t start_vpn = vpn - num_pages + 1;

            for (uint32_t i = 0; i<num_pages; i++) {
                set_bit(virt_bitmap, start_vpn + i);
            }
            pthread_mutex_unlock(&vm_lock);
            vaddr32_t base_va = (vaddr32_t)(start_vpn << PFN_SHIFT);
            return U2VA(base_va);
        }

    }
    
    pthread_mutex_unlock(&vm_lock);
    return NULL; // No available block placeholder.
}

/*
 * n_malloc()
 * -----------
 * Allocates a given number of bytes in virtual memory.
 * Initializes physical memory and page directories if not already done.
 *
 * Return:
 *   Pointer to the starting virtual address of allocated memory (success).
 *   NULL if allocation fails.
 */
void *n_malloc(unsigned int num_bytes)
{
    // TODO: Determine required pages, allocate them, and map them.

    if (num_bytes == 0) {
        return NULL;
    }

    if (phys_mem == NULL) {
        set_physical_mem();
    }
    int num_pages = (num_bytes + PGSIZE - 1) / PGSIZE; // ceiling division
    if (num_pages <= 0){
        return NULL;
    }

    void *base_va = get_next_avail(num_pages);
    if (base_va == NULL) {
        return NULL; // No available virtual pages
    }
    for(int i = 0; i < num_pages; i++){
        void *curr_va = (char*)base_va + i * PGSIZE;

        pthread_mutex_lock(&vm_lock);

        void *curr_pa = allocate_phys_page();

        if (curr_pa == NULL) {
            pthread_mutex_unlock(&vm_lock);
            // Free previously allocated pages
            return NULL; // No available physical pages
        }

        if (map_page(page_dir, curr_va, curr_pa) != 0) {
            pthread_mutex_unlock(&vm_lock);
            // Free previously allocated pages
            return NULL; // Mapping failure
        }
        pthread_mutex_unlock(&vm_lock);
    }
    return base_va; // Allocation failure placeholder.
}

/*
 * n_free()
 * ---------
 * Frees one or more pages of memory starting at the given virtual address.
 * Marks the corresponding virtual and physical pages as free.
 * Removes the translation from the TLB.
 *
 * Return value: None.
 */
void n_free(void *va, int size)
{
    // TODO: Clear page table entries, update bitmaps, and invalidate TLB.
    


}

// -----------------------------------------------------------------------------
// Data Movement
// -----------------------------------------------------------------------------

/*
 * put_data()
 * ----------
 * Copies data from a user buffer into simulated physical memory using
 * the virtual address. Handle page boundaries properly.
 *
 * Return:
 *   0  -> Success (data written successfully)
 *  -1  -> Failure (e.g., translation failure)
 */
int put_data(void *va, void *val, int size)
{
    // TODO: Walk virtual pages, translate to physical addresses,
    // and copy data into simulated memory.
    



    return -1; // Failure placeholder.
}

/*
 * get_data()
 * -----------
 * Copies data from simulated physical memory (accessed via virtual address)
 * into a user buffer.
 *
 * Return value: None.
 */
void get_data(void *va, void *val, int size)
{
    // TODO: Perform reverse operation of put_data().
    //
}

// -----------------------------------------------------------------------------
// Matrix Multiplication
// -----------------------------------------------------------------------------

/*
 * mat_mult()
 * ----------
 * Performs matrix multiplication of two matrices stored in virtual memory.
 * Each element is accessed and stored using get_data() and put_data().
 *
 * Return value: None.
 */
void mat_mult(void *mat1, void *mat2, int size, void *answer)
{
    int i, j, k;
    uint32_t a, b, c;

    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            c = 0;
            for (k = 0; k < size; k++) {
                // TODO: Compute addresses for mat1[i][k] and mat2[k][j].
                // Retrieve values using get_data() and perform multiplication.
                get_data(NULL, &a, sizeof(int));  // placeholder
                get_data(NULL, &b, sizeof(int));  // placeholder
                c += (a * b);
            }
            // TODO: Store the result in answer[i][j] using put_data().
            put_data(NULL, (void *)&c, sizeof(int)); // placeholder
        }
    }
}

