/* Copyright 2015 Stanford University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Processor/ProcessorGroup implementations for Realm

#ifndef REALM_PROC_IMPL_H
#define REALM_PROC_IMPL_H

#include "processor.h"
#include "id.h"

#include "activemsg.h"
#include "operation.h"
#include "profiling.h"

#include "event_impl.h"
#include "rsrv_impl.h"

#include "tasks.h"
#include "threads.h"

namespace Realm {

    // TODO: get rid of this class
    template <class T>
    class Atomic {
    public:
      Atomic(T _value) : value(_value)
      {
	//printf("%d: atomic %p = %d\n", gasnet_mynode(), this, value);
      }

      T get(void) const { return (*((volatile T*)(&value))); }

      void decrement(void)
      {
	AutoHSLLock a(mutex);
	//T old_value(value);
	value--;
	//printf("%d: atomic %p %d -> %d\n", gasnet_mynode(), this, old_value, value);
      }

    protected:
      T value;
      GASNetHSL mutex;
    };

    class ProcessorImpl {
    public:
      ProcessorImpl(Processor _me, Processor::Kind _kind);

      virtual ~ProcessorImpl(void);

      void run(Atomic<int> *_run_counter)
      {
	run_counter = _run_counter;
      }

      virtual void start_processor(void) = 0;
      virtual void shutdown_processor(void) = 0;
      virtual void initialize_processor(void) = 0;
      virtual void finalize_processor(void) = 0;

      virtual void enqueue_task(Task *task) = 0;

      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
			      //std::set<RegionInstance> instances_needed,
			      Event start_event, Event finish_event,
                              int priority) = 0;

      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
                              const ProfilingRequestSet &reqs,
			      Event start_event, Event finish_event,
                              int priority) = 0;

      void finished(void)
      {
	if(run_counter)
	  run_counter->decrement();
      }

    public:
      Processor me;
      Processor::Kind kind;
      Atomic<int> *run_counter;
    }; 

    // generic local task processor - subclasses must create and configure a task
    // scheduler and pass in with the set_scheduler() method
    class LocalTaskProcessor : public ProcessorImpl {
    public:
      LocalTaskProcessor(Processor _me, Processor::Kind _kind);
      virtual ~LocalTaskProcessor(void);

      virtual void enqueue_task(Task *task);
      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
			      //std::set<RegionInstance> instances_needed,
			      Event start_event, Event finish_event,
                              int priority);
      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
                              const ProfilingRequestSet &reqs,
			      Event start_event, Event finish_event,
                              int priority);

      // blocks until things are cleaned up
      virtual void shutdown(void);

    protected:
      void set_scheduler(ThreadedTaskScheduler *_sched);

      // old methods to delete
      virtual void start_processor(void);
      virtual void shutdown_processor(void);
      virtual void initialize_processor(void);
      virtual void finalize_processor(void);

      ThreadedTaskScheduler *sched;
      PriorityQueue<Task *, GASNetHSL> task_queue;
    };

    // three simple subclasses for:
    // a) "CPU" processors, which request a dedicated core and use user threads
    //      when possible
    // b) "utility" processors, which also use user threads but share cores with
    //      other runtime threads
    // c) "IO" processors, which use kernel threads so that blocking IO calls
    //      are permitted
    //
    // each of these is implemented just by supplying the right kind of scheduler to
    //  LocalTaskProcessor in the constructor

    class LocalCPUProcessor : public LocalTaskProcessor {
    public:
      LocalCPUProcessor(Processor _me, size_t _stack_size);
      virtual ~LocalCPUProcessor(void);
    protected:
      CoreReservation *core_rsrv;
    };

    class LocalUtilityProcessor : public LocalTaskProcessor {
    public:
      LocalUtilityProcessor(Processor _me, size_t _stack_size);
      virtual ~LocalUtilityProcessor(void);
    protected:
      CoreReservation *core_rsrv;
    };

    class LocalIOProcessor : public LocalTaskProcessor {
    public:
      LocalIOProcessor(Processor _me, size_t _stack_size,
		       int _concurrent_io_threads);
      virtual ~LocalIOProcessor(void);
    protected:
      CoreReservation *core_rsrv;
    };

    class RemoteProcessor : public ProcessorImpl {
    public:
      RemoteProcessor(Processor _me, Processor::Kind _kind);
      virtual ~RemoteProcessor(void);

      virtual void start_processor(void);
      virtual void shutdown_processor(void);
      virtual void initialize_processor(void);
      virtual void finalize_processor(void);

      virtual void enqueue_task(Task *task);

      virtual void tasks_available(int priority);

      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
			      //std::set<RegionInstance> instances_needed,
			      Event start_event, Event finish_event,
                              int priority);

      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
                              const ProfilingRequestSet &reqs,
			      Event start_event, Event finish_event,
                              int priority);
    };

    class ProcessorGroup : public ProcessorImpl {
    public:
      ProcessorGroup(void);

      virtual ~ProcessorGroup(void);

      static const ID::ID_Types ID_TYPE = ID::ID_PROCGROUP;

      void init(Processor _me, int _owner);

      void set_group_members(const std::vector<Processor>& member_list);

      void get_group_members(std::vector<Processor>& member_list);

      virtual void start_processor(void);
      virtual void shutdown_processor(void);
      virtual void initialize_processor(void);
      virtual void finalize_processor(void);

      virtual void enqueue_task(Task *task);

      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
			      //std::set<RegionInstance> instances_needed,
			      Event start_event, Event finish_event,
                              int priority);

      virtual void spawn_task(Processor::TaskFuncID func_id,
			      const void *args, size_t arglen,
                              const ProfilingRequestSet &reqs,
			      Event start_event, Event finish_event,
                              int priority);


    public: //protected:
      bool members_valid;
      bool members_requested;
      std::vector<ProcessorImpl *> members;
      ReservationImpl lock;
      ProcessorGroup *next_free;

      void request_group_members(void);
    };
    
    // this is generally useful to all processor implementations, so put it here
    class DeferredTaskSpawn : public EventWaiter {
    public:
      DeferredTaskSpawn(ProcessorImpl *_proc, Task *_task) 
        : proc(_proc), task(_task) {}

      virtual ~DeferredTaskSpawn(void)
      {
        // we do _NOT_ own the task - do not free it
      }

      virtual bool event_triggered(void);
      virtual void print_info(FILE *f);

    protected:
      ProcessorImpl *proc;
      Task *task;
    };

    // active messages

    struct SpawnTaskMessage {
      // Employ some fancy struct packing here to fit in 64 bytes
      struct RequestArgs : public BaseMedium {
	Processor proc;
	Event::id_t start_id;
	Event::id_t finish_id;
	size_t user_arglen;
	int priority;
	Processor::TaskFuncID func_id;
	Event::gen_t start_gen;
	Event::gen_t finish_gen;
      };

      static void handle_request(RequestArgs args, const void *data, size_t datalen);

      typedef ActiveMessageMediumNoReply<SPAWN_TASK_MSGID,
 	                                 RequestArgs,
 	                                 handle_request> Message;

      static void send_request(gasnet_node_t target, Processor proc,
			       Processor::TaskFuncID func_id,
			       const void *args, size_t arglen,
			       const ProfilingRequestSet *prs,
			       Event start_event, Event finish_event,
			       int priority);
    };
    
}; // namespace Realm

#endif // ifndef REALM_PROC_IMPL_H
