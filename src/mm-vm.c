// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, int vicfpn , int swpfpn)
{
    __swap_cp_page(caller->mram, vicfpn, caller->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
// Hàm này cấp phát và trả về một vùng nhớ mới (region) được căn chỉnh tại vị trí sbrk hiện tại của VMA,
// đảm bảo rằng vùng nhớ đó có kích thước `size` và được căn chỉnh theo `alignedsz`.
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, int size, int alignedsz)
{
    struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
    if (newrg == NULL) return NULL;

    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
    if (cur_vma == NULL) {
        free(newrg);
        return NULL;
    }

    // Căn chỉnh địa chỉ sbrk lên bội số gần nhất của alignedsz
    newrg->rg_start = (cur_vma->sbrk + alignedsz - 1) / alignedsz * alignedsz;

    // Xác định địa chỉ kết thúc vùng nhớ
    newrg->rg_end = newrg->rg_start + size;

    return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
 int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, int vmastart, int vmaend)
 {
   struct vm_area_struct *vma = caller->mm->mmap;
   if (vma == NULL) return -1; // Không có vma nào trong tiến trình
 
   while (vma != NULL)
   {
     if (vmaid == vma->vm_id){
       vma = vma->vm_next;
       continue; // Bỏ qua chính vma đang được mở rộng
     }
 
     // Kiểm tra 3 trường hợp chồng lấp vùng nhớ:
     // 1. Điểm bắt đầu nằm trong vùng vma khác
     // 2. Điểm kết thúc nằm trong vùng vma khác
     // 3. Bao trùm toàn bộ một vma khác
     if ((vmastart >= vma->vm_start && vmastart < vma->vm_end) || 
         (vmaend > vma->vm_start && vmaend <= vma->vm_end) ||
         (vmastart < vma->vm_start && vmaend > vma->vm_end)) 
       return -1; // Phát hiện chồng lấp
 
     vma = vma->vm_next;
   }
 
   return 0; // Không chồng lấp, hợp lệ
 }

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
 int inc_vma_limit(struct pcb_t *caller, int vmaid, int inc_sz)
 {
   struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
   int inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);  // Căn chỉnh kích thước theo trang
   int incnumpage =  inc_amt / PAGING_PAGESZ;  // Tính số trang cần cấp phát
   struct vm_rg_struct *region = get_vm_area_node_at_brk(caller, vmaid, inc_sz, PAGING_PAGESZ);
   struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
   if (region == NULL) return -1; // Không thể tạo vùng nhớ mới
 
   int old_end = cur_vma->vm_end;
 
   if (validate_overlap_vm_area(caller, vmaid, region->rg_start, region->rg_end) == -1) return -1; // Trùng vùng nhớ, không cấp phát
 
   cur_vma->vm_end += inc_amt;  // Mở rộng vùng nhớ của vma
   cur_vma->sbrk = cur_vma->vm_end; // Cập nhật điểm cuối heap mới sau khi tăng vm_end
 
   if (vm_map_ram(caller, region->rg_start, region->rg_end, 
                     old_end, incnumpage , newrg) < 0){
     free(newrg);
     cur_vma->vm_end = old_end;
     return -1; // Không thể ánh xạ vùng nhớ vào RAM
   }
 
   return 0;
 }

// #endif
