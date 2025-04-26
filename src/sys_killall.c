/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "common.h"
#include "syscall.h"
#include "stdio.h"
#include "libmem.h"
#include "string.h"
#include "stdlib.h"
#include "queue.h"

int __sys_killall(struct pcb_t *caller, struct sc_regs* regs)
{
    char proc_name[100];
    uint32_t data;

    // Lấy ID vùng nhớ chứa tên tiến trình mục tiêu (demo, hardcoded)
    uint32_t memrg = regs->a1;

    /* Đọc tên tiến trình từ vùng nhớ do tiến trình gọi syscall cung cấp */
    int i = 0;
    data = 0;
    while (data != -1) {
        libread(caller, memrg, i, &data);  // Đọc từng byte từ vùng nhớ được chỉ định
        proc_name[i] = data;
        if (data == -1) proc_name[i] = '\0';  // Kết thúc chuỗi khi gặp -1
        i++;
    }
    printf("The procname retrieved from memregionid %d is \"%s\"\n", memrg, proc_name);

    /* Duyệt danh sách các tiến trình đang chạy và tìm tiến trình có tên khớp */
    struct queue_t *run_list = caller->running_list;

    for (int i = 0; i < run_list->size; i++) {
        struct pcb_t *curr_proc_r = run_list->proc[i];
        
        if (curr_proc_r == NULL || curr_proc_r->path == NULL) continue;

        // Nếu tên tiến trình chứa proc_name thì "dừng" nó bằng cách đặt PC về cuối code
        if (strstr(curr_proc_r->path, proc_name)) {
            curr_proc_r->pc = curr_proc_r->code->size;  // Dừng tiến trình
            break;
        }
    }

    /* Duyệt các hàng đợi và xóa tiến trình khớp tên */
    for (int i = 0; i < MAX_PRIO; i++) {
        struct queue_t *ready_list = &caller->mlq_ready_queue[i];
        for (int j = 0; j < ready_list->size; j++) {
            struct pcb_t *curr_proc_rdq = ready_list->proc[j];

            if (curr_proc_rdq == NULL || curr_proc_rdq->path == NULL) continue;

            // Nếu tên khớp thì loại khỏi hàng đợi và giải phóng tiến trình
            if (strstr(curr_proc_rdq->path, proc_name)) {
                dequeue_running(ready_list, curr_proc_rdq);
                free(curr_proc_rdq);
                break;
            }
        }
    }
    return 0;
}

