#pragma once

namespace DSPatch
{

class ThreadingProbe : public Component
{
public:
    ThreadingProbe()
        : _count( 0 )
    {
        SetInputCount_( 3 );
    }

    int GetCount() const
    {
        return _count;
    }

    void Reset()
    {
        _count = 0;
    }

protected:
    virtual void Process_( SignalBus const& inputs, SignalBus& ) override
    {
        auto in0 = inputs.GetValue<int>( 0 );
        REQUIRE( in0 != nullptr );

        auto in1 = inputs.GetValue<int>( 1 );
        REQUIRE( in1 != nullptr );

        auto in2 = inputs.GetValue<int>( 2 );
        REQUIRE( in2 != nullptr );

        REQUIRE( *in0 == _count );
        REQUIRE( *in1 == _count );
        REQUIRE( *in2 == _count++ );
    }

private:
    int _count;
};

}  // namespace DSPatch
