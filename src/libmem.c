/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */


// Hàm thêm một vùng nhớ trống (region) mới vào danh sách vùng nhớ trống hiện tại của tiến trình theo thứ tự tăng dần các chỉ số
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;
  struct vm_rg_struct *prev_rg_node = NULL;

  // Kiểm tra vùng nhớ hợp lệ
  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  // Tìm vị trí thích hợp để chèn vùng nhớ mới theo thứ tự tăng dần
  while(rg_node){
    if(rg_node->rg_start >= rg_elmt->rg_end){
      if(prev_rg_node == NULL){
        // Chèn vào đầu danh sách
        rg_elmt->rg_next = mm->mmap->vm_freerg_list;
        mm->mmap->vm_freerg_list = rg_elmt;
      }
      else{
        // Chèn vào giữa danh sách
        prev_rg_node->rg_next = rg_elmt;
        rg_elmt->rg_next = rg_node;
      }
      break;
    }
    prev_rg_node = rg_node;
    rg_node = rg_node->rg_next;
  }

  // Nếu chưa chèn được, thêm vào cuối danh sách
  if (rg_node == NULL) {
    // Danh sách rỗng
    if(prev_rg_node == NULL) {
      mm->mmap->vm_freerg_list = rg_elmt;
    }
    else{
      prev_rg_node->rg_next = rg_elmt;
    }
    rg_elmt->rg_next = NULL;
  }
  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
{
  /* Bảo vệ vùng nhớ dùng mutex */
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct rgnode;

  // Thử cấp phát từ vùng nhớ trống hiện tại
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    caller->mm->symrgtbl[rgid].rg_next = NULL;

    *alloc_addr = rgnode.rg_start;

#ifdef DEBUG
  printf("=========== PHYSICAL MEMORY AFTER (NO-SYSCALL) ALLOCATION ===========\n");
  printf("PID=%d - Region=%d - Address=%08x - Size=%d byte\n", caller->pid, rgid, *alloc_addr, size);
#endif
#ifdef PAGETBL_DUMP
  print_pgtbl(caller, 0, -1); //print max TBL
#endif
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  // Không đủ vùng nhớ, gọi syscall để mở rộng heap
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int old_sbrk = cur_vma->sbrk;
  int old_end = cur_vma->vm_end;

  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = size;

  // Gọi syscall để mở rộng vùng nhớ
  if(syscall(caller, 17, &regs) < 0)
  {
    cur_vma->vm_end = old_end;
    cur_vma->sbrk = old_sbrk;
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  // Cập nhật thông tin vùng nhớ vừa cấp phát
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  caller->mm->symrgtbl[rgid].rg_next = NULL;
  
  *alloc_addr = old_sbrk;

  // Tạo vùng nhớ trống còn dư sau khi cấp phát
  struct vm_rg_struct * new_free_rg = malloc(sizeof(struct vm_rg_struct));
  new_free_rg->rg_start = old_sbrk + size;
  new_free_rg->rg_end = cur_vma->sbrk;
  new_free_rg->rg_next = NULL;

  enlist_vm_freerg_list(caller->mm, new_free_rg); // Đưa vào danh sách vùng nhớ trống

#ifdef DEBUG
    printf("=========== PHYSICAL MEMORY AFTER (SYSCALL) ALLOCATION ===========\n");
    printf("PID=%d - Region=%d - Address=%08x - Size=%d byte\n", caller->pid, rgid, *alloc_addr, size);
#ifdef PAGETBL_DUMP
    print_pgtbl(caller, 0, -1); //print max TBL
#endif
#endif

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  // Kiểm tra chỉ số hợp lệ
  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ){
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  
  // Tạo vùng nhớ đại diện cho vùng bị thu hồi
  struct vm_rg_struct * new_frrg = malloc(sizeof(struct vm_rg_struct));

  unsigned long begin = caller->mm->symrgtbl[rgid].rg_start;
  unsigned long end = caller->mm->symrgtbl[rgid].rg_end;

  struct vm_rg_struct * tmp_fr_rg = caller->mm->mmap->vm_freerg_list;
  struct vm_rg_struct * prev_fr_rg = NULL;
  new_frrg->rg_start = begin;
  new_frrg->rg_end = end;

  // Gộp vùng nhớ thu hồi với các vùng nhớ trống liền kề
  while(tmp_fr_rg){
    struct vm_rg_struct *next = tmp_fr_rg->rg_next;
    int merged = 0;
  
    if(tmp_fr_rg->rg_end == new_frrg->rg_start){
      new_frrg->rg_start = tmp_fr_rg->rg_start;
      merged = 1;
    } 
    else if(tmp_fr_rg->rg_start == new_frrg->rg_end){
      new_frrg->rg_end = tmp_fr_rg->rg_end;
      merged = 1;
    }
  
    if(merged){
      // Loại bỏ vùng đã bị gộp khỏi danh sách
      if(prev_fr_rg == NULL){
        caller->mm->mmap->vm_freerg_list = next;
      } 
      else {
        prev_fr_rg->rg_next = next;
      }
      free(tmp_fr_rg);
      tmp_fr_rg = next;
      continue; // Không cập nhật prev nếu đã xóa phần tử hiện tại
    }
    prev_fr_rg = tmp_fr_rg;
    tmp_fr_rg = next;
  }

  // Thêm vùng nhớ đã gộp vào danh sách vùng nhớ trống
  if(enlist_vm_freerg_list(caller->mm, new_frrg) == -1) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  
  // Xóa thông tin vùng nhớ khỏi bảng ánh xạ
  caller->mm->symrgtbl[rgid].rg_start = 0;
  caller->mm->symrgtbl[rgid].rg_end = 0;
  caller->mm->symrgtbl[rgid].rg_next = NULL;
  
#ifdef DEBUG
    printf("=========== PHYSICAL MEMORY AFTER DEALLOCATION ===========\n");
    printf("PID=%d - Region=%d\n", caller->pid, rgid);
#ifdef PAGETBL_DUMP
    print_pgtbl(caller, 0, -1); //print max TBL
#endif
#endif

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  /* TODO Implement allocation on vm area 0 */
  int addr;
  /* By default using vmaid = 0 */
  return __alloc(proc, 0, reg_index, size, &addr);
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  /* TODO Implement free region */

  /* By default using vmaid = 0 */
  return __free(proc, 0, reg_index);
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = mm->pgd[pgn];

  /* Nếu trang chưa được load vào RAM */
  if (!PAGING_PAGE_PRESENT(pte))
  {
    int vicpgn, swpfpn, vicfpn;
    uint32_t vicpte;

    /* Tìm trang nạn nhân để thay thế (victim page) */
    find_victim_page(caller->mm, &vicpgn);
    vicpte = mm->pgd[vicpgn];
    vicfpn = PAGING_PTE_FPN(vicpte);

    /* Tìm frame trống trong bộ nhớ swap */
    if(MEMPHY_get_freefp(caller->active_mswp, &swpfpn) == -1) return -1;

    /* Gọi syscall để sao chép trang nạn nhân từ RAM -> SWAP */
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;
    regs.a3 = swpfpn;
    if(syscall(caller, 17, &regs) == -1) return -1;

    /* Cập nhật lại PTE của victim page, chuyển sang trạng thái swap */
    pte_set_swap(&mm->pgd[vicpgn], caller->active_mswp_id, swpfpn);

    /* Lấy frame vật lý đang chứa dữ liệu của trang cần nạp */
    int tgtfpn = PAGING_PTE_SWP(pte);

    /* Copy từ swap vào frame trống (là frame victim vừa bị thay thế) */
    if(__swap_cp_page(caller->active_mswp, tgtfpn, caller->mram, vicfpn) == -1) return -1;

    /* Cập nhật PTE cho trang đích, đánh dấu đã có mặt trong RAM */
    pte_set_fpn(&mm->pgd[pgn], vicfpn);
  }

  /* Thêm trang này vào danh sách FIFO của tiến trình */
  enlist_pgn_node(&caller->mm->fifo_pgn, pgn);

  /* Trả về frame number đã cấp phát */
  *fpn = PAGING_FPN(mm->pgd[pgn]);
  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);     // Tính chỉ số trang (page number)
  int offs = PAGING_OFFST(addr);  // Tính offset trong trang
  int fpn;

  // Đảm bảo trang đã có trong RAM (swap in nếu cần)
  if (pg_getpage(mm, pgn, &fpn, caller) == -1) return -1; // Truy cập trang không hợp lệ

  int phyaddr = fpn * PAGING_PAGESZ + offs;  // Tính địa chỉ vật lý

  // Gọi syscall để đọc dữ liệu từ RAM tại địa chỉ vật lý
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;

  if(syscall(caller, 17, &regs) == -1) return -1; // Đọc thất bại

  *data = regs.a3;  // Gán dữ liệu đọc được vào biến data

  return 0;
}
 

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
 int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
 {
   int pgn = PAGING_PGN(addr);     // Tính chỉ số trang từ địa chỉ ảo
   int offs = PAGING_OFFST(addr);  // Tính offset trong trang
   int fpn;
 
   // Đảm bảo trang đã có trong RAM (swap in nếu cần)
   if (pg_getpage(mm, pgn, &fpn, caller) == -1) return -1; // Truy cập trang không hợp lệ
 
   int phyaddr = fpn * PAGING_PAGESZ + offs;  // Tính địa chỉ vật lý tương ứng
 
   // Gọi syscall để ghi dữ liệu vào RAM tại địa chỉ vật lý
   struct sc_regs regs;
   regs.a1 = SYSMEM_IO_WRITE;
   regs.a2 = phyaddr;
   regs.a3 = value;
 
   if(syscall(caller, 17, &regs) == -1) return -1; // Ghi thất bại
 
   return 0;
 }
 

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    uint32_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  /* TODO update result of reading action*/
  //destination
  *destination = data;
#ifdef DEBUG
  printf("=========== PHYSICAL MEMORY AFTER READING ===========\n");
#endif
#ifdef IODUMP
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    uint32_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
#ifdef DEBUG
  printf("=========== PHYSICAL MEMORY AFTER WRITING ===========\n");
#endif
#ifdef IODUMP
  printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;


  for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte= caller->mm->pgd[pagenum];

    if (!PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_PTE_FPN(pte);
      MEMPHY_put_freefp(caller->mram, fpn);
    } else {
      fpn = PAGING_PTE_SWP(pte);
      MEMPHY_put_freefp(caller->active_mswp, fpn);    
    }
  }

  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
// Tìm và trả về trang nạn nhân theo cơ chế FIFO
int find_victim_page(struct mm_struct *mm, int *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  // Danh sách rỗng
  if(pg == NULL) return -1;

  // Chỉ có một trang
  if(pg->pg_next == NULL){
    *retpgn = pg->pgn;
    free(pg);
    mm->fifo_pgn = NULL;
    return 0;
  }

  // Duyệt để tìm trang cuối
  struct pgn_t * prev_pg = NULL;
  while(pg->pg_next != NULL){
    prev_pg = pg;
    pg = pg->pg_next;
  }
  
  // Lưu số hiệu trang nạn nhân và xóa khỏi danh sách
  *retpgn = pg->pgn;
  prev_pg->pg_next = NULL;
  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
// Tìm và cấp phát một vùng bộ nhớ ảo trống đủ kích thước trong vma
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  struct vm_rg_struct *rgit_prev = NULL;

  // Danh sách vùng trống rỗng, trả về -1
  if (rgit == NULL)
    return -1;

  // Khởi tạo vùng mới
  newrg->rg_start = 0;
  newrg->rg_end = 0;

  // Duyệt danh sách vùng trống
  while(rgit != NULL){
    // Tìm vùng đủ kích thước
    if(rgit->rg_end - rgit->rg_start >= size){
      // Cấp phát vùng mới
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      // Xóa vùng đã dùng khỏi danh sách
      if(rgit_prev == NULL){
        cur_vma->vm_freerg_list = rgit->rg_next;
      }
      else{
        rgit_prev->rg_next = rgit->rg_next;
      }
      
      // Tạo vùng trống còn sót lại sau khi sử dụng
      struct vm_rg_struct * new_free_rg = malloc(sizeof(struct vm_rg_struct));
      if(new_free_rg == NULL) return -1;
      new_free_rg->rg_start = rgit->rg_start + size;
      new_free_rg->rg_end = rgit->rg_end;
      new_free_rg->rg_next = NULL;
      enlist_vm_freerg_list(caller->mm, new_free_rg);

      free(rgit);
      return 0;
    }
    rgit_prev = rgit;
    rgit = rgit->rg_next;
  }

  // Không tìm thấy vùng phù hợp
  return -1;
}

//#endif
