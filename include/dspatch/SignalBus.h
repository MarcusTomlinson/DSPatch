/******************************************************************************
DSPatch - The Refreshingly Simple C++ Dataflow Framework
Copyright (c) 2024, Marcus Tomlinson

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

#pragma once

#include <any>
#include <vector>

namespace DSPatch
{

/// Signal container

/**
Components process and transfer data between each other in the form of "signals" via interconnected wires. SignalBuses are signal
containers. Via the Process_() method, a Component receives signals into its "inputs" SignalBus and provides signals to its
"outputs" SignalBus. The SignalBus class provides public getters and setters for manipulating its internal signal values,
abstracting the need to retrieve and interface with them directly.

Signals can be dynamically typed at runtime, this means a signal has the ability to change its data type at any point during
program execution. This is designed such that a SignalBus can hold any number of different typed variables, as well as to allow
for a variable to dynamically change its type when needed - this can be useful for inputs that accept a number of different data
types (E.g. Varying sample size in an audio buffer: array of byte / int / float).
*/

class SignalBus final
{
public:
    SignalBus( const SignalBus& ) = delete;
    SignalBus& operator=( const SignalBus& ) = delete;

    SignalBus();
    SignalBus( SignalBus&& );
    ~SignalBus();

    void SetSignalCount( int signalCount );
    int GetSignalCount() const;

    std::any* GetSignal( int signalIndex );

    bool HasValue( int signalIndex ) const;

    template <typename ValueType>
    ValueType* GetValue( int signalIndex ) const;

    template <typename ValueType>
    void SetValue( int signalIndex, const ValueType& newValue );

    template <typename ValueType>
    void MoveValue( int signalIndex, ValueType&& newValue );

    void SetSignal( int toSignalIndex, const std::any& fromSignal );
    void MoveSignal( int toSignalIndex, std::any& fromSignal );

    void ClearValue( int signalIndex );
    void ClearAllValues();

    const std::type_info& GetType( int signalIndex ) const;

private:
    std::vector<std::any> _signals;
};

inline SignalBus::SignalBus() = default;

inline SignalBus::SignalBus( SignalBus&& rhs )
    : _signals( std::move( rhs._signals ) )
{
}

inline SignalBus::~SignalBus() = default;

inline void SignalBus::SetSignalCount( int signalCount )
{
    _signals.resize( signalCount );
}

inline int SignalBus::GetSignalCount() const
{
    return (int)_signals.size();
}

inline std::any* SignalBus::GetSignal( int signalIndex )
{
    // You might be thinking: Why the raw pointer return here?

    // This is for usability and performance reasons. Usability, because a pointer allows the user
    // to manipulate the contained value externally. Performance, because returning a smart pointer
    // means having to store the value as a smart pointer too - this adds yet another level of
    // indirection to the value, as well as some reference counting overhead. These Get() and Set()
    // methods are VERY frequently called, so doing as little as possible with the data here is best.

    return &_signals[signalIndex];
}

inline bool SignalBus::HasValue( int signalIndex ) const
{
    return _signals[signalIndex].has_value();
}

template <typename ValueType>
inline ValueType* SignalBus::GetValue( int signalIndex ) const
{
    // You might be thinking: Why the raw pointer return here?

    // See: GetSignal().

    try
    {
        return const_cast<ValueType*>( &std::any_cast<const ValueType&>( _signals[signalIndex] ) );
    }
    catch ( const std::exception& )
    {
        return nullptr;
    }
}

template <typename ValueType>
inline void SignalBus::SetValue( int signalIndex, const ValueType& newValue )
{
    _signals[signalIndex].emplace<ValueType>( newValue );
}

template <typename ValueType>
inline void SignalBus::MoveValue( int signalIndex, ValueType&& newValue )
{
    _signals[signalIndex].emplace<ValueType>( std::forward<ValueType>( newValue ) );
}

inline void SignalBus::SetSignal( int toSignalIndex, const std::any& fromSignal )
{
    _signals[toSignalIndex] = fromSignal;
}

inline void SignalBus::MoveSignal( int toSignalIndex, std::any& fromSignal )
{
    // You might be thinking: Why swap and not move here?

    // This is a really nifty little optimisation actually. When we move a signal value from an
    // output to an input (or vice-versa within a component) we move its type_info along with it.
    // If you look at any::emplace(), you'll see that type_info is really useful in determining
    // whether we need to delete and copy (re)construct our contained value, or can simply copy
    // assign. To avoid the former as much as possible, a swap is done between source and target
    // signals such that, between these two points, just two value holders need to be constructed,
    // and shared back and forth from then on.

    _signals[toSignalIndex].swap( fromSignal );
}

inline void SignalBus::ClearValue( int signalIndex )
{
    _signals[signalIndex].reset();
}

inline void SignalBus::ClearAllValues()
{
    for ( auto& signal : _signals )
    {
        signal.reset();
    }
}

inline const std::type_info& SignalBus::GetType( int signalIndex ) const
{
    return _signals[signalIndex].type();
}

}  // namespace DSPatch
