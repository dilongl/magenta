// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/dispatcher.h>
#include <magenta/futex_context.h>
#include <magenta/magenta.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>
#include <magenta/user_thread.h>

#include <utils/intrusive_double_list.h>
#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>
#include <utils/string_piece.h>

class ProcessDispatcher : public Dispatcher
                        , public utils::DoublyLinkedListable<ProcessDispatcher*> {
public:
    static mx_status_t Create(utils::StringPiece name,
                              utils::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    static ProcessDispatcher* GetCurrent() {
        UserThread* current = UserThread::GetCurrent();
        DEBUG_ASSERT(current);
        return current->process();
    }

    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_PROCESS; }
    ProcessDispatcher* get_process_dispatcher() final { return this; }

    StateTracker* get_state_tracker() final { return &state_tracker_; }

    ~ProcessDispatcher() final;

    // state of the process
    enum class State {
        INITIAL, // initial state, no thread present in process
        RUNNING, // first thread has started and is running
        DYING,   // process has delivered kill signal to all threads
        DEAD,    // all threads have entered DEAD state and potentially dropped refs on process
    };

    // Performs initialization on a newly constructed ProcessDispatcher
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize();

    // Map a |handle| to an integer which can be given to usermode as a
    // handle value. Uses MapHandleToU32() plus additional mixing.
    mx_handle_t MapHandleToValue(Handle* handle);

    // Maps a handle value into a Handle as long we can verify that
    // it belongs to this process.
    Handle* GetHandle_NoLock(mx_handle_t handle_value);

    // Adds |hadle| to this process handle list. The handle->process_id() is
    // set to this process id().
    void AddHandle(HandleUniquePtr handle);
    void AddHandle_NoLock(HandleUniquePtr handle);

    // Removes the Handle corresponding to |handle_value| from this process
    // handle list.
    HandleUniquePtr RemoveHandle(mx_handle_t handle_value);
    HandleUniquePtr RemoveHandle_NoLock(mx_handle_t handle_value);

    // Puts back the |handle_value| which has not yet been given to another process
    // back into this process.
    void UndoRemoveHandle_NoLock(mx_handle_t handle_value);

    bool GetDispatcher(mx_handle_t handle_value, utils::RefPtr<Dispatcher>* dispatcher,
                       uint32_t* rights);

    // accessors
    mx_pid_t id() const { return id_; }
    mutex_t& handle_table_lock() { return handle_table_lock_; }
    FutexContext* futex_context() { return &futex_context_; }
    StateTracker* state_tracker() { return &state_tracker_; }
    State state() const { return state_; }
    utils::RefPtr<VmAspace> aspace() { return aspace_; }
    const utils::StringPiece name() const { return name_; }

    char StateChar() const;
    mx_tid_t GetNextThreadId();

    // Starts the process running
    status_t Start(void* arg, mx_vaddr_t vaddr);

    void Exit(int retcode);
    void Kill();

    status_t GetInfo(mx_process_info_t* info);

    // exception handling routines
    status_t SetExceptionHandler(utils::RefPtr<Dispatcher> handler, mx_exception_behaviour_t behaviour);
    utils::RefPtr<Dispatcher> exception_handler();

    // The following two methods can be slow and innacurrate and should only be
    // called from diagnostics code.
    uint32_t HandleStats(uint32_t*handle_type, size_t size) const;
    uint32_t ThreadCount() const;

    // Outputs via the console the current list of processes;
    static void DebugDumpProcessList();
    static void DumpProcessListKeyMap();

private:
    explicit ProcessDispatcher(utils::StringPiece name);

    ProcessDispatcher(const ProcessDispatcher&) = delete;
    ProcessDispatcher& operator=(const ProcessDispatcher&) = delete;

    // Thread lifecycle support
    friend class UserThread;
    status_t AddThread(UserThread* t);
    void RemoveThread(UserThread* t);

    void SetState(State);

    // Kill all threads
    void KillAllThreads();

    // Utility routine used with public debug routines.
    char* DebugDumpHandleTypeCount_NoLock() const;

    // Add a process to the global process list.  Allocate a new process ID from
    // the global pool at the same time, and assign it to the process.
    static void AddProcess(ProcessDispatcher* process);

    // Remove a process from the global process list.
    static void RemoveProcess(ProcessDispatcher* process);

    mx_pid_t id_ = 0;

    mx_handle_t handle_rand_ = 0;

    // The next thread id to assign.
    // This is an int as we use atomic_add. TODO(dje): wip
    int next_thread_id_ = 1;

    // protects thread_list_, as well as the UserThread joined_ and detached_ flags
    mutable mutex_t thread_list_lock_ = MUTEX_INITIAL_VALUE(thread_list_lock_);

    // list of threads in this process
    utils::DoublyLinkedList<UserThread*> thread_list_;

    // a ref to the main thread
    utils::RefPtr<UserThread> main_thread_;

    // our address space
    utils::RefPtr<VmAspace> aspace_;

    // our list of handles
    mutable mutex_t handle_table_lock_ =
        MUTEX_INITIAL_VALUE(handle_table_lock_); // protects |handles_|.
    utils::DoublyLinkedList<Handle*> handles_;

    StateTracker state_tracker_;

    FutexContext futex_context_;

    // our state
    State state_ = State::INITIAL;
    mutex_t state_lock_ = MUTEX_INITIAL_VALUE(state_lock_);

    // process return code
    int retcode_ = 0;

    // main entry point to the process
    thread_start_routine entry_ = nullptr;

    utils::RefPtr<Dispatcher> exception_handler_;
    mx_exception_behaviour_t exception_behaviour_ = MX_EXCEPTION_BEHAVIOUR_DEFAULT;
    mutex_t exception_lock_ = MUTEX_INITIAL_VALUE(exception_lock_);

    // The user-friendly process name. For debug purposes only.
    char name_[THREAD_NAME_LENGTH / 2] = {};

    // The global process list, process id generator, and its mutex.
    static uint32_t next_process_id_;
    static mutex_t global_process_list_mutex_;
    static utils::DoublyLinkedList<ProcessDispatcher*> global_process_list_;
};

const char* StateToString(ProcessDispatcher::State state);