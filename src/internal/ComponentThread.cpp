/******************************************************************************
DSPatch - The Refreshingly Simple C++ Dataflow Framework
Copyright (c) 2023, Marcus Tomlinson

BSD 2-Clause License

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#include <internal/ComponentThread.h>

using namespace DSPatch::internal;

ComponentThread::ComponentThread() = default;

// cppcheck-suppress missingMemberCopy
ComponentThread::ComponentThread( ComponentThread&& )
{
}

ComponentThread::~ComponentThread()
{
    Stop();
}

void ComponentThread::Setup( DSPatch::Component* component, int bufferNo )
{
    _component = component;
    _bufferNo = bufferNo;
}

void ComponentThread::Start()
{
    if ( !_stopped )
    {
        return;
    }

    _stop = false;
    _stopped = false;
    _gotSync = false;

    _thread = std::thread( &ComponentThread::_Run, this );

    Sync();
}

void ComponentThread::Stop()
{
    if ( _stopped )
    {
        return;
    }

    Sync();

    _stop = true;

    Resume();

    if ( _thread.joinable() )
    {
        _thread.join();
    }
}

void ComponentThread::Sync()
{
    if ( _stopped || _gotSync )
    {
        return;
    }

    std::unique_lock<std::mutex> lock( _resumeMutex );

    // cppcheck-suppress knownConditionTrueFalse
    if ( !_gotSync )  // if haven't already got sync
    {
        _syncCondt.wait( lock );  // wait for sync
    }
}

void ComponentThread::Resume()
{
    if ( _stopped )
    {
        Start();
    }

    std::lock_guard<std::mutex> lock( _resumeMutex );

    _gotSync = false;  // reset the sync flag

    _resumeCondt.notify_all();
}

void ComponentThread::_Run()
{
    SetThreadHighPriority();

    while ( !_stop )
    {
        {
            std::unique_lock<std::mutex> lock( _resumeMutex );

            _gotSync = true;  // set the sync flag
            _syncCondt.notify_all();
            _resumeCondt.wait( lock );  // wait for resume
        }

        // cppcheck-suppress knownConditionTrueFalse
        if ( !_stop )
        {
            _component->_TickParallel( _bufferNo );
        }
    }

    _stopped = true;
}
