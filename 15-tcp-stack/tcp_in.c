#include "tcp.h"
#include "tcp_sock.h"
#include "tcp_timer.h"

#include "log.h"
#include "ring_buffer.h"

#include <stdlib.h>
// update the snd_wnd of tcp_sock
//
// if the snd_wnd before updating is zero, notify tcp_sock_send (wait_send)
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u16 old_snd_wnd = tsk->snd_wnd;
	tsk->snd_wnd = cb->rwnd;
	if (old_snd_wnd == 0)
		wake_up(tsk->wait_send);
}

// update the snd_wnd safely: cb->ack should be between snd_una and snd_nxt
static inline void tcp_update_window_safe(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	if (less_or_equal_32b(tsk->snd_una, cb->ack) && less_or_equal_32b(cb->ack, tsk->snd_nxt))
		tcp_update_window(tsk, cb);
}

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

// check whether the sequence number of the incoming packet is in the receiving
// window
static inline int is_tcp_seq_valid(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u32 rcv_end = tsk->rcv_nxt + max(tsk->rcv_wnd, 1);
	if (less_than_32b(cb->seq, rcv_end) && less_or_equal_32b(tsk->rcv_nxt, cb->seq_end)) {
		return 1;
	}
	else {
		log(ERROR, "received packet with invalid seq, drop it.");
		return 0;
	}
}

void invalid_state(struct tcp_sock *tsk, struct tcp_cb *cb) {
	tcp_send_reset(cb);
	tcp_set_state(tsk, TCP_CLOSED);
}

void tcp_sock_listen_dequeue(struct tcp_sock *tsk) { //TODO
    if (tsk->parent) {
        if (!list_empty(&tsk->parent->listen_queue)) {
            struct tcp_sock *entry = NULL;
            list_for_each_entry(entry, &(tsk->parent->listen_queue), list) 
                if (entry == tsk)
                    list_delete_entry(&(entry->list));                
        }
    }
}

struct tcp_sock *set_up_child_tsk(struct tcp_sock *tsk, struct tcp_cb *cb) {
    struct tcp_sock *csk = alloc_tcp_sock();
    csk->parent = tsk;
    // set four tuple
    csk->sk_sip = cb->daddr;
    csk->sk_dip = cb->saddr;
    csk->sk_sport = cb->dport;
    csk->sk_dport = cb->sport;
    // rcv and snd
    csk->rcv_nxt = cb->seq_end;
    csk->iss = tcp_new_iss();
    csk->snd_nxt = csk->iss;

    struct sock_addr skaddr = {htonl(csk->sk_sip), htons(csk->sk_sport)};
    if (tcp_sock_bind(csk, &skaddr) < 0) exit(-1);
    list_add_tail(&csk->list, &tsk->listen_queue);
    return csk;
}

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
    if (cb->flags & TCP_RST) {
        tcp_set_state(tsk, TCP_CLOSED);
        tcp_unhash(tsk);
        return ;
    }

	enum tcp_state state = (enum tcp_state) tsk->state;
    if (state != TCP_CLOSED && state != TCP_LISTEN) {
    	//After TCP_LISTEN, we start record seq_num
		tsk->snd_una = cb->ack;
	    tsk->rcv_nxt = cb->seq_end;
	    //After TCP_SYN_SENT, we start checking seq_num's validity
	    if ((state != TCP_SYN_SENT) && (!is_tcp_seq_valid(tsk, cb))) {
	        log(ERROR, "tcp_process(): received packet with invalid seq, drop it.");
	        return ;
	    }
    }

	switch (state) {
		case TCP_CLOSED: //CLOSED -> X
			invalid_state(tsk, cb);
			break;
		case TCP_LISTEN: //LISTEN -> SYN_RCVD
			if (cb->flags & TCP_SYN) {
				struct tcp_sock *csk = set_up_child_tsk(tsk, cb);
				tcp_set_state(csk, TCP_SYN_RECV);
				tcp_hash(csk);
				tcp_send_control_packet(csk, TCP_SYN | TCP_ACK);
			} else invalid_state(tsk, cb);
			break;
		case TCP_SYN_RECV: //SYN_RECV -> ESTABLISHED
			if (cb->flags & TCP_ACK) {
				tcp_set_state(tsk, TCP_ESTABLISHED);
				tcp_sock_listen_dequeue(tsk);
				tcp_sock_accept_enqueue(tsk);
				tcp_update_window_safe(tsk, cb);
				wake_up(tsk->parent->wait_accept);
			} else invalid_state(tsk, cb);
			break;
		case TCP_SYN_SENT: //SYN_SENT -> ESTABLISHED
			if (cb->flags & (TCP_ACK | TCP_SYN)) {
				tcp_set_state(tsk, TCP_ESTABLISHED);
				tcp_update_window_safe(tsk, cb);
       			wake_up(tsk->wait_connect);
       			tcp_send_control_packet(tsk, TCP_ACK);
			} else invalid_state(tsk, cb);
			break;
		case TCP_ESTABLISHED: //ESTABLISHED -> ESTABLISHED or CLOSE_WAIT
			if (cb->flags & (TCP_FIN)) { //TCP_FIN|TCP_ACK
				tcp_set_state(tsk, TCP_CLOSE_WAIT);
        		tcp_send_control_packet(tsk, TCP_ACK);

        		wake_up(tsk->wait_recv); // to explicit wake up server's recv() to end cycle in server
			} else if (cb->flags & (TCP_ACK)) {
				tcp_update_window_safe(tsk, cb);
				if (cb->pl_len > 0) {
					pthread_mutex_lock(&tsk->rcv_buf_lock);
						write_ring_buffer(tsk->rcv_buf, cb->payload, cb->pl_len);
        				wake_up(tsk->wait_recv);
        			pthread_mutex_unlock(&tsk->rcv_buf_lock);
        		}

    			//tcp_send_control_packet(tsk, TCP_ACK); //todo
			} else invalid_state(tsk, cb);
			break;
		case TCP_CLOSE_WAIT: //should not recv packet in CLOSE_WAIT
			log(ERROR, "tcp_process(): peer should not send tcp packet when I'm in TCP_CLOSE_WAIT.\n");
			invalid_state(tsk, cb);
			break;
		case TCP_LAST_ACK: //LAST_ACK -> CLOSED
			if ((cb->ack == tsk->snd_nxt) && (cb->flags & TCP_ACK)) {
				tcp_unhash(tsk);
				tcp_bind_unhash(tsk);
				tcp_set_state(tsk, TCP_CLOSED);
			}
			else invalid_state(tsk, cb);
			break;
		case TCP_FIN_WAIT_1: //FIN_WAIT_1 -> FIN_WAIT_2
			if ((cb->ack == tsk->snd_nxt) && (cb->flags & TCP_ACK)) 
				tcp_set_state(tsk, TCP_FIN_WAIT_2);
			else invalid_state(tsk, cb);
			break;
		case TCP_FIN_WAIT_2: //FIN_WAIT_2 -> TIME_WAIT
			if ((cb->ack == tsk->snd_nxt) && (cb->flags & (TCP_FIN | TCP_ACK))) {
				tcp_set_state(tsk, TCP_TIME_WAIT);
        		tcp_set_timewait_timer(tsk);
        		tcp_send_control_packet(tsk, TCP_ACK);
			}
			else invalid_state(tsk, cb);
			break;
		case TCP_CLOSING: case TCP_TIME_WAIT: //should not recv packet in CLOSING or TIME_WAIT
			log(ERROR, "tcp_process(): peer should not send tcp packet when I'm in TCP_CLOSING or TIME_WAIT!\n");
			invalid_state(tsk, cb);
			break;
		default: 
			log(ERROR, "tcp_process(): I can't think of any other circumstance I didn't take control :(\n");
			invalid_state(tsk, cb);
			break;
	}
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
}
