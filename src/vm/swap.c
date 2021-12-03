#include "vm/swap.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/page.h"

#define SECTOR_NUM (PGSIZE / BLOCK_SECTOR_SIZE)

static struct bitmap *swap_valid_table;
static struct block *swap_disk;

void init_swap_valid_table()
{
    swap_disk = block_get_role(BLOCK_SWAP);
    swap_valid_table = bitmap_create(block_size(swap_disk) / SECTOR_NUM);

    bitmap_set_all(swap_valid_table, true);
}

void swap_in(struct spte *page, void *kva)
{
    int i;
    int id = page->swap_id;

    if (id > bitmap_size(swap_valid_table) || id < 0)
    {
        sys_exit(-1);
    }

    if (bitmap_test(swap_valid_table, id) == true)
    {
        /* This swapping slot is empty. */
        sys_exit(-1);
    }

    for (i = 0; i < SECTOR_NUM; i++)
    {
        block_read(swap_disk, id * SECTOR_NUM + i, kva + (i * BLOCK_SECTOR_SIZE));
    }

    bitmap_set(swap_valid_table, id, false);
}

int swap_out(void *kva)
{
    int i;
    int id = bitmap_scan(swap_valid_table, 0, 1, true);

    for (i = 0; i < SECTOR_NUM; ++i)
    {
        block_write(swap_disk, id * SECTOR_NUM + i, kva + (BLOCK_SECTOR_SIZE * i));
    }

    bitmap_set(swap_valid_table, id, false);

    return id;
}
