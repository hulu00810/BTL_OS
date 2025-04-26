#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t * q) {
        if (q == NULL) return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t * q, struct pcb_t * proc) {
        if (proc == NULL  || q == NULL) return;
        if (q->size >= MAX_QUEUE_SIZE) return;

        //Nếu sử dụng giải thuật MLQ thì ghi đè priority của process bằng prio
        #ifdef MLQ_SCHED
        proc->priority = proc->prio;
        #endif

        //Thêm process vào cuối hàng đợi, tương đương với việc proc tới sau sẽ được phục vụ sau
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t * dequeue(struct queue_t * q) {
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (empty(q)) return NULL;

        //Lấy process đầu tiên ra khỏi hàng đợi và trả về process đó
        struct pcb_t * proc = q->proc[0];
        //Dịch các process phía sau lên một vị trí
        for(int i = 0; i < q->size - 1; i++){
                q->proc[i] = q->proc[i + 1];
        }
        q->proc[q->size-1] = NULL;
        q->size--;

        return proc;
}

int dequeue_running(struct queue_t *q, struct pcb_t *proc) {
        if (empty(q)) return 0;
    
        for (int i = 0; i < q->size; i++) {
                //Kiểm tra pid của process trong hàng đợi
            if (q->proc[i] && q->proc[i]->pid == proc->pid) {
                // Dịch các tiến trình phía sau lên một vị trí
                for (int j = i; j < q->size - 1; j++) {
                    q->proc[j] = q->proc[j + 1];
                }
                q->proc[q->size - 1] = NULL;
                q->size--;
                return 1; // Đã xoá thành công
            }
        }
        return 0; // Không tìm thấy
}

