//  synth based on supercollider-style synthdef
//  Copyright (C) 2009 Tim Blechmann
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; see the file COPYING.  If not, write to
//  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
//  Boston, MA 02111-1307, USA.

#include "sc_synth.hpp"
#include "sc_ugen_factory.hpp"

void Rate_Init(Rate *inRate, double inSampleRate, int inBufLength);

namespace nova
{

sc_synth::sc_synth(int node_id, sc_synth_prototype_ptr const & prototype):
    abstract_synth(node_id, prototype), unit_buffers(0)
{
    World const & world = ugen_factory.world;
    Rate_Init(&full_rate, world.mSampleRate, world.mBufLength);
    Rate_Init(&control_rate, world.mSampleRate/world.mBufLength, 1);
    rgen.init((uint32_t)(uint64_t)this);

    /* initialize sc wrapper class */
    mRGen = &rgen;
    mSampleOffset = 0;
    mLocalAudioBusUnit = 0;
    mLocalControlBusUnit = 0;

    localBufNum = 0;
    localMaxBufNum = 0;

    mNode.mID = node_id;

    sc_synthdef const & synthdef = prototype->synthdef;

    const size_t parameter_count = synthdef.parameter_count();
    const size_t constants_count = synthdef.constants.size();

    /* we allocate one memory chunk */
    const size_t alloc_size = parameter_count * (sizeof(float) + sizeof(int) + sizeof(float*))
                              + constants_count * sizeof(Wire);
    char * chunk = (char*)allocate(alloc_size);
    if (chunk == NULL)
        throw std::bad_alloc();

    /* prepare controls */
    mNumControls = parameter_count;
    mControls = (float*)chunk;     chunk += sizeof(float) * parameter_count;
    mControlRates = (int*)chunk;   chunk += sizeof(int) * parameter_count;
    mMapControls = (float**)chunk; chunk += sizeof(float*) * parameter_count;

    /* initialize controls */
    for (size_t i = 0; i != parameter_count; ++i) {
        mControls[i] = synthdef.parameters[i]; /* initial parameters */
        mMapControls[i] = &mControls[i]; /* map to control values */
        mControlRates[i] = 0;                  /* init to 0*/
    }

    /* allocate constant wires */
    mWire = (Wire*)chunk;          chunk += sizeof(Wire) * constants_count;
    for (size_t i = 0; i != synthdef.constants.size(); ++i) {
        Wire * wire = mWire + i;
        wire->mFromUnit = 0;
        wire->mCalcRate = 0;
        wire->mBuffer = 0;
        wire->mScalarValue = get_constant(i);
    }

    unit_buffers = allocate<sample>(64 * synthdef.buffer_count + 64); /* allocate 64 bytes more than required */

    /* allocate unit generators */
    for (graph_t::const_iterator it = synthdef.graph.begin();
         it != synthdef.graph.end(); ++it)
    {
        sc_unit unit = ugen_factory.allocate_ugen(this, *it);
        units.push_back(unit);
    }
}

sc_synth::~sc_synth(void)
{
    free(mControls);
    free(unit_buffers);

    std::for_each(units.begin(), units.end(), boost::bind(&sc_ugen_factory::free_ugen, &ugen_factory, _1));
}


void sc_synth::set(slot_index_t slot_index, sample val)
{
    mControlRates[slot_index] = 0;
    mMapControls[slot_index] = &mControls[slot_index];
    mControls[slot_index] = val;
}


void sc_synth::run(dsp_context const & context)
{
    for (size_t i = 0; i != units.size(); ++i) {
        Unit * unit = units[i].unit;
        if (unit->mCalcRate == calc_FullRate or
            unit->mCalcRate == calc_BufRate)
            (unit->mCalcFunc)(unit, unit->mBufLength);
    }
}

} /* namespace nova */
