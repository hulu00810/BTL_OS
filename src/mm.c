// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(uint32_t *pte,
             int pre,    // present
             int fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             int swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_swap - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(uint32_t *pte, int fpn)
{
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
 int vmap_page_range(struct pcb_t *caller,            // tiến trình gọi ánh xạ
                      int addr,                       // địa chỉ bắt đầu đã được căn trang
                      int pgnum,                      // số lượng trang cần ánh xạ
                      struct framephy_struct *frames, // danh sách frame vật lý cần ánh xạ
                      struct vm_rg_struct *ret_rg)    // trả lại vùng đã ánh xạ (thực tế)
{
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  int pgn = PAGING_PGN(addr); // Chuyển địa chỉ thành số trang
  int end_addr = addr + pgnum * PAGING_PAGESZ;  // Tính địa chỉ kết thúc của vùng ánh xạ

  // Cập nhật vùng nhớ đã ánh xạ (trả về qua ret_rg)
  ret_rg->rg_end = end_addr;
  ret_rg->rg_start = addr;

  // Ánh xạ từng frame vật lý vào bảng trang
  for (pgit = 0; pgit < pgnum; pgit++){
    if(fpit == NULL) return -1; // Không đủ frame để ánh xạ

    int curr_pgn = pgn + pgit; // Số trang hiện tại

    // Gán frame number vào entry trong bảng trang
    pte_set_fpn(&caller->mm->pgd[curr_pgn], fpit->fpn);

    // Đánh dấu trang là đang hiện diện và có thể ghi
    // (đã được thực hiện bên trong pte_set_fpn hoặc có thể bổ sung thêm nếu cần)

    // Thêm trang này vào hàng đợi FIFO để quản lý thay thế trang sau này
    enlist_pgn_node(&caller->mm->fifo_pgn, curr_pgn);

    // Di chuyển đến frame tiếp theo
    fpit = fpit->fp_next;
  }
  return 0; // Thành công
}


/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  int pgit, fpn;
  struct framephy_struct *newfp_str = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    // Cố gắng cấp phát một frame vật lý từ bộ nhớ chính
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      // Cấp phát thành công, tạo struct framephy_struct tương ứng
      newfp_str = malloc(sizeof(struct framephy_struct));
      if (newfp_str == NULL) return -1; // Lỗi cấp phát bộ nhớ

      newfp_str->owner = caller->mm;    // Gán chủ sở hữu là tiến trình hiện tại
      newfp_str->fpn = fpn;             // Lưu số frame vật lý
      newfp_str->fp_next = *frm_lst;    // Chèn vào đầu danh sách frame đã cấp phát
      *frm_lst = newfp_str;
    }
    else
    {
      // Không còn đủ frame để cấp phát -> rollback lại các frame đã lấy
      struct framephy_struct *curr = *frm_lst;
      struct framephy_struct *next = NULL;

      // Trả lại các frame đã cấp phát trước đó
      while(curr != NULL){
        next = curr->fp_next;
        MEMPHY_put_freefp(caller->mram, curr->fpn); // Trả lại frame cho bộ nhớ
        free(curr); // Giải phóng struct framephy_struct
        curr = next;
      }

      return -3000; // Lỗi: không cấp phát đủ số lượng trang yêu cầu
    }
  }

  return 0; // Thành công: đã cấp phát đủ số trang
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
int vm_map_ram(struct pcb_t *caller, int astart, int aend, int mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  int ret_alloc;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
#ifdef MMDBG
    printf("OOM: vm_map_ram out of memory \n");
#endif
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, int srcfpn,
                   struct memphy_struct *mpdst, int dstfpn)
{
  int cellidx;
  int addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct)); // Cấp phát VMA đầu tiên (vma_id = 0)

  mm->pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t)); // Cấp phát bảng trang (page directory)

  /* Thiết lập thông tin cho VMA đầu tiên */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;     // Vùng ban đầu chưa có gì
  vma0->sbrk = vma0->vm_start;       // Con trỏ break trỏ đến đầu vùng

  // Khởi tạo danh sách vùng nhớ trống ban đầu (có thể rỗng nếu vm_start == vm_end)
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  vma0->vm_next = NULL;             // Chưa có VMA tiếp theo
  mm->mmap = vma0;                  // mmap trỏ đến VMA đầu tiên
  mm->mmap->vm_freerg_list = NULL;  // Ban đầu chưa có vùng nhớ trống thực tế
  mm->mmap->vm_next = NULL;         // Vẫn là VMA duy nhất
  mm->fifo_pgn = NULL;              // Hàng đợi quản lý trang trống ban đầu

  // Khởi tạo bảng ký hiệu (symbol region table) rỗng
  mm->symrgtbl[0].rg_start = 0;
  mm->symrgtbl[0].rg_end = 0;
  mm->symrgtbl[0].rg_next = NULL;

  vma0->vm_mm = mm; // Thiết lập liên kết ngược từ VMA về mm_struct

  return 0;
}
 

struct vm_rg_struct *init_vm_rg(int rg_start, int rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, int pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[%d]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[%ld->%ld]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[%ld->%ld]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[%d]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, uint32_t start, uint32_t end)
{
  int pgn_start, pgn_end;
  int pgit;

  if (end == -1)
  {
    pgn_start = 0;
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, 0);
    end = cur_vma->vm_end;
  }
  pgn_start = PAGING_PGN(start);
  pgn_end = PAGING_PGN(end);

  printf("print_pgtbl: %d - %d", start, end);
  if (caller == NULL) { printf("NULL caller\n"); return -1;}
  printf("\n");

  for (pgit = pgn_start; pgit < pgn_end; pgit++)
  {
    printf("%08ld: %08x\n", pgit * sizeof(uint32_t), caller->mm->pgd[pgit]);
  }

  return 0;
}

// #endif
