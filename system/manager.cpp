#include "manager.h"
#include "row.h"
#include "txn.h"
#include "pthread.h"
#include <vector>
#include <unordered_map>
#include "log_pending_table.h"
//#include <junction/junction/Core.h>

struct pending_entry {
	uint32_t pred_size;
	vector<uint64_t> child;
};

__thread uint64_t Manager::_thread_id;
//unordered_map<uint64_t, pending_entry *> _log_pending_map;
unordered_map<uint64_t, pending_entry *> _log_pending_map;

void Manager::init() {
	timestamp = (uint64_t *) _mm_malloc(sizeof(uint64_t), 64);
	*timestamp = 1;
	_last_min_ts_time = 0;
	_min_ts = 0;
	_epoch = (uint64_t *) _mm_malloc(sizeof(uint64_t), 64);
	_last_epoch_update_time = (ts_t *) _mm_malloc(sizeof(uint64_t), 64);
	_epoch = 0;
	_last_epoch_update_time = 0;
	all_ts = (ts_t volatile **) _mm_malloc(sizeof(ts_t *) * g_thread_cnt, 64);
	for (uint32_t i = 0; i < g_thread_cnt; i++) 
		all_ts[i] = (ts_t *) _mm_malloc(sizeof(ts_t), 64);

	_all_txns = new txn_man * [g_thread_cnt];
	for (UInt32 i = 0; i < g_thread_cnt; i++) {
		*all_ts[i] = UINT64_MAX;
		_all_txns[i] = NULL;
	}
	for (UInt32 i = 0; i < BUCKET_CNT; i++)
		pthread_mutex_init( &mutexes[i], NULL );
	// initialize log_pending_map
	// unorderedmap<uint64_t, * pred_entry> _log_pending_map = {};
	//_log_pending_table = new LogPendingTable;
}

uint64_t 
Manager::get_ts(uint64_t thread_id) {
	if (g_ts_batch_alloc)
		assert(g_ts_alloc == TS_CAS);
	uint64_t time;
	uint64_t starttime = get_sys_clock();
	switch(g_ts_alloc) {
	case TS_MUTEX :
		pthread_mutex_lock( &ts_mutex );
		time = ++(*timestamp);
		pthread_mutex_unlock( &ts_mutex );
		break;
	case TS_CAS :
		if (g_ts_batch_alloc)
			time = ATOM_FETCH_ADD((*timestamp), g_ts_batch_num);
		else 
			time = ATOM_FETCH_ADD((*timestamp), 1);
		break;
	case TS_HW :
#ifndef NOGRAPHITE
		time = CarbonGetTimestamp();
#else
		assert(false);
#endif
		break;
	case TS_CLOCK :
		time = get_sys_clock() * g_thread_cnt + thread_id;
		break;
	default :
		assert(false);
	}
	INC_STATS(thread_id, time_ts_alloc, get_sys_clock() - starttime);
	return time;
}

ts_t Manager::get_min_ts(uint64_t tid) {
	uint64_t now = get_sys_clock();
	uint64_t last_time = _last_min_ts_time; 
	if (tid == 0 && now - last_time > MIN_TS_INTVL)
	{ 
		ts_t min = UINT64_MAX;
    	for (UInt32 i = 0; i < g_thread_cnt; i++) 
	    	if (*all_ts[i] < min)
    	    	min = *all_ts[i];
		if (min > _min_ts)
			_min_ts = min;
	}
	return _min_ts;
}

void Manager::add_ts(uint64_t thd_id, ts_t ts) {
	assert( ts >= *all_ts[thd_id] || 
		*all_ts[thd_id] == UINT64_MAX);
	*all_ts[thd_id] = ts;
}

void Manager::set_txn_man(txn_man * txn) {
	int thd_id = txn->get_thd_id();
	_all_txns[thd_id] = txn;
}


uint64_t Manager::hash(row_t * row) {
	uint64_t addr = (uint64_t)row / MEM_ALLIGN;
    return (addr * 1103515247 + 12345) % BUCKET_CNT;
}
 
void Manager::lock_row(row_t * row) {
	int bid = hash(row);
	pthread_mutex_lock( &mutexes[bid] );	
}

void Manager::release_row(row_t * row) {
	int bid = hash(row);
	pthread_mutex_unlock( &mutexes[bid] );
}
	
void
Manager::update_epoch()
{
	ts_t time = get_sys_clock();
	if (time - *_last_epoch_update_time > LOG_BATCH_TIME * 1000 * 1000) {
		*_epoch = *_epoch + 1;
		*_last_epoch_update_time = time;
	}
}

// TODO. make this lock free.
/*
bool
Manager::is_log_pending(uint64_t txn_id)
{
	pthread_mutex_lock( &_log_mutex );
	bool is_pending = (_log_pending_map.find(txn_id) != _log_pending_map.end()) ;
	pthread_mutex_unlock( &_log_mutex );
	return is_pending;
}*/


void
Manager::add_log_pending(uint64_t txn_id, uint64_t * predecessors, uint32_t predecessor_size)
{
	//_log_pending_table->add_log_pending(txn_id, predecessors, predecessor_size);
	/*
	pthread_mutex_lock(&_log_mutex);
	pending_entry * my_pending_entry = new pending_entry;
	//unordered_set<uint64_t> _preds; 
	for(uint64_t i = 0; i < predecessor_size; i++) {
		//my_pending_entry->preds.insert(predecessors[i]);
		// if a txn that the current txn depends on is already committed, then we
		// don't need to consider it
		if(_log_pending_map.find(predecessors[i]) != _log_pending_map.end()) {
			_log_pending_map.at(predecessors[i])->child.push_back(txn_id);
			my_pending_entry->pred_size++;
		}
	}
	_log_pending_map.insert(pair<uint64_t, pending_entry *>(txn_id, my_pending_entry));
	pthread_mutex_unlock(&_log_mutex);
	*/
}

void
Manager::remove_log_pending(uint64_t txn_id)
{
	//_log_pending_table->remove_log_pending(txn_id);
	/*
	pthread_mutex_lock(&_log_mutex);
	for(auto it = _log_pending_map.at(txn_id)->child.begin(); it!= _log_pending_map.at(txn_id)->child.end(); it++) {
		//if(_log_pending_map.find(*it) != _log_pending_map.end()) {
			_log_pending_map.at(*it)->pred_size--;
			if(_log_pending_map.at(*it)->pred_size == 0) {
				remove_log_pending(*it);
			}
		//}		
	}
	_log_pending_map.erase(txn_id);
	//COMMIT
	pthread_mutex_lock(&_log_mutex);
	*/
}
