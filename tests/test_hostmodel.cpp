#include "mini_test.h"
#include "host/HostModel.h"
using namespace mnm::host;

TEST_CASE(hostmodel_pitch_and_tune)
{
    CHECK_EQ(HostModel::pitchWord(0), 0u);
    CHECK_EQ(HostModel::pitchWord(60), 10240u);
    CHECK_EQ(HostModel::pitchWord(127), 21674u);
    HostModel h; h.setMasterTuneHz(440.0);
    CHECK_EQ(h.nextBlock().w[Tune], 0xFFFFFFu);   // 65535 sign-extended = -1 pitch unit
    h.setMasterTuneHz(400.0);
    for (int b = 0; b < 3; ++b) h.nextBlock();      // the info words are latched at the next frame
    CHECK_EQ(h.current().w[Tune], (0x1000000u - 5958u));   // 65536*400/440 = 59578 → int16 -5958
}

TEST_CASE(hostmodel_tick_is_24_x_bpm)
{
    HostModel h;
    h.setBpm(120.0); CHECK_EQ(h.tick(), 2880u);
    h.setBpm(300.0); CHECK_EQ(h.tick(), 7200u);
    h.setBpm(400.0); CHECK_EQ(h.tick(), 7200u);   // clamped to 300 BPM
    h.setBpm(20.0);  CHECK_EQ(h.tick(), 720u);
    h.setBpm(120.0);
    const auto& b = h.nextBlock();
    CHECK_EQ(b.w[Tick], 2880u); CHECK_EQ(b.w[TickRecip], 0x800000u / 2880u);
}

TEST_CASE(hostmodel_frames_and_oneshots)
{
    HostModel h; h.setMachine(Machine::FM_PAR);
    // block 0 is a frame: init bit (machine assign) goes out once, then the one-shots are clear
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0x80u);
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0u);
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0u);
    // a note-on between frames is latched at the next frame start (block 3), like the hardware
    h.noteOn(69);
    const auto& b = h.nextBlock();
    CHECK_EQ(b.w[NoteTrig], 0x81u); CHECK_EQ(b.w[FiltTrig], 1u); CHECK_EQ(b.w[Pitch], HostModel::pitchWord(69));
    CHECK_EQ(b.w[MachineIdx], 9u);
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0u); CHECK_EQ(h.current().w[FiltTrig], 0u);
    h.nextBlock();
    h.noteOff();
    CHECK_EQ(h.nextBlock().w[NoteTrig], 2u);
    CHECK_EQ(h.frameCount(), 3u);
}

TEST_CASE(hostmodel_slew_every_8th_frame)
{
    // (3w+t)>>2 towards 64<<16 from 0, once per 8 frames (24 blocks), the first at frame 1 (block 3)
    HostModel s; s.setMachine(Machine::FM_PAR);
    uint32_t w = 0; const uint32_t t = 64u << 16;
    CHECK_EQ(s.nextBlock().w[AmpDec], 0u);       // frame 0
    CHECK_EQ(s.nextBlock().w[AmpDec], 0u);
    CHECK_EQ(s.nextBlock().w[AmpDec], 0u);
    for (int k = 0; k < 6; ++k) {
        w = (3 * w + t) >> 2;
        CHECK_EQ(s.nextBlock().w[AmpDec], w);    // frame 8k+1: slewed
        for (int b = 1; b < 24; ++b) CHECK_EQ(s.nextBlock().w[AmpDec], w);   // held for the next 7 frames
    }
    // settle() snaps everything (engine start convenience)
    HostModel q; q.setMachine(Machine::FM_PAR); q.setLevel(100); q.settle();
    CHECK_EQ(q.nextBlock().w[AmpDec], t); CHECK_EQ(q.current().w[Level], 100u << 16); CHECK_EQ(q.current().w[PitchMod], 64u << 16);
    CHECK_EQ(q.current().w[Syn0], uint32_t(kFmParDef.defaults[0]) << 16);
}

TEST_CASE(hostmodel_routing_word)
{
    using namespace mnm::host;
    HostModel h;
    // kit default: OUT BUS AB, no input flags, both filters track the key
    CHECK_EQ(h.routingWord(), kRouteOutAB | kRouteLpKeyTrack | kRouteHpKeyTrack);
    h.setOutBuses(kRouteOutCD | kRouteOutEF);
    h.setRouting(fxInputBits(FxInput::BusCD));
    h.setKeyTracking(false, true);
    CHECK_EQ(h.routingWord(), kRouteOutCD | kRouteOutEF | kRouteBusIn | kRouteBusCD | kRouteHpKeyTrack);
    CHECK_EQ(fxInputBits(FxInput::InpAB), kRouteExternalStereoIn);
    CHECK_EQ(fxInputBits(FxInput::Neighbor), kRouteAuxIn);
    CHECK_EQ(fxInputBits(FxInput::InpB), kRouteAuxIn | kRouteAdcR);
    CHECK_EQ(dspInputBits(FxInput::InpB), kRouteAuxIn | kRouteAdcR);
    CHECK_EQ(dspInputBits(FxInput::Neighbor), kRouteExternalStereoIn);   // the plugin feeds internal sources through the ADC ring
    CHECK_EQ(dspInputBits(FxInput::BusEF), kRouteExternalStereoIn);
    h.setMachine(Machine::REVERB);
    h.nextBlock();
    CHECK_EQ(h.current().w[Mode], h.routingWord());
    CHECK_EQ(h.current().w[MachineIdx], uint32_t(Machine::REVERB));
}

TEST_CASE(hostmodel_note_events_in_one_frame)
{
    using namespace mnm::host;
    // T+0 is one byte: the last note event before a frame wins (the OS never ORs on and off together)
    HostModel h;
    h.settle();
    for (int i = 0; i < 3; ++i) h.nextBlock();   // clear the assign-trig frame
    h.noteOff();
    h.noteOn(60);
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0x81u);   // off then on: a note on, as the hardware does
    for (int i = 0; i < 2; ++i) h.nextBlock();
    h.noteOn(64);
    h.noteOff();
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0x02u);   // on then off: the off wins
    for (int i = 0; i < 2; ++i) h.nextBlock();
    h.forceInit();
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0x80u);   // machine assign alone: the init/trig flag
    for (int i = 0; i < 2; ++i) h.nextBlock();
    CHECK_EQ(h.nextBlock().w[NoteTrig], 0u);      // one-shot
}
