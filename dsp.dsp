declare option "[midi:on][nvoices:8]";

import("stdfaust.lib");

gate = button("gate");

key = hslider("key",0,0,1000,1);

shift_saw = vslider("h:[0]Oscillators/h:[0]Sawtooth/[0]shift[style:knob]",0,-1,1,0.05);
gain_saw = vslider("h:[0]Oscillators/h:[0]Sawtooth/[1]volume[style:knob]",0,0,1,0.01);
enable_saw = checkbox("h:[0]Oscillators/h:[0]Sawtooth/[1]enable");
shift_tri = vslider("h:[0]Oscillators/h:[1]Triangle/[0]shift[style:knob]",0,-1,1,0.05);
gain_tri = vslider("h:[0]Oscillators/h:[1]Triangle/[1]volume[style:knob]",0,0,1,0.01);
enable_tri = checkbox("h:[0]Oscillators/h:[1]Triangle/[1]enable");
oscillators = 
    os.sawtooth(
        ba.midikey2hz(key + shift_saw)) * gain_saw * enable_saw
    + os.triangle(
        ba.midikey2hz(key + shift_tri)) * gain_tri * enable_tri;

attack = vslider("h:[1]Envelope/[0]attack[style:knob]",0.1,0,2,0.01);
decay = vslider("h:[1]Envelope/[1]decay[style:knob]",0.3,0,2,0.01);
sustain = vslider("h:[1]Envelope/[2]sustain[style:knob]",0.8,0,1,0.01);
release = vslider("h:[1]Envelope/[3]release[style:knob]",1,0,4,0.01);

env = en.adsr(attack, decay, sustain, release, gate);

lfo = ((os.osc(5)+1)/2) * hslider("mw[midi:ctrl 1]",0,0,1,0.01);

process = oscillators * (env/2 + lfo);
