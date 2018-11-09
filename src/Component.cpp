/************************************************************************
DSPatch - The C++ Flow-Based Programming Framework
Copyright (c) 2012-2018 Marcus Tomlinson

This file is part of DSPatch.

GNU Lesser General Public License Usage
This file may be used under the terms of the GNU Lesser General Public
License version 3.0 as published by the Free Software Foundation and
appearing in the file LICENSE included in the packaging of this file.
Please review the following information to ensure the GNU Lesser
General Public License version 3.0 requirements will be met:
http://www.gnu.org/copyleft/lgpl.html.

Other Usage
Alternatively, this file may be used in accordance with the terms and
conditions contained in a signed written agreement between you and
Marcus Tomlinson.

DSPatch is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
************************************************************************/

#include <dspatch/Component.h>

#include <internal/Wire.h>

#include <string>

using namespace DSPatch;

namespace DSPatch
{
namespace internal
{

class Component
{
public:
    enum class TickStatus
    {
        NotTicked,
        TickStarted
    };

    Component( DSPatch::Component::ProcessOrder processOrder )
        : processOrder( processOrder )
    {
    }

    void WaitForRelease( int threadNo );
    void ReleaseThread( int threadNo );

    DSPatch::Signal::SPtr const& GetOutput( int bufferNo, int outputNo, bool& canMove );

    void IncRefs( int output );
    void DecRefs( int output );

    const DSPatch::Component::ProcessOrder processOrder;

    int bufferCount = 0;

    std::vector<DSPatch::SignalBus> inputBuses;
    std::vector<DSPatch::SignalBus> outputBuses;

    std::vector<std::vector<std::pair<int, int>>> refs;  // ref_count:ref_counter per output, per buffer

    std::vector<Wire> inputWires;

    std::vector<TickStatus> tickStatuses;
    std::vector<bool> gotReleases;
    std::vector<std::unique_ptr<std::mutex>> releaseMutexes;
    std::vector<std::unique_ptr<std::condition_variable>> releaseCondts;

    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
};

}  // namespace internal
}  // namespace DSPatch

Component::Component( ProcessOrder processOrder )
    : p( new internal::Component( processOrder ) )
{
    SetBufferCount( 1 );
}

Component::~Component()
{
    DisconnectAllInputs();
}

bool Component::ConnectInput( Component::SPtr const& fromComponent, int fromOutput, int toInput )
{
    if ( fromOutput >= fromComponent->GetOutputCount() || toInput >= p->inputBuses[0].GetSignalCount() )
    {
        return false;
    }

    // first make sure there are no wires already connected to this input
    DisconnectInput( toInput );

    p->inputWires.emplace_back( fromComponent, fromOutput, toInput );

    // update source output's reference count
    fromComponent->p->IncRefs( fromOutput );

    return true;
}

void Component::DisconnectInput( int inputNo )
{
    // remove wires connected to inputNo from inputWires
    for ( size_t i = 0; i < p->inputWires.size(); ++i )
    {
        auto wire = p->inputWires[i];
        if ( wire.toInput == inputNo )
        {
            p->inputWires.erase( p->inputWires.begin() + i );

            // update source output's reference count
            wire.fromComponent->p->DecRefs( wire.fromOutput );
        }
    }
}

void Component::DisconnectInput( Component::SCPtr const& fromComponent )
{
    // remove fromComponent from inputWires
    for ( auto& wire : p->inputWires )
    {
        if ( wire.fromComponent == fromComponent )
        {
            DisconnectInput( wire.toInput );
        }
    }
}

void Component::DisconnectAllInputs()
{
    // remove all wires from inputWires
    for ( int i = 0; i < p->inputBuses[0].GetSignalCount(); ++i )
    {
        DisconnectInput( i );
    }
}

int Component::GetInputCount() const
{
    return p->inputBuses[0].GetSignalCount();
}

int Component::GetOutputCount() const
{
    return p->outputBuses[0].GetSignalCount();
}

std::string Component::GetInputName( int inputNo ) const
{
    if ( inputNo < (int)p->inputNames.size() )
    {
        return p->inputNames[inputNo];
    }
    return "";
}

std::string Component::GetOutputName( int outputNo ) const
{
    if ( outputNo < (int)p->outputNames.size() )
    {
        return p->outputNames[outputNo];
    }
    return "";
}

void Component::SetBufferCount( int bufferCount )
{
    // p->bufferCount is the current thread count / bufferCount is new thread count

    if ( bufferCount <= 0 )
    {
        bufferCount = 1;  // there needs to be at least 1 buffer
    }

    p->tickStatuses.resize( bufferCount );

    p->inputBuses.resize( bufferCount );
    p->outputBuses.resize( bufferCount );

    p->refs.resize( bufferCount );

    p->gotReleases.resize( bufferCount );
    p->releaseMutexes.resize( bufferCount );
    p->releaseCondts.resize( bufferCount );

    for ( int i = p->bufferCount; i < bufferCount; ++i )
    {
        if ( !p->releaseCondts[i] )
        {
            p->releaseMutexes[i] = std::unique_ptr<std::mutex>( new std::mutex() );
            p->releaseCondts[i] = std::unique_ptr<std::condition_variable>( new std::condition_variable() );
        }

        p->tickStatuses[i] = internal::Component::TickStatus::NotTicked;
        p->gotReleases[i] = false;

        p->inputBuses[i].SetSignalCount( p->inputBuses[0].GetSignalCount() );
        p->outputBuses[i].SetSignalCount( p->outputBuses[0].GetSignalCount() );

        p->refs[i].resize( p->refs[0].size() );

        for ( size_t j = 0; j < p->refs[0].size(); ++j )
        {
            // sync output reference counts
            p->refs[i][j] = p->refs[0][j];
        }
    }

    p->gotReleases[0] = true;

    p->bufferCount = bufferCount;
}

int Component::GetBufferCount() const
{
    return p->inputBuses.size();
}

void Component::Tick( int bufferNo )
{
    // continue only if this component has not already been ticked
    if ( p->tickStatuses[bufferNo] == internal::Component::TickStatus::NotTicked )
    {
        // 1. set tickStatus
        p->tickStatuses[bufferNo] = internal::Component::TickStatus::TickStarted;

        // 2. get outputs required from input components
        for ( auto& wire : p->inputWires )
        {
            wire.fromComponent->Tick( bufferNo );

            bool canMove;
            auto& signal = wire.fromComponent->p->GetOutput( bufferNo, wire.fromOutput, canMove );
            if ( canMove )
            {
                // we are the final reference, take the original
                p->inputBuses[bufferNo].MoveSignal( wire.toInput, signal );
            }
            else
            {
                p->inputBuses[bufferNo].CopySignal( wire.toInput, signal );
            }
        }

        // 3. clear all outputs
        p->outputBuses[bufferNo].ClearAllValues();

        if ( p->processOrder == ProcessOrder::InOrder )
        {
            // 4. wait for your turn to process.
            p->WaitForRelease( bufferNo );

            // 5. call Process_() with newly aquired inputs
            Process_( p->inputBuses[bufferNo], p->outputBuses[bufferNo] );

            // 6. signal that you're done processing.
            p->ReleaseThread( bufferNo );
        }
        else
        {
            // 4. call Process_() with newly aquired inputs
            Process_( p->inputBuses[bufferNo], p->outputBuses[bufferNo] );
        }
    }
}

void Component::Reset( int bufferNo )
{
    // clear all inputs
    p->inputBuses[bufferNo].ClearAllValues();

    // reset tickStatus
    p->tickStatuses[bufferNo] = internal::Component::TickStatus::NotTicked;
}

void Component::SetInputCount_( int inputCount, std::vector<std::string> const& inputNames )
{
    p->inputNames = inputNames;
    for ( auto& inputBus : p->inputBuses )
    {
        inputBus.SetSignalCount( inputCount );
    }
}

void Component::SetOutputCount_( int outputCount, std::vector<std::string> const& outputNames )
{
    p->outputNames = outputNames;

    for ( auto& outputBus : p->outputBuses )
    {
        outputBus.SetSignalCount( outputCount );
    }

    // Add reference counters for our new outputs
    for ( auto& ref : p->refs )
    {
        ref.resize( outputCount );
    }
}

void internal::Component::WaitForRelease( int threadNo )
{
    std::unique_lock<std::mutex> lock( *releaseMutexes[threadNo] );
    if ( !gotReleases[threadNo] )
    {
        releaseCondts[threadNo]->wait( lock );  // wait for resume
    }
    gotReleases[threadNo] = false;  // reset the release flag
}

void internal::Component::ReleaseThread( int threadNo )
{
    threadNo = threadNo + 1 == bufferCount ? 0 : threadNo + 1;  // we're actually releasing the next available thread

    std::lock_guard<std::mutex> lock( *releaseMutexes[threadNo] );

    gotReleases[threadNo] = true;
    releaseCondts[threadNo]->notify_one();
}

DSPatch::Signal::SPtr const& internal::Component::GetOutput( int bufferNo, int outputNo, bool& canMove )
{
    if ( outputBuses[bufferNo].HasValue( outputNo ) && ++refs[bufferNo][outputNo].second == refs[bufferNo][outputNo].first )
    {
        // this is the final reference, reset the counter
        refs[bufferNo][outputNo].second = 0;
        canMove = true;
    }
    else
    {
        canMove = false;
    }

    return outputBuses[bufferNo].GetSignal( outputNo );
}

void internal::Component::IncRefs( int output )
{
    for ( auto& ref : refs )
    {
        ++ref[output].first;
    }
}

void internal::Component::DecRefs( int output )
{
    for ( auto& ref : refs )
    {
        --ref[output].first;
    }
}
