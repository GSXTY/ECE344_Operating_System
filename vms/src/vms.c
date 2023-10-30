#include "vms.h"
#include "mmu.h"
#include "pages.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT 0
#define COW 1

static int ref_track[MAX_PAGES] = {0};

void page_fault_handler(void* virtual_address, int level, void* page_table) {
  uint64_t* pte_entry = vms_page_table_pte_entry(page_table, virtual_address, level);
  if (vms_pte_custom(pte_entry)) {
    void* current_page = vms_ppn_to_page(vms_pte_get_ppn(pte_entry));
    int page_idx = vms_get_page_index(current_page);
    vms_pte_write_set(pte_entry);    
    vms_pte_custom_clear(pte_entry);   
    if (0 == ref_track[page_idx]) return;
    void* new_page = vms_new_page();
    memcpy(new_page, current_page, PAGE_SIZE);
    vms_pte_set_ppn(pte_entry, vms_page_to_ppn(new_page));
    -- ref_track[page_idx];
  }
}

void* copy(int mode) {
  void* old_l2 = vms_get_root_page_table();
  void* new_l2 = vms_new_page();

  for (int i = 0; i < NUM_PTE_ENTRIES; ++ i) {
    uint64_t* l2_entry_original = vms_page_table_pte_entry_from_index(old_l2, i);
    uint64_t* l2_entry_forked = vms_page_table_pte_entry_from_index(new_l2, i);

    if (!vms_pte_valid(l2_entry_original)) continue;
    void* l1_original = vms_ppn_to_page(vms_pte_get_ppn(l2_entry_original));
    void* l1_forked = vms_new_page();

    for (int j = 0; j < NUM_PTE_ENTRIES; ++ j) {
      uint64_t* l1_entry_original = vms_page_table_pte_entry_from_index(l1_original, j);
      uint64_t* l1_entry_forked = vms_page_table_pte_entry_from_index(l1_forked, j);

      if (!vms_pte_valid(l1_entry_original)) continue;
      void* l0_original = vms_ppn_to_page(vms_pte_get_ppn(l1_entry_original));
      void* l0_forked = vms_new_page();

      for (int k = 0; k < NUM_PTE_ENTRIES; ++ k) {
        uint64_t* l0_entry_original = vms_page_table_pte_entry_from_index(l0_original, k);
        uint64_t* l0_entry_forked = vms_page_table_pte_entry_from_index(l0_forked, k);

        if (!vms_pte_valid(l0_entry_original)) continue;

        if (DEFAULT == mode) {
          void* p0 = vms_ppn_to_page(vms_pte_get_ppn(l0_entry_original));
          void* p0_forked = vms_new_page();
          memcpy(p0_forked, p0, PAGE_SIZE);
          *l0_entry_forked = *l0_entry_original;  
          vms_pte_set_ppn(l0_entry_forked, vms_page_to_ppn(p0_forked));
        } else if (COW == mode) {
          if (!vms_pte_write(l0_entry_original) && !vms_pte_custom(l0_entry_original)) {
            *l0_entry_forked = *l0_entry_original;
            continue;
          }
          vms_pte_write_clear(l0_entry_forked);
          vms_pte_custom_set(l0_entry_forked);
          vms_pte_write_clear(l0_entry_original);
          vms_pte_custom_set(l0_entry_original);
          void* page_num_ptr = vms_ppn_to_page(vms_pte_get_ppn(l0_entry_original));
          int page_idx = vms_get_page_index(page_num_ptr);
          ++ ref_track[page_idx];
          *l0_entry_forked = *l0_entry_original;
        }
      }
      *l1_entry_forked = *l1_entry_original;  
      vms_pte_set_ppn(l1_entry_forked, vms_page_to_ppn(l0_forked));  
    }
    *l2_entry_forked = *l2_entry_original; 
    vms_pte_set_ppn(l2_entry_forked, vms_page_to_ppn(l1_forked));
  }
  return new_l2;
}

void* vms_fork_copy() {
  return copy(DEFAULT);
}

void* vms_fork_copy_on_write() {
  return copy(COW);
}
