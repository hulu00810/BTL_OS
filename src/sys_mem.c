/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "syscall.h"
#include "libmem.h"
#include "mm.h"

//typedef char BYTE;

int __sys_memmap(struct pcb_t *caller, struct sc_regs* regs)
{
   int memop = regs->a1;  // Lấy mã lệnh memop từ tham số của syscall
   BYTE value;

   // Kiểm tra mã lệnh memop và thực hiện các thao tác tương ứng
   switch (memop) {
   case SYSMEM_MAP_OP:
            // Trường hợp dành cho thao tác map bộ nhớ, hiện tại chưa xử lý
            /* Reserved process case*/
            break;
   case SYSMEM_INC_OP:
            // Thực hiện thao tác tăng giới hạn vùng bộ nhớ ảo (vma) cho tiến trình
            if (inc_vma_limit(caller, regs->a2, regs->a3) == -1) return -1; // Nếu thất bại, trả về -1
            break;
   case SYSMEM_SWP_OP:
            // Thực hiện thao tác hoán đổi trang bộ nhớ
            if (__mm_swap_page(caller, regs->a2, regs->a3) == -1) return -1; // Nếu thất bại, trả về -1
            break;
   case SYSMEM_IO_READ:
            // Đọc giá trị từ bộ nhớ vật lý (IO Read)
            if (MEMPHY_read(caller->mram, regs->a2, &value) == -1) return -1; // Nếu thất bại, trả về -1
            regs->a3 = value;  // Lưu giá trị đọc được vào tham số a3
            break;
   case SYSMEM_IO_WRITE:
            // Ghi giá trị vào bộ nhớ vật lý (IO Write)
            if (MEMPHY_write(caller->mram, regs->a2, regs->a3) == -1) return -1; // Nếu thất bại, trả về -1
            break;
   default:
            // Nếu không nhận diện được mã lệnh memop, in ra mã lệnh và tiếp tục
            printf("Memop code: %d\n", memop);
            break;
   }

   return 0; // Trả về 0 nếu thực hiện thành công
}



