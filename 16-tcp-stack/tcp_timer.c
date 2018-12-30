#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_sock.h"

#include <unistd.h>
#include <stdio.h>

extern struct list_head timer_list;
extern struct tcp_sock *static_tsk;

void tcp_reset_retrans_timer(struct tcp_sock *tsk) {
	struct tcp_timer *timer = &tsk->retrans_timer;
	if(!list_empty(&tsk->send_buf)){
		struct send_packet *buf_pac = list_entry(tsk->send_buf.next, struct send_packet, list);
		timer->timeout = (1 << buf_pac->retrans_num) * TCP_RETRANS_INTERVAL_INITIAL;
	}
}

// scan the timer_list, find the tcp sock which stays for at 2*MSL, release it
void tcp_scan_timer_list()
{
	struct tcp_sock *tsk;
	struct tcp_timer *t, *q;
	list_for_each_entry_safe(t, q, &timer_list, list) {
		if (t->enable == 0) continue;
		t->timeout -= TCP_TIMER_SCAN_INTERVAL;
		if (t->timeout <= 0 && t->type == 0) {
			list_delete_entry(&t->list);
			t->enable = 0;
			// only support time wait now
			tsk = timewait_to_tcp_sock(t);
			//printf("DEBUG: tsk->parent %o %d\n", tsk->parent, tsk->ref_cnt);
			if (! tsk->parent)
				tcp_bind_unhash(tsk); //decrease ref_cnt by tcp_bind_hash
			tcp_unhash(tsk); //decrease ref_cnt by tcp_hash
			tcp_set_state(tsk, TCP_CLOSED);
			free_tcp_sock(tsk); //decrease ref_cnt by timer
		} else if (t->timeout <= 0 && (t->type == 1)) {
			tsk = retranstimer_to_tcp_sock(t);
            if(!list_empty(&tsk->send_buf)){
            	struct send_packet *buf_pac = list_entry(tsk->send_buf.next, struct send_packet, list);
            	if(buf_pac->retrans_num == 10){
	            	
	            	if (tsk->state!=TCP_CLOSED) {
	            		printf("debug Unfortunately I've resent 10 times but failed.\n");
	            		tcp_sock_close(tsk);
	            	}
	                return;
	            }
                char *packet = (char *)malloc(buf_pac->len);
                memcpy(packet, buf_pac->packet, buf_pac->len);
                ip_send_packet(packet, buf_pac->len);
                buf_pac->retrans_num++;
                tcp_reset_retrans_timer(tsk);
                printf("debug Resent %d time\n", buf_pac->retrans_num);
			}
		}
	}
}

// set the timewait timer of a tcp sock, by adding the timer into timer_list
void tcp_set_timewait_timer(struct tcp_sock *tsk)
{
	struct tcp_timer *timer = &tsk->timewait;

	timer->type = 0;
	timer->enable = 1;
	timer->timeout = TCP_TIMEWAIT_TIMEOUT;
	list_add_tail(&timer->list, &timer_list);

	tcp_sock_inc_ref_cnt(tsk);
}

void display_timer_list() {
	printf("timer list:");
	struct tcp_timer *t;
	list_for_each_entry(t, &timer_list, list) {
		printf("(t%d, e%d, %d)->", t->type, t->enable, t->timeout);
	}
	printf("\n");
	show_snd_buf(static_tsk);
	show_rcv_ofo_buf(static_tsk);
}

void tcp_set_retrans_timer(struct tcp_sock *tsk) {
	struct tcp_timer *timer = &tsk->retrans_timer;
	timer->type = 1;
	if (timer->enable == 0) { //initial set
		timer->enable = 1;
		timer->timeout = TCP_RETRANS_INTERVAL_INITIAL;
		//list_add_tail(&timer->list, &timer_list);
		//display_timer_list();
		//printf("timer: retrans time INITIALLY set to %d\n", timer->timeout);
		//tsk->ref_cnt ++;
	} else { //otherwise, reset timeout only
		tcp_reset_retrans_timer(tsk);
		//printf("timer: retrans time set to %d\n", timer->timeout);
	}
}

void tcp_unset_retrans_timer(struct tcp_sock *tsk){
	struct tcp_timer *timer = &tsk->retrans_timer;
	timer->enable = 0;
	//list_delete_entry(&tsk->retrans_timer.list);
	//tsk->ref_cnt --;
}

// scan the timer_list periodically by calling tcp_scan_timer_list
void *tcp_timer_thread(void *arg)
{
	printf("debug I've init timer_list!\n");
	int sec_tick = 0;
	while (1) {
		usleep(TCP_TIMER_SCAN_INTERVAL);
		sec_tick += TCP_TIMER_SCAN_INTERVAL;
		if (sec_tick >= 5000000) {
			display_timer_list();
			sec_tick = 0;
		}
		tcp_scan_timer_list();
	}

	return NULL;
}
